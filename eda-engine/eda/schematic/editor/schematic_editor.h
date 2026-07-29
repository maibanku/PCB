#pragma once

// eda/schematic/editor/schematic_editor.h
//
// P7 原理图编辑器交互模块（工具模式 + 鼠标放置/移动/旋转/删除 + 命令化）。
//
// 依赖：
//   - P7 Schematic / Component / Wire / NetLabel   （eda/schematic/*.h）
//   - P6 Symbol                                     （eda/library/symbol.h）
//   - P3 UndoStack / Command                        （eda/command/*.h）
//   - P1 Canvas2D / InputEvent                      （scene/2d/canvas_2d.h、core/input/input_event.h）
//
// 职责：
//   * 维护当前工具（SELECT/COMPONENT/WIRE/NET_LABEL/DELETE）的状态机。
//   * 把鼠标/键盘事件翻译为对 Schematic 的编辑动作（命令化，经 UndoStack 入栈）。
//   * 维护选择集 + 视图变换（屏幕像素 ↔ Schematic nm）。
//   * 通过 Canvas2D 绘制编辑覆盖层（选择高亮 / WIRE 预览 / 十字光标）。
//
// 命令化策略：
//   * Component 有稳定 UUID，且 Schematic 暴露按 UUID 的增删查（add_component /
//     remove_component_by_uuid / find_component_by_uuid），故 Component 全套操作
//     （Add / Move / Rotate / Delete）均按 UUID 寻址，完全可逆：
//       - Add：execute 把新 Component（带预生成 UUID）加入；undo 按 UUID 移除；
//              redo 用相同 UUID 重建并加入。
//       - Move / Rotate：execute 写新几何，undo 写旧几何（值快照）。
//       - Delete：execute 移除，undo 用快照参数重建。
//   * Wire / NetLabel 为值类型、按索引管理，命令化以"末尾插入 / 末尾移除"实现
//     （UndoStack 严格 LIFO，每步 undo 反向自身，索引稳定性可保证）。
//
// 坐标约定：
//   * 输入接口 handle_mouse_* 直接接受 Schematic 世界坐标（int64 nm，Point）。
//     调用方负责把屏幕像素先经 screen_to_schematic(zoom, offset) 换算成 nm 再传入。
//   * render_overlay 接收 zoom / offset 参数，把内部世界坐标点换算为屏幕像素绘制。
//   * 公式：nm = (px - offset) / zoom；px = nm * zoom + offset。
//   * zoom 单位：像素 / nm（如 0.001f 表示 1 px = 1 μm = 1000 nm）。
//
// 命名规范：namespace eda::sch、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <cstddef>
#include <string>
#include <vector>

#include "core/input/input_event.h"
#include "core/math/vector2.h"
#include "core/object/ref.h"
#include "core/object/signal.h"
#include "eda/geometry/types.h"
#include "eda/schematic/component.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/wire.h"

namespace eda {
class Canvas2D;
}  // namespace eda

namespace eda::command {
class UndoStack;
}  // namespace eda::command

namespace eda::sch {

class Schematic;
}  // namespace eda::sch

namespace eda {
class Symbol;
}  // namespace eda

namespace eda::sch {

// ============================================================================
// Tool —— 原理图编辑器工具状态机。
// ============================================================================
enum class Tool {
    SELECT,      // 选择/移动器件
    COMPONENT,   // 放置器件（单击）
    WIRE,        // 画导线（两点）
    NET_LABEL,   // 放置网络标号（单击）
    DELETE,      // 点击删除器件
};

namespace detail {
class AddComponentCommand;
class MoveComponentCommand;
class RotateComponentCommand;
class DeleteComponentCommand;
class AddWireCommand;
class AddNetLabelCommand;
}  // namespace detail

// ============================================================================
// SchematicEditor —— 原理图编辑器交互核心。
// ============================================================================
//
// 不可拷贝/不可移动：内部持有 Signal<>，需稳定地址。
//
// 使用范式：
//   Schematic sch;
//   UndoStack stack;
//   SchematicEditor editor;
//   editor.set_schematic(&sch);
//   editor.set_undo_stack(&stack);
//   editor.set_default_symbol(resistor_sym);
//   editor.set_tool(Tool::COMPONENT);
//   editor.handle_mouse_down(Point{1'000'000, 1'000'000});   // 放置 R1
//   editor.handle_mouse_down(Point{2'000'000, 1'000'000});   // 放置 R2
//   editor.set_tool(Tool::WIRE);
//   editor.handle_mouse_down(Point{...});                    // 起点
//   editor.handle_mouse_move(Point{...});                    // 预览
//   editor.handle_mouse_up(Point{...});                      // 终点 → add_wire
//
class SchematicEditor {
public:
    SchematicEditor();
    ~SchematicEditor();

    SchematicEditor(const SchematicEditor&) = delete;
    SchematicEditor& operator=(const SchematicEditor&) = delete;
    SchematicEditor(SchematicEditor&&) = delete;
    SchematicEditor& operator=(SchematicEditor&&) = delete;

    // ---------------- Schematic / UndoStack ----------------

    void set_schematic(Schematic* schematic);
    Schematic* get_schematic() { return schematic_; }
    const Schematic* get_schematic() const { return schematic_; }

    void set_undo_stack(command::UndoStack* stack);
    command::UndoStack* get_undo_stack() { return undo_stack_; }

    // ---------------- 默认 Symbol（COMPONENT 工具放置时引用）----------------

    void set_default_symbol(Ref<Symbol> sym);
    Ref<Symbol> get_default_symbol() const { return default_symbol_; }

    // ---------------- NET_LABEL 工具默认网络名 ----------------
    //
    // handle_mouse_down 在 NET_LABEL 模式下放置一个 NetLabel，名字取自该字段。
    // 调用方可随需要修改（如 UI 输入框联动）。默认 "SDA"。
    //
    void set_default_net_name(std::string name);
    const std::string& default_net_name() const { return default_net_name_; }

    // ---------------- 工具 ----------------

    void set_tool(Tool tool);
    Tool get_tool() const { return tool_; }

    // ---------------- 输入（坐标单位：Schematic nm）----------------

    // 鼠标按下：
    //   SELECT    —— 命中测试 + 选中 + 开始拖拽；
    //   COMPONENT —— 放置 Component（refdes 自动递增）；
    //   WIRE      —— 第一次设起点；第二次完成放置（click-click）；
    //   NET_LABEL —— 放置 NetLabel；
    //   DELETE    —— 删除命中器件。
    void handle_mouse_down(geometry::Point pos);

    // 鼠标移动：更新光标；SELECT 拖拽时实时移动器件；WIRE 更新预览端点。
    void handle_mouse_move(geometry::Point pos);

    // 鼠标抬起：SELECT 提交移动命令；WIRE 若处于放置中则完成（click-drag 兼容）。
    void handle_mouse_up(geometry::Point pos);

    // 键盘：仅响应 PRESS。R = 旋转选中 90° CCW；Delete = 删除选中；Esc = 取消。
    void handle_key(KeyAction action, int key);

    // ---------------- 视图变换（屏幕像素 ↔ Schematic nm）----------------
    //
    // 约定（与 eda::pcb::PcbEditor 一致）：
    //   schematic_nm = (screen_px - offset) / zoom
    //   screen_px = schematic_nm * zoom + offset
    //   zoom 单位：像素 / nm（如 0.001f 表示 1 px = 1 μm）
    //   offset 单位：像素，表示 schematic 原点 (0,0) 在屏幕上的位置
    //
    static geometry::Point screen_to_schematic(Vector2 screen, float zoom, Vector2 offset);
    static Vector2 schematic_to_screen(geometry::Point sch, float zoom, Vector2 offset);

    // ---------------- 渲染覆盖层 ----------------

    // 在 Canvas2D 上绘制：选择高亮框 + WIRE 预览线 + 十字光标。
    // 调用方负责先绘制 Schematic 本体，再调用本方法叠加覆盖层。
    void render_overlay(Canvas2D& canvas, float zoom, Vector2 offset);

    // ---------------- 选择集 ----------------

    const std::vector<std::string>& selection() const { return selection_; }
    bool is_selected(const std::string& uuid) const;
    void clear_selection();

    // ---------------- 信号 ----------------

    Signal<>& selection_changed() { return selection_changed_; }
    Signal<>& schematic_modified() { return schematic_modified_; }

private:
    // ---- 内部工具 ----
    Component* hit_test_component(geometry::Point p);
    const Component* hit_test_component(geometry::Point p) const;
    void select_only(const std::string& uuid);
    void rotate_selected_90();
    void delete_selected();
    void cancel_action();

    std::string allocate_refdes() const;

    void emit_selection_changed() { selection_changed_.emit(); }
    void emit_schematic_modified() { schematic_modified_.emit(); }

    // 供 detail 命令类通知（命令执行后刷新信号）。
    void notify_schematic_modified() { emit_schematic_modified(); }
    void notify_selection_changed() { emit_selection_changed(); }
    void remove_from_selection(const std::string& uuid);

    // 工具分发
    void on_select_down(geometry::Point pos);
    void on_select_move(geometry::Point pos);
    void on_select_up(geometry::Point pos);
    void on_component_down(geometry::Point pos);
    void on_wire_down(geometry::Point pos);
    void on_wire_move(geometry::Point pos);
    void on_wire_up(geometry::Point pos);
    void on_net_label_down(geometry::Point pos);
    void on_delete_down(geometry::Point pos);

    Schematic* schematic_ = nullptr;
    command::UndoStack* undo_stack_ = nullptr;
    Ref<Symbol> default_symbol_;
    Tool tool_ = Tool::SELECT;

    // NET_LABEL 工具默认网络名。
    std::string default_net_name_ = "SDA";

    // 选择集（Component UUID 列表）。
    std::vector<std::string> selection_;

    // SELECT 拖拽状态
    bool dragging_ = false;
    std::string drag_uuid_;
    geometry::Point drag_start_{};       // 鼠标按下时的 nm 坐标
    geometry::Point drag_old_position_{}; // 拖拽前器件位置（用于 undo 快照）

    // WIRE 放置状态
    bool wire_placing_ = false;
    geometry::Point wire_start_{};     // 起点nm
    geometry::Point wire_current_{};   // 当前预览终点 nm

    // 最近一次光标位置（schematic nm），用于绘十字光标。
    geometry::Point cursor_{};

    Signal<> selection_changed_;
    Signal<> schematic_modified_;

    // detail 命令类需要访问内部通知接口以实现命令化。
    friend class detail::AddComponentCommand;
    friend class detail::MoveComponentCommand;
    friend class detail::RotateComponentCommand;
    friend class detail::DeleteComponentCommand;
    friend class detail::AddWireCommand;
    friend class detail::AddNetLabelCommand;
};

}  // namespace eda::sch
