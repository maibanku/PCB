// tests/test_schematic_editor.cpp
//
// 原理图编辑器交互模块测试。
//
// 覆盖：
//   - 工具切换（set_tool/get_tool，enum 覆盖 5 种工具）
//   - Schematic / UndoStack 挂接 + 默认 Symbol getter/setter
//   - 视图变换：screen_to_schematic / schematic_to_screen（zoom + offset 往返）
//   - COMPONENT 工具：单击放置 → refdes 自动递增（R1/R2/R3）+ 命令化 + undo/redo
//   - WIRE 工具：click-click 与 click-drag 两种模式 → AddWireCommand + undo/redo
//   - NET_LABEL 工具：单击放置 → AddNetLabelCommand + undo/redo
//   - SELECT 工具：命中 → 选中（selection_changed 信号）；空白点击 → 清空
//   - SELECT 拖拽移动：mouse_down → move → up，位置更新 + MoveComponentCommand 入栈 + undo 还原
//   - 旋转：R 键 → Rotation 90° CCW 步进 + RotateComponentCommand 入栈 + undo/redo
//   - 删除命令化：Delete 键 / DELETE 工具 → DeleteComponentCommand 入栈 + undo 还原（含 refdes/position）
//   - Esc 取消：取消 WIRE 放置 + 清空选择
//   - render_overlay：选择高亮框 + WIRE 预览 + 十字光标生成顶点
//   - schematic_modified / selection_changed 信号在编辑动作后触发
//   - 无 UndoStack 时仍可工作（直接应用，不入栈）
//
// 集成：tests/CMakeLists.txt 追加 test_schematic_editor，link eda_schematic_editor。

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/input/input_event.h"
#include "core/math/vector2.h"
#include "core/object/ref.h"
#include "eda/command/undo_stack.h"
#include "eda/geometry/types.h"
#include "eda/library/symbol.h"
#include "eda/schematic/component.h"
#include "eda/schematic/editor/schematic_editor.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/schematic.h"
#include "eda/schematic/wire.h"
#include "scene/2d/canvas_2d.h"

using namespace eda;
using namespace eda::geometry;
using namespace eda::sch;
using namespace eda::command;

namespace {

constexpr Coord kMm = 1'000'000;  // 1 mm = 1_000_000 nm

// GLFW 键码（与 schematic_editor.cpp 内部常量一致）。
constexpr int kKeyR = 82;
constexpr int kKeyDelete = 261;
constexpr int kKeyEscape = 256;

// 构造简单电阻符号：N 个引脚沿 +x 等距排列，designation "R?"。
Ref<Symbol> make_resistor_symbol(int pin_count = 2, Coord pitch_nm = 2'000'000) {
    Ref<Symbol> sym(new Symbol());
    sym->set_name("Resistor");
    sym->set_designation("R?");
    for (int i = 0; i < pin_count; ++i) {
        const std::string num = std::to_string(i + 1);
        const Point local{(i - (pin_count - 1) / 2.0) * pitch_nm, 0};
        sym->add_pin(Pin(num, ("P" + num), local, ElectricalType::PASSIVE,
                         Orientation::RIGHT, 0));
    }
    return sym;
}

// 测试夹具：Schematic + UndoStack + SchematicEditor + 默认电阻符号。
struct EditorFixture {
    Schematic sch;
    UndoStack stack;
    SchematicEditor editor;
    Ref<Symbol> sym;

    EditorFixture() {
        sym = make_resistor_symbol(2);
        editor.set_schematic(&sch);
        editor.set_undo_stack(&stack);
        editor.set_default_symbol(sym);
    }
};

// 预置一个器件到 Schematic（直接 API，不经 UndoStack 入栈），返回其引用。
// 用于"移动 / 旋转 / 删除"类测试的初始状态搭建，与 PcbEditor 测试里
// f.board.add_pad / add_track / add_via 的直接预置模式保持一致——这样后续断言
// f.stack.size() 只反映被测的那一次编辑动作，不被放置命令污染。
Component& place_component(Schematic& sch, Ref<Symbol> sym, Point pos,
                           std::string refdes = "R1") {
    Component c;  // 构造即自动生成 UUID
    c.set_refdes(std::move(refdes));
    c.set_position(pos);
    c.set_symbol(sym);
    return sch.add_component(std::move(c));
}

}  // namespace

// ============================================================
// 工具切换
// ============================================================

TEST(SchEditorToolTest, DefaultToolIsSelect) {
    SchematicEditor editor;
    EXPECT_EQ(editor.get_tool(), Tool::SELECT);
}

TEST(SchEditorToolTest, SetGetToolRoundTrip) {
    SchematicEditor editor;
    editor.set_tool(Tool::COMPONENT);
    EXPECT_EQ(editor.get_tool(), Tool::COMPONENT);
    editor.set_tool(Tool::WIRE);
    EXPECT_EQ(editor.get_tool(), Tool::WIRE);
    editor.set_tool(Tool::NET_LABEL);
    EXPECT_EQ(editor.get_tool(), Tool::NET_LABEL);
    editor.set_tool(Tool::DELETE);
    EXPECT_EQ(editor.get_tool(), Tool::DELETE);
    editor.set_tool(Tool::SELECT);
    EXPECT_EQ(editor.get_tool(), Tool::SELECT);
}

// ============================================================
// Schematic / UndoStack / Symbol 挂接
// ============================================================

TEST(SchEditorBindingTest, SetSchematicAndGetSchematic) {
    SchematicEditor editor;
    Schematic sch;
    editor.set_schematic(&sch);
    EXPECT_EQ(editor.get_schematic(), &sch);
}

TEST(SchEditorBindingTest, SetUndoStackAndGetUndoStack) {
    SchematicEditor editor;
    UndoStack stack;
    editor.set_undo_stack(&stack);
    EXPECT_EQ(editor.get_undo_stack(), &stack);
}

TEST(SchEditorBindingTest, SetDefaultSymbolRoundTrip) {
    SchematicEditor editor;
    auto sym = make_resistor_symbol();
    editor.set_default_symbol(sym);
    EXPECT_TRUE(editor.get_default_symbol().valid());
    EXPECT_EQ(editor.get_default_symbol()->designation(), "R?");
}

TEST(SchEditorBindingTest, DefaultNetNameIsSDA) {
    SchematicEditor editor;
    EXPECT_EQ(editor.default_net_name(), "SDA");
    editor.set_default_net_name("SCL");
    EXPECT_EQ(editor.default_net_name(), "SCL");
}

// ============================================================
// 视图变换：屏幕像素 ↔ Schematic nm
// ============================================================

TEST(SchEditorTransformTest, ScreenToSchematicWithZoomAndOffset) {
    const Point p = SchematicEditor::screen_to_schematic(Vector2{500.0f, 700.0f},
                                                         0.001f, Vector2{});
    EXPECT_EQ(p.x, 500 * 1000);
    EXPECT_EQ(p.y, 700 * 1000);
}

TEST(SchEditorTransformTest, ScreenToSchematicRespectsOffset) {
    const Point origin = SchematicEditor::screen_to_schematic(Vector2{100.0f, 200.0f},
                                                              0.001f, Vector2{100.0f, 200.0f});
    EXPECT_EQ(origin.x, 0);
    EXPECT_EQ(origin.y, 0);
}

TEST(SchEditorTransformTest, SchematicToScreenRoundTrip) {
    const Point sch_pt{1 * kMm, 2 * kMm};
    const Vector2 screen = SchematicEditor::schematic_to_screen(sch_pt, 0.002f, Vector2{50.0f, 50.0f});
    EXPECT_FLOAT_EQ(screen.x, 1'000'000.0f * 0.002f + 50.0f);
    EXPECT_FLOAT_EQ(screen.y, 2'000'000.0f * 0.002f + 50.0f);

    const Point back = SchematicEditor::screen_to_schematic(screen, 0.002f, Vector2{50.0f, 50.0f});
    EXPECT_EQ(back.x, 1 * kMm);
    EXPECT_EQ(back.y, 2 * kMm);
}

TEST(SchEditorTransformTest, ZeroZoomClampedToOne) {
    // zoom=0 不应导致除零；内部回落到 1.0，结果 = screen - offset。
    const Point p = SchematicEditor::screen_to_schematic(Vector2{10.0f, 20.0f}, 0.0f,
                                                         Vector2{5.0f, 5.0f});
    EXPECT_EQ(p.x, 5);
    EXPECT_EQ(p.y, 15);
}

// ============================================================
// COMPONENT 工具：放置器件 + refdes 自动递增
// ============================================================

TEST(SchEditorComponentToolTest, PlacementIncrementsRefdes) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);

    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    f.editor.handle_mouse_down(Point{2 * kMm, 1 * kMm});
    f.editor.handle_mouse_down(Point{3 * kMm, 1 * kMm});

    ASSERT_EQ(f.sch.component_count(), 3u);
    EXPECT_EQ(f.sch.component(0).refdes(), "R1");
    EXPECT_EQ(f.sch.component(1).refdes(), "R2");
    EXPECT_EQ(f.sch.component(2).refdes(), "R3");

    EXPECT_EQ(f.sch.component(0).position().x, 1 * kMm);
    EXPECT_EQ(f.sch.component(1).position().x, 2 * kMm);
    EXPECT_EQ(f.sch.component(2).position().x, 3 * kMm);

    // 默认 Symbol 被引用到每个 Component 上。
    EXPECT_TRUE(f.sch.component(0).has_symbol());
    EXPECT_EQ(f.sch.component(0).rotation_degrees(), 0);

    EXPECT_EQ(f.stack.size(), 3u);
}

TEST(SchEditorComponentToolTest, PlacementCommandifiedAndUndoable) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);

    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    ASSERT_EQ(f.sch.component_count(), 1u);
    ASSERT_EQ(f.stack.size(), 1u);

    // undo → 器件消失
    f.stack.undo();
    EXPECT_EQ(f.sch.component_count(), 0u);

    // redo → 器件恢复（含同 UUID、同 refdes）
    f.stack.redo();
    ASSERT_EQ(f.sch.component_count(), 1u);
    EXPECT_EQ(f.sch.component(0).refdes(), "R1");
    EXPECT_EQ(f.sch.component(0).position().x, 1 * kMm);
}

TEST(SchEditorComponentToolTest, PlacementRespectsExistingRefdes) {
    EditorFixture f;
    // 预置一个 R5：allocate_refdes 应取 max=5，下一个=R6。
    Component existing;
    existing.set_refdes("R5");
    existing.set_symbol(f.sym);
    f.sch.add_component(std::move(existing));
    ASSERT_EQ(f.sch.component_count(), 1u);

    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    ASSERT_EQ(f.sch.component_count(), 2u);
    EXPECT_EQ(f.sch.component(1).refdes(), "R6");
}

TEST(SchEditorComponentToolTest, PlacementWithoutStackStillAdds) {
    Schematic sch;
    SchematicEditor editor;
    editor.set_schematic(&sch);  // 不设 undo_stack
    editor.set_default_symbol(make_resistor_symbol());
    editor.set_tool(Tool::COMPONENT);

    editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    EXPECT_EQ(sch.component_count(), 1u);
    EXPECT_EQ(sch.component(0).refdes(), "R1");
}

// ============================================================
// WIRE 工具：click-click / click-drag → AddWireCommand
// ============================================================

TEST(SchEditorWireToolTest, ClickClickPlacementAddsWire) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);

    // 第一次点击：起点
    f.editor.handle_mouse_down(Point{0, 0});
    EXPECT_EQ(f.sch.wire_count(), 0u);

    // 预览
    f.editor.handle_mouse_move(Point{5 * kMm, 0});
    EXPECT_EQ(f.sch.wire_count(), 0u);

    // 第二次点击：完成
    f.editor.handle_mouse_down(Point{5 * kMm, 0});
    ASSERT_EQ(f.sch.wire_count(), 1u);
    EXPECT_EQ(f.sch.wire(0).a(), (Point{0, 0}));
    EXPECT_EQ(f.sch.wire(0).b(), (Point{5 * kMm, 0}));
    EXPECT_EQ(f.stack.size(), 1u);
}

TEST(SchEditorWireToolTest, ClickDragPlacementAddsWire) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);

    f.editor.handle_mouse_down(Point{0, 0});
    f.editor.handle_mouse_move(Point{3 * kMm, 4 * kMm});
    f.editor.handle_mouse_up(Point{3 * kMm, 4 * kMm});

    ASSERT_EQ(f.sch.wire_count(), 1u);
    EXPECT_EQ(f.sch.wire(0).a(), (Point{0, 0}));
    EXPECT_EQ(f.sch.wire(0).b(), (Point{3 * kMm, 4 * kMm}));
    EXPECT_EQ(f.stack.size(), 1u);
}

TEST(SchEditorWireToolTest, ZeroLengthWireNotCreated) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);

    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    f.editor.handle_mouse_up(Point{1 * kMm, 1 * kMm});  // 同点

    EXPECT_EQ(f.sch.wire_count(), 0u);
    EXPECT_EQ(f.stack.size(), 0u);
}

TEST(SchEditorWireToolTest, AddWireUndoRedo) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);

    f.editor.handle_mouse_down(Point{0, 0});
    f.editor.handle_mouse_up(Point{5 * kMm, 0});
    ASSERT_EQ(f.sch.wire_count(), 1u);

    f.stack.undo();
    EXPECT_EQ(f.sch.wire_count(), 0u);

    f.stack.redo();
    EXPECT_EQ(f.sch.wire_count(), 1u);
    EXPECT_EQ(f.sch.wire(0).b().x, 5 * kMm);
}

// ============================================================
// NET_LABEL 工具：单击放置
// ============================================================

TEST(SchEditorNetLabelToolTest, SingleClickPlacesLabel) {
    EditorFixture f;
    f.editor.set_default_net_name("SDA");
    f.editor.set_tool(Tool::NET_LABEL);

    f.editor.handle_mouse_down(Point{2 * kMm, 3 * kMm});

    ASSERT_EQ(f.sch.net_label_count(), 1u);
    EXPECT_EQ(f.sch.net_label(0).net_name(), "SDA");
    EXPECT_EQ(f.sch.net_label(0).position().x, 2 * kMm);
    EXPECT_EQ(f.sch.net_label(0).position().y, 3 * kMm);
    EXPECT_EQ(f.stack.size(), 1u);
}

TEST(SchEditorNetLabelToolTest, NetLabelUndoRedo) {
    EditorFixture f;
    f.editor.set_tool(Tool::NET_LABEL);
    f.editor.handle_mouse_down(Point{0, 0});
    ASSERT_EQ(f.sch.net_label_count(), 1u);

    f.stack.undo();
    EXPECT_EQ(f.sch.net_label_count(), 0u);

    f.stack.redo();
    EXPECT_EQ(f.sch.net_label_count(), 1u);
}

// ============================================================
// SELECT 工具：命中 + 选择 + 清空
// ============================================================

TEST(SchEditorSelectTest, ClickOnComponentSelectsIt) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    const std::string uuid = f.sch.component(0).uuid();

    int signal_count = 0;
    f.editor.selection_changed().connect([&] { ++signal_count; });

    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});

    EXPECT_EQ(f.editor.selection().size(), 1u);
    EXPECT_TRUE(f.editor.is_selected(uuid));
    EXPECT_GE(signal_count, 1);
}

TEST(SchEditorSelectTest, ClickEmptyAreaClearsSelection) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});

    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_EQ(f.editor.selection().size(), 1u);

    // 远离器件位置点击
    f.editor.handle_mouse_down(Point{500 * kMm, 500 * kMm});
    EXPECT_TRUE(f.editor.selection().empty());
}

TEST(SchEditorSelectTest, ClearSelectionEmitsSignal) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_EQ(f.editor.selection().size(), 1u);

    int count = 0;
    f.editor.selection_changed().connect([&] { ++count; });
    f.editor.clear_selection();
    EXPECT_GE(count, 1);
}

// ============================================================
// SELECT 拖拽移动：实时更新 + 命令化 + undo 还原
// ============================================================

TEST(SchEditorMoveTest, DragComponentUpdatesPositionAndPushesCommand) {
    EditorFixture f;
    const std::string uuid = place_component(f.sch, f.sym, Point{5 * kMm, 5 * kMm}).uuid();

    f.editor.set_tool(Tool::SELECT);
    // 在器件位置按下
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_TRUE(f.editor.is_selected(uuid));

    // 拖拽 Δ=(2mm, 1mm)
    f.editor.handle_mouse_move(Point{7 * kMm, 6 * kMm});
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().x, 7 * kMm);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().y, 6 * kMm);
    EXPECT_EQ(f.stack.size(), 0u);  // 拖拽中不入栈

    // 抬起：提交 MoveComponentCommand
    f.editor.handle_mouse_up(Point{7 * kMm, 6 * kMm});
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().x, 7 * kMm);
    EXPECT_EQ(f.stack.size(), 1u);

    // undo 还原
    f.stack.undo();
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().x, 5 * kMm);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().y, 5 * kMm);

    // redo 重做
    f.stack.redo();
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->position().x, 7 * kMm);
}

TEST(SchEditorMoveTest, NoMoveOnMouseUpLeavesStackEmpty) {
    EditorFixture f;
    place_component(f.sch, f.sym, Point{5 * kMm, 5 * kMm});
    f.editor.set_tool(Tool::SELECT);

    // 按下后不移动直接抬起
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    f.editor.handle_mouse_up(Point{5 * kMm, 5 * kMm});
    EXPECT_EQ(f.stack.size(), 0u);
}

// ============================================================
// 旋转：R 键 → 90° CCW 步进
// ============================================================

TEST(SchEditorRotateTest, RotateKeyStepsNinetyDegrees) {
    EditorFixture f;
    const std::string uuid = place_component(f.sch, f.sym, Point{1 * kMm, 1 * kMm}).uuid();
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 0);

    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});  // 选中
    ASSERT_TRUE(f.editor.is_selected(uuid));

    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 90);
    EXPECT_EQ(f.stack.size(), 1u);

    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 180);

    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 270);

    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 0);

    // undo 撤销最后一次旋转（0° → 270°）
    f.stack.undo();
    EXPECT_EQ(f.sch.find_component_by_uuid(uuid)->rotation_degrees(), 270);
}

TEST(SchEditorRotateTest, RotateWithNoSelectionIsNoop) {
    EditorFixture f;
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.stack.size(), 0u);
}

TEST(SchEditorRotateTest, RotateReleaseKeyIgnored) {
    EditorFixture f;
    place_component(f.sch, f.sym, Point{1 * kMm, 1 * kMm});
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});

    f.editor.handle_key(KeyAction::RELEASE, kKeyR);
    EXPECT_EQ(f.stack.size(), 0u);
}

// ============================================================
// 删除命令化：Delete 键 + DELETE 工具
// ============================================================

TEST(SchEditorDeleteTest, DeleteKeyRemovesSelectedAndCommandify) {
    EditorFixture f;
    const std::string uuid = place_component(f.sch, f.sym, Point{5 * kMm, 5 * kMm}).uuid();
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_TRUE(f.editor.is_selected(uuid));

    f.editor.handle_key(KeyAction::PRESS, kKeyDelete);
    EXPECT_EQ(f.sch.component_count(), 0u);
    EXPECT_TRUE(f.editor.selection().empty());
    EXPECT_EQ(f.stack.size(), 1u);

    // undo → 器件恢复（refdes 与 position 保持）
    f.stack.undo();
    ASSERT_EQ(f.sch.component_count(), 1u);
    EXPECT_EQ(f.sch.component(0).refdes(), "R1");
    EXPECT_EQ(f.sch.component(0).position().x, 5 * kMm);
}

TEST(SchEditorDeleteTest, DeleteToolClickRemovesComponent) {
    EditorFixture f;
    const std::string uuid = place_component(f.sch, f.sym, Point{5 * kMm, 5 * kMm}).uuid();
    ASSERT_EQ(f.sch.component_count(), 1u);

    f.editor.set_tool(Tool::DELETE);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});

    EXPECT_EQ(f.sch.component_count(), 0u);
    EXPECT_EQ(f.stack.size(), 1u);

    f.stack.undo();
    ASSERT_EQ(f.sch.component_count(), 1u);
    EXPECT_EQ(f.sch.component(0).uuid(), uuid);
    EXPECT_EQ(f.sch.component(0).refdes(), "R1");
    EXPECT_EQ(f.sch.component(0).position().x, 5 * kMm);
}

TEST(SchEditorDeleteTest, DeleteToolOnEmptyIsNoop) {
    EditorFixture f;
    f.editor.set_tool(Tool::DELETE);
    f.editor.handle_mouse_down(Point{500 * kMm, 500 * kMm});
    EXPECT_EQ(f.stack.size(), 0u);
    EXPECT_EQ(f.sch.component_count(), 0u);
}

// ============================================================
// Esc 键取消当前动作
// ============================================================

TEST(SchEditorCancelTest, EscapeCancelsWirePlacement) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);
    f.editor.handle_mouse_down(Point{0, 0});

    f.editor.handle_key(KeyAction::PRESS, kKeyEscape);
    // 已取消，再点击空白不应完成放置
    f.editor.handle_mouse_down(Point{5 * kMm, 0});

    EXPECT_EQ(f.sch.wire_count(), 0u);
}

TEST(SchEditorCancelTest, EscapeClearsSelection) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_EQ(f.editor.selection().size(), 1u);

    f.editor.handle_key(KeyAction::PRESS, kKeyEscape);
    EXPECT_TRUE(f.editor.selection().empty());
}

// ============================================================
// render_overlay：选择高亮 + WIRE 预览 + 十字光标
// ============================================================

TEST(SchEditorRenderTest, RenderHighlightForSelection) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Point{5 * kMm, 5 * kMm});
    ASSERT_EQ(f.editor.selection().size(), 1u);

    Canvas2D canvas;
    f.editor.render_overlay(canvas, 0.001f, Vector2{});
    // 选择框 4 线 + 十字 2 线 = 6 线 × 6 顶点 = 36 顶点（最少）
    EXPECT_GE(canvas.vertices().size(), 36u);
}

TEST(SchEditorRenderTest, RenderWirePreviewWhilePlacing) {
    EditorFixture f;
    f.editor.set_tool(Tool::WIRE);
    f.editor.handle_mouse_down(Point{0, 0});
    f.editor.handle_mouse_move(Point{5 * kMm, 0});

    Canvas2D canvas;
    f.editor.render_overlay(canvas, 0.001f, Vector2{});
    // WIRE 预览 1 线（6 顶点） + 十字 2 线（12 顶点）= 18 顶点（最少）
    EXPECT_GE(canvas.vertices().size(), 18u);
}

TEST(SchEditorRenderTest, RenderEmptyWhenNoSchematic) {
    SchematicEditor editor;  // 无 schematic
    Canvas2D canvas;
    editor.render_overlay(canvas, 0.001f, Vector2{});
    EXPECT_EQ(canvas.vertices().size(), 0u);
}

// ============================================================
// schematic_modified 信号在编辑动作后触发
// ============================================================

TEST(SchEditorSignalTest, SchematicModifiedEmittedOnPlacement) {
    EditorFixture f;
    int modified = 0;
    f.editor.schematic_modified().connect([&] { ++modified; });

    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    EXPECT_GE(modified, 1);
}

TEST(SchEditorSignalTest, SchematicModifiedEmittedOnWireAndNetLabel) {
    EditorFixture f;
    int modified = 0;
    f.editor.schematic_modified().connect([&] { ++modified; });

    f.editor.set_tool(Tool::WIRE);
    int before = modified;
    f.editor.handle_mouse_down(Point{0, 0});
    f.editor.handle_mouse_up(Point{3 * kMm, 0});
    EXPECT_GT(modified, before);

    f.editor.set_tool(Tool::NET_LABEL);
    before = modified;
    f.editor.handle_mouse_down(Point{0, 0});
    EXPECT_GT(modified, before);
}

TEST(SchEditorSignalTest, UndoRedoEmitsModified) {
    EditorFixture f;
    f.editor.set_tool(Tool::COMPONENT);
    f.editor.handle_mouse_down(Point{1 * kMm, 1 * kMm});
    ASSERT_EQ(f.sch.component_count(), 1u);

    int modified = 0;
    f.editor.schematic_modified().connect([&] { ++modified; });

    f.stack.undo();
    EXPECT_GE(modified, 1);
    modified = 0;
    f.stack.redo();
    EXPECT_GE(modified, 1);
}
