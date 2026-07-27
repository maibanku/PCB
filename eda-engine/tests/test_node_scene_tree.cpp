// Node / SceneTree 单元测试（P2 Task 6）。
//
// 覆盖：
//   - add_child / remove_child 父子关系：get_parent / get_child_count / get_children
//   - 环检测与重复父拒绝
//   - 生命周期顺序：set_root / add_child 触发 ENTER_TREE 自顶向下、READY 自底向上
//   - remove_child 触发 EXIT_TREE（子树自底向上）
//   - SceneTree::process(delta) 自顶向下分发 _process，delta 正确传递；set_process(false) 跳过
//   - SceneTree::input(event) 自顶向下分发 _input；set_process_input(false) 跳过
//   - is_inside_tree / get_tree 在挂入与卸载时正确切换
//
// 所有权：SceneTree 持有 root；父节点持有子节点（~Node 级联 delete）。测试中通过
// ~SceneTree 或显式 remove_child + delete 释放，不在用例里手动 delete 仍挂树的节点。

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/input/input_event.h"
#include "core/object/object.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

using namespace eda;

namespace {

// SpyNode —— 记录所有生命周期 / 每帧 / 输入回调到全局日志，便于断言顺序。
class SpyNode : public Node {
public:
    std::string name;
    std::vector<std::string>* log = nullptr;
    double last_delta = -1.0;

    SpyNode(std::string n, std::vector<std::string>* l)
        : name(std::move(n)), log(l) {}

    std::string get_class() const override { return "SpyNode"; }

    void _enter_tree() override   { log->push_back(name + ":enter_tree"); }
    void _ready() override        { log->push_back(name + ":ready"); }
    void _exit_tree() override    { log->push_back(name + ":exit_tree"); }
    void _process(double delta) override {
        last_delta = delta;
        log->push_back(name + ":process");
    }
    void _input(const InputEvent&) override {
        log->push_back(name + ":input");
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// 父子关系：add_child / remove_child / get_children
// ---------------------------------------------------------------------------

TEST(NodeTest, AddChildSetsParentAndChildren) {
    Node parent;
    Node* child = new Node;
    parent.add_child(child);

    EXPECT_EQ(child->get_parent(), &parent);
    EXPECT_EQ(parent.get_child_count(), 1);
    ASSERT_EQ(parent.get_children().size(), 1u);
    EXPECT_EQ(parent.get_children()[0], child);
}

TEST(NodeTest, RemoveChildClearsParent) {
    Node parent;
    Node* child = new Node;
    parent.add_child(child);
    parent.remove_child(child);

    EXPECT_EQ(child->get_parent(), nullptr);
    EXPECT_EQ(parent.get_child_count(), 0);
    EXPECT_TRUE(parent.get_children().empty());
    delete child;
}

TEST(NodeTest, AddChildOrderPreserved) {
    Node parent;
    Node* a = new Node;
    Node* b = new Node;
    Node* c = new Node;
    parent.add_child(a);
    parent.add_child(b);
    parent.add_child(c);

    EXPECT_EQ(parent.get_child_count(), 3);
    const auto& kids = parent.get_children();
    EXPECT_EQ(kids[0], a);
    EXPECT_EQ(kids[1], b);
    EXPECT_EQ(kids[2], c);
}

TEST(NodeTest, RemoveChildNotInParentIsNoop) {
    Node parent;
    Node* stranger = new Node;
    parent.remove_child(stranger);  // 不在 parent 下：无副作用
    EXPECT_EQ(parent.get_child_count(), 0);
    delete stranger;
}

TEST(NodeTest, AddNullChildIsNoop) {
    Node parent;
    parent.add_child(nullptr);
    EXPECT_EQ(parent.get_child_count(), 0);
}

TEST(NodeTest, AddSelfAsChildRejected) {
    Node n;
    n.add_child(&n);
    EXPECT_EQ(n.get_child_count(), 0);
    EXPECT_EQ(n.get_parent(), nullptr);
}

TEST(NodeTest, AddChildWithExistingParentRejected) {
    Node p1, p2;
    Node* child = new Node;
    p1.add_child(child);
    p2.add_child(child);  // 已有父：拒绝
    EXPECT_EQ(child->get_parent(), &p1);
    EXPECT_EQ(p2.get_child_count(), 0);
    EXPECT_EQ(p1.get_child_count(), 1);
}

TEST(NodeTest, AddCycleRejected) {
    // 构造 A -> B，然后试图把 A 挂到 B 下（成环）
    Node* a = new Node;
    Node* b = new Node;
    a->add_child(b);          // A -> B
    b->add_child(a);          // 拒绝：A 是 B 的祖先
    EXPECT_EQ(a->get_parent(), nullptr);
    EXPECT_EQ(b->get_parent(), a);
    EXPECT_EQ(b->get_child_count(), 0);
    delete a;  // 级联释放 b
}

TEST(NodeTest, IsClassMatchesSelfAndObject) {
    Node n;
    EXPECT_TRUE(n.is_class("Node"));
    EXPECT_TRUE(n.is_class("Object"));
    EXPECT_FALSE(n.is_class("RefCounted"));
    EXPECT_EQ(Node::get_class_static(), "Node");
}

// ---------------------------------------------------------------------------
// 生命周期：set_root / ENTER_TREE 自顶向下 / READY 自底向上
// ---------------------------------------------------------------------------

TEST(NodeLifecycleTest, SetRootFiresEnterTreeTopDown) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    SpyNode* grand = new SpyNode("grand", &log);
    child->add_child(grand);
    root->add_child(child);

    // 挂入前：root 不在树中（但子树结构已就位）
    EXPECT_FALSE(root->is_inside_tree());

    tree.set_root(root);

    // ENTER_TREE 自顶向下：root -> child -> grand
    ASSERT_GE(log.size(), 3u);
    EXPECT_EQ(log[0], "root:enter_tree");
    EXPECT_EQ(log[1], "child:enter_tree");
    EXPECT_EQ(log[2], "grand:enter_tree");
    EXPECT_TRUE(root->is_inside_tree());
    EXPECT_TRUE(grand->is_inside_tree());
    EXPECT_EQ(grand->get_tree(), &tree);
}

TEST(NodeLifecycleTest, ReadyFiresBottomUp) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    SpyNode* grand = new SpyNode("grand", &log);
    child->add_child(grand);
    root->add_child(child);
    tree.set_root(root);

    // 在 enter 序列之后查找 ready 序列
    auto find_idx = [&](const std::string& needle) {
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i] == needle) return static_cast<int>(i);
        }
        return -1;
    };
    int r_grand = find_idx("grand:ready");
    int r_child = find_idx("child:ready");
    int r_root  = find_idx("root:ready");
    ASSERT_GE(r_grand, 0);
    ASSERT_GE(r_child, 0);
    ASSERT_GE(r_root, 0);
    // READY 自底向上：grand < child < root
    EXPECT_LT(r_grand, r_child);
    EXPECT_LT(r_child, r_root);
}

TEST(NodeLifecycleTest, AddChildToInTreeParentFiresEnterTree) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    tree.set_root(root);
    log.clear();

    SpyNode* child = new SpyNode("child", &log);
    root->add_child(child);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "child:enter_tree");
    EXPECT_EQ(log[1], "child:ready");
    EXPECT_TRUE(child->is_inside_tree());
}

TEST(NodeLifecycleTest, ReadyFiresOnlyOncePerEnter) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    tree.set_root(root);

    int ready_count = 0;
    for (const auto& s : log) {
        if (s == "root:ready") ++ready_count;
    }
    EXPECT_EQ(ready_count, 1);
}

// ---------------------------------------------------------------------------
// EXIT_TREE：remove_child 触发，子树自底向上
// ---------------------------------------------------------------------------

TEST(NodeLifecycleTest, RemoveChildFiresExitTree) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    root->add_child(child);
    tree.set_root(root);
    log.clear();

    root->remove_child(child);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "child:exit_tree");
    EXPECT_FALSE(child->is_inside_tree());
    EXPECT_EQ(child->get_tree(), nullptr);
    delete child;
}

TEST(NodeLifecycleTest, ExitTreeFiresBottomUpOnSubtree) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    SpyNode* grand = new SpyNode("grand", &log);
    child->add_child(grand);
    root->add_child(child);
    tree.set_root(root);
    log.clear();

    root->remove_child(child);

    // EXIT_TREE 自底向上：grand 先，child 后；root 不动
    auto find_idx = [&](const std::string& needle) {
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i] == needle) return static_cast<int>(i);
        }
        return -1;
    };
    int g = find_idx("grand:exit_tree");
    int c = find_idx("child:exit_tree");
    ASSERT_GE(g, 0);
    ASSERT_GE(c, 0);
    EXPECT_LT(g, c);
    EXPECT_FALSE(child->is_inside_tree());
    EXPECT_FALSE(grand->is_inside_tree());
    delete child;
}

TEST(NodeLifecycleTest, ReAddChildRefiresReady) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    tree.set_root(root);

    root->add_child(child);
    root->remove_child(child);
    log.clear();
    root->add_child(child);

    // 重新挂入：enter + ready 再次触发
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "child:enter_tree");
    EXPECT_EQ(log[1], "child:ready");
}

// ---------------------------------------------------------------------------
// SceneTree::process —— delta 传递 + 自顶向下分发
// ---------------------------------------------------------------------------

TEST(SceneTreeProcessTest, ProcessCallsAllNodesTopDown) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* mid = new SpyNode("mid", &log);
    SpyNode* leaf = new SpyNode("leaf", &log);
    mid->add_child(leaf);
    root->add_child(mid);
    tree.set_root(root);
    log.clear();

    tree.process(0.016);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "root:process");
    EXPECT_EQ(log[1], "mid:process");
    EXPECT_EQ(log[2], "leaf:process");
}

TEST(SceneTreeProcessTest, ProcessPassesDelta) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    tree.set_root(root);

    tree.process(0.125);
    EXPECT_DOUBLE_EQ(root->last_delta, 0.125);

    tree.process(1.0 / 60.0);
    EXPECT_NEAR(root->last_delta, 1.0 / 60.0, 1e-9);
}

TEST(SceneTreeProcessTest, SetProcessFalseSkipsNode) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* off = new SpyNode("off", &log);
    SpyNode* leaf = new SpyNode("leaf", &log);
    off->add_child(leaf);
    root->add_child(off);
    tree.set_root(root);
    off->set_process(false);
    log.clear();

    tree.process(0.016);

    // off 被跳过，但其子 leaf 仍处理（关闭仅针对本节点）
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "root:process");
    EXPECT_EQ(log[1], "leaf:process");
}

TEST(SceneTreeProcessTest, ProcessWithoutRootIsNoop) {
    SceneTree tree;  // 未 set_root
    tree.process(0.016);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SceneTree::input —— 自顶向下分发
// ---------------------------------------------------------------------------

TEST(SceneTreeInputTest, InputDispatchedTopDown) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* child = new SpyNode("child", &log);
    root->add_child(child);
    tree.set_root(root);
    log.clear();

    InputEvent ev;
    ev.type = InputEvent::Type::KEY;
    ev.key = 65;
    ev.action = KeyAction::PRESS;
    tree.input(ev);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "root:input");
    EXPECT_EQ(log[1], "child:input");
}

TEST(SceneTreeInputTest, SetProcessInputFalseSkipsNode) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* root = new SpyNode("root", &log);
    SpyNode* muted = new SpyNode("muted", &log);
    SpyNode* leaf = new SpyNode("leaf", &log);
    muted->add_child(leaf);
    root->add_child(muted);
    tree.set_root(root);
    muted->set_process_input(false);
    log.clear();

    InputEvent ev;
    ev.type = InputEvent::Type::MOUSE_BUTTON;
    tree.input(ev);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "root:input");
    EXPECT_EQ(log[1], "leaf:input");
}

// ---------------------------------------------------------------------------
// SceneTree 单例
// ---------------------------------------------------------------------------

TEST(SceneTreeTest, SingletonTracksLatestInstance) {
    SceneTree* first = new SceneTree;
    EXPECT_EQ(SceneTree::get_singleton(), first);
    {
        SceneTree second;
        EXPECT_EQ(SceneTree::get_singleton(), &second);
    }
    // second 死后 singleton_ 被清空（简单"最后实例"语义：构造覆盖、最后死者清空，
    // 不做栈式回退到 first——与 Godot 单 SceneTree 约束一致）。
    EXPECT_EQ(SceneTree::get_singleton(), nullptr);
    delete first;
    EXPECT_EQ(SceneTree::get_singleton(), nullptr);
}

TEST(SceneTreeTest, ReplaceRootFiresExitThenEnter) {
    std::vector<std::string> log;
    SceneTree tree;
    SpyNode* r1 = new SpyNode("r1", &log);
    SpyNode* r2 = new SpyNode("r2", &log);

    tree.set_root(r1);
    log.clear();
    tree.set_root(r2);  // 旧 root r1 应发 EXIT_TREE（r1 已 delete）

    // r1 的 exit_tree 已触发（在 delete 之前由 _propagate_exit_tree 发出）
    bool r1_exit = false;
    bool r2_enter = false;
    for (const auto& s : log) {
        if (s == "r1:exit_tree") r1_exit = true;
        if (s == "r2:enter_tree") r2_enter = true;
    }
    EXPECT_TRUE(r1_exit);
    EXPECT_TRUE(r2_enter);
    EXPECT_EQ(tree.get_root(), r2);
}

// ---------------------------------------------------------------------------
// 子树遍历工具
// ---------------------------------------------------------------------------

TEST(NodeTraversalTest, ForEachInSubtreeVisitsAllPreOrder) {
    Node root;
    Node* a = new Node;
    Node* b = new Node;
    Node* a1 = new Node;
    root.add_child(a);
    root.add_child(b);
    a->add_child(a1);

    std::vector<Node*> visited;
    root.for_each_in_subtree([&visited](Node* n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 4u);
    EXPECT_EQ(visited[0], &root);
    EXPECT_EQ(visited[1], a);
    EXPECT_EQ(visited[2], a1);
    EXPECT_EQ(visited[3], b);
}
