// CanvasItem —— 自绘渲染节点基类与自绘画布接口（P4 Task 1）。
//
// 在 Node（P2 节点树）之上叠加：
//   - 屏幕几何（position / size / rect；virtual 以便 Control 用锚点覆盖）
//   - z_index（同层渲染排序，值大者在上）
//   - 自绘入口：queue_redraw() 标脏 → 遍历器调 draw_if_dirty() 分派 _draw()
//
// 同文件提供 ControlCanvas 抽象 + RecordingControlCanvas（测试）+
// ControlCanvasScope（RAII 绑定 thread-local 当前画布）：_draw() 经此接口下发
// 自绘调用（生产包装 RenderingServer.canvas_item_add_*；测试记录调用断言）。
//
// 本文件头文件内联实现（无 .cpp）：全部短方法 + inline thread_local 画布寄存器。
// 对齐 Godot scene/main/canvas_item.h + scene/gui/control_canvas 抽象。

#pragma once

#include "scene/main/node.h"
#include "core/math/vector2.h"
#include "core/math/rect2.h"
#include "core/math/color.h"

#include <string>
#include <utility>
#include <vector>

namespace eda {

class CanvasItem;  // 前向

// ====== 自绘画布接口 ======

// _draw() 经此接口下发自绘调用。
//   生产环境：派生实现包装 RenderingServer.canvas_item_add_*；
//   测试环境：RecordingControlCanvas 记录调用以断言。
class ControlCanvas {
public:
    virtual ~ControlCanvas() = default;
    virtual void draw_rect(Rect2 rect, Color color, bool filled) = 0;
    virtual void draw_text(const std::string& text, Vector2 pos, Color color) = 0;
    // 后续 Task 按需补：draw_line / draw_style_box / draw_texture / draw_circle ...
};

namespace detail {
// 当前线程绑定的画布（ControlCanvasScope RAII 设置）。
// inline thread_local：跨 TU include 本头后合并为同一线程同一实例。
inline thread_local ControlCanvas* g_current_control_canvas = nullptr;
}  // namespace detail

// 返回当前线程绑定的 ControlCanvas（未设置则 nullptr）。
inline ControlCanvas* current_control_canvas() {
    return detail::g_current_control_canvas;
}

// RAII：在作用域内将 c 设为当前画布，析构恢复前值。
// 遍历器在调 control->draw_if_dirty() 前用 scope 绑定真实画布。
class ControlCanvasScope {
public:
    explicit ControlCanvasScope(ControlCanvas& c)
        : prev_(detail::g_current_control_canvas) {
        detail::g_current_control_canvas = &c;
    }
    ~ControlCanvasScope() {
        detail::g_current_control_canvas = prev_;
    }
    ControlCanvasScope(const ControlCanvasScope&) = delete;
    ControlCanvasScope& operator=(const ControlCanvasScope&) = delete;

private:
    ControlCanvas* prev_;
};

// 测试用：记录所有绘制调用以便断言。
struct RecordedRect {
    Rect2 rect;
    Color color;
    bool filled;
};

class RecordingControlCanvas : public ControlCanvas {
public:
    std::vector<RecordedRect> rects;
    std::vector<std::pair<std::string, Vector2>> texts;

    void draw_rect(Rect2 r, Color c, bool f) override { rects.push_back({r, c, f}); }
    void draw_text(const std::string& t, Vector2 p, Color /*color*/) override {
        texts.emplace_back(t, p);
    }
};

// ====== CanvasItem：最小渲染节点基类 ======

class CanvasItem : public Node {
public:
    CanvasItem() = default;
    ~CanvasItem() override = default;

    CanvasItem(const CanvasItem&) = delete;
    CanvasItem& operator=(const CanvasItem&) = delete;

    // —— 几何（virtual：Control 用锚点覆盖；普通 CanvasItem 用简单成员）——
    virtual void    set_position(Vector2 p) { position_ = p; queue_redraw(); }
    virtual Vector2 get_position() const { return position_; }
    virtual void    set_size(Vector2 s) { size_ = s; queue_redraw(); }
    virtual Vector2 get_size() const { return size_; }
    virtual void    set_rect(Rect2 r) { position_ = r.pos; size_ = r.size; queue_redraw(); }
    virtual Rect2   get_rect() const { return Rect2{get_position(), get_size()}; }

    // —— z_index（同层渲染排序；值大者在上）——
    void set_z_index(int z) { z_index_ = z; }
    int  get_z_index() const { return z_index_; }

    // —— 父子便捷（委托 Node::add_child / remove_child；测试与布局常用）——
    // 注意：与 add_child 一致，父节点析构会级联 delete 仍在 children_ 中的子节点，
    // 因此栈对象需保证 parent 生命周期长于 children（即 parent 先声明后析构）。
    void set_parent(Node* new_parent) {
        if (parent_ == new_parent) return;
        if (parent_ != nullptr) {
            parent_->remove_child(this);
        }
        if (new_parent != nullptr) {
            new_parent->add_child(this);
        }
    }

    // —— 自绘 ——
    // 当前线程绑定的画布（由 ControlCanvasScope 设置）。
    static ControlCanvas* current_canvas() { return current_control_canvas(); }

    bool is_draw_dirty() const { return draw_dirty_; }
    void queue_redraw() { draw_dirty_ = true; }

    // 遍历器调：dirty 则清脏 + _draw()（派生类可覆盖以追加框架绘制，
    // 如 Control::draw_if_dirty 追加焦点高亮）。
    virtual void draw_if_dirty() {
        if (!draw_dirty_) return;
        draw_dirty_ = false;
        _draw();
    }

    // 子类覆盖以自绘（默认空）。
    virtual void _draw() {}

    // —— 类型查询（基于类名字符串，无 RTTI）——
    static std::string get_class_static() { return "CanvasItem"; }
    std::string get_class() const override { return "CanvasItem"; }
    bool is_class(const std::string& name) const override {
        return name == "CanvasItem" || Node::is_class(name);
    }

protected:
    Vector2 position_{0, 0};
    Vector2 size_{0, 0};
    int     z_index_    = 0;
    bool    draw_dirty_ = true;   // 首帧视为脏，确保初次 draw_if_dirty() 触发绘制
};

}  // namespace eda
