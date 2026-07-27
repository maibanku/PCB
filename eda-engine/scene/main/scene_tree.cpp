#include "scene/main/scene_tree.h"

#include "scene/main/node.h"

namespace eda {

SceneTree* SceneTree::singleton_ = nullptr;

SceneTree::SceneTree() {
    // 多实例场景下，最后构造的覆盖 singleton_（与 Godot 行为一致）。
    singleton_ = this;
}

SceneTree::~SceneTree() {
    if (root_ != nullptr) {
        // 拆除时先发 EXIT_TREE（虚分派仍可达派生类的 _exit_tree），
        // 再 delete（级联释放子树；~Node 不再发 EXIT_TREE）。
        if (root_->is_inside_tree()) {
            root_->_propagate_exit_tree();
        }
        delete root_;
        root_ = nullptr;
    }
    if (singleton_ == this) {
        singleton_ = nullptr;
    }
}

void SceneTree::set_root(Node* root) {
    if (root_ == root) return;
    if (root_ != nullptr) {
        if (root_->is_inside_tree()) {
            root_->_propagate_exit_tree();
        }
        delete root_;
    }
    root_ = root;
    if (root_ != nullptr) {
        root_->tree_ = this;
        root_->_propagate_enter_tree();
    }
}

void SceneTree::process(double delta) {
    if (root_ == nullptr) return;
    process_subtree(root_, delta);
}

void SceneTree::input(const InputEvent& event) {
    if (root_ == nullptr) return;
    input_subtree(root_, event);
}

void SceneTree::process_subtree(Node* n, double delta) {
    if (n == nullptr) return;
    if (n->is_processing()) {
        n->process_delta_ = delta;  // 友元写入：NOTIFICATION_PROCESS 分派到 _process(delta)
        n->notification(Node::NOTIFICATION_PROCESS);
    }
    // 快照遍历：回调内可能修改子节点列表。
    auto snapshot = n->get_children();
    for (Node* c : snapshot) {
        process_subtree(c, delta);
    }
}

void SceneTree::input_subtree(Node* n, const InputEvent& event) {
    if (n == nullptr) return;
    if (n->is_processing_input()) {
        n->_input(event);
    }
    auto snapshot = n->get_children();
    for (Node* c : snapshot) {
        input_subtree(c, event);
    }
}

}  // namespace eda
