// eda/schematic/editor/schematic_editor.cpp
//
// 原理图编辑器交互模块实现。
//
// 命令族（namespace eda::sch::detail，全部经 UndoStack 入栈，可撤销）：
//   * AddComponentCommand    —— 放置器件（预生成 UUID；execute 加入；undo 移除；redo 重建）
//   * MoveComponentCommand   —— 移动器件（旧/新位置快照；execute 写新；undo 写旧）
//   * RotateComponentCommand —— 旋转器件（旧/新旋转快照；同上）
//   * DeleteComponentCommand —— 删除器件（execute 移除；undo 用快照参数重建）
//   * AddWireCommand         —— 添加导线（execute 末尾追加；undo 末尾移除）
//   * AddNetLabelCommand     —— 添加网络标号（同上）
//
// Component 不可拷贝（仅移动），命令因此不持有 Component 实例，而是持有"构造参数 +
// 预生成 UUID"。execute() 每次重建一个 Component 并以同一 UUID 加入 Schematic，保证
// undo/redo 完全可逆。Wire / NetLabel 是值类型，直接持有值。

#include "eda/schematic/editor/schematic_editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "core/object/ref.h"
#include "core/object/signal.h"
#include "eda/command/command.h"
#include "eda/command/undo_stack.h"
#include "eda/library/symbol.h"
#include "eda/project/project.h"  // generate_uuid
#include "eda/schematic/component.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/schematic.h"
#include "eda/schematic/wire.h"
#include "scene/2d/canvas_2d.h"

namespace eda::sch {

// ============================================================================
// 内部常量
// ============================================================================

namespace {

// GLFW 键码（与 editor/shortcuts/input_map.cpp 的常量一致，本模块不引入 eda_editor）。
constexpr int kKeyR = 82;        // GLFW_KEY_R
constexpr int kKeyDelete = 261;  // GLFW_KEY_DELETE
constexpr int kKeyEscape = 256;  // GLFW_KEY_ESCAPE

// 默认命中容差（无 Symbol 时围绕 position 的命中半边长，单位 nm）。
constexpr geometry::Coord kDefaultHitHalfSize = 1'000'000;  // 1 mm

// 计算 Component 的命中包围盒：
//   - 有 Symbol 时取 position + 各 connection_points 的 AABB；
//   - 无 Symbol 时取 position ± kDefaultHitHalfSize；
//   - 额外留 kDefaultHitHalfSize/2 余量，避免边界点恰好落在边线上 miss。
geometry::Box component_bbox(const Component& c) {
    std::vector<geometry::Point> pts;
    pts.push_back(c.position());
    for (const auto& cp : c.connection_points()) {
        pts.push_back(cp.position);
    }
    geometry::Box box = geometry::Box::from_points(pts);
    // 对称扩展半边余量，保证 contains 判定友好（Box::contains 为左闭右开）。
    constexpr geometry::Coord kPad = kDefaultHitHalfSize / 2;
    box.min.x -= kPad;
    box.min.y -= kPad;
    box.max.x += kPad;
    box.max.y += kPad;
    return box;
}

}  // namespace

// ============================================================================
// detail 命令族（经 UndoStack 入栈，可撤销）
// ============================================================================

namespace detail {

// 放置器件：预生成 UUID；execute 用参数重建 Component 加入；undo 按 UUID 移除；redo 重建。
class AddComponentCommand : public command::Command {
public:
    AddComponentCommand(SchematicEditor* editor, Schematic* schematic, std::string uuid,
                        std::string refdes, geometry::Point position, Rotation rotation,
                        Ref<Symbol> symbol, std::string name = "Add Component")
        : Command(std::move(name)),
          editor_(editor),
          schematic_(schematic),
          uuid_(std::move(uuid)),
          refdes_(std::move(refdes)),
          position_(position),
          rotation_(rotation),
          symbol_(std::move(symbol)) {}

    void execute() override {
        Component c;
        c.assign_uuid_for_test(uuid_);  // 复用同一 UUID，保证 undo/redo 稳定寻址
        c.set_refdes(refdes_);
        c.set_position(position_);
        c.set_rotation(rotation_);
        if (symbol_.valid()) c.set_symbol(symbol_);
        schematic_->add_component(std::move(c));
        editor_->notify_schematic_modified();
    }
    void undo() override {
        schematic_->remove_component_by_uuid(uuid_);
        editor_->notify_schematic_modified();
    }
    void redo() override { execute(); }

    const std::string& created_uuid() const { return uuid_; }

    static std::string get_class_static() { return "AddComponentCommand"; }
    std::string get_class() const override { return "AddComponentCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddComponentCommand" || Command::is_class(n);
    }

private:
    SchematicEditor* editor_;
    Schematic* schematic_;
    std::string uuid_;
    std::string refdes_;
    geometry::Point position_;
    Rotation rotation_;
    Ref<Symbol> symbol_;
};

// 移动器件：execute 写新位置；undo 写旧位置。
class MoveComponentCommand : public command::Command {
public:
    MoveComponentCommand(Schematic* schematic, SchematicEditor* editor, std::string uuid,
                         geometry::Point old_pos, geometry::Point new_pos,
                         std::string name = "Move Component")
        : Command(std::move(name)),
          schematic_(schematic),
          editor_(editor),
          uuid_(std::move(uuid)),
          old_pos_(old_pos),
          new_pos_(new_pos) {}

    void execute() override { apply(new_pos_); }
    void undo() override { apply(old_pos_); }
    void redo() override { execute(); }

    static std::string get_class_static() { return "MoveComponentCommand"; }
    std::string get_class() const override { return "MoveComponentCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "MoveComponentCommand" || Command::is_class(n);
    }

private:
    Schematic* schematic_;
    SchematicEditor* editor_;
    std::string uuid_;
    geometry::Point old_pos_;
    geometry::Point new_pos_;

    void apply(geometry::Point p) {
        Component* c = schematic_->find_component_by_uuid(uuid_);
        if (c) c->set_position(p);
        editor_->notify_schematic_modified();
    }
};

// 旋转器件：execute 写新旋转；undo 写旧旋转。
class RotateComponentCommand : public command::Command {
public:
    RotateComponentCommand(Schematic* schematic, SchematicEditor* editor, std::string uuid,
                           Rotation old_rot, Rotation new_rot,
                           std::string name = "Rotate Component")
        : Command(std::move(name)),
          schematic_(schematic),
          editor_(editor),
          uuid_(std::move(uuid)),
          old_rot_(old_rot),
          new_rot_(new_rot) {}

    void execute() override { apply(new_rot_); }
    void undo() override { apply(old_rot_); }
    void redo() override { execute(); }

    static std::string get_class_static() { return "RotateComponentCommand"; }
    std::string get_class() const override { return "RotateComponentCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "RotateComponentCommand" || Command::is_class(n);
    }

private:
    Schematic* schematic_;
    SchematicEditor* editor_;
    std::string uuid_;
    Rotation old_rot_;
    Rotation new_rot_;

    void apply(Rotation r) {
        Component* c = schematic_->find_component_by_uuid(uuid_);
        if (c) c->set_rotation(r);
        editor_->notify_schematic_modified();
    }
};

// 删除器件：execute 移除；undo 用快照参数重建（同 Add 路径）。
class DeleteComponentCommand : public command::Command {
public:
    DeleteComponentCommand(SchematicEditor* editor, Schematic* schematic, std::string uuid,
                           std::string refdes, geometry::Point position, Rotation rotation,
                           Ref<Symbol> symbol, std::string name = "Delete Component")
        : Command(std::move(name)),
          editor_(editor),
          schematic_(schematic),
          uuid_(std::move(uuid)),
          refdes_(std::move(refdes)),
          position_(position),
          rotation_(rotation),
          symbol_(std::move(symbol)) {}

    void execute() override {
        schematic_->remove_component_by_uuid(uuid_);
        editor_->remove_from_selection(uuid_);
        editor_->notify_schematic_modified();
    }
    void undo() override {
        Component c;
        c.assign_uuid_for_test(uuid_);
        c.set_refdes(refdes_);
        c.set_position(position_);
        c.set_rotation(rotation_);
        if (symbol_.valid()) c.set_symbol(symbol_);
        schematic_->add_component(std::move(c));
        editor_->notify_schematic_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "DeleteComponentCommand"; }
    std::string get_class() const override { return "DeleteComponentCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "DeleteComponentCommand" || Command::is_class(n);
    }

private:
    SchematicEditor* editor_;
    Schematic* schematic_;
    std::string uuid_;
    std::string refdes_;
    geometry::Point position_;
    Rotation rotation_;
    Ref<Symbol> symbol_;
};

// 添加导线：execute 末尾追加；undo 末尾移除。
// LIFO 假设：UndoStack 严格按入栈逆序 undo，每条导线的"末尾"始终对齐自身添加顺序。
class AddWireCommand : public command::Command {
public:
    AddWireCommand(SchematicEditor* editor, Schematic* schematic, Wire wire,
                   std::string name = "Add Wire")
        : Command(std::move(name)),
          editor_(editor),
          schematic_(schematic),
          wire_(std::move(wire)) {}

    void execute() override {
        schematic_->add_wire(wire_);
        editor_->notify_schematic_modified();
    }
    void undo() override {
        if (schematic_->wire_count() > 0) {
            schematic_->remove_wire(schematic_->wire_count() - 1);
        }
        editor_->notify_schematic_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "AddWireCommand"; }
    std::string get_class() const override { return "AddWireCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddWireCommand" || Command::is_class(n);
    }

private:
    SchematicEditor* editor_;
    Schematic* schematic_;
    Wire wire_;
};

// 添加网络标号：execute 末尾追加；undo 末尾移除。
class AddNetLabelCommand : public command::Command {
public:
    AddNetLabelCommand(SchematicEditor* editor, Schematic* schematic, NetLabel label,
                       std::string name = "Add NetLabel")
        : Command(std::move(name)),
          editor_(editor),
          schematic_(schematic),
          label_(std::move(label)) {}

    void execute() override {
        schematic_->add_net_label(label_);
        editor_->notify_schematic_modified();
    }
    void undo() override {
        if (schematic_->net_label_count() > 0) {
            schematic_->remove_net_label(schematic_->net_label_count() - 1);
        }
        editor_->notify_schematic_modified();
    }
    void redo() override { execute(); }

    static std::string get_class_static() { return "AddNetLabelCommand"; }
    std::string get_class() const override { return "AddNetLabelCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddNetLabelCommand" || Command::is_class(n);
    }

private:
    SchematicEditor* editor_;
    Schematic* schematic_;
    NetLabel label_;
};

}  // namespace detail

// ============================================================================
// SchematicEditor 公共 API
// ============================================================================

SchematicEditor::SchematicEditor() = default;
SchematicEditor::~SchematicEditor() = default;

void SchematicEditor::set_schematic(Schematic* schematic) { schematic_ = schematic; }

void SchematicEditor::set_undo_stack(command::UndoStack* stack) { undo_stack_ = stack; }

void SchematicEditor::set_default_symbol(Ref<Symbol> sym) { default_symbol_ = std::move(sym); }

void SchematicEditor::set_default_net_name(std::string name) {
    default_net_name_ = std::move(name);
}

void SchematicEditor::set_tool(Tool tool) { tool_ = tool; }

// ---------------- 视图变换 ----------------

geometry::Point SchematicEditor::screen_to_schematic(Vector2 screen, float zoom, Vector2 offset) {
    const float z = zoom > 0.0f ? zoom : 1.0f;
    const float bx = (screen.x - offset.x) / z;
    const float by = (screen.y - offset.y) / z;
    return geometry::Point{static_cast<geometry::Coord>(std::round(bx)),
                           static_cast<geometry::Coord>(std::round(by))};
}

Vector2 SchematicEditor::schematic_to_screen(geometry::Point sch, float zoom, Vector2 offset) {
    const float z = zoom > 0.0f ? zoom : 1.0f;
    return Vector2{static_cast<float>(sch.x) * z + offset.x,
                   static_cast<float>(sch.y) * z + offset.y};
}

// ---------------- 选择集 ----------------

bool SchematicEditor::is_selected(const std::string& uuid) const {
    return std::find(selection_.begin(), selection_.end(), uuid) != selection_.end();
}

void SchematicEditor::clear_selection() {
    if (selection_.empty()) return;
    selection_.clear();
    emit_selection_changed();
}

void SchematicEditor::select_only(const std::string& uuid) {
    if (selection_.size() == 1 && selection_.front() == uuid) return;
    selection_.clear();
    selection_.push_back(uuid);
    emit_selection_changed();
}

void SchematicEditor::remove_from_selection(const std::string& uuid) {
    auto it = std::find(selection_.begin(), selection_.end(), uuid);
    if (it != selection_.end()) {
        selection_.erase(it);
        emit_selection_changed();
    }
}

// ---------------- hit_test ----------------

Component* SchematicEditor::hit_test_component(geometry::Point p) {
    if (!schematic_) return nullptr;
    // Schematic 仅暴露 const components() 视图；命中后通过 UUID 取可变指针。
    for (const auto& c : schematic_->components()) {
        if (component_bbox(c).contains(p)) {
            return schematic_->find_component_by_uuid(c.uuid());
        }
    }
    return nullptr;
}

const Component* SchematicEditor::hit_test_component(geometry::Point p) const {
    if (!schematic_) return nullptr;
    for (const auto& c : schematic_->components()) {
        if (component_bbox(c).contains(p)) return &c;
    }
    return nullptr;
}

// ---------------- refdes 自动递增 ----------------
//
// 解析 default_symbol_.designation()（如 "R?" / "U?" / "C?"）取前缀字母段；扫描
// schematic_->components() 中同前缀的 refdes，取最大数字后 +1。无 Symbol 或无
// 合法前缀时回落 "R"。无既有器件时从 1 开始。
std::string SchematicEditor::allocate_refdes() const {
    std::string prefix = "R";
    if (default_symbol_.valid()) {
        const std::string& desig = default_symbol_->designation();
        std::string alpha;
        for (char c : desig) {
            if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
                alpha.push_back(c);
            } else {
                break;
            }
        }
        if (!alpha.empty()) prefix = alpha;
    }

    int max_num = 0;
    if (schematic_) {
        for (const auto& c : schematic_->components()) {
            const std::string& r = c.refdes();
            if (r.size() <= prefix.size()) continue;
            if (r.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string rest = r.substr(prefix.size());
            bool all_digit = !rest.empty();
            for (char ch : rest) {
                if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
                    all_digit = false;
                    break;
                }
            }
            if (all_digit) {
                const int n = std::stoi(rest);
                if (n > max_num) max_num = n;
            }
        }
    }
    return prefix + std::to_string(max_num + 1);
}

// ---------------- 输入分发 ----------------

void SchematicEditor::handle_mouse_down(geometry::Point pos) {
    cursor_ = pos;
    if (!schematic_) return;

    switch (tool_) {
        case Tool::SELECT:     on_select_down(pos); break;
        case Tool::COMPONENT:  on_component_down(pos); break;
        case Tool::WIRE:       on_wire_down(pos); break;
        case Tool::NET_LABEL:  on_net_label_down(pos); break;
        case Tool::DELETE:     on_delete_down(pos); break;
    }
}

void SchematicEditor::handle_mouse_move(geometry::Point pos) {
    cursor_ = pos;
    if (!schematic_) return;
    switch (tool_) {
        case Tool::SELECT: on_select_move(pos); break;
        case Tool::WIRE:   on_wire_move(pos); break;
        default: break;
    }
}

void SchematicEditor::handle_mouse_up(geometry::Point pos) {
    cursor_ = pos;
    if (!schematic_) return;
    switch (tool_) {
        case Tool::SELECT: on_select_up(pos); break;
        case Tool::WIRE:   on_wire_up(pos); break;
        default: break;
    }
}

void SchematicEditor::handle_key(KeyAction action, int key) {
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

void SchematicEditor::cancel_action() {
    wire_placing_ = false;
    dragging_ = false;
    drag_uuid_.clear();
}

// ============================================================================
// SELECT 工具
// ============================================================================

void SchematicEditor::on_select_down(geometry::Point pos) {
    Component* hit = hit_test_component(pos);
    if (hit) {
        select_only(hit->uuid());
        dragging_ = true;
        drag_uuid_ = hit->uuid();
        drag_start_ = pos;
        drag_old_position_ = hit->position();
    } else {
        clear_selection();
    }
}

void SchematicEditor::on_select_move(geometry::Point pos) {
    if (!dragging_ || drag_uuid_.empty()) return;
    Component* c = schematic_->find_component_by_uuid(drag_uuid_);
    if (!c) return;
    // 增量 = 当前鼠标 nm - 按下时鼠标 nm；新位置 = 拖拽前位置 + 增量。
    const geometry::Point delta{pos.x - drag_start_.x, pos.y - drag_start_.y};
    c->set_position({drag_old_position_.x + delta.x, drag_old_position_.y + delta.y});
}

void SchematicEditor::on_select_up(geometry::Point /*pos*/) {
    if (!dragging_ || drag_uuid_.empty()) {
        dragging_ = false;
        drag_uuid_.clear();
        return;
    }
    Component* c = schematic_->find_component_by_uuid(drag_uuid_);
    if (c) {
        const geometry::Point new_pos = c->position();
        // 与起点相同则视为未移动，不入栈（避免冗余命令）。
        if (new_pos.x != drag_old_position_.x || new_pos.y != drag_old_position_.y) {
            if (undo_stack_) {
                undo_stack_->push(std::make_unique<detail::MoveComponentCommand>(
                    schematic_, this, drag_uuid_, drag_old_position_, new_pos));
            }
            emit_schematic_modified();
        }
    }
    dragging_ = false;
    drag_uuid_.clear();
}

// ============================================================================
// COMPONENT 工具：单击放置（refdes 自动递增）
// ============================================================================

void SchematicEditor::on_component_down(geometry::Point pos) {
    const std::string refdes = allocate_refdes();
    const std::string uuid = eda::generate_uuid();
    auto cmd = std::make_unique<detail::AddComponentCommand>(
        this, schematic_, uuid, refdes, pos, Rotation::DEG_0, default_symbol_);
    if (undo_stack_) {
        undo_stack_->push(std::move(cmd));
    } else {
        cmd->execute();
    }
}

// ============================================================================
// WIRE 工具：两点画导线
//   - click-click：mouse_down 第一次设起点；第二次完成放置。
//   - click-drag：mouse_down 设起点；mouse_move 预览；mouse_up 完成。
// ============================================================================

void SchematicEditor::on_wire_down(geometry::Point pos) {
    if (!wire_placing_) {
        wire_start_ = pos;
        wire_current_ = pos;
        wire_placing_ = true;
        return;
    }
    // 第二次 click：完成放置。
    wire_current_ = pos;
    wire_placing_ = false;
    if (wire_start_ != wire_current_) {
        Wire w{wire_start_, wire_current_};
        if (undo_stack_) {
            undo_stack_->push(std::make_unique<detail::AddWireCommand>(this, schematic_, w));
        } else {
            schematic_->add_wire(w);
            emit_schematic_modified();
        }
    }
}

void SchematicEditor::on_wire_move(geometry::Point pos) {
    if (!wire_placing_) return;
    wire_current_ = pos;
}

void SchematicEditor::on_wire_up(geometry::Point pos) {
    if (!wire_placing_) return;
    wire_placing_ = false;
    wire_current_ = pos;
    if (wire_start_ != wire_current_) {
        Wire w{wire_start_, wire_current_};
        if (undo_stack_) {
            undo_stack_->push(std::make_unique<detail::AddWireCommand>(this, schematic_, w));
        } else {
            schematic_->add_wire(w);
            emit_schematic_modified();
        }
    }
}

// ============================================================================
// NET_LABEL 工具：单击放置
// ============================================================================

void SchematicEditor::on_net_label_down(geometry::Point pos) {
    NetLabel l{default_net_name_, pos, LabelDirection::HORIZONTAL};
    if (undo_stack_) {
        undo_stack_->push(std::make_unique<detail::AddNetLabelCommand>(this, schematic_, l));
    } else {
        schematic_->add_net_label(l);
        emit_schematic_modified();
    }
}

// ============================================================================
// DELETE 工具：点击删除器件
// ============================================================================

void SchematicEditor::on_delete_down(geometry::Point pos) {
    Component* hit = hit_test_component(pos);
    if (!hit) return;
    const std::string uuid = hit->uuid();
    auto cmd = std::make_unique<detail::DeleteComponentCommand>(
        this, schematic_, uuid, hit->refdes(), hit->position(), hit->rotation(), hit->symbol());
    if (undo_stack_) {
        undo_stack_->push(std::move(cmd));
    } else {
        cmd->execute();
    }
}

// ============================================================================
// 键盘动作
// ============================================================================

void SchematicEditor::rotate_selected_90() {
    if (selection_.empty() || !schematic_) return;
    // 拷贝一份：command 执行可能修改 selection_（旋转不修改，但保持稳健）。
    auto snapshot = selection_;
    for (const auto& uuid : snapshot) {
        Component* c = schematic_->find_component_by_uuid(uuid);
        if (!c) continue;
        const Rotation old_rot = c->rotation();
        const int new_deg = (to_degrees(old_rot) + 90) % 360;
        const Rotation new_rot = rotation_from_degrees(new_deg);
        if (undo_stack_) {
            undo_stack_->push(std::make_unique<detail::RotateComponentCommand>(
                schematic_, this, uuid, old_rot, new_rot));
        } else {
            c->set_rotation(new_rot);
            emit_schematic_modified();
        }
    }
}

void SchematicEditor::delete_selected() {
    if (selection_.empty() || !schematic_) return;
    auto snapshot = selection_;
    for (const auto& uuid : snapshot) {
        Component* c = schematic_->find_component_by_uuid(uuid);
        if (!c) continue;
        auto cmd = std::make_unique<detail::DeleteComponentCommand>(
            this, schematic_, uuid, c->refdes(), c->position(), c->rotation(), c->symbol());
        if (undo_stack_) {
            undo_stack_->push(std::move(cmd));
        } else {
            cmd->execute();
        }
    }
}

// ============================================================================
// 渲染覆盖层
// ============================================================================

void SchematicEditor::render_overlay(Canvas2D& canvas, float zoom, Vector2 offset) {
    if (!schematic_) return;

    // 1. 选择高亮框（每个选中 Component 的 AABB，4 条线）
    if (!selection_.empty()) {
        canvas.set_color(Color{0.0f, 1.0f, 1.0f, 1.0f});  // 青色
        canvas.set_line_thickness(1.0f);
        for (const auto& uuid : selection_) {
            const Component* c = schematic_->find_component_by_uuid(uuid);
            if (!c) continue;
            const geometry::Box bb = component_bbox(*c);
            const Vector2 tl = schematic_to_screen(bb.min, zoom, offset);
            const Vector2 br = schematic_to_screen(bb.max, zoom, offset);
            const Vector2 tr{br.x, tl.y};
            const Vector2 bl{tl.x, br.y};
            canvas.draw_line(tl, tr);
            canvas.draw_line(tr, br);
            canvas.draw_line(br, bl);
            canvas.draw_line(bl, tl);
        }
    }

    // 2. WIRE 预览线段
    if (wire_placing_) {
        canvas.set_color(Color{1.0f, 1.0f, 0.0f, 1.0f});  // 黄色
        canvas.set_line_thickness(2.0f);
        const Vector2 a = schematic_to_screen(wire_start_, zoom, offset);
        const Vector2 b = schematic_to_screen(wire_current_, zoom, offset);
        canvas.draw_line(a, b);
    }

    // 3. 十字光标（屏幕 ±10 px）
    const Vector2 cs = schematic_to_screen(cursor_, zoom, offset);
    canvas.set_color(Color{0.5f, 0.5f, 0.5f, 1.0f});  // 灰色
    canvas.set_line_thickness(1.0f);
    canvas.draw_line({cs.x - 10.0f, cs.y}, {cs.x + 10.0f, cs.y});
    canvas.draw_line({cs.x, cs.y - 10.0f}, {cs.x, cs.y + 10.0f});
}

}  // namespace eda::sch
