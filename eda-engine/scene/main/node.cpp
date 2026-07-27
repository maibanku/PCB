#include "scene/main/node.h"

#include <algorithm>
#include <utility>

namespace eda {

Node::~Node() {
    // 从父节点的 children_ 中抹去自己（析构期不发 EXIT_TREE——虚分派到不了派生类）。
    if (parent_ != nullptr) {
        auto& siblings = parent_->children_;
        auto it = std::find(siblings.begin(), siblings.end(), this);
        if (it != siblings.end()) siblings.erase(it);
        parent_ = nullptr;
    }
    // 级联 delete 全部子节点（父节点拥有子节点，对齐 Godot 语义）。
    // 先把每个 child->parent_ 置空，避免子节点析构时回头改本节点的 children_。
    for (Node* c : children_) {
        c->parent_ = nullptr;
        delete c;
    }
    children_.clear();
    inside_tree_   = false;
    tree_          = nullptr;
    ready_emitted_ = false;
}

void Node::notification(int what) {
    // 先链到基类（Object::notification 当前为空，但保留给派生类的链式调用）。
    Object::notification(what);
    switch (what) {
        case NOTIFICATION_ENTER_TREE: _enter_tree();              break;
        case NOTIFICATION_READY:      _ready();                   break;
        case NOTIFICATION_PROCESS:    _process(process_delta_);   break;
        case NOTIFICATION_EXIT_TREE:  _exit_tree();               break;
        default: break;
    }
}

void Node::add_child(Node* child) {
    if (child == nullptr || child == this) return;
    // 已有父节点：拒绝（调用方需先 remove_child 再挂入）。
    if (child->parent_ != nullptr) return;
    // 环检测：child 不能是 this 的祖先。
    for (Node* p = parent_; p != nullptr; p = p->parent_) {
        if (p == child) return;
    }
    children_.push_back(child);
    child->parent_ = this;
    // 若 this 已在树中，新挂入的子树立即进入树。
    if (inside_tree_) {
        child->tree_ = tree_;
        child->_propagate_enter_tree();
    }
}

void Node::remove_child(Node* child) {
    if (child == nullptr) return;
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it == children_.end()) return;
    // 若在树中：子树自底向上发 EXIT_TREE，并清掉 inside_tree_/tree_/ready_emitted_。
    if (child->inside_tree_) {
        child->_propagate_exit_tree();
    }
    children_.erase(it);
    child->parent_ = nullptr;
}

void Node::_propagate_enter_tree() {
    // ENTER_TREE 自顶向下：本节点先进入，再递归子节点。
    inside_tree_ = true;
    notification(NOTIFICATION_ENTER_TREE);
    // 快照：回调内可能 add/remove 子节点，迭代快照避免迭代器失效。
    auto snapshot = children_;
    for (Node* c : snapshot) {
        if (c->parent_ == this) {  // 仍是我的直接子节点（未被回调移除）
            c->tree_ = tree_;
            c->_propagate_enter_tree();
        }
    }
    // READY 自底向上：所有子树就绪后本节点才就绪。仅首次触发。
    if (!ready_emitted_) {
        ready_emitted_ = true;
        notification(NOTIFICATION_READY);
    }
}

void Node::_propagate_exit_tree() {
    // EXIT_TREE 自底向上：先递归子节点，最后本节点离开。
    auto snapshot = children_;
    for (Node* c : snapshot) {
        if (c->inside_tree_) {
            c->_propagate_exit_tree();
        }
    }
    inside_tree_   = false;
    tree_          = nullptr;
    ready_emitted_ = false;  // 允许重新挂入时再次触发 READY
    notification(NOTIFICATION_EXIT_TREE);
}

}  // namespace eda
