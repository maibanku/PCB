#pragma once

// eda/pcb/editor/pcb_editor.h
//
// PCB 编辑器交互模块（P8 编辑器交互层）。
//
// 依赖：
//   - P8 Board / Track / Pad / Via         （eda/pcb/board.h）
//   - P3 UndoStack / Command                （eda/command/undo_stack.h）
//   - P1 Canvas2D / InputEvent              （scene/2d/canvas_2d.h、core/input/input_event.h）
//
// 职责：
//   * 维护当前工具（SELECT/TRACK/PAD/VIA/DELETE）的状态机。
//   * 把鼠标/键盘事件翻译为对 Board 的编辑动作（命令化，经 UndoStack 入栈）。
//   * 维护选择集 + 视图变换（屏幕像素 ↔ Board nm）。
//   * 通过 Canvas2D 绘制编辑覆盖层（选择高亮 / TRACK 预览 / 十字光标）。
//
// 命令化策略（为何用 editor 端 hidden_uuids_ 而非 Board::remove_*）：
//   Board 当前只暴露 add_*，没有 remove_*。为遵守"只读既有源文件"的约束，
//   本编辑器把"删除"实现为"逻辑隐藏"——在 editor 侧维护 hidden_uuids_ 集合，
//   hit_test / render / 选择 主动跳过其中的 UUID。
//   DeleteEntityCommand / AddTrackCommand 因此可逆：
//     - 删除：execute 把 UUID 加入 hidden；undo 把 UUID 移出 hidden。
//     - 添加走线：execute 调 board.add_track（首次）并记录 UUID；
//                 undo 把 UUID 加入 hidden（视觉上消失）；redo 移出 hidden。
//   实际 Board 中的对象不被销毁，UUID 稳定，撤销/重做完全可逆。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>
#include <unordered_set>
#include <vector>

#include "core/input/input_event.h"
#include "core/math/vector2.h"
#include "core/object/signal.h"
#include "eda/geometry/types.h"

namespace eda {
class Canvas2D;
}  // namespace eda

namespace eda::command {
class UndoStack;
}  // namespace eda::command

namespace eda::pcb {

class Board;
class PcbEntity;

// ============================================================================
// Tool —— 当前编辑工具状态机。
// ============================================================================
enum class Tool {
    SELECT,  // 选择/移动图元
    TRACK,   // 放置走线（按下两点）
    PAD,     // 放置孤立焊盘（单击）
    VIA,     // 放置过孔（单击）
    DELETE,  // 点击删除图元
};

namespace detail {
class MovePointCommand;
class MoveTrackCommand;
class RotateTrackCommand;
class DeleteEntityCommand;
class AddTrackCommand;
class AddPadCommand;
class AddViaCommand;
}  // namespace detail

// ============================================================================
// PcbEditor —— PCB 编辑器交互核心。
// ============================================================================
//
// 不可拷贝：内部持有 Signal<>，需稳定地址。
//
// 使用范式：
//   PcbEditor editor;
//   editor.set_board(&board);
//   editor.set_undo_stack(&stack);
//   editor.set_zoom(0.001f);          // 1 px = 1 μm
//   editor.set_offset({100, 100});    // 板原点在屏幕 (100, 100)
//   editor.set_tool(Tool::SELECT);
//   editor.handle_mouse_down(pos, MouseButton::LEFT);
//   editor.handle_mouse_move(pos);
//   editor.handle_mouse_up(pos);
//   editor.render_overlay(canvas);
//
class PcbEditor {
public:
    PcbEditor();
    ~PcbEditor();

    PcbEditor(const PcbEditor&) = delete;
    PcbEditor& operator=(const PcbEditor&) = delete;
    PcbEditor(PcbEditor&&) = delete;
    PcbEditor& operator=(PcbEditor&&) = delete;

    // ---------------- 工具 ----------------

    void set_tool(Tool tool);
    Tool get_tool() const { return tool_; }

    // ---------------- Board / UndoStack ----------------

    void set_board(Board* board);
    Board* get_board() { return board_; }
    const Board* get_board() const { return board_; }

    void set_undo_stack(command::UndoStack* stack);
    command::UndoStack* get_undo_stack() { return undo_stack_; }

    // ---------------- 视图变换（屏幕像素 ↔ Board nm）----------------
    //
    // 约定：
    //   board_nm = (screen_px - offset) / zoom
    //   screen_px = board_nm * zoom + offset
    //   zoom 单位：像素 / nm（如 0.001f 表示 1 px = 1 μm = 1000 nm）
    //   offset 单位：像素，表示板原点 (0,0) 在屏幕上的位置
    //
    void set_zoom(float pixels_per_nm);
    float get_zoom() const { return zoom_; }

    void set_offset(Vector2 screen_offset);
    Vector2 get_offset() const { return offset_; }

    geometry::Point screen_to_board(Vector2 screen) const;
    Vector2 board_to_screen(geometry::Point board) const;

    // ---------------- 输入 ----------------

    // 鼠标按下：LEFT 触发工具主动作，RIGHT 取消当前动作（等同 Esc）。
    void handle_mouse_down(Vector2 pos, MouseButton button);
    // 鼠标移动：更新十字光标 + SELECT 拖拽 / TRACK 预览。
    void handle_mouse_move(Vector2 pos);
    // 鼠标抬起：SELECT 提交移动命令 / TRACK 完成走线放置。
    void handle_mouse_up(Vector2 pos);

    // 键盘：仅响应 PRESS。R = 旋转 90°，Delete = 删除选中，Esc = 取消。
    void handle_key(KeyAction action, int key);

    // ---------------- 渲染覆盖层 ----------------

    // 在 Canvas2D 上绘制：选择高亮框 + TRACK 预览线段 + 十字光标。
    // 调用方负责先绘制 Board 本体，再调用本方法叠加覆盖层。
    void render_overlay(Canvas2D& canvas);

    // ---------------- 选择集 ----------------

    const std::vector<std::string>& selection() const { return selection_; }
    bool is_selected(const std::string& uuid) const;
    void clear_selection();

    // ---------------- 可见性（"逻辑删除"标记）----------------

    // UUID 是否被标记为隐藏（删除命令的视觉效果）。
    bool is_hidden(const std::string& uuid) const { return hidden_uuids_.count(uuid) > 0; }
    // 隐藏 / 取消隐藏（供 detail 命令类使用）。
    void mark_hidden(const std::string& uuid);
    void mark_visible(const std::string& uuid);

    // ---------------- 信号 ----------------

    Signal<>& selection_changed() { return selection_changed_; }
    Signal<>& board_modified() { return board_modified_; }

private:
    Board* board_ = nullptr;
    command::UndoStack* undo_stack_ = nullptr;
    Tool tool_ = Tool::SELECT;

    // 视图变换
    float zoom_ = 1.0f;    // 像素 / nm
    Vector2 offset_{};     // 板原点的屏幕位置（像素）

    // 选择集（UUID 列表；当前实现以单选为主，但用 vector 便于扩展多选）
    std::vector<std::string> selection_;

    // "逻辑删除"集合：删除命令把 UUID 加入此处；hit_test/render/选择跳过。
    std::unordered_set<std::string> hidden_uuids_;

    // SELECT 拖拽状态
    bool dragging_ = false;
    PcbEntity* drag_entity_ = nullptr;
    geometry::Point drag_start_board_{};   // 鼠标按下时的板坐标
    geometry::Point drag_old_position_{};  // 拖拽前的位置（Pad/Via）
    geometry::Path drag_old_path_{};       // 拖拽前的路径（Track）

    // TRACK 放置状态
    bool track_placing_ = false;
    geometry::Point track_start_{};      // 走线起点（板坐标）
    geometry::Point track_current_{};    // 当前鼠标（板坐标，预览用）

    Vector2 mouse_screen_{};  // 最近一次鼠标位置（屏幕像素，绘十字光标用）

    // ---- 内部工具 ----
    PcbEntity* hit_test_board(geometry::Point p);
    void select_only(const std::string& uuid);
    void rotate_selected_90();
    void delete_selected();
    void cancel_action();
    void emit_selection_changed() { selection_changed_.emit(); }
    void emit_board_modified() { board_modified_.emit(); }

    // 工具分发
    void on_select_down(Vector2 pos);
    void on_select_move(Vector2 pos);
    void on_select_up(Vector2 pos);
    void on_track_down(Vector2 pos);
    void on_track_move(Vector2 pos);
    void on_track_up(Vector2 pos);
    void on_pad_down(Vector2 pos);
    void on_via_down(Vector2 pos);
    void on_delete_down(Vector2 pos);

    // 供 detail 命令类访问的内部通知（命令执行后刷新信号）。
    void notify_board_modified() { emit_board_modified(); }
    void notify_selection_changed() { emit_selection_changed(); }
    // 从选择集中移除指定 UUID（删除命令用）。
    void remove_from_selection(const std::string& uuid);

    Signal<> selection_changed_;
    Signal<> board_modified_;

    // detail 命令类需要访问 mark_hidden / mark_visible / remove_from_selection /
    // notify_* 等内部接口以实现命令化（execute/undo/redo）。
    friend class detail::MovePointCommand;
    friend class detail::MoveTrackCommand;
    friend class detail::RotateTrackCommand;
    friend class detail::DeleteEntityCommand;
    friend class detail::AddTrackCommand;
    friend class detail::AddPadCommand;
    friend class detail::AddViaCommand;
};

}  // namespace eda::pcb
