// tests/test_validation.cpp
//
// P11 统一校验框架 · 骨架测试（11-01 统一校验中心 / D1）。
//
// 覆盖：
//   - Severity ↔ 字符串往返
//   - ValidationResult 字段 + 链式构造器
//   - ValidationRule 抽象基类（ClassDB 元信息 / 继承 Object）
//   - ValidationEngine：
//       * add_rule 注册 + rule_count
//       * run_all 聚合（按注册顺序）
//       * group_by_severity 三级分组
//       * completed Signal 触发（携带结果总数）
//       * 空上下文安全（无规则 / 无 board / board=nullptr）
//       * clear_rules
//   - ERC 规则触发：
//       * UnconnectedPinRule    —— net_uuid 为空的 pad 触发 WARNING + 可修
//       * DuplicateRefdesRule   —— 重复 refdes 触发 ERROR + 不可修
//       * PowerGroundConflictRule —— 多驱动电源/地网络触发 ERROR
//   - 集成：engine + 三规则联合 run_all，结果按 Severity 正确分组
//
// 数据协议（与 erc.h 一致）：测试用 plain Object + set_meta 构造"伪 board"，
//   填充 erc.pads / erc.footprints / erc.nets 元数据键。
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_validation test_validation.cpp)
//   target_link_libraries(test_validation PRIVATE eda_validation gtest_main)
//   gtest_discover_tests(test_validation)

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/object/object.h"
#include "core/object/variant.h"
#include "eda/geometry/types.h"
#include "eda/validation/erc.h"
#include "eda/validation/validation.h"

using namespace eda;
using namespace eda::validation;

namespace {

// 便捷：构造 STRING Variant（避免 const char* 隐式歧义）。
Variant VStr(const std::string& s) { return Variant(s); }

// 便捷：构造 INT Variant。
Variant VInt(int64_t v) { return Variant(v); }

// 便捷：构造 DICTIONARY Variant（从 initializer 列表）。
Variant VDict(std::initializer_list<std::pair<std::string, Variant>> items) {
    std::unordered_map<std::string, Variant> m;
    for (const auto& kv : items) m.emplace(kv.first, kv.second);
    return Variant(m);
}

// 便捷：构造 ARRAY Variant。
Variant VArr(std::vector<Variant> items) { return Variant(items); }

// 构造一个"伪 board"：plain Object + 预填元数据。规则经 get_meta 读取。
// 调用方按需 set_meta 更多键（erc.pads / erc.footprints / erc.nets）。
class FakeBoard : public Object {
public:
    std::string get_class() const override { return "Board"; }
};

}  // namespace

// ============================================================
// Severity ↔ 字符串
// ============================================================

TEST(SeverityTest, RoundTripAllLevels) {
    EXPECT_EQ(severity_string(Severity::ERROR), "error");
    EXPECT_EQ(severity_string(Severity::WARNING), "warning");
    EXPECT_EQ(severity_string(Severity::INFO), "info");

    EXPECT_EQ(parse_severity("error"), Severity::ERROR);
    EXPECT_EQ(parse_severity("warning"), Severity::WARNING);
    EXPECT_EQ(parse_severity("info"), Severity::INFO);
}

TEST(SeverityTest, ParseIsCaseInsensitive) {
    EXPECT_EQ(parse_severity("ERROR"), Severity::ERROR);
    EXPECT_EQ(parse_severity("WARNING"), Severity::WARNING);
    EXPECT_EQ(parse_severity("Info"), Severity::INFO);
}

TEST(SeverityTest, ParseUnknownFallsBackToInfo) {
    EXPECT_EQ(parse_severity(""), Severity::INFO);
    EXPECT_EQ(parse_severity("bogus"), Severity::INFO);
}

// ============================================================
// ValidationResult 字段 + 链式构造
// ============================================================

TEST(ValidationResultTest, DefaultValues) {
    ValidationResult r;
    EXPECT_EQ(r.severity, Severity::WARNING);
    EXPECT_TRUE(r.category.empty());
    EXPECT_TRUE(r.message.empty());
    EXPECT_FALSE(r.location.has_value());
    EXPECT_TRUE(r.entity_uuid.empty());
    EXPECT_FALSE(r.fixable);
}

TEST(ValidationResultTest, ChainBuilder) {
    const auto r = ValidationResult()
                       .with_severity(Severity::ERROR)
                       .with_category("ERC")
                       .with_message("dup")
                       .with_location({100, 200})
                       .with_entity("fp-1")
                       .with_fixable();
    EXPECT_EQ(r.severity, Severity::ERROR);
    EXPECT_EQ(r.category, "ERC");
    EXPECT_EQ(r.message, "dup");
    ASSERT_TRUE(r.location.has_value());
    EXPECT_EQ(r.location->x, 100);
    EXPECT_EQ(r.location->y, 200);
    EXPECT_EQ(r.entity_uuid, "fp-1");
    EXPECT_TRUE(r.fixable);
}

// ============================================================
// ValidationRule 抽象基类（ClassDB 元信息 / 继承 Object）
// ============================================================

TEST(ValidationRuleTest, ClassHierarchy) {
    UnconnectedPinRule r;
    EXPECT_EQ(r.get_class(), "UnconnectedPinRule");
    EXPECT_TRUE(r.is_class("UnconnectedPinRule"));
    EXPECT_TRUE(r.is_class("ValidationRule"));  // 链式向上
    EXPECT_TRUE(r.is_class("Object"));          // 根
    EXPECT_FALSE(r.is_class("Board"));
    EXPECT_EQ(ValidationRule::get_class_static(), "ValidationRule");
}

TEST(ValidationRuleTest, NameContracts) {
    EXPECT_EQ(UnconnectedPinRule{}.name(), "erc.unconnected_pin");
    EXPECT_EQ(DuplicateRefdesRule{}.name(), "erc.duplicate_refdes");
    EXPECT_EQ(PowerGroundConflictRule{}.name(), "erc.power_ground_conflict");
}

// ============================================================
// ValidationEngine：注册 / run_all / 分组 / 信号 / 空上下文
// ============================================================

TEST(ValidationEngineTest, RegisterAndCount) {
    ValidationEngine eng;
    EXPECT_EQ(eng.rule_count(), 0u);

    eng.add_rule(std::make_unique<UnconnectedPinRule>());
    eng.add_rule(std::make_unique<DuplicateRefdesRule>());
    EXPECT_EQ(eng.rule_count(), 2u);
}

TEST(ValidationEngineTest, EmplaceRuleReturnsRawPointer) {
    ValidationEngine eng;
    auto* raw = eng.emplace_rule<UnconnectedPinRule>();
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->name(), "erc.unconnected_pin");
    EXPECT_EQ(eng.rule_count(), 1u);
}

TEST(ValidationEngineTest, AddNullRuleIsIgnored) {
    ValidationEngine eng;
    eng.add_rule(nullptr);
    EXPECT_EQ(eng.rule_count(), 0u);
}

TEST(ValidationEngineTest, RunAllEmptyContextSafe) {
    // 无规则 + 空上下文 → 空结果，不崩。
    ValidationEngine eng;
    auto results = eng.run_all(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

TEST(ValidationEngineTest, RunAllNoRuleWithBoardSafe) {
    // 有 board 但无规则 → 仍空结果。
    ValidationEngine eng;
    FakeBoard board;
    auto results = eng.run_all(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(ValidationEngineTest, RunAllRulesWithNullBoardSafe) {
    // 有规则但 board=nullptr → 规则各自跳过，返回空。
    ValidationEngine eng;
    eng.add_rule(std::make_unique<UnconnectedPinRule>());
    eng.add_rule(std::make_unique<DuplicateRefdesRule>());
    eng.add_rule(std::make_unique<PowerGroundConflictRule>());
    auto results = eng.run_all(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

TEST(ValidationEngineTest, CompletedSignalFiresWithCount) {
    ValidationEngine eng;
    eng.add_rule(std::make_unique<UnconnectedPinRule>());

    std::size_t fired_count = 0;
    bool fired = false;
    eng.completed().connect([&](std::size_t n) {
        fired = true;
        fired_count = n;
    });

    FakeBoard board;
    board.set_meta("erc.pads", VArr({
        VDict({{"uuid", VStr("p1")}, {"net_uuid", VStr("")}}),  // 未连 → 1 条
        VDict({{"uuid", VStr("p2")}, {"net_uuid", VStr("n1")}}), // 已连
    }));

    auto results = eng.run_all(ValidationContext{&board});
    EXPECT_TRUE(fired);
    EXPECT_EQ(fired_count, results.size());
    EXPECT_EQ(fired_count, 1u);
}

TEST(ValidationEngineTest, CompletedSignalFiresOnEmptyResult) {
    // 即使结果为空，completed 也应触发（计数 0），便于面板切换"运行中→完成"。
    ValidationEngine eng;
    bool fired = false;
    std::size_t n = 999;
    eng.completed().connect([&](std::size_t c) {
        fired = true;
        n = c;
    });
    eng.run_all(ValidationContext::empty());
    EXPECT_TRUE(fired);
    EXPECT_EQ(n, 0u);
}

TEST(ValidationEngineTest, GroupBySeverityHasAllThreeLevels) {
    ValidationEngine eng;
    const std::vector<ValidationResult> sample = {
        ValidationResult().with_severity(Severity::ERROR),
        ValidationResult().with_severity(Severity::ERROR),
        ValidationResult().with_severity(Severity::WARNING),
        ValidationResult().with_severity(Severity::INFO),
    };
    const auto g = eng.group_by_severity(sample);
    EXPECT_EQ(g.size(), 3u);
    EXPECT_EQ(g.at(Severity::ERROR).size(), 2u);
    EXPECT_EQ(g.at(Severity::WARNING).size(), 1u);
    EXPECT_EQ(g.at(Severity::INFO).size(), 1u);
}

TEST(ValidationEngineTest, GroupBySeverityEmptyInputStillHasAllLevels) {
    ValidationEngine eng;
    const auto g = eng.group_by_severity({});
    EXPECT_EQ(g.size(), 3u);
    EXPECT_TRUE(g.at(Severity::ERROR).empty());
    EXPECT_TRUE(g.at(Severity::WARNING).empty());
    EXPECT_TRUE(g.at(Severity::INFO).empty());
}

TEST(ValidationEngineTest, ClearRules) {
    ValidationEngine eng;
    eng.add_rule(std::make_unique<UnconnectedPinRule>());
    eng.add_rule(std::make_unique<DuplicateRefdesRule>());
    EXPECT_EQ(eng.rule_count(), 2u);
    eng.clear_rules();
    EXPECT_EQ(eng.rule_count(), 0u);
}

// ============================================================
// ERC 规则触发：UnconnectedPinRule
// ============================================================

TEST(UnconnectedPinRuleTest, FiresOnPadWithoutNet) {
    UnconnectedPinRule rule;
    FakeBoard board;
    board.set_meta("erc.pads", VArr({
        VDict({{"uuid", VStr("pad-A")}, {"refdes", VStr("R1")}, {"net_uuid", VStr("")},
               {"x", VInt(1000)}, {"y", VInt(2000)}}),
        VDict({{"uuid", VStr("pad-B")}, {"net_uuid", VStr("net-1")}}),  // 已连
        VDict({{"uuid", VStr("pad-C")}, {"net_uuid", VStr("")}}),        // 未连，无 refdes
    }));

    auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 2u);

    // 第一条：有 refdes，可修，带坐标。
    const auto& r1 = results[0];
    EXPECT_EQ(r1.severity, Severity::WARNING);
    EXPECT_EQ(r1.category, "ERC");
    EXPECT_TRUE(r1.fixable);
    EXPECT_EQ(r1.entity_uuid, "pad-A");
    ASSERT_TRUE(r1.location.has_value());
    EXPECT_EQ(r1.location->x, 1000);
    EXPECT_EQ(r1.location->y, 2000);
    EXPECT_NE(r1.message.find("R1"), std::string::npos);

    // 第二条：无 refdes，无坐标。
    const auto& r2 = results[1];
    EXPECT_EQ(r2.entity_uuid, "pad-C");
    EXPECT_FALSE(r2.location.has_value());
    EXPECT_NE(r2.message.find("pad-C"), std::string::npos);
}

TEST(UnconnectedPinRuleTest, EmptyWhenAllConnected) {
    UnconnectedPinRule rule;
    FakeBoard board;
    board.set_meta("erc.pads", VArr({
        VDict({{"uuid", VStr("p1")}, {"net_uuid", VStr("n1")}}),
        VDict({{"uuid", VStr("p2")}, {"net_uuid", VStr("n2")}}),
    }));
    auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(UnconnectedPinRuleTest, EmptyWhenNoPadMeta) {
    // board 无 erc.pads 键 → 安全返回空。
    UnconnectedPinRule rule;
    FakeBoard board;
    auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(UnconnectedPinRuleTest, EmptyWhenBoardNull) {
    UnconnectedPinRule rule;
    auto results = rule.check(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

// ============================================================
// ERC 规则触发：DuplicateRefdesRule
// ============================================================

TEST(DuplicateRefdesRuleTest, FiresOnDuplicate) {
    DuplicateRefdesRule rule;
    FakeBoard board;
    board.set_meta("erc.footprints", VArr({
        VDict({{"uuid", VStr("fp-1")}, {"refdes", VStr("R1")}}),
        VDict({{"uuid", VStr("fp-2")}, {"refdes", VStr("R2")}}),
        VDict({{"uuid", VStr("fp-3")}, {"refdes", VStr("R1")}}),  // 重复 R1
    }));

    auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 1u);

    const auto& r = results[0];
    EXPECT_EQ(r.severity, Severity::ERROR);
    EXPECT_EQ(r.category, "ERC");
    EXPECT_FALSE(r.fixable);  // 用户决定保留哪个
    EXPECT_EQ(r.entity_uuid, "fp-3");
    EXPECT_NE(r.message.find("R1"), std::string::npos);
    EXPECT_NE(r.message.find("fp-1"), std::string::npos);
}

TEST(DuplicateRefdesRuleTest, UniqueRefdesNoViolation) {
    DuplicateRefdesRule rule;
    FakeBoard board;
    board.set_meta("erc.footprints", VArr({
        VDict({{"uuid", VStr("fp-1")}, {"refdes", VStr("R1")}}),
        VDict({{"uuid", VStr("fp-2")}, {"refdes", VStr("R2")}}),
        VDict({{"uuid", VStr("fp-3")}, {"refdes", VStr("R3")}}),
    }));
    auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(DuplicateRefdesRuleTest, SkipsEmptyRefdes) {
    // 未放置器件（refdes 空）不参与重复检测。
    DuplicateRefdesRule rule;
    FakeBoard board;
    board.set_meta("erc.footprints", VArr({
        VDict({{"uuid", VStr("fp-1")}, {"refdes", VStr("")}}),
        VDict({{"uuid", VStr("fp-2")}, {"refdes", VStr("")}}),
    }));
    auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(DuplicateRefdesRuleTest, EmptyWhenBoardNull) {
    DuplicateRefdesRule rule;
    auto results = rule.check(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

// ============================================================
// ERC 规则触发：PowerGroundConflictRule
// ============================================================

TEST(PowerGroundConflictRuleTest, FiresOnMultiDriverPowerNet) {
    PowerGroundConflictRule rule;
    FakeBoard board;
    board.set_meta("erc.nets", VArr({
        VDict({{"name", VStr("VCC")}, {"type", VStr("power")}, {"driver_count", VInt(2)}}),
        VDict({{"name", VStr("GND")}, {"type", VStr("ground")}, {"driver_count", VInt(3)}}),
        VDict({{"name", VStr("N1")}, {"type", VStr("signal")}, {"driver_count", VInt(5)}}),  // signal 跳过
        VDict({{"name", VStr("VDD")}, {"type", VStr("power")}, {"driver_count", VInt(1)}}),  // 单驱动
    }));

    auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 2u);

    EXPECT_EQ(results[0].severity, Severity::ERROR);
    EXPECT_EQ(results[0].category, "ERC");
    EXPECT_FALSE(results[0].fixable);
    EXPECT_NE(results[0].message.find("VCC"), std::string::npos);

    EXPECT_EQ(results[1].severity, Severity::ERROR);
    EXPECT_NE(results[1].message.find("GND"), std::string::npos);
}

TEST(PowerGroundConflictRuleTest, EmptyWhenAllSingleDriver) {
    PowerGroundConflictRule rule;
    FakeBoard board;
    board.set_meta("erc.nets", VArr({
        VDict({{"name", VStr("VCC")}, {"type", VStr("power")}, {"driver_count", VInt(1)}}),
        VDict({{"name", VStr("GND")}, {"type", VStr("ground")}, {"driver_count", VInt(1)}}),
    }));
    auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(PowerGroundConflictRuleTest, EmptyWhenBoardNull) {
    PowerGroundConflictRule rule;
    auto results = rule.check(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

// ============================================================
// 集成：engine + 三规则联合 run_all
// ============================================================

TEST(ValidationIntegrationTest, AllRulesAggregateAndGroup) {
    ValidationEngine eng;
    eng.add_rule(std::make_unique<UnconnectedPinRule>());
    eng.add_rule(std::make_unique<DuplicateRefdesRule>());
    eng.add_rule(std::make_unique<PowerGroundConflictRule>());

    FakeBoard board;
    board.set_meta("erc.pads", VArr({
        VDict({{"uuid", VStr("p1")}, {"net_uuid", VStr("")}}),       // WARNING ×1
        VDict({{"uuid", VStr("p2")}, {"net_uuid", VStr("n1")}}),
    }));
    board.set_meta("erc.footprints", VArr({
        VDict({{"uuid", VStr("f1")}, {"refdes", VStr("U1")}}),
        VDict({{"uuid", VStr("f2")}, {"refdes", VStr("U1")}}),       // ERROR ×1
    }));
    board.set_meta("erc.nets", VArr({
        VDict({{"name", VStr("VCC")}, {"type", VStr("power")}, {"driver_count", VInt(2)}}),  // ERROR ×1
    }));

    // completed 信号：触发一次，携带总数。
    std::size_t fired_count = 0;
    bool fired = false;
    eng.completed().connect([&](std::size_t n) {
        fired = true;
        fired_count = n;
    });

    auto results = eng.run_all(ValidationContext{&board});
    EXPECT_TRUE(fired);
    EXPECT_EQ(fired_count, results.size());
    ASSERT_EQ(results.size(), 3u);

    // 分组：2 ERROR（重复位号 + 电源冲突）+ 1 WARNING（未连）。
    const auto g = eng.group_by_severity(results);
    EXPECT_EQ(g.at(Severity::ERROR).size(), 2u);
    EXPECT_EQ(g.at(Severity::WARNING).size(), 1u);
    EXPECT_EQ(g.at(Severity::INFO).size(), 0u);
}

TEST(ValidationIntegrationTest, EmptyContextYieldsNoViolations) {
    ValidationEngine eng;
    eng.add_rule(std::make_unique<UnconnectedPinRule>());
    eng.add_rule(std::make_unique<DuplicateRefdesRule>());
    eng.add_rule(std::make_unique<PowerGroundConflictRule>());

    // 空 board / 空上下文均不触发任何规则。
    auto r1 = eng.run_all(ValidationContext::empty());
    EXPECT_TRUE(r1.empty());

    FakeBoard empty_board;  // 无 meta 键
    auto r2 = eng.run_all(ValidationContext{&empty_board});
    EXPECT_TRUE(r2.empty());
}
