// tests/test_drc.cpp
//
// P11 DRC 基础规则测试（11-01 统一校验中心 / PCB 物理规则）。
//
// 覆盖：
//   - ClearanceRule    —— Track-Track / Track-Pad / Pad-Pad 间距
//                          * 两 Track 太近 → ERROR
//                          * 两 Track 够远 → 通过
//                          * Track-Pad 太近 → ERROR
//                          * 同 Net 跳过 / 跨层跳过
//   - MinWidthRule     —— Track.width 不足 → ERROR；足够 → 通过
//   - ShortCircuitRule —— 不同 Net 图元重叠
//                          * Track 中心线相交 → ERROR
//                          * Pad-Box 重叠 → ERROR
//                          * Track 穿入 Pad-Box → ERROR
//                          * 同 Net 不算短
//   - UnroutedNetRule  —— Net 含 Pad/Via 但无 Track → WARNING；有 Track → 通过
//   - ValidationEngine 集成：4 规则联合 run_all
//                          * 正常板 → 0 违例
//                          * 多违例板 → 聚合 + completed 信号
//   - DrcOptions       —— 自定义参数被规则尊重
//   - ClassDB 元信息   —— 各规则 get_class / is_class 链
//   - 空 context 安全  —— board=nullptr 不崩
//
// 数据：直接构造 pcb::Board（Track/Pad/Net），不再走 meta 键。
// ValidationContext{&board} 把 Board* upcast 成 const Object*；规则内 is_class + static_cast 下转。
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_drc test_drc.cpp)
//   target_link_libraries(test_drc PRIVATE eda_validation gtest_main)
//   gtest_discover_tests(test_drc)

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/net.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/validation/drc.h"
#include "eda/validation/validation.h"

using namespace eda;
using namespace eda::pcb;
using namespace eda::geometry;
using namespace eda::validation;

namespace {

// 1 mm = 1_000_000 nm（Coord 单位为 nm）
constexpr Coord kMm = 1'000'000;

// 构造一条水平 Track（path 沿 x 轴，给定 y / width / net）。
Track& add_horizontal_track(Board& board, Coord x0, Coord x1, Coord y,
                            Coord width, const std::string& layer,
                            const std::string& net_uuid) {
    Path p;
    p.points = {Point{x0, y}, Point{x1, y}};
    p.width = width;
    return board.add_track(p, layer, net_uuid);
}

// 构造一条垂直 Track。
Track& add_vertical_track(Board& board, Coord x, Coord y0, Coord y1,
                          Coord width, const std::string& layer,
                          const std::string& net_uuid) {
    Path p;
    p.points = {Point{x, y0}, Point{x, y1}};
    p.width = width;
    return board.add_track(p, layer, net_uuid);
}

// 统计结果中某 Severity 的数量。
std::size_t count_severity(const std::vector<ValidationResult>& rs, Severity s) {
    std::size_t n = 0;
    for (const auto& r : rs) if (r.severity == s) ++n;
    return n;
}

// 统计 category="DRC" 的数量。
std::size_t count_drc(const std::vector<ValidationResult>& rs) {
    std::size_t n = 0;
    for (const auto& r : rs) if (r.category == "DRC") ++n;
    return n;
}

}  // namespace

// ============================================================
// ClearanceRule
// ============================================================

TEST(ClearanceRuleTest, TwoTracksTooCloseFiresError) {
    Board board;
    // 两水平 Track，中心线 y=0 / y=300k，width=200k → 铜距 = 300k - 200k = 100k < 200k 最小间距
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");

    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    const auto results = rule.check(ValidationContext{&board});

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].severity, Severity::ERROR);
    EXPECT_EQ(results[0].category, "DRC");
    EXPECT_TRUE(results[0].location.has_value());
    EXPECT_FALSE(results[0].entity_uuid.empty());
}

TEST(ClearanceRuleTest, TwoTracksFarEnoughPasses) {
    Board board;
    // y=0 / y=500k，width=200k → 铜距 = 500k - 200k = 300k ≥ 200k
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 500'000, 200'000, "F.Cu", "net-B");

    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(ClearanceRuleTest, TrackPadTooCloseFiresError) {
    Board board;
    // Track 在 y=0，width=200k（铜区 y∈[-100k, 100k]）。
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    // Pad 中心 (500k, 250k)，300k x 300k → 铜盒 y∈[100k, 400k]，与 Track 铜区恰好接触。
    // 让 Pad 中心下移到 (500k, 200k) → 盒 y∈[50k, 350k]，与 Track 铜区 [−100k,100k] 间隙 0。
    // 用 (500k, 150k) 让盒 y∈[0, 300k] 与 Track 铜区 [−100k, 100k] 相交叠 → 铜距 ≤ 0 < 200k。
    board.add_pad(Point{500'000, 150'000}, 300'000, 300'000, PadShape::RECT, "F.Cu");

    // Pad 默认 net_uuid 为空，Track net=net-A → 视为不同节点（任一空即不同），间距要查。
    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_EQ(count_drc(results), 1u);
    EXPECT_EQ(count_severity(results, Severity::ERROR), 1u);
}

TEST(ClearanceRuleTest, SameNetPairSkipped) {
    Board board;
    // 同 Net 的两条近距离 Track：不应报间距（同节点无间距要求）。
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-A");

    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(ClearanceRuleTest, CrossLayerPairSkipped) {
    Board board;
    // 跨层 Track（F.Cu / B.Cu）：即便铜距很近也不查（叠层专项规则负责）。
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 0, 200'000, "B.Cu", "net-B");

    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(ClearanceRuleTest, PadPadTooCloseFiresError) {
    Board board;
    // 两个 300k x 300k Pad，中心相距 400k → 盒间距 = 400k - 300k = 100k < 200k
    board.add_pad(Point{0, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    board.add_pad(Point{400'000, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");

    ClearanceRule rule(DrcOptions{.min_clearance_nm = 200'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_EQ(count_severity(results, Severity::ERROR), 1u);
}

// ============================================================
// MinWidthRule
// ============================================================

TEST(MinWidthRuleTest, TooThinFiresError) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 100'000, "F.Cu", "net-A");  // 100k < 150k min

    MinWidthRule rule(DrcOptions{.min_width_nm = 150'000});
    const auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].severity, Severity::ERROR);
    EXPECT_EQ(results[0].category, "DRC");
    EXPECT_TRUE(results[0].fixable);
    EXPECT_TRUE(results[0].location.has_value());
}

TEST(MinWidthRuleTest, OkWidthPasses) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 300'000, "F.Cu", "net-A");  // 300k ≥ 150k

    MinWidthRule rule(DrcOptions{.min_width_nm = 150'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(MinWidthRuleTest, ExactBoundaryPasses) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 150'000, "F.Cu", "net-A");  // == min

    MinWidthRule rule(DrcOptions{.min_width_nm = 150'000});
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());  // < 才报，== 通过
}

// ============================================================
// ShortCircuitRule
// ============================================================

TEST(ShortCircuitRuleTest, TracksCrossFiresError) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_vertical_track(board, 500'000, -500'000, 500'000, 200'000, "F.Cu", "net-B");
    // 两中心线在 (500k, 0) 相交 → 短路

    ShortCircuitRule rule;
    const auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].severity, Severity::ERROR);
    EXPECT_EQ(results[0].category, "DRC");
    EXPECT_TRUE(results[0].location.has_value());
}

TEST(ShortCircuitRuleTest, PadPadOverlapFiresError) {
    Board board;
    Pad& p1 = board.add_pad(Point{0, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    Pad& p2 = board.add_pad(Point{100'000, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    p1.set_net_uuid("net-A");
    p2.set_net_uuid("net-B");
    // 两 Pad 盒相交（P1 [-150k,150k], P2 [-50k,250k] 在 x 上重叠）

    ShortCircuitRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_EQ(count_severity(results, Severity::ERROR), 1u);
}

TEST(ShortCircuitRuleTest, TrackThroughPadFiresError) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 100'000, "F.Cu", "net-A");
    Pad& p = board.add_pad(Point{500'000, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    p.set_net_uuid("net-B");
    // Track 中心线 y=0 穿过 Pad 盒 x∈[350k,650k] → 短路

    ShortCircuitRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_EQ(count_severity(results, Severity::ERROR), 1u);
}

TEST(ShortCircuitRuleTest, SameNetNoShort) {
    Board board;
    // 同 Net 即便中心线相交也不算短路（同节点）
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_vertical_track(board, 500'000, -500'000, 500'000, 200'000, "F.Cu", "net-A");

    ShortCircuitRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(ShortCircuitRuleTest, CrossLayerNoShort) {
    Board board;
    // 跨层相交不是短路（不同铜层）
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_vertical_track(board, 500'000, -500'000, 500'000, 200'000, "B.Cu", "net-B");

    ShortCircuitRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

// ============================================================
// UnroutedNetRule
// ============================================================

TEST(UnroutedNetRuleTest, NetWithPadNoTrackFiresWarning) {
    Board board;
    Net& net = board.add_net("N1");
    Pad& pad = board.add_pad(Point{0, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    pad.set_net_uuid(net.uuid());
    net.add_member(pad.uuid());
    // Net 有 1 个 Pad 成员，0 个 Track 成员 → 未布线

    UnroutedNetRule rule;
    const auto results = rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].severity, Severity::WARNING);
    EXPECT_EQ(results[0].category, "DRC");
    EXPECT_TRUE(results[0].fixable);
    EXPECT_TRUE(results[0].location.has_value());
}

TEST(UnroutedNetRuleTest, NetWithPadAndTrackPasses) {
    Board board;
    Net& net = board.add_net("N1");
    Pad& pad = board.add_pad(Point{0, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    Track& track = add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "");
    pad.set_net_uuid(net.uuid());
    net.add_member(pad.uuid());
    net.add_member(track.uuid());

    UnroutedNetRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

TEST(UnroutedNetRuleTest, EmptyNetPasses) {
    Board board;
    board.add_net("N1");  // 无成员 → 不算未布线

    UnroutedNetRule rule;
    const auto results = rule.check(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
}

// ============================================================
// ValidationEngine 集成
// ============================================================

TEST(DrcEngineIntegrationTest, NormalBoardNoViolations) {
    Board board;
    // 两条足够远、足够宽、不交叉、不同 Net 的走线
    add_horizontal_track(board, 0, kMm, 0, 300'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 1'000'000, 300'000, "F.Cu", "net-B");
    // 铜距 = 1mm - 300k = 700k ≥ 200k；width=300k ≥ 150k；不交叉；无 Net 对象 → 无未布线

    ValidationEngine engine;
    engine.emplace_rule<ClearanceRule>(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    engine.emplace_rule<MinWidthRule>(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    engine.emplace_rule<ShortCircuitRule>();
    engine.emplace_rule<UnroutedNetRule>();
    EXPECT_EQ(engine.rule_count(), 4u);

    std::size_t completed_count = 0;
    engine.completed().connect([&completed_count](std::size_t n) { completed_count = n; });

    const auto results = engine.run_all(ValidationContext{&board});
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(completed_count, 0u);
}

TEST(DrcEngineIntegrationTest, MixedViolationsAggregated) {
    Board board;
    // 一条过细 Track（width=100k < 150k min）→ MinWidth ERROR
    add_horizontal_track(board, 0, kMm, 0, 100'000, "F.Cu", "net-A");
    // 两条过近 Track（同层异 Net，铜距 100k < 200k）→ Clearance ERROR
    add_horizontal_track(board, 0, kMm, 500'000, 200'000, "F.Cu", "net-B");
    add_horizontal_track(board, 0, kMm, 800'000, 200'000, "F.Cu", "net-C");
    // 铜距 = 800k - 500k - 200k = 100k < 200k → 1 个 clearance 违例

    ValidationEngine engine;
    engine.emplace_rule<ClearanceRule>(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    engine.emplace_rule<MinWidthRule>(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    engine.emplace_rule<ShortCircuitRule>();
    engine.emplace_rule<UnroutedNetRule>();

    const auto results = engine.run_all(ValidationContext{&board});
    EXPECT_EQ(results.size(), 2u);  // 1 width + 1 clearance
    EXPECT_EQ(count_severity(results, Severity::ERROR), 2u);

    // group_by_severity：ERROR 桶非空
    const auto grouped = engine.group_by_severity(results);
    EXPECT_EQ(grouped.at(Severity::ERROR).size(), 2u);
    EXPECT_EQ(grouped.at(Severity::WARNING).size(), 0u);
}

TEST(DrcEngineIntegrationTest, EmptyContextSafe) {
    ValidationEngine engine;
    engine.emplace_rule<ClearanceRule>();
    engine.emplace_rule<MinWidthRule>();
    engine.emplace_rule<ShortCircuitRule>();
    engine.emplace_rule<UnroutedNetRule>();

    const auto results = engine.run_all(ValidationContext::empty());
    EXPECT_TRUE(results.empty());
}

// ============================================================
// DrcOptions 尊重
// ============================================================

TEST(DrcOptionsTest, CustomMinWidthRespected) {
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 300'000, "F.Cu", "net-A");

    // 默认 min_width=150k：300k 通过
    MinWidthRule default_rule;
    EXPECT_TRUE(default_rule.check(ValidationContext{&board}).empty());

    // 自定义 min_width=400k：300k 违例
    MinWidthRule strict_rule(DrcOptions{.min_width_nm = 400'000});
    const auto results = strict_rule.check(ValidationContext{&board});
    ASSERT_EQ(results.size(), 1u);
}

TEST(DrcOptionsTest, CustomMinClearanceRespected) {
    Board board;
    // 两 Track 铜距 300k
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 500'000, 200'000, "F.Cu", "net-B");

    // min_clearance=200k：300k 铜距通过
    ClearanceRule loose(DrcOptions{.min_clearance_nm = 200'000});
    EXPECT_TRUE(loose.check(ValidationContext{&board}).empty());

    // min_clearance=400k：300k 铜距违例
    ClearanceRule strict(DrcOptions{.min_clearance_nm = 400'000});
    EXPECT_EQ(strict.check(ValidationContext{&board}).size(), 1u);
}

// ============================================================
// ClassDB 元信息
// ============================================================

TEST(DrcRuleClassDbTest, ClearanceRuleClassChain) {
    ClearanceRule r;
    EXPECT_EQ(r.get_class(), "ClearanceRule");
    EXPECT_TRUE(r.is_class("ClearanceRule"));
    EXPECT_TRUE(r.is_class("ValidationRule"));
    EXPECT_TRUE(r.is_class("Object"));
    EXPECT_EQ(r.name(), "drc.clearance");
}

TEST(DrcRuleClassDbTest, MinWidthRuleClassChain) {
    MinWidthRule r;
    EXPECT_EQ(r.get_class(), "MinWidthRule");
    EXPECT_TRUE(r.is_class("MinWidthRule"));
    EXPECT_TRUE(r.is_class("ValidationRule"));
    EXPECT_EQ(r.name(), "drc.min_width");
}

TEST(DrcRuleClassDbTest, ShortCircuitRuleClassChain) {
    ShortCircuitRule r;
    EXPECT_EQ(r.get_class(), "ShortCircuitRule");
    EXPECT_TRUE(r.is_class("ShortCircuitRule"));
    EXPECT_TRUE(r.is_class("ValidationRule"));
    EXPECT_EQ(r.name(), "drc.short_circuit");
}

TEST(DrcRuleClassDbTest, UnroutedNetRuleClassChain) {
    UnroutedNetRule r;
    EXPECT_EQ(r.get_class(), "UnroutedNetRule");
    EXPECT_TRUE(r.is_class("UnroutedNetRule"));
    EXPECT_TRUE(r.is_class("ValidationRule"));
    EXPECT_EQ(r.name(), "drc.unrouted_net");
}

// ============================================================
// 非 Board 上下文安全跳过
// ============================================================

TEST(DrcNonBoardContextTest, NonBoardObjectSkipsSilently) {
    // 一个非 Board 的 Object 子类，is_class("Board")=false → 所有 DRC 规则应安全返回空。
    class NotABoard : public Object {
    public:
        std::string get_class() const override { return "NotABoard"; }
    };
    NotABoard obj;
    ValidationContext ctx{&obj};

    ClearanceRule clr;
    MinWidthRule mw;
    ShortCircuitRule sc;
    UnroutedNetRule ur;

    EXPECT_TRUE(clr.check(ctx).empty());
    EXPECT_TRUE(mw.check(ctx).empty());
    EXPECT_TRUE(sc.check(ctx).empty());
    EXPECT_TRUE(ur.check(ctx).empty());
}
