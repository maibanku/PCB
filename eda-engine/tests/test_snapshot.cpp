// P5 Task 2 · 版本快照（Snapshot）测试。
//
// 覆盖：
//   - capture -> list 非空（快照落盘到 <project>/.eda-snapshots/）
//   - capture 后修改树 -> restore -> 树恢复相等（深比较）
//   - label 持久：含空格 / 特殊字符的标签 round-trip 保留原样
//   - 上限丢弃最旧（limit=3，capture 5 份 -> 仅留最新 3 份）
//   - 时间线按时间倒序（list 最新在前，created_time 单调不增）
//   - restore 找不到 snapshot_id 返回 false
//   - diff 占位：boards / pages 数量差异（delta = b - a）
//   - parent_uuid 链：每份快照的 parent 指向上一份
//
// 依赖：eda/project/snapshot.h + eda/project/project.h。
// 不改 CMakeLists.txt：test_snapshot 由主循环加入 tests/CMakeLists。
//
// 注：测试在 NTFS / ext4 / tmpfs 上时间分辨率足够区分同秒内的多次 capture；
//     为可移植性，关键时序测试加 1ms sleep 保底。

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "eda/project/project.h"
#include "eda/project/snapshot.h"

using namespace eda;

namespace {

// 每个测试用独立临时工程目录，避免相互污染。
class SnapshotTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path() /
                    ("eda_snapshot_test_" + std::to_string(counter_++));
        std::filesystem::create_directories(base, ec);
        dir_ = base.string();
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    const std::string& dir() const { return dir_; }

private:
    std::string dir_;
    static int counter_;
};
int SnapshotTest::counter_ = 0;

// 填充一棵示例树：1 板 "Main" + 1 页 "S1"。
void fill_demo_tree(ProjectTree& tree, const std::string& project_name) {
    tree.root().set_name(project_name);
    tree.root().add_board("Main", BoardType::MAIN)
        .add_page("S1", PageType::SCHEMATIC, "res://sheets/s1.sch.toml");
}

// 微小延迟：确保文件系统 last_write_time 可区分（跨平台保底）。
void touch_fs_clock() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}  // namespace

// ============================================================================
// capture -> list 非空
// ============================================================================

TEST_F(SnapshotTest, CaptureProducesNonEmptyList) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "Demo");

    const auto info = mgr.capture(tree, "first");
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->uuid.empty());
    EXPECT_EQ(info->note, "first");
    EXPECT_GT(info->created_time, 0);
    EXPECT_GT(info->size, 0);
    EXPECT_FALSE(info->filename.empty());

    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].uuid, info->uuid);
    EXPECT_EQ(all[0].note, "first");
    EXPECT_EQ(all[0].created_time, info->created_time);
    EXPECT_EQ(all[0].filename, info->filename);

    // 文件确实落在 <project>/.eda-snapshots/ 下
    const std::string expected_subdir =
        dir() + "/.eda-snapshots/" + info->filename;
    EXPECT_TRUE(std::filesystem::exists(expected_subdir));
}

TEST_F(SnapshotTest, EmptyProjectDirListReturnsEmpty) {
    SnapshotManager mgr(dir());
    EXPECT_TRUE(mgr.list().empty());
    // 还未 capture 时，.eda-snapshots 目录不存在，list 不应崩溃。
}

// ============================================================================
// restore：回滚到指定快照
// ============================================================================

TEST_F(SnapshotTest, RestoreRollsBackTree) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "Original");
    const auto snap = mgr.capture(tree, "v1");
    ASSERT_TRUE(snap.has_value());

    // 修改树：改名 + 加一块板 + 加一页
    tree.root().set_name("Modified");
    tree.root().add_board("Extra", BoardType::DAUGHTER);
    tree.root().board(0).add_page("S2", PageType::SCHEMATIC, "res://s2.sch.toml");
    ASSERT_EQ(tree.root().board_count(), 2u);
    ASSERT_EQ(tree.root().name(), "Modified");

    // 回滚
    ASSERT_TRUE(mgr.restore(tree, snap->uuid));
    EXPECT_EQ(tree.root().board_count(), 1u);
    EXPECT_EQ(tree.root().name(), "Original");
    ASSERT_EQ(tree.root().board(0).name(), "Main");
    EXPECT_EQ(tree.root().board(0).page_count(), 1u);
    EXPECT_EQ(tree.root().board(0).page(0).name(), "S1");
}

TEST_F(SnapshotTest, RestoreDeepEqualityWithCapturedTree) {
    SnapshotManager mgr(dir());
    ProjectTree original;
    fill_demo_tree(original, "DeepEq");
    original.root().add_dependency("lib://standard@1.2.0");
    const auto snap = mgr.capture(original, "deep");
    ASSERT_TRUE(snap.has_value());

    // 用一份完全不同的树来 restore，结果应与 original 深比较相等。
    ProjectTree other;
    other.root().set_name("Placeholder");
    other.root().add_board("Throwaway", BoardType::MAIN);

    ASSERT_TRUE(mgr.restore(other, snap->uuid));
    // 深比较：name / uuid / boards / pages / dependencies 全等。
    EXPECT_EQ(other.root(), original.root());
}

TEST_F(SnapshotTest, RestoreUnknownSnapshotIdReturnsFalse) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "X");
    ASSERT_TRUE(mgr.capture(tree, "only").has_value());

    ProjectTree target;
    target.root().set_name("Untouched");
    EXPECT_FALSE(mgr.restore(target, "non-existent-uuid"));
    EXPECT_EQ(target.root().name(), "Untouched");  // 未被改动
    EXPECT_FALSE(mgr.restore(target, ""));         // 空 id
}

// ============================================================================
// label 持久
// ============================================================================

TEST_F(SnapshotTest, LabelPersistsWithSpaces) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "X");

    const auto a = mgr.capture(tree, "alpha");
    touch_fs_clock();
    const auto b = mgr.capture(tree, "beta with spaces");  // 含空格
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());

    // note 字段应原样 round-trip（文件名做了安全化，但 note 不变）
    EXPECT_EQ(a->note, "alpha");
    EXPECT_EQ(b->note, "beta with spaces");

    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 2u);
    // 倒序：beta 在前
    EXPECT_EQ(all[0].note, "beta with spaces");
    EXPECT_EQ(all[1].note, "alpha");
}

TEST_F(SnapshotTest, LabelPersistsWithUnicodeAndSpecialChars) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "X");

    // 含中文 / 引号 / 斜杠（文件名会安全化，note 原样）
    const std::string label = "v1.0 \"final\"/版本-2";
    const auto snap = mgr.capture(tree, label);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->note, label);

    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].note, label);
}

// ============================================================================
// 上限丢弃最旧
// ============================================================================

TEST_F(SnapshotTest, LimitDropsOldest) {
    SnapshotManager mgr(dir(), 3);  // 上限 3
    ProjectTree tree;
    fill_demo_tree(tree, "Limited");

    // 5 份快照，唯一标签 v0..v4，顺序创建
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(mgr.capture(tree, "v" + std::to_string(i)).has_value());
        touch_fs_clock();
    }

    const auto all = mgr.list();
    EXPECT_EQ(all.size(), 3u);  // 上限生效
    // 保留最新 3 份：v4, v3, v2（v0、v1 被丢）
    EXPECT_EQ(all[0].note, "v4");
    EXPECT_EQ(all[1].note, "v3");
    EXPECT_EQ(all[2].note, "v2");
}

TEST_F(SnapshotTest, DefaultLimitIs100) {
    SnapshotManager mgr(dir());
    EXPECT_EQ(mgr.limit(), SnapshotManager::kDefaultLimit);
    EXPECT_EQ(SnapshotManager::kDefaultLimit, 100u);
}

TEST_F(SnapshotTest, SetLimitPrunesImmediately) {
    // 缩小上限时，下一次 capture 触发 prune 把超出的最旧条目清掉。
    SnapshotManager mgr(dir(), 5);
    ProjectTree tree;
    fill_demo_tree(tree, "X");
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(mgr.capture(tree, "v" + std::to_string(i)).has_value());
        touch_fs_clock();
    }
    ASSERT_EQ(mgr.list().size(), 5u);

    mgr.set_limit(2);
    ASSERT_TRUE(mgr.capture(tree, "v5").has_value());  // 触发 prune 到 2

    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].note, "v5");
    EXPECT_EQ(all[1].note, "v4");
}

// ============================================================================
// 时间线按时间倒序
// ============================================================================

TEST_F(SnapshotTest, TimelineNewestFirst) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "Timeline");

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(mgr.capture(tree, "t" + std::to_string(i)).has_value());
        touch_fs_clock();
    }

    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 4u);
    // 倒序：t3 在前，t0 在后
    EXPECT_EQ(all[0].note, "t3");
    EXPECT_EQ(all[3].note, "t0");
    // created_time 单调不增
    for (size_t i = 1; i < all.size(); ++i) {
        EXPECT_GE(all[i - 1].created_time, all[i].created_time);
    }
}

// ============================================================================
// diff 占位：boards / pages 数量差异
// ============================================================================

TEST_F(SnapshotTest, DiffReportsCountDeltas) {
    SnapshotManager mgr(dir());

    // 快照 A：1 板 1 页
    ProjectTree a;
    a.root().set_name("A");
    a.root().add_board("Main", BoardType::MAIN)
        .add_page("S1", PageType::SCHEMATIC, "res://s1.sch.toml");
    const auto sa = mgr.capture(a, "a");

    // 快照 B：2 板 4 页（Main 1 页 + Expansion 3 页）
    ProjectTree b;
    b.root().set_name("B");
    auto& bm = b.root().add_board("Main", BoardType::MAIN);
    bm.add_page("S1", PageType::SCHEMATIC, "res://s1.sch.toml");
    auto& be = b.root().add_board("Expansion", BoardType::DAUGHTER);
    be.add_page("S2", PageType::SCHEMATIC, "res://s2.sch.toml");
    be.add_page("S3", PageType::SCHEMATIC, "res://s3.sch.toml");
    be.add_page("S4", PageType::SCHEMATIC, "res://s4.sch.toml");
    const auto sb = mgr.capture(b, "b");

    ASSERT_TRUE(sa.has_value());
    ASSERT_TRUE(sb.has_value());

    const auto d = mgr.diff(sa->uuid, sb->uuid);
    EXPECT_EQ(d.board_count_a, 1);
    EXPECT_EQ(d.page_count_a, 1);
    EXPECT_EQ(d.board_count_b, 2);
    EXPECT_EQ(d.page_count_b, 4);
    EXPECT_EQ(d.board_delta(), 1);  // 2 - 1
    EXPECT_EQ(d.page_delta(), 3);   // 4 - 1
}

TEST_F(SnapshotTest, DiffWithUnknownIdYieldsZeroCounts) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "X");
    const auto snap = mgr.capture(tree, "only");
    ASSERT_TRUE(snap.has_value());

    // 一边有效、一边无效 -> 无效侧计数字段为 0
    const auto d = mgr.diff(snap->uuid, "no-such-snapshot");
    EXPECT_EQ(d.board_count_a, 1);
    EXPECT_EQ(d.page_count_a, 1);
    EXPECT_EQ(d.board_count_b, 0);
    EXPECT_EQ(d.page_count_b, 0);
}

// ============================================================================
// parent_uuid 链
// ============================================================================

TEST_F(SnapshotTest, ParentUuidChainsSnapshots) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "Chain");

    const auto s0 = mgr.capture(tree, "s0");
    touch_fs_clock();
    const auto s1 = mgr.capture(tree, "s1");
    touch_fs_clock();
    const auto s2 = mgr.capture(tree, "s2");
    ASSERT_TRUE(s0.has_value() && s1.has_value() && s2.has_value());

    // 首份 parent 为空；后续每份 parent 指向上一份的 uuid
    EXPECT_TRUE(s0->parent_uuid.empty());
    EXPECT_EQ(s1->parent_uuid, s0->uuid);
    EXPECT_EQ(s2->parent_uuid, s1->uuid);

    // 持久化后从 list 读回仍然成立
    const auto all = mgr.list();
    ASSERT_EQ(all.size(), 3u);  // 倒序：s2, s1, s0
    EXPECT_TRUE(all[2].parent_uuid.empty());
    EXPECT_EQ(all[1].parent_uuid, all[2].uuid);
    EXPECT_EQ(all[0].parent_uuid, all[1].uuid);
}

// ============================================================================
// restore 后可继续 capture（链路不被破坏）
// ============================================================================

TEST_F(SnapshotTest, CanCaptureAfterRestore) {
    SnapshotManager mgr(dir());
    ProjectTree tree;
    fill_demo_tree(tree, "X");
    const auto s0 = mgr.capture(tree, "s0");
    ASSERT_TRUE(s0.has_value());

    ASSERT_TRUE(mgr.restore(tree, s0->uuid));
    const auto s1 = mgr.capture(tree, "s1");  // 不应崩溃，且 parent = s0
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(s1->parent_uuid, s0->uuid);

    EXPECT_EQ(mgr.list().size(), 2u);
}
