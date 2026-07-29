// tests/test_pcb_editor.cpp
//
// PCB 编辑器交互模块测试。
//
// 覆盖：
//   - 工具切换（set_tool/get_tool，enum 覆盖 5 种工具）
//   - Board / UndoStack 挂接
//   - 视图变换：screen_to_board / board_to_screen（zoom + offset 往返）
//   - SELECT 工具：点击命中 Track/Pad/Via → 选中（selection_changed 信号）
//   - SELECT 工具：空白处点击 → 清空选择
//   - SELECT 拖拽移动：mouse_down → move → up，实体位置更新 + MoveCommand 入栈 + undo 还原
//   - 旋转：R 键 → Track 路径绕质心旋转 90° CCW + RotateCommand 入栈
//   - 删除命令化：DELETE 键 → DeleteEntityCommand 入栈，is_hidden=true，undo 还原
//   - DELETE 工具：点击图元 → 删除（命令化，undo 还原）
//   - TRACK 工具：mouse_down 起点 → mouse_move 预览 → mouse_up 终点 → AddTrackCommand
//   - PAD / VIA 工具：单击 → AddPad / AddVia 命令
//   - render_overlay：选择高亮 + 预览 + 十字光标生成顶点
//   - board_modified 信号在编辑动作后触发
//   - 命令化策略：所有编辑动作经 UndoStack，undo/redo 完全可逆
//
// 集成：tests/CMakeLists.txt 追加 test_pcb_editor，link eda_pcb_editor。

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/input/input_event.h"
#include "core/math/vector2.h"
#include "eda/command/undo_stack.h"
#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/editor/pcb_editor.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"
#include "scene/2d/canvas_2d.h"

using namespace eda;
using namespace eda::pcb;
using namespace eda::geometry;
using namespace eda::command;

namespace {

constexpr Coord kMm = 1'000'000;  // 1 mm = 1_000_000 nm

// GLFW 键码（与 pcb_editor.cpp 内部常量一致）。
constexpr int kKeyR = 82;
constexpr int kKeyDelete = 261;
constexpr int kKeyEscape = 256;

// 测试夹具：Board + UndoStack + PcbEditor，配置 1 px = 1000 nm（1 μm）、
// 板原点位于屏幕 (0, 0)。screen_to_board(screen) = screen * 1000 nm。
struct EditorFixture {
    Board board;
    UndoStack stack;
    PcbEditor editor;

    EditorFixture() {
        editor.set_board(&board);
        editor.set_undo_stack(&stack);
        editor.set_zoom(0.001f);      // 1 px = 1000 nm
        editor.set_offset(Vector2{});  // 板原点 = 屏幕 (0,0)
    }

    // 屏幕 → 板坐标（zoom=0.001, offset=0）：screen_px * 1000 = 板 nm。
    Point screen_to_board_px(float sx, float sy) const {
        return Point{static_cast<Coord>(sx * 1000.0f), static_cast<Coord>(sy * 1000.0f)};
    }

    Vector2 board_to_screen_px(Point p) const {
        return Vector2{static_cast<float>(p.x) / 1000.0f, static_cast<float>(p.y) / 1000.0f};
    }
};

// 计数 board_modified 信号触发次数。
struct SignalCounter {
    int count = 0;
    SignalCounter() = default;
};

}  // namespace

// ============================================================
// 工具切换
// ============================================================

TEST(PcbEditorToolTest, DefaultToolIsSelect) {
    PcbEditor editor;
    EXPECT_EQ(editor.get_tool(), Tool::SELECT);
}

TEST(PcbEditorToolTest, SetGetToolRoundTrip) {
    PcbEditor editor;
    editor.set_tool(Tool::TRACK);
    EXPECT_EQ(editor.get_tool(), Tool::TRACK);
    editor.set_tool(Tool::PAD);
    EXPECT_EQ(editor.get_tool(), Tool::PAD);
    editor.set_tool(Tool::VIA);
    EXPECT_EQ(editor.get_tool(), Tool::VIA);
    editor.set_tool(Tool::DELETE);
    EXPECT_EQ(editor.get_tool(), Tool::DELETE);
    editor.set_tool(Tool::SELECT);
    EXPECT_EQ(editor.get_tool(), Tool::SELECT);
}

// ============================================================
// Board / UndoStack 挂接
// ============================================================

TEST(PcbEditorBindingTest, SetBoardAndGetBoard) {
    PcbEditor editor;
    Board board;
    editor.set_board(&board);
    EXPECT_EQ(editor.get_board(), &board);
    EXPECT_EQ(editor.get_board()->name(), "Board");
}

TEST(PcbEditorBindingTest, SetUndoStackAndGetUndoStack) {
    PcbEditor editor;
    UndoStack stack;
    editor.set_undo_stack(&stack);
    EXPECT_EQ(editor.get_undo_stack(), &stack);
}

// ============================================================
// 视图变换：屏幕像素 ↔ Board nm
// ============================================================

TEST(PcbEditorTransformTest, ScreenToBoardWithZoomAndOffset) {
    PcbEditor editor;
    editor.set_zoom(0.001f);          // 1 px = 1000 nm
    editor.set_offset(Vector2{});     // 板原点 = 屏幕 (0,0)

    const Point p = editor.screen_to_board(Vector2{500.0f, 700.0f});
    EXPECT_EQ(p.x, 500 * 1000);  // 500 px * 1000 nm/px
    EXPECT_EQ(p.y, 700 * 1000);
}

TEST(PcbEditorTransformTest, ScreenToBoardRespectsOffset) {
    PcbEditor editor;
    editor.set_zoom(0.001f);
    editor.set_offset(Vector2{100.0f, 200.0f});  // 板原点偏移到屏幕 (100, 200)

    // 屏幕 (100, 200) 对应板原点 (0, 0)
    const Point origin = editor.screen_to_board(Vector2{100.0f, 200.0f});
    EXPECT_EQ(origin.x, 0);
    EXPECT_EQ(origin.y, 0);

    // 屏幕 (200, 300) 对应板 (100*1000, 100*1000)
    const Point p = editor.screen_to_board(Vector2{200.0f, 300.0f});
    EXPECT_EQ(p.x, 100 * 1000);
    EXPECT_EQ(p.y, 100 * 1000);
}

TEST(PcbEditorTransformTest, BoardToScreenRoundTrip) {
    PcbEditor editor;
    editor.set_zoom(0.002f);          // 1 px = 500 nm
    editor.set_offset(Vector2{50.0f, 50.0f});

    const Point board_pt{1 * kMm, 2 * kMm};
    const Vector2 screen = editor.board_to_screen(board_pt);
    // 1_000_000 nm * 0.002 px/nm + 50 = 2000 + 50 = 2050
    EXPECT_FLOAT_EQ(screen.x, 2050.0f);
    EXPECT_FLOAT_EQ(screen.y, 2 * 1'000'000.0f * 0.002f + 50.0f);  // 4050

    // 往返：board → screen → board 应回到同一整数 nm（精度内）
    const Point back = editor.screen_to_board(screen);
    EXPECT_EQ(back.x, 1 * kMm);
    EXPECT_EQ(back.y, 2 * kMm);
}

TEST(PcbEditorTransformTest, ZeroZoomClampedToOne) {
    PcbEditor editor;
    editor.set_zoom(0.0f);  // 非法值，应被钳制
    EXPECT_FLOAT_EQ(editor.get_zoom(), 1.0f);
}

// ============================================================
// SELECT 工具：命中测试 + 选择
// ============================================================

TEST(PcbEditorSelectTest, ClickOnTrackSelectsIt) {
    EditorFixture f;
    Track& t = f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    int signal_count = 0;
    f.editor.selection_changed().connect([&] { ++signal_count; });

    // 走线中点 (5mm, 0) → 屏幕 (5000, 0) px
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);

    EXPECT_EQ(f.editor.selection().size(), 1u);
    EXPECT_TRUE(f.editor.is_selected(t.uuid()));
    EXPECT_GE(signal_count, 1);
}

TEST(PcbEditorSelectTest, ClickOnPadSelectsIt) {
    EditorFixture f;
    Pad& p = f.board.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000, PadShape::RECT,
                             "F.Cu", "1");
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{5000.0f, 5000.0f}, MouseButton::LEFT);

    EXPECT_EQ(f.editor.selection().size(), 1u);
    EXPECT_TRUE(f.editor.is_selected(p.uuid()));
}

TEST(PcbEditorSelectTest, ClickOnViaSelectsIt) {
    EditorFixture f;
    Via& v = f.board.add_via(Point{3 * kMm, 3 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{3000.0f, 3000.0f}, MouseButton::LEFT);

    EXPECT_EQ(f.editor.selection().size(), 1u);
    EXPECT_TRUE(f.editor.is_selected(v.uuid()));
}

TEST(PcbEditorSelectTest, ClickEmptyAreaClearsSelection) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    // 先选中
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_EQ(f.editor.selection().size(), 1u);

    // 点击空白
    f.editor.handle_mouse_down(Vector2{50000.0f, 50000.0f}, MouseButton::LEFT);
    EXPECT_TRUE(f.editor.selection().empty());
}

TEST(PcbEditorSelectTest, HiddenEntityNotHit) {
    EditorFixture f;
    Track& t = f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.mark_hidden(t.uuid());  // 模拟已删除
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_TRUE(f.editor.selection().empty());  // 跳过隐藏图元
}

TEST(PcbEditorSelectTest, RightClickDoesNotSelect) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::RIGHT);
    EXPECT_TRUE(f.editor.selection().empty());
}

// ============================================================
// SELECT 拖拽移动：实时更新 + 命令化 + undo 还原
// ============================================================

TEST(PcbEditorMoveTest, DragPadUpdatesPositionAndPushesCommand) {
    EditorFixture f;
    Pad& p = f.board.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000, PadShape::RECT,
                             "F.Cu", "1");
    f.editor.set_tool(Tool::SELECT);

    // 1) 在焊盘中心按下
    f.editor.handle_mouse_down(Vector2{5000.0f, 5000.0f}, MouseButton::LEFT);
    ASSERT_TRUE(f.editor.is_selected(p.uuid()));

    // 2) 拖拽到 (6mm, 5mm)：mouse_move 实时更新位置
    f.editor.handle_mouse_move(Vector2{6000.0f, 5000.0f});
    EXPECT_EQ(p.position().x, 6 * kMm);  // 实时移动
    EXPECT_EQ(p.position().y, 5 * kMm);
    EXPECT_EQ(f.stack.size(), 0u);  // 拖拽中不入栈

    // 3) 抬起：提交 MovePointCommand
    f.editor.handle_mouse_up(Vector2{6000.0f, 5000.0f});
    EXPECT_EQ(p.position().x, 6 * kMm);
    EXPECT_EQ(f.stack.size(), 1u);
    EXPECT_TRUE(f.stack.can_undo());

    // 4) undo 还原
    f.stack.undo();
    EXPECT_EQ(p.position().x, 5 * kMm);
    EXPECT_EQ(p.position().y, 5 * kMm);

    // 5) redo 重做
    f.stack.redo();
    EXPECT_EQ(p.position().x, 6 * kMm);
}

TEST(PcbEditorMoveTest, DragTrackUpdatesPathAndPushesCommand) {
    EditorFixture f;
    Track& t = f.board.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    // 在中点 (5mm, 0) 按下
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);

    // 拖拽 Δ=(2mm, 3mm)
    f.editor.handle_mouse_move(Vector2{7000.0f, 3000.0f});
    ASSERT_EQ(t.path().points.size(), 2u);
    EXPECT_EQ(t.path().points[0].x, 2 * kMm);  // 0 + 2mm
    EXPECT_EQ(t.path().points[0].y, 3 * kMm);  // 0 + 3mm
    EXPECT_EQ(t.path().points[1].x, 12 * kMm);  // 10mm + 2mm
    EXPECT_EQ(t.path().points[1].y, 3 * kMm);

    f.editor.handle_mouse_up(Vector2{7000.0f, 3000.0f});
    EXPECT_EQ(f.stack.size(), 1u);

    f.stack.undo();
    EXPECT_EQ(t.path().points[0].x, 0);
    EXPECT_EQ(t.path().points[0].y, 0);
    EXPECT_EQ(t.path().points[1].x, 10 * kMm);

    f.stack.redo();
    EXPECT_EQ(t.path().points[0].x, 2 * kMm);
}

TEST(PcbEditorMoveTest, DragViaUpdatesPosition) {
    EditorFixture f;
    Via& v = f.board.add_via(Point{3 * kMm, 3 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{3000.0f, 3000.0f}, MouseButton::LEFT);
    f.editor.handle_mouse_move(Vector2{3000.0f, 4000.0f});  // Δ=(0, 1mm)
    f.editor.handle_mouse_up(Vector2{3000.0f, 4000.0f});

    EXPECT_EQ(v.position().x, 3 * kMm);
    EXPECT_EQ(v.position().y, 4 * kMm);
    EXPECT_EQ(f.stack.size(), 1u);
}

// ============================================================
// 旋转：R 键 → Track 旋转 90° CCW
// ============================================================

TEST(PcbEditorRotateTest, RotateTrack90CcwAroundCentroid) {
    EditorFixture f;
    // 水平走线 (0,0) → (10mm, 0)，质心 (5mm, 0)
    Track& t = f.board.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    // 选中
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    ASSERT_TRUE(f.editor.is_selected(t.uuid()));

    // R 键旋转
    f.editor.handle_key(KeyAction::PRESS, kKeyR);

    // (0,0) 相对质心 (5mm,0) = (-5mm, 0) → CCW 90° → (0, -5mm) → 全局 (5mm, -5mm)
    // (10mm,0) 相对质心 = (5mm, 0) → CCW 90° → (0, 5mm) → 全局 (5mm, 5mm)
    ASSERT_EQ(t.path().points.size(), 2u);
    EXPECT_EQ(t.path().points[0].x, 5 * kMm);
    EXPECT_EQ(t.path().points[0].y, -5 * kMm);
    EXPECT_EQ(t.path().points[1].x, 5 * kMm);
    EXPECT_EQ(t.path().points[1].y, 5 * kMm);

    EXPECT_EQ(f.stack.size(), 1u);
    EXPECT_TRUE(f.stack.can_undo());

    // undo 还原水平走线
    f.stack.undo();
    EXPECT_EQ(t.path().points[0].x, 0);
    EXPECT_EQ(t.path().points[0].y, 0);
    EXPECT_EQ(t.path().points[1].x, 10 * kMm);
    EXPECT_EQ(t.path().points[1].y, 0);
}

TEST(PcbEditorRotateTest, RotateWithNoSelectionIsNoop) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    // 不选中，直接按 R
    f.editor.handle_key(KeyAction::PRESS, kKeyR);
    EXPECT_EQ(f.stack.size(), 0u);  // 无动作
}

TEST(PcbEditorRotateTest, RotateReleaseKeyIgnored) {
    EditorFixture f;
    Track& t = f.board.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);

    f.editor.handle_key(KeyAction::RELEASE, kKeyR);  // 仅 PRESS 触发
    EXPECT_EQ(f.stack.size(), 0u);
    EXPECT_EQ(t.path().points[1].x, 10 * kMm);  // 未变
}

// ============================================================
// 删除命令化：DELETE 键 + DELETE 工具
// ============================================================

TEST(PcbEditorDeleteTest, DeleteKeyHidesSelectedAndCommandify) {
    EditorFixture f;
    Track& t = f.board.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    ASSERT_TRUE(f.editor.is_selected(t.uuid()));

    f.editor.handle_key(KeyAction::PRESS, kKeyDelete);

    EXPECT_TRUE(f.editor.is_hidden(t.uuid()));     // 视觉上已"删除"
    EXPECT_TRUE(f.editor.selection().empty());      // 从选择集移除
    EXPECT_EQ(f.stack.size(), 1u);

    // undo 恢复
    f.stack.undo();
    EXPECT_FALSE(f.editor.is_hidden(t.uuid()));

    // 重新命中验证：撤销后该 Track 再次可被选中
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_TRUE(f.editor.is_selected(t.uuid()));
}

TEST(PcbEditorDeleteTest, DeleteToolClickHidesEntity) {
    EditorFixture f;
    Track& t = f.board.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::DELETE);

    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);

    EXPECT_TRUE(f.editor.is_hidden(t.uuid()));
    EXPECT_EQ(f.stack.size(), 1u);

    f.stack.undo();
    EXPECT_FALSE(f.editor.is_hidden(t.uuid()));
}

TEST(PcbEditorDeleteTest, DeleteToolOnEmptyIsNoop) {
    EditorFixture f;
    f.editor.set_tool(Tool::DELETE);
    f.editor.handle_mouse_down(Vector2{50000.0f, 50000.0f}, MouseButton::LEFT);
    EXPECT_EQ(f.stack.size(), 0u);
}

// ============================================================
// TRACK 工具：按下两点放置走线
// ============================================================

TEST(PcbEditorTrackToolTest, TwoPointPlacementAddsTrackCommand) {
    EditorFixture f;
    f.editor.set_tool(Tool::TRACK);

    int modified = 0;
    f.editor.board_modified().connect([&] { ++modified; });

    // 起点 (0,0)
    f.editor.handle_mouse_down(Vector2{0.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_FALSE(f.editor.is_selected(""));  // TRACK 工具不修改选择
    EXPECT_EQ(f.board.track_count(), 0u);

    // 移动预览
    f.editor.handle_mouse_move(Vector2{5000.0f, 0.0f});
    EXPECT_EQ(f.board.track_count(), 0u);  // 仍未提交

    // 抬起完成
    f.editor.handle_mouse_up(Vector2{5000.0f, 0.0f});

    EXPECT_EQ(f.board.track_count(), 1u);
    EXPECT_EQ(f.stack.size(), 1u);
    EXPECT_GE(modified, 1);

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    EXPECT_EQ(tracks[0]->path().points.size(), 2u);
    EXPECT_EQ(tracks[0]->path().points[0].x, 0);
    EXPECT_EQ(tracks[0]->path().points[1].x, 5 * kMm);
    EXPECT_EQ(tracks[0]->width(), 200 * 1000);  // 默认宽度
    EXPECT_EQ(tracks[0]->layer_id(), "F.Cu");
}

TEST(PcbEditorTrackToolTest, ZeroLengthTrackNotCreated) {
    EditorFixture f;
    f.editor.set_tool(Tool::TRACK);

    f.editor.handle_mouse_down(Vector2{1000.0f, 1000.0f}, MouseButton::LEFT);
    f.editor.handle_mouse_up(Vector2{1000.0f, 1000.0f});  // 同点

    EXPECT_EQ(f.board.track_count(), 0u);
    EXPECT_EQ(f.stack.size(), 0u);
}

TEST(PcbEditorTrackToolTest, TrackAddUndoRedo) {
    EditorFixture f;
    f.editor.set_tool(Tool::TRACK);

    f.editor.handle_mouse_down(Vector2{0.0f, 0.0f}, MouseButton::LEFT);
    f.editor.handle_mouse_up(Vector2{5000.0f, 0.0f});
    ASSERT_EQ(f.board.track_count(), 1u);
    const std::string uuid = f.board.tracks()[0]->uuid();

    // undo → 走线隐藏
    f.stack.undo();
    EXPECT_TRUE(f.editor.is_hidden(uuid));
    // hit_test 跳过隐藏：点击该位置不再命中
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Vector2{2500.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_TRUE(f.editor.selection().empty());

    // redo → 走线恢复可见
    f.stack.redo();
    EXPECT_FALSE(f.editor.is_hidden(uuid));
    f.editor.handle_mouse_down(Vector2{2500.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_FALSE(f.editor.selection().empty());
}

// ============================================================
// PAD / VIA 工具：单击放置
// ============================================================

TEST(PcbEditorPadToolTest, SingleClickAddsPadCommand) {
    EditorFixture f;
    f.editor.set_tool(Tool::PAD);

    f.editor.handle_mouse_down(Vector2{3000.0f, 4000.0f}, MouseButton::LEFT);

    EXPECT_EQ(f.board.pad_count(), 1u);
    EXPECT_EQ(f.stack.size(), 1u);
    const auto& pads = f.board.pads();
    ASSERT_EQ(pads.size(), 1u);
    EXPECT_EQ(pads[0]->position().x, 3 * kMm);
    EXPECT_EQ(pads[0]->position().y, 4 * kMm);

    f.stack.undo();
    EXPECT_TRUE(f.editor.is_hidden(pads[0]->uuid()));
    f.stack.redo();
    EXPECT_FALSE(f.editor.is_hidden(pads[0]->uuid()));
}

TEST(PcbEditorViaToolTest, SingleClickAddsViaCommand) {
    EditorFixture f;
    f.editor.set_tool(Tool::VIA);

    f.editor.handle_mouse_down(Vector2{2000.0f, 2000.0f}, MouseButton::LEFT);

    EXPECT_EQ(f.board.via_count(), 1u);
    EXPECT_EQ(f.stack.size(), 1u);
    const auto& vias = f.board.vias();
    ASSERT_EQ(vias.size(), 1u);
    EXPECT_EQ(vias[0]->position().x, 2 * kMm);
    EXPECT_EQ(vias[0]->via_type(), ViaType::THROUGH);
}

// ============================================================
// Esc 键取消当前动作
// ============================================================

TEST(PcbEditorCancelTest, EscapeCancelsTrackPlacement) {
    EditorFixture f;
    f.editor.set_tool(Tool::TRACK);

    f.editor.handle_mouse_down(Vector2{0.0f, 0.0f}, MouseButton::LEFT);
    // 按下后处于 placing 状态；Esc 取消
    f.editor.handle_key(KeyAction::PRESS, kKeyEscape);
    f.editor.handle_mouse_up(Vector2{5000.0f, 0.0f});  // 已取消，不应创建

    EXPECT_EQ(f.board.track_count(), 0u);
}

TEST(PcbEditorCancelTest, EscapeClearsSelection) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    ASSERT_EQ(f.editor.selection().size(), 1u);

    f.editor.handle_key(KeyAction::PRESS, kKeyEscape);
    EXPECT_TRUE(f.editor.selection().empty());
}

// ============================================================
// render_overlay：选择高亮 + TRACK 预览 + 十字光标
// ============================================================

TEST(PcbEditorRenderTest, RenderHighlightForSelection) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);
    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    ASSERT_EQ(f.editor.selection().size(), 1u);

    Canvas2D canvas;
    f.editor.render_overlay(canvas);
    // 选择框 4 线 + 十字 2 线 = 6 线 × 6 顶点 = 36 顶点（最少）
    EXPECT_GE(canvas.vertices().size(), 36u);
}

TEST(PcbEditorRenderTest, RenderTrackPreviewWhilePlacing) {
    EditorFixture f;
    f.editor.set_tool(Tool::TRACK);

    // 按下后移动，处于 placing 状态
    f.editor.handle_mouse_down(Vector2{0.0f, 0.0f}, MouseButton::LEFT);
    f.editor.handle_mouse_move(Vector2{5000.0f, 0.0f});

    Canvas2D canvas;
    f.editor.render_overlay(canvas);
    // TRACK 预览 1 线（6 顶点） + 十字 2 线（12 顶点）= 18 顶点（最少）
    EXPECT_GE(canvas.vertices().size(), 18u);
}

TEST(PcbEditorRenderTest, RenderEmptyWhenNoBoard) {
    PcbEditor editor;  // 无 board
    Canvas2D canvas;
    editor.render_overlay(canvas);  // 不崩溃
    EXPECT_EQ(canvas.vertices().size(), 0u);
}

// ============================================================
// board_modified 信号在编辑动作后触发
// ============================================================

TEST(PcbEditorSignalTest, BoardModifiedEmittedOnEdit) {
    EditorFixture f;
    int modified = 0;
    f.editor.board_modified().connect([&] { ++modified; });

    // PAD 放置触发
    f.editor.set_tool(Tool::PAD);
    f.editor.handle_mouse_down(Vector2{1000.0f, 1000.0f}, MouseButton::LEFT);
    EXPECT_GE(modified, 1);

    // 删除触发
    modified = 0;
    f.editor.set_tool(Tool::DELETE);
    f.editor.handle_mouse_down(Vector2{1000.0f, 1000.0f}, MouseButton::LEFT);
    EXPECT_GE(modified, 1);
}

TEST(PcbEditorSignalTest, SelectionChangedEmittedOnSelect) {
    EditorFixture f;
    f.board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    f.editor.set_tool(Tool::SELECT);

    int count = 0;
    f.editor.selection_changed().connect([&] { ++count; });

    f.editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);
    EXPECT_GE(count, 1);
}

// ============================================================
// 无 UndoStack 时仍可工作（直接应用，不入栈）
// ============================================================

TEST(PcbEditorNoStackTest, DeleteWithoutStackStillHides) {
    Board board;
    PcbEditor editor;
    editor.set_board(&board);  // 不设 undo_stack
    editor.set_zoom(0.001f);
    editor.set_tool(Tool::DELETE);

    Track& t = board.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 200 * 1000}, "F.Cu");
    editor.handle_mouse_down(Vector2{5000.0f, 0.0f}, MouseButton::LEFT);

    EXPECT_TRUE(editor.is_hidden(t.uuid()));
}
