// eda/pcb/editor/pcb_editor.cpp
//
// PCB 编辑器交互模块实现。
//
// 命令族（namespace eda::pcb::detail，全部经 UndoStack 入栈，可撤销）：
//   * MovePointCommand   —— 移动 Pad/Via（基于 position 前/后快照）
//   * MoveTrackCommand   —— 移动 Track（基于 path 前/后快照）
//   * RotateTrackCommand —— 旋转 Track 90°（path 前/后快照）
//   * DeleteEntityCommand—— 逻辑删除（editor.mark_hidden / mark_visible）
//   * AddTrackCommand    —— 添加走线（首次 board.add_track；undo mark_hidden）
//   * AddPadCommand      —— 添加焊盘（同上）
//   * AddViaCommand      —— 添加过孔（同上）
//
// 命令的 execute/undo/redo 语义统一：
//   - execute 首次由 UndoStack::push 触发，应用"新状态"。
//   - undo 恢复"旧状态"。
//   - redo 再次应用"新状态"（C++ 拷贝快照，无副作用累积）。
//   对于"移动 / 旋转"，编辑器在 mouse_up 时实体已被实时拖拽到新状态，
//   故 push 后 execute 再写一次新状态属幂等 no-op，但语义一致、安全。

#include "eda/pcb/editor/pcb_editor.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/object/signal.h"
#include "eda/command/command.h"
#include "eda/command/undo_stack.h"
#include "eda/pcb/board.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"
#include "scene/2d/canvas_2d.h"

namespace eda::pcb {

// ============================================================================
// 内部常量
// ============================================================================

namespace {

// GLFW 键码（与 editor/shortcuts/input_map.cpp 的常量一致，本模块不引入 eda_editor）。
constexpr int kKeyR = 82;        // GLFW_KEY_R
constexpr int kKeyDelete = 261;  // GLFW_KEY_DELETE
constexpr int kKeyEscape = 256;  // GLFW_KEY_ESCAPE

// 默认走线/焊盘/过孔参数（骨架版，单位 nm）。
constexpr geometry::Coord kDefaultTrackWidth = 200 * 1000;   // 0.2 mm
constexpr geometry::Coord kDefaultPadSize = 500 * 1000;      // 0.5 mm
constexpr geometry::Coord kDefaultViaDrill = 300 * 1000;     // 0.3 mm
constexpr geometry::Coord kDefaultViaPad = 600 * 1000;       // 0.6 mm

const char* kDefaultLayer = "F.Cu";
const char* kDefaultBottomLayer = "B.Cu";

// 把路径绕其质心逆时针旋转 90°：(dx, dy) → (-dy, dx)。
geometry::Path rotate_path_90_ccw(const geometry::Path& path) {
    if (path.points.empty()) return path;
    double cx = 0.0;
    double cy = 0.0;
    for (const auto& pt : path.points) {
        cx += static_cast<double>(pt.x);
        cy += static_cast<double>(pt.y);
    }
    cx /= static_cast<double>(path.points.size());
    cy /= static_cast<double>(path.points.size());

    geometry::Path out = path;
    for (auto& pt : out.points) {
        const double dx = static_cast<double>(pt.x) - cx;
        const double dy = static_cast<double>(pt.y) - cy;
        // CCW 90°: (dx, dy) → (-dy, dx)
        pt.x = static_cast<geometry::Coord>(std::round(cx - dy));
        pt.y = static_cast<geometry::Coord>(std::round(cy + dx));
    }
    return out;
}

// 按 UUID 在 Board 中查找活跃（非隐藏）图元。
PcbEntity* find_active_entity(Board* board, const std::string& uuid) {
    if (!board) return nullptr;
    PcbEntity* e = board->find_by_uuid(uuid);
    return e;
}

}  // namespace

// ============================================================================
// detail 命令族（经 UndoStack 入栈，可撤销）
// ============================================================================

namespace detail {

// 移动 Pad/Via（point entity）：execute 写新位置，undo 写旧位置。
class MovePointCommand : public command::Command {
public:
    MovePointCommand(PcbEntity* entity, geometry::Point old_pos, geometry::Point new_pos,
                     std::string name = "Move Entity")
        : Command(std::move(name)),
          entity_(entity),
          old_pos_(old_pos),
          new_pos_(new_pos) {}

    void execute() override { apply(new_pos_); }
    void undo() override { apply(old_pos_); }
    void redo() override { execute(); }

    static std::string get_class_static() { return "MovePointCommand"; }
    std::string get_class() const override { return "MovePointCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "MovePointCommand" || Command::is_class(n);
    }

private:
    PcbEntity* entity_;
    geometry::Point old_pos_;
    geometry::Point new_pos_;

    void apply(geometry::Point p) {
        if (entity_->get_class() == "Pad") {
            static_cast<Pad*>(entity_)->set_position(p);
        } else if (entity_->get_class() == "Via") {
            static_cast<Via*>(entity_)->set_position(p);
        }
    }
};

// 移动 Track：execute 写新路径，undo 写旧路径。
class MoveTrackCommand : public command::Command {
public:
    MoveTrackCommand(Track* track, geometry::Path old_path, geometry::Path new_path,
                     std::string name = "Move Track")
        : Command(std::move(name)),
          track_(track),
          old_path_(std::move(old_path)),
          new_path_(std::move(new_path)) {}

    void execute() override { track_->set_path(new_path_); }
    void undo() override { track_->set_path(old_path_); }
    void redo() override { execute(); }

    static std::string get_class_static() { return "MoveTrackCommand"; }
    std::string get_class() const override { return "MoveTrackCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "MoveTrackCommand" || Command::is_class(n);
    }

private:
    Track* track_;
    geometry::Path old_path_;
    geometry::Path new_path_;
};

// 旋转 Track 90°（CCW）。
class RotateTrackCommand : public command::Command {
public:
    RotateTrackCommand(Track* track, geometry::Path old_path, geometry::Path new_path,
                       std::string name = "Rotate Track")
        : Command(std::move(name)),
          track_(track),
          old_path_(std::move(old_path)),
          new_path_(std::move(new_path)) {}

    void execute() override { track_->set_path(new_path_); }
    void undo() override { track_->set_path(old_path_); }
    void redo() override { execute(); }

    static std::string get_class_static() { return "RotateTrackCommand"; }
    std::string get_class() const override { return "RotateTrackCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "RotateTrackCommand" || Command::is_class(n);
    }

private:
    Track* track_;
    geometry::Path old_path_;
    geometry::Path new_path_;
};

// 逻辑删除：execute mark_hidden，undo mark_visible。
class DeleteEntityCommand : public command::Command {
public:
    DeleteEntityCommand(PcbEditor* editor, std::string uuid,
                        std::string name = "Delete Entity")
        : Command(std::move(name)), editor_(editor), uuid_(std::move(uuid)) {}

    void execute() override {
        editor_->mark_hidden(uuid_);
        editor_->remove_from_selection(uuid_);
        editor_->notify_board_modified();
    }
    void undo() override {
        editor_->mark_visible(uuid_);
        editor_->notify_board_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "DeleteEntityCommand"; }
    std::string get_class() const override { return "DeleteEntityCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "DeleteEntityCommand" || Command::is_class(n);
    }

private:
    PcbEditor* editor_;
    std::string uuid_;
};

// 添加走线：首次 execute 调 board.add_track 并记录 UUID；
// 后续 execute（redo）只 mark_visible。undo mark_hidden。
class AddTrackCommand : public command::Command {
public:
    AddTrackCommand(PcbEditor* editor, Board* board, geometry::Path path,
                    std::string layer_id, std::string name = "Add Track")
        : Command(std::move(name)),
          editor_(editor),
          board_(board),
          path_(std::move(path)),
          layer_id_(std::move(layer_id)) {}

    void execute() override {
        if (!created_) {
            Track& t = board_->add_track(path_, layer_id_);
            created_uuid_ = t.uuid();
            created_ = true;
        } else {
            editor_->mark_visible(created_uuid_);
        }
        editor_->notify_board_modified();
    }
    void undo() override {
        editor_->mark_hidden(created_uuid_);
        editor_->notify_board_modified();
    }
    void redo() override { execute(); }

    const std::string& created_uuid() const { return created_uuid_; }

    static std::string get_class_static() { return "AddTrackCommand"; }
    std::string get_class() const override { return "AddTrackCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddTrackCommand" || Command::is_class(n);
    }

private:
    PcbEditor* editor_;
    Board* board_;
    geometry::Path path_;
    std::string layer_id_;
    std::string created_uuid_;
    bool created_ = false;
};

// 添加焊盘（语义同 AddTrackCommand，针对 Pad 构造参数）。
class AddPadCommand : public command::Command {
public:
    AddPadCommand(PcbEditor* editor, Board* board, geometry::Point position,
                  std::string name = "Add Pad")
        : Command(std::move(name)),
          editor_(editor),
          board_(board),
          position_(position) {}

    void execute() override {
        if (!created_) {
            Pad& p = board_->add_pad(position_, kDefaultPadSize, kDefaultPadSize,
                                     PadShape::RECT, kDefaultLayer, "");
            created_uuid_ = p.uuid();
            created_ = true;
        } else {
            editor_->mark_visible(created_uuid_);
        }
        editor_->notify_board_modified();
    }
    void undo() override {
        editor_->mark_hidden(created_uuid_);
        editor_->notify_board_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "AddPadCommand"; }
    std::string get_class() const override { return "AddPadCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddPadCommand" || Command::is_class(n);
    }

private:
    PcbEditor* editor_;
    Board* board_;
    geometry::Point position_;
    std::string created_uuid_;
    bool created_ = false;
};

// 添加过孔。
class AddViaCommand : public command::Command {
public:
    AddViaCommand(PcbEditor* editor, Board* board, geometry::Point position,
                  std::string name = "Add Via")
        : Command(std::move(name)),
          editor_(editor),
          board_(board),
          position_(position) {}

    void execute() override {
        if (!created_) {
            Via& v = board_->add_via(position_, kDefaultViaDrill, kDefaultViaPad,
                                     kDefaultLayer, kDefaultBottomLayer, "");
            created_uuid_ = v.uuid();
            created_ = true;
        } else {
            editor_->mark_visible(created_uuid_);
        }
        editor_->notify_board_modified();
    }
    void undo() override {
        editor_->mark_hidden(created_uuid_);
        editor_->notify_board_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "AddViaCommand"; }
    std::string get_class() const override { return "AddViaCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddViaCommand" || Command::is_class(n);
    }

private:
    PcbEditor* editor_;
    Board* board_;
    geometry::Point position_;
    std::string created_uuid_;
    bool created_ = false;
};

}  // namespace detail

// ============================================================================
// PcbEditor 公共 API
// ============================================================================

PcbEditor::PcbEditor() = default;
PcbEditor::~PcbEditor() = default;

void PcbEditor::set_tool(Tool tool) { tool_ = tool; }

void PcbEditor::set_board(Board* board) { board_ = board; }

void PcbEditor::set_undo_stack(command::UndoStack* stack) { undo_stack_ = stack; }

void PcbEditor::set_zoom(float pixels_per_nm) {
    zoom_ = pixels_per_nm > 0.0f ? pixels_per_nm : 1.0f;
}

void PcbEditor::set_offset(Vector2 screen_offset) { offset_ = screen_offset; }

geometry::Point PcbEditor::screen_to_board(Vector2 screen) const {
    const float bx = (screen.x - offset_.x) / zoom_;
    const float by = (screen.y - offset_.y) / zoom_;
    return geometry::Point{static_cast<geometry::Coord>(std::round(bx)),
                           static_cast<geometry::Coord>(std::round(by))};
}

Vector2 PcbEditor::board_to_screen(geometry::Point board) const {
    return Vector2{static_cast<float>(board.x) * zoom_ + offset_.x,
                   static_cast<float>(board.y) * zoom_ + offset_.y};
}

bool PcbEditor::is_selected(const std::string& uuid) const {
    return std::find(selection_.begin(), selection_.end(), uuid) != selection_.end();
}

void PcbEditor::clear_selection() {
    if (selection_.empty()) return;
    selection_.clear();
    emit_selection_changed();
}

void PcbEditor::select_only(const std::string& uuid) {
    if (selection_.size() == 1 && selection_.front() == uuid) return;
    selection_.clear();
    selection_.push_back(uuid);
    emit_selection_changed();
}

void PcbEditor::mark_hidden(const std::string& uuid) { hidden_uuids_.insert(uuid); }

void PcbEditor::mark_visible(const std::string& uuid) { hidden_uuids_.erase(uuid); }

void PcbEditor::remove_from_selection(const std::string& uuid) {
    auto it = std::find(selection_.begin(), selection_.end(), uuid);
    if (it != selection_.end()) {
        selection_.erase(it);
        emit_selection_changed();
    }
}

// ============================================================================
// hit_test：点击命中测试（用 bounding_box，跳过隐藏 UUID）
// ============================================================================

PcbEntity* PcbEditor::hit_test_board(geometry::Point p) {
    if (!board_) return nullptr;
    // 顺序：Via → Pad → Track（视觉上"画在上面"的先命中）。
    for (auto& v : board_->vias()) {
        if (is_hidden(v->uuid())) continue;
        if (v->bounding_box().contains(p)) return v.get();
    }
    for (auto& pad : board_->pads()) {
        if (is_hidden(pad->uuid())) continue;
        if (pad->bounding_box().contains(p)) return pad.get();
    }
    for (auto& t : board_->tracks()) {
        if (is_hidden(t->uuid())) continue;
        if (t->bounding_box().contains(p)) return t.get();
    }
    return nullptr;
}

// ============================================================================
// 输入分发
// ============================================================================

void PcbEditor::handle_mouse_down(Vector2 pos, MouseButton button) {
    mouse_screen_ = pos;
    if (!board_) return;
    if (button == MouseButton::RIGHT) {
        cancel_action();
        return;
    }
    if (button != MouseButton::LEFT) return;

    switch (tool_) {
        case Tool::SELECT: on_select_down(pos); break;
        case Tool::TRACK: on_track_down(pos); break;
        case Tool::PAD: on_pad_down(pos); break;
        case Tool::VIA: on_via_down(pos); break;
        case Tool::DELETE: on_delete_down(pos); break;
    }
}

void PcbEditor::handle_mouse_move(Vector2 pos) {
    mouse_screen_ = pos;
    if (!board_) return;
    switch (tool_) {
        case Tool::SELECT: on_select_move(pos); break;
        case Tool::TRACK: on_track_move(pos); break;
        default: break;
    }
}

void PcbEditor::handle_mouse_up(Vector2 pos) {
    mouse_screen_ = pos;
    if (!board_) return;
    switch (tool_) {
        case Tool::SELECT: on_select_up(pos); break;
        case Tool::TRACK: on_track_up(pos); break;
        default: break;
    }
}

void PcbEditor::handle_key(KeyAction action, int key) {
    if (action != KeyAction::PRESS) return;
    if (key == kKeyR) {
        rotate_selected_90();
    } else if (key == kKeyDelete) {
        delete_selected();
    } else if (key == kKeyEscape) {
        cancel_action();
        clear_selection();
    }
}

void PcbEditor::cancel_action() {
    track_placing_ = false;
    dragging_ = false;
    drag_entity_ = nullptr;
}

// ============================================================================
// SELECT 工具
// ============================================================================

void PcbEditor::on_select_down(Vector2 pos) {
    const geometry::Point bp = screen_to_board(pos);
    PcbEntity* hit = hit_test_board(bp);
    if (hit) {
        select_only(hit->uuid());
        dragging_ = true;
        drag_entity_ = hit;
        drag_start_board_ = bp;
        if (hit->get_class() == "Track") {
            drag_old_path_ = static_cast<Track*>(hit)->path();
        } else if (hit->get_class() == "Pad") {
            drag_old_position_ = static_cast<Pad*>(hit)->position();
        } else if (hit->get_class() == "Via") {
            drag_old_position_ = static_cast<Via*>(hit)->position();
        }
    } else {
        clear_selection();
    }
}

void PcbEditor::on_select_move(Vector2 pos) {
    if (!dragging_ || !drag_entity_) return;
    const geometry::Point bp = screen_to_board(pos);
    const geometry::Point delta{bp.x - drag_start_board_.x, bp.y - drag_start_board_.y};

    if (drag_entity_->get_class() == "Track") {
        geometry::Path new_path = drag_old_path_;
        for (auto& pt : new_path.points) {
            pt.x += delta.x;
            pt.y += delta.y;
        }
        static_cast<Track*>(drag_entity_)->set_path(new_path);
    } else if (drag_entity_->get_class() == "Pad") {
        Pad* p = static_cast<Pad*>(drag_entity_);
        p->set_position({drag_old_position_.x + delta.x, drag_old_position_.y + delta.y});
    } else if (drag_entity_->get_class() == "Via") {
        Via* v = static_cast<Via*>(drag_entity_);
        v->set_position({drag_old_position_.x + delta.x, drag_old_position_.y + delta.y});
    }
}

void PcbEditor::on_select_up(Vector2 /*pos*/) {
    if (!dragging_ || !drag_entity_) {
        dragging_ = false;
        drag_entity_ = nullptr;
        return;
    }
    PcbEntity* e = drag_entity_;

    if (e->get_class() == "Track") {
        Track* t = static_cast<Track*>(e);
        geometry::Path new_path = t->path();  // 当前（拖拽后）路径
        if (undo_stack_) {
            undo_stack_->push(
                std::make_unique<detail::MoveTrackCommand>(t, drag_old_path_, new_path));
        }
    } else if (e->get_class() == "Pad") {
        Pad* p = static_cast<Pad*>(e);
        const geometry::Point new_pos = p->position();
        if (undo_stack_) {
            undo_stack_->push(
                std::make_unique<detail::MovePointCommand>(p, drag_old_position_, new_pos));
        }
    } else if (e->get_class() == "Via") {
        Via* v = static_cast<Via*>(e);
        const geometry::Point new_pos = v->position();
        if (undo_stack_) {
            undo_stack_->push(
                std::make_unique<detail::MovePointCommand>(v, drag_old_position_, new_pos));
        }
    }

    dragging_ = false;
    drag_entity_ = nullptr;
    emit_board_modified();
}

// ============================================================================
// TRACK 工具：按下两点放置走线（mouse_down 设起点，mouse_up 完成放置）
// ============================================================================

void PcbEditor::on_track_down(Vector2 pos) {
    const geometry::Point bp = screen_to_board(pos);
    track_start_ = bp;
    track_current_ = bp;
    track_placing_ = true;
}

void PcbEditor::on_track_move(Vector2 pos) {
    if (!track_placing_) return;
    track_current_ = screen_to_board(pos);
}

void PcbEditor::on_track_up(Vector2 pos) {
    if (!track_placing_) return;
    track_placing_ = false;
    const geometry::Point end = screen_to_board(pos);

    // 零长度走线不创建（避免误触产生空 Track）。
    if (end.x == track_start_.x && end.y == track_start_.y) return;

    geometry::Path path{{track_start_, end}, kDefaultTrackWidth};
    if (undo_stack_) {
        undo_stack_->push(
            std::make_unique<detail::AddTrackCommand>(this, board_, path, kDefaultLayer));
    } else {
        board_->add_track(path, kDefaultLayer);
    }
    emit_board_modified();
}

// ============================================================================
// PAD / VIA 工具：单击放置
// ============================================================================

void PcbEditor::on_pad_down(Vector2 pos) {
    const geometry::Point bp = screen_to_board(pos);
    if (undo_stack_) {
        undo_stack_->push(std::make_unique<detail::AddPadCommand>(this, board_, bp));
    } else {
        board_->add_pad(bp, kDefaultPadSize, kDefaultPadSize, PadShape::RECT, kDefaultLayer, "");
    }
    emit_board_modified();
}

void PcbEditor::on_via_down(Vector2 pos) {
    const geometry::Point bp = screen_to_board(pos);
    if (undo_stack_) {
        undo_stack_->push(std::make_unique<detail::AddViaCommand>(this, board_, bp));
    } else {
        board_->add_via(bp, kDefaultViaDrill, kDefaultViaPad, kDefaultLayer, kDefaultBottomLayer,
                        "");
    }
    emit_board_modified();
}

// ============================================================================
// DELETE 工具：点击删除图元（命令化）
// ============================================================================

void PcbEditor::on_delete_down(Vector2 pos) {
    const geometry::Point bp = screen_to_board(pos);
    PcbEntity* hit = hit_test_board(bp);
    if (!hit) return;
    const std::string uuid = hit->uuid();
    if (undo_stack_) {
        undo_stack_->push(std::make_unique<detail::DeleteEntityCommand>(this, uuid));
    } else {
        mark_hidden(uuid);
        remove_from_selection(uuid);
    }
    emit_board_modified();
}

// ============================================================================
// 键盘动作
// ============================================================================

void PcbEditor::rotate_selected_90() {
    if (selection_.empty() || !board_) return;
    // 仅 Track 支持旋转（Pad/Via 无旋转字段——圆形对称，旋转无几何意义）。
    for (const auto& uuid : selection_) {
        PcbEntity* e = find_active_entity(board_, uuid);
        if (!e || is_hidden(uuid)) continue;
        if (e->get_class() != "Track") continue;
        Track* t = static_cast<Track*>(e);
        geometry::Path old_path = t->path();
        geometry::Path new_path = rotate_path_90_ccw(old_path);
        if (undo_stack_) {
            undo_stack_->push(
                std::make_unique<detail::RotateTrackCommand>(t, old_path, new_path));
        } else {
            t->set_path(new_path);
        }
    }
    emit_board_modified();
}

void PcbEditor::delete_selected() {
    if (selection_.empty()) return;
    // 拷贝一份：command 执行时会修改 selection_。
    std::vector<std::string> snapshot = selection_;
    for (const auto& uuid : snapshot) {
        if (undo_stack_) {
            undo_stack_->push(std::make_unique<detail::DeleteEntityCommand>(this, uuid));
        } else {
            mark_hidden(uuid);
            remove_from_selection(uuid);
        }
    }
    emit_board_modified();
}

// ============================================================================
// 渲染覆盖层
// ============================================================================

void PcbEditor::render_overlay(Canvas2D& canvas) {
    if (!board_) return;

    // 1. 选择高亮框（4 条线绘制 AABB 轮廓）
    if (!selection_.empty()) {
        canvas.set_color(Color{0.0f, 1.0f, 1.0f, 1.0f});  // 青色
        canvas.set_line_thickness(1.0f);
        for (const auto& uuid : selection_) {
            const PcbEntity* e = board_->find_by_uuid(uuid);
            if (!e) continue;
            const geometry::Box bb = e->bounding_box();
            const Vector2 tl = board_to_screen(bb.min);
            const Vector2 br = board_to_screen(bb.max);
            const Vector2 tr{br.x, tl.y};
            const Vector2 bl{tl.x, br.y};
            canvas.draw_line(tl, tr);
            canvas.draw_line(tr, br);
            canvas.draw_line(br, bl);
            canvas.draw_line(bl, tl);
        }
    }

    // 2. TRACK 预览线段
    if (track_placing_) {
        canvas.set_color(Color{1.0f, 1.0f, 0.0f, 1.0f});  // 黄色
        canvas.set_line_thickness(2.0f);
        const Vector2 a = board_to_screen(track_start_);
        const Vector2 b = board_to_screen(track_current_);
        canvas.draw_line(a, b);
    }

    // 3. 十字光标
    canvas.set_color(Color{0.5f, 0.5f, 0.5f, 1.0f});  // 灰色
    canvas.set_line_thickness(1.0f);
    canvas.draw_line({mouse_screen_.x - 10.0f, mouse_screen_.y},
                     {mouse_screen_.x + 10.0f, mouse_screen_.y});
    canvas.draw_line({mouse_screen_.x, mouse_screen_.y - 10.0f},
                     {mouse_screen_.x, mouse_screen_.y + 10.0f});
}

}  // namespace eda::pcb
