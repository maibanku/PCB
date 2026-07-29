// tests/test_eco.cpp
//
// P14 ECO 工程变更订单 · 测试（原理图 ↔ PCB 双向同步骨架）。
//
// 覆盖：
//   - ChangeType 枚举字符串往返
//   - EcoDiff::compute_diff：
//       * 原理图有 "VCC" / PCB 无      → ADDED
//       * PCB 有 / 原理图无           → REMOVED
//       * 双方一致                    → 无 diff
//       * 电源分类不一致              → MODIFIED
//       * 多差异混合 / 确定性（同一输入两次结果相同）
//   - EcoApplier::apply：
//       * ADDED → Board 实际新增 Net（无 UndoStack 直连模式）
//       * ADDED + UndoStack → 命令化，macro 合并为单步；net 落地
//       * REMOVED → skipped + errors（Board 无 remove_net，骨架限制）
//       * MODIFIED → skipped + errors（骨架限制）
//       * 空 changes → 0 计数、ok()
//       * 非 "Net" entity_type → skipped
//   - 端到端：compute_diff → apply 后再 diff → 空（同步达成）
//
// 集成：tests/CMakeLists.txt 已追加
//   add_executable(test_eco test_eco.cpp)
//   target_link_libraries(test_eco PRIVATE eda_eco gtest_main)

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/object/ref.h"
#include "eda/command/command.h"
#include "eda/command/undo_stack.h"
#include "eda/eco/eco_applier.h"
#include "eda/eco/eco_diff.h"
#include "eda/geometry/types.h"
#include "eda/library/symbol.h"
#include "eda/pcb/board.h"
#include "eda/pcb/net.h"
#include "eda/schematic/component.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/power_port.h"
#include "eda/schematic/schematic.h"
#include "eda/schematic/wire.h"

using namespace eda;
using namespace eda::geometry;
using namespace eda::sch;
using namespace eda::pcb;
using namespace eda::eco;

namespace {

// 1 引脚水平排列的电阻符号，用于构造最小连通原理图（与 test_schematic 同款）。
Ref<Symbol> make_one_pin_symbol() {
    Ref<Symbol> sym(new Symbol());
    sym->set_name("Resistor");
    sym->set_designation("R?");
    sym->add_pin(Pin("1", "P1", Point{0, 0}, ElectricalType::PASSIVE,
                      Orientation::RIGHT, 0));
    return sym;
}

// 构造一个仅含单个"普通"命名网络的原理图（NetLabel 命名，非电源）。
// 通过 1-pin 器件 + 导线末端 NetLabel 实现。
// 注：Schematic 继承 RefCounted（含 std::atomic），不可拷贝/移动，故采用 out 参数。
void build_schematic_with_net(Schematic& s, const std::string& net_name) {
    Ref<Symbol> sym = make_one_pin_symbol();
    Component u1;
    u1.set_symbol(sym);
    u1.set_refdes("U1");
    u1.set_position(Point{1'000'000, 1'000'000});
    s.add_component(std::move(u1));
    s.add_wire(Wire(Point{1'000'000, 1'000'000}, Point{2'000'000, 1'000'000}));
    s.add_net_label(NetLabel(net_name, Point{2'000'000, 1'000'000}));
}

// 构造一个含单个"电源"命名网络的原理图（PowerPort 命名，is_power=true）。
void build_schematic_with_power_net(Schematic& s, const std::string& net_name) {
    Ref<Symbol> sym = make_one_pin_symbol();
    Component u1;
    u1.set_symbol(sym);
    u1.set_refdes("U1");
    u1.set_position(Point{1'000'000, 1'000'000});
    s.add_component(std::move(u1));
    s.add_wire(Wire(Point{1'000'000, 1'000'000}, Point{1'000'000, 2'000'000}));
    s.add_power_port(PowerPort(net_name, Point{1'000'000, 2'000'000}));
}

// 在 Board 上查找名为 name 的 Net（找不到返回 nullptr）。
// Net 在 sch / pcb 两个命名空间都有定义，故显式限定 eda::pcb::Net。
const eda::pcb::Net* find_board_net_by_name(const Board& b, const std::string& name) {
    for (const auto& n : b.nets()) {
        if (n && n->name() == name) return n.get();
    }
    return nullptr;
}

// 把 diff 结果转为 "TYPE:uuid" 字符串集合，便于断言。
std::set<std::string> diff_summary(const std::vector<EcoItem>& items) {
    std::set<std::string> out;
    for (const auto& it : items) {
        out.insert(std::string(change_type_to_string(it.type)) + ":" + it.entity_uuid);
    }
    return out;
}

}  // namespace

// ============================================================================
// ChangeType 枚举字符串往返
// ============================================================================

TEST(EcoChangeTypeTest, EnumStringRoundTrip) {
    EXPECT_STREQ(change_type_to_string(ChangeType::ADDED), "ADDED");
    EXPECT_STREQ(change_type_to_string(ChangeType::REMOVED), "REMOVED");
    EXPECT_STREQ(change_type_to_string(ChangeType::MODIFIED), "MODIFIED");

    EXPECT_EQ(change_type_from_string("ADDED"), ChangeType::ADDED);
    EXPECT_EQ(change_type_from_string("REMOVED"), ChangeType::REMOVED);
    EXPECT_EQ(change_type_from_string("MODIFIED"), ChangeType::MODIFIED);
    EXPECT_EQ(change_type_from_string("UNKNOWN"), ChangeType::ADDED);  // 前向兼容
}

// ============================================================================
// EcoDiff::compute_diff —— 核心：ADDED / REMOVED / 一致
// ============================================================================

TEST(EcoDiffTest, SchematicHasVccBoardMissingReportsAdded) {
    // 任务规范用例：Schematic 有 "VCC" 但 Board 无 → ADDED
    Schematic s;
    build_schematic_with_net(s, "VCC");
    Board b;  // 空 Board

    auto items = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ChangeType::ADDED);
    EXPECT_EQ(items[0].entity_type, "Net");
    EXPECT_EQ(items[0].entity_uuid, "VCC");
    // 描述应包含网络名
    EXPECT_NE(items[0].description.find("VCC"), std::string::npos);
}

TEST(EcoDiffTest, BoardHasNetSchematicMissingReportsRemoved) {
    // 任务规范用例：Board 有 Net 但 Schematic 无 → REMOVED
    Schematic s;  // 空原理图（无 component）
    Board b;
    b.add_net("OLD_NET");

    auto items = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ChangeType::REMOVED);
    EXPECT_EQ(items[0].entity_type, "Net");
    EXPECT_EQ(items[0].entity_uuid, "OLD_NET");
}

TEST(EcoDiffTest, BothConsistentReportsNoDiff) {
    // 任务规范用例：双方一致 → 无 diff
    Schematic s;
    build_schematic_with_net(s, "SDA");
    Board b;
    b.add_net("SDA");

    auto items = EcoDiff::compute_diff(s, b);
    EXPECT_TRUE(items.empty());
}

TEST(EcoDiffTest, EmptySchematicAndEmptyBoardProduceNoDiff) {
    Schematic s;
    Board b;
    auto items = EcoDiff::compute_diff(s, b);
    EXPECT_TRUE(items.empty());
}

TEST(EcoDiffTest, MultipleMixedChanges) {
    // 多差异混合：SDA/GND 在双方一致；VCC 仅原理图；N1/N2 仅 PCB
    Schematic s;
    build_schematic_with_net(s, "VCC");
    {
        // 追加第二个普通网络（不同坐标，避免引脚合并到同一连通分量）
        Ref<Symbol> sym = make_one_pin_symbol();
        Component u2;
        u2.set_symbol(sym);
        u2.set_refdes("U2");
        u2.set_position(Point{10'000'000, 10'000'000});
        s.add_component(std::move(u2));
        s.add_wire(Wire(Point{10'000'000, 10'000'000}, Point{11'000'000, 10'000'000}));
        s.add_net_label(NetLabel("SDA", Point{11'000'000, 10'000'000}));
    }

    Board b;
    b.add_net("SDA");   // 与原理图一致
    b.add_net("N1");    // PCB 独有 → REMOVED
    b.add_net("N2");    // PCB 独有 → REMOVED
    // "VCC" 仅原理图 → ADDED

    auto items = EcoDiff::compute_diff(s, b);
    auto summary = diff_summary(items);

    EXPECT_EQ(summary.count("ADDED:VCC"), 1u);
    EXPECT_EQ(summary.count("REMOVED:N1"), 1u);
    EXPECT_EQ(summary.count("REMOVED:N2"), 1u);
    EXPECT_EQ(summary.count("MODIFIED:SDA"), 0u);  // SDA 普通 vs 普通一致
    // 总差异 = 1 ADDED + 2 REMOVED
    EXPECT_EQ(items.size(), 3u);
}

TEST(EcoDiffTest, PowerClassificationMismatchReportsModified) {
    // 原理图侧 is_power=true（PowerPort 命名）；PCB 侧 net_class 非 "power" → MODIFIED
    Schematic s;
    build_schematic_with_power_net(s, "VCC");
    Board b;
    eda::pcb::Net& n = b.add_net("VCC");
    // net_class 默认为空 → "looks_power" 判定为 false → 与原理图 is_power=true 不一致
    EXPECT_TRUE(n.net_class().empty());
    auto items = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ChangeType::MODIFIED);
    EXPECT_EQ(items[0].entity_uuid, "VCC");
}

TEST(EcoDiffTest, PowerClassificationMatchWhenBoardNetClassIsPower) {
    // 原理图 is_power=true；PCB net_class="power" → 一致，无 diff
    Schematic s;
    build_schematic_with_power_net(s, "VCC");
    Board b;
    eda::pcb::Net& n = b.add_net("VCC");
    n.set_net_class("power");

    auto items = EcoDiff::compute_diff(s, b);
    EXPECT_TRUE(items.empty());
}

TEST(EcoDiffTest, DiffIsDeterministicAcrossRuns) {
    Schematic s;
    build_schematic_with_net(s, "VCC");
    Board b;
    b.add_net("GND");

    auto a1 = EcoDiff::compute_diff(s, b);
    auto a2 = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(a1.size(), a2.size());
    for (std::size_t i = 0; i < a1.size(); ++i) {
        EXPECT_EQ(a1[i].type, a2[i].type);
        EXPECT_EQ(a1[i].entity_uuid, a2[i].entity_uuid);
        EXPECT_EQ(a1[i].entity_type, a2[i].entity_type);
    }
}

// ============================================================================
// EcoApplier::apply —— 无 UndoStack（直连模式）
// ============================================================================

TEST(EcoApplierTest, ApplyAddedWithoutStackCreatesNetOnBoard) {
    // 任务规范主用例：diff 报 ADDED → apply → Board 新增 Net
    Schematic s;
    build_schematic_with_net(s, "VCC");
    Board b;

    auto items = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(items.size(), 1u);
    ASSERT_EQ(items[0].type, ChangeType::ADDED);

    EcoApplier applier;  // 无 UndoStack
    ApplyResult r = applier.apply(items, b);

    EXPECT_EQ(r.applied_count, 1u);
    EXPECT_EQ(r.skipped_count, 0u);
    EXPECT_TRUE(r.ok());
    // Board 上确实有了 VCC
    const eda::pcb::Net* n = find_board_net_by_name(b, "VCC");
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->name(), "VCC");
}

TEST(EcoApplierTest, ApplyRemovedWithoutStackSkipsWithErrors) {
    // REMOVED 无法在骨架阶段落地（Board 无 remove_net）
    Board b;
    b.add_net("OLD_NET");
    std::vector<EcoItem> changes = {
        {ChangeType::REMOVED, "Net", "OLD_NET", "Remove net OLD_NET"},
    };

    EcoApplier applier;
    ApplyResult r = applier.apply(changes, b);

    EXPECT_EQ(r.applied_count, 0u);
    EXPECT_EQ(r.skipped_count, 1u);
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_NE(r.errors[0].find("OLD_NET"), std::string::npos);
    // Board 上 OLD_NET 仍在
    EXPECT_NE(find_board_net_by_name(b, "OLD_NET"), nullptr);
}

TEST(EcoApplierTest, ApplyModifiedSkipsWithErrors) {
    Board b;
    b.add_net("VCC");
    std::vector<EcoItem> changes = {
        {ChangeType::MODIFIED, "Net", "VCC", "Classification changed"},
    };

    EcoApplier applier;
    ApplyResult r = applier.apply(changes, b);

    EXPECT_EQ(r.applied_count, 0u);
    EXPECT_EQ(r.skipped_count, 1u);
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_NE(r.errors[0].find("VCC"), std::string::npos);
}

TEST(EcoApplierTest, ApplyEmptyChangesIsNoOp) {
    Board b;
    b.add_net("PRE");
    EcoApplier applier;
    ApplyResult r = applier.apply({}, b);

    EXPECT_EQ(r.applied_count, 0u);
    EXPECT_EQ(r.skipped_count, 0u);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(b.net_count(), 1u);  // 板上原 Net 不动
}

TEST(EcoApplierTest, ApplyUnsupportedEntityTypeSkipped) {
    Board b;
    std::vector<EcoItem> changes = {
        {ChangeType::ADDED, "Component", "C1", "Add component C1"},
    };
    EcoApplier applier;
    ApplyResult r = applier.apply(changes, b);

    EXPECT_EQ(r.applied_count, 0u);
    EXPECT_EQ(r.skipped_count, 1u);
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_NE(r.errors[0].find("Component"), std::string::npos);
}

// ============================================================================
// EcoApplier::apply —— 有 UndoStack（命令化）
// ============================================================================

TEST(EcoApplierTest, ApplyWithStackPushesMacroAndCreatesNet) {
    Schematic s;
    build_schematic_with_net(s, "VCC");
    Board b;

    auto items = EcoDiff::compute_diff(s, b);

    command::UndoStack stack;
    EcoApplier applier(&stack);
    ApplyResult r = applier.apply(items, b);

    EXPECT_EQ(r.applied_count, 1u);
    EXPECT_EQ(r.skipped_count, 0u);
    EXPECT_TRUE(r.ok());

    // Board 上有 VCC
    EXPECT_NE(find_board_net_by_name(b, "VCC"), nullptr);
    // UndoStack 上有 1 条命令（macro 合并了 1 个 AddNetCommand）
    EXPECT_EQ(stack.size(), 1u);
}

TEST(EcoApplierTest, ApplyWithStackMultipleNetsSingleMacro) {
    // 多条 ADDED 在一个 macro 里 → 1 个撤销单元
    std::vector<EcoItem> changes = {
        {ChangeType::ADDED, "Net", "VCC", ""},
        {ChangeType::ADDED, "Net", "GND", ""},
        {ChangeType::ADDED, "Net", "SDA", ""},
    };
    Board b;
    command::UndoStack stack;
    EcoApplier applier(&stack);

    ApplyResult r = applier.apply(changes, b);
    EXPECT_EQ(r.applied_count, 3u);
    EXPECT_EQ(b.net_count(), 3u);
    EXPECT_EQ(stack.size(), 1u);  // 整批合并为单 macro
}

TEST(EcoApplierTest, ApplyWithStackMixedChanges) {
    // 混合：2 ADDED + 1 REMOVED + 1 MODIFIED
    Board b;
    b.add_net("TO_REMOVE");
    b.add_net("TO_MODIFY");
    std::vector<EcoItem> changes = {
        {ChangeType::ADDED,   "Net", "NEW1", ""},
        {ChangeType::ADDED,   "Net", "NEW2", ""},
        {ChangeType::REMOVED, "Net", "TO_REMOVE", ""},
        {ChangeType::MODIFIED,"Net", "TO_MODIFY", ""},
    };

    command::UndoStack stack;
    EcoApplier applier(&stack);
    ApplyResult r = applier.apply(changes, b);

    EXPECT_EQ(r.applied_count, 2u);  // 2 ADDED 落地
    EXPECT_EQ(r.skipped_count, 2u);  // 1 REMOVED + 1 MODIFIED 跳过
    EXPECT_EQ(r.errors.size(), 2u);
    EXPECT_EQ(b.net_count(), 4u);    // 原 2 + 新 2
    EXPECT_NE(find_board_net_by_name(b, "NEW1"), nullptr);
    EXPECT_NE(find_board_net_by_name(b, "NEW2"), nullptr);
    // 原 Net 仍在（REMOVED 跳过）
    EXPECT_NE(find_board_net_by_name(b, "TO_REMOVE"), nullptr);
}

// ============================================================================
// 端到端：compute_diff → apply → 再 diff（同步达成）
// ============================================================================

TEST(EcoEndToEndTest, ForwardSyncBringsBoardIntoAgreement) {
    // 原理图有 VCC + SDA；Board 仅有 SDA + STALE
    // diff: VCC ADDED, STALE REMOVED
    // apply（骨架）：VCC 落地、STALE 跳过
    // 再 diff：只剩 STALE 的 REMOVED（VCC 已同步）
    Schematic s;
    build_schematic_with_net(s, "VCC");
    {
        Ref<Symbol> sym = make_one_pin_symbol();
        Component u2;
        u2.set_symbol(sym);
        u2.set_refdes("U2");
        u2.set_position(Point{20'000'000, 20'000'000});
        s.add_component(std::move(u2));
        s.add_wire(Wire(Point{20'000'000, 20'000'000}, Point{21'000'000, 20'000'000}));
        s.add_net_label(NetLabel("SDA", Point{21'000'000, 20'000'000}));
    }

    Board b;
    b.add_net("SDA");
    b.add_net("STALE");

    // 第 1 次 diff
    auto before = EcoDiff::compute_diff(s, b);
    ASSERT_EQ(before.size(), 2u);
    auto sb = diff_summary(before);
    EXPECT_EQ(sb.count("ADDED:VCC"), 1u);
    EXPECT_EQ(sb.count("REMOVED:STALE"), 1u);

    // 应用
    EcoApplier applier;
    ApplyResult r = applier.apply(before, b);
    EXPECT_EQ(r.applied_count, 1u);   // VCC
    EXPECT_EQ(r.skipped_count, 1u);   // STALE 跳过

    // 第 2 次 diff：VCC 已同步，只剩 STALE 的 REMOVED
    auto after = EcoDiff::compute_diff(s, b);
    auto sa = diff_summary(after);
    EXPECT_EQ(sa.count("ADDED:VCC"), 0u);        // 已同步
    EXPECT_EQ(sa.count("REMOVED:STALE"), 1u);    // 仍未处理（骨架限制）
    EXPECT_EQ(after.size(), 1u);
}
