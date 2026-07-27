#pragma once

// Node —— 节点树基本单元（对齐 Godot scene/main/node.h）。
//
// 设计要点：
//   - 继承 Object（不继承 RefCounted）：挂树期间由 SceneTree 经 root 持有；
//     父节点拥有子节点（~Node 级联 delete 子树）。
//   - 父子关系：parent_ / children_ 列表；add_child / remove_child 维护。
//   - 生命周期回调（虚函数，子类 override）：
//       _enter_tree()           节点进入场景树（自顶向下分发）
//       _ready()                节点及全部子节点已就绪（自底向上分发）
//       _process(double delta)  每帧推进（自顶向下分发，由 SceneTree::process 驱动）
//       _exit_tree()            节点即将离开场景树（自底向上分发）
//       _input(InputEvent)      输入事件（自顶向下分发，由 SceneTree::input 驱动）
//   - 通知常量：NOTIFICATION_ENTER_TREE / READY / PROCESS / EXIT_TREE。
//     调用 notification(int) 时按常量分派到上述虚回调，子类既可 override
//     单个虚回调，也可 override notification(int) 统一处理。
//   - is_inside_tree()：节点当前是否在场景树中。
//
// 不变式：
//   - inside_tree_ == true 当且仅当从 root 到本节点的整条 parent 链均挂入同一棵 SceneTree。
//   - parent_ == nullptr 表示根节点（或未挂树）。
//   - 子节点一旦挂到 in-tree 父节点下，立即 _propagate_enter_tree；移除时 _propagate_exit_tree。

#include <cstddef>
#include <string>
#include <vector>

#include "core/object/object.h"
#include "core/input/input_event.h"

namespace eda {

class SceneTree;

class Node : public Object {
public:
    // ---- 通知常量（与 Object 的 NOTIFICATION_PREDELETE=0 / POSTINITIALIZE=1 错峰）----
    static constexpr int NOTIFICATION_ENTER_TREE = 10;
    static constexpr int NOTIFICATION_READY      = 11;
    static constexpr int NOTIFICATION_PROCESS    = 12;
    static constexpr int NOTIFICATION_EXIT_TREE  = 13;

    Node() = default;
    ~Node() override;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // ---- 父子树 ----
    // 挂入子节点。child 必须无父且不能是 this 的祖先（环检测）。
    // 若 this 已在树中，child 子树立即收到 ENTER_TREE / READY。
    void add_child(Node* child);

    // 移除子节点。若 child 在树中，其子树先收到 EXIT_TREE 再断开。
    // 移除后 child 的生命周期归调用方（不在 SceneTree 持有范围内）。
    void remove_child(Node* child);

    Node* get_parent() const { return parent_; }
    int get_child_count() const { return static_cast<int>(children_.size()); }
    const std::vector<Node*>& get_children() const { return children_; }

    // ---- 树状态 ----
    bool is_inside_tree() const { return inside_tree_; }
    SceneTree* get_tree() const { return tree_; }

    // ---- 处理 / 输入开关（默认 true）----
    void set_process(bool enable) { process_enabled_ = enable; }
    bool is_processing() const { return process_enabled_; }
    void set_process_input(bool enable) { input_enabled_ = enable; }
    bool is_processing_input() const { return input_enabled_; }

    // ---- 虚回调（子类 override 以接收生命周期 / 每帧 / 输入）----
    virtual void _enter_tree() {}
    virtual void _ready() {}
    virtual void _process(double /*delta*/) {}
    virtual void _exit_tree() {}
    virtual void _input(const InputEvent& /*event*/) {}

    // ---- 通知入口：按常量分派到虚回调。未知值回退到 Object::notification。----
    void notification(int what) override;

    // ---- 类型查询（基于类名字符串，无 RTTI）----
    static std::string get_class_static() { return "Node"; }
    std::string get_class() const override { return "Node"; }
    bool is_class(const std::string& name) const override {
        return name == "Node" || Object::is_class(name);
    }

    // ---- 子树遍历：深度优先前序，对 this 与全部后代调用 fn(Node*)。----
    template <typename F>
    void for_each_in_subtree(F&& fn) {
        fn(this);
        for (Node* c : children_) {
            c->for_each_in_subtree(fn);
        }
    }

protected:
    // 子树传播：由 add_child / set_root / remove_child 调用。
    void _propagate_enter_tree();
    void _propagate_exit_tree();

    Node*                parent_          = nullptr;
    std::vector<Node*>   children_;
    SceneTree*           tree_            = nullptr;
    bool                 inside_tree_     = false;
    bool                 ready_emitted_   = false;  // 防止 READY 重入；exit 后重置以支持重新挂入
    bool                 process_enabled_ = true;
    bool                 input_enabled_   = true;
    double               process_delta_   = 0.0;    // 由 SceneTree 在 NOTIFICATION_PROCESS 前写入

    friend class SceneTree;
};

}  // namespace eda
