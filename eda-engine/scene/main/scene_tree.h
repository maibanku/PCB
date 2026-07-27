#pragma once

// SceneTree —— 场景树拥有者与每帧驱动者（对齐 Godot scene/main/scene_tree.h）。
//
// 职责：
//   - 持有 root_（Node*），拥有其整棵子树（set_root 替换时释放旧 root；~SceneTree 释放 root）。
//   - process(double delta)：自顶向下遍历整树，对每个节点设置 process_delta_ 后发
//     NOTIFICATION_PROCESS（→ _process(delta)）。可通过 Node::set_process(false) 跳过。
//   - input(InputEvent)：自顶向下遍历整树，对每个节点调 _input(event)（受 set_process_input 影响）。
//   - 节点挂入树（set_root / add_child 到 in-tree 父节点）时自动发 ENTER_TREE→READY；
//     卸载（remove_child / set_root 替换）时发 EXIT_TREE。
//
// 单例：构造时把 this 写入 singleton_，析构时若仍是 singleton_ 则清空。
// 测试与主循环都通过实例化 SceneTree 使用；同时提供 get_singleton() 供全局访问。

#include "core/input/input_event.h"

#include <string>

namespace eda {

class Node;

class SceneTree {
public:
    SceneTree();
    ~SceneTree();

    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;

    static SceneTree* get_singleton() { return singleton_; }

    // 设置根节点。SceneTree 接管 root 的所有权：
    //   - 若已有旧 root：先对其子树发 EXIT_TREE，再 delete（级联释放整棵旧树）。
    //   - 新 root 若非空：对其子树发 ENTER_TREE / READY。
    // 传 nullptr 等价于拆除当前 root。
    void set_root(Node* root);
    Node* get_root() const { return root_; }

    // 每帧推进：自顶向下遍历 root 子树，调 _process(delta)（默认对每个节点；
    // 被 set_process(false) 关闭的节点跳过）。
    void process(double delta);

    // 输入分发：自顶向下遍历 root 子树，调 _input(event)（被 set_process_input(false)
    // 关闭的节点跳过）。
    void input(const InputEvent& event);

private:
    void process_subtree(Node* n, double delta);
    void input_subtree(Node* n, const InputEvent& event);

    Node*  root_ = nullptr;
    static SceneTree* singleton_;
};

}  // namespace eda
