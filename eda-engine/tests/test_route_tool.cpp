// tests/test_route_tool.cpp
//
// PCB 布线交互工具（RouteTool）测试。
//
// 覆盖（任务规范要求的 6 个用例）：
//   1. 两点 → 1 Track（首点=起点，次点=终点，1 段 Track 入栈 + board.track_count=1）
//   2. 三点 → 2 Track（每相邻节点对生成 1 段，共 2 段，节点链正确）
//   3. 栅格吸附（snap_grid=1mm，输入 1.4mm → 落到 1mm）
//   4. 正交 L 形（对角线输入 → 路径含 1 个 90° 拐角，3 点）
//   5. cancel 清空节点列表（取消后 has_start=false）
//   6. preview 返回正确路径（从最后节点到鼠标，不提交，board.track_count 不变）
//
// 额外覆盖：
//   - track_placed 信号每次 push 触发（携带 UUID）
//   - 零长度节点忽略
//   - UndoStack 命令化：undo → 逻辑隐藏；redo → 恢复
//   - 会话生命周期：begin → is_active；finish → 解除绑定
//   - 无 UndoStack 时直接 add_track（不入栈）
//
// 集成：tests/CMakeLists.txt 追加 test_route_tool，link eda_pcb_editor。

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "eda/command/undo_stack.h"
#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/editor/route_tool.h"
#include "eda/pcb/track.h"

using namespace eda;
using namespace eda::pcb;
using namespace eda::geometry;
using namespace eda::command;

namespace {

constexpr Coord kMm = 1'000'000;  // 1 mm = 1_000_000 nm
constexpr Coord kDefaultWidth = 200 * 1000;  // 0.2 mm

// 测试夹具：Board + UndoStack + RouteTool，会话已 begin（F.Cu 层 / 0.2mm 默认宽度）。
struct RouteFixture {
    Board board;
    UndoStack stack;
    RouteTool tool;

    RouteFixture() {
        tool.begin(&board, &stack, "F.Cu", kDefaultWidth);
    }
};

}  // namespace

// ============================================================
// 1. 两点 → 1 Track
// ============================================================

TEST(RouteToolTwoPointTest, FirstPointIsStartNoTrackCreated) {
    RouteFixture f;
    EXPECT_FALSE(f.tool.has_start());  // begin 后无起点

    const std::string uuid = f.tool.add_point(Point{0, 0});
    EXPECT_TRUE(uuid.empty());             // 起点：不创建图元
    EXPECT_TRUE(f.tool.has_start());
    EXPECT_EQ(f.tool.nodes().size(), 1u);
    EXPECT_EQ(f.board.track_count(), 0u);
    EXPECT_EQ(f.stack.size(), 0u);
}

TEST(RouteToolTwoPointTest, SecondPointCreatesOneTrack) {
    RouteFixture f;

    f.tool.add_point(Point{0, 0});
    const std::string uuid = f.tool.add_point(Point{5 * kMm, 0});

    EXPECT_FALSE(uuid.empty());
    EXPECT_EQ(f.board.track_count(), 1u);
    EXPECT_EQ(f.stack.size(), 1u);

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    ASSERT_EQ(tracks[0]->path().points.size(), 2u);
    EXPECT_EQ(tracks[0]->path().points[0], (Point{0, 0}));
    EXPECT_EQ(tracks[0]->path().points[1], (Point{5 * kMm, 0}));
    EXPECT_EQ(tracks[0]->width(), kDefaultWidth);
    EXPECT_EQ(tracks[0]->layer_id(), "F.Cu");
    EXPECT_EQ(tracks[0]->uuid(), uuid);

    // 节点链：起点 + 终点 = 2
    EXPECT_EQ(f.tool.nodes().size(), 2u);
    EXPECT_EQ(f.tool.nodes().back(), (Point{5 * kMm, 0}));
}

// ============================================================
// 2. 三点 → 2 Track
// ============================================================

TEST(RouteToolThreePointTest, ThreePointsProduceTwoTracks) {
    RouteFixture f;

    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 0});
    f.tool.add_point(Point{5 * kMm, 3 * kMm});

    EXPECT_EQ(f.board.track_count(), 2u);
    EXPECT_EQ(f.stack.size(), 2u);

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 2u);
    // 第 1 段：(0,0) → (5mm, 0)
    ASSERT_EQ(tracks[0]->path().points.size(), 2u);
    EXPECT_EQ(tracks[0]->path().points[0], (Point{0, 0}));
    EXPECT_EQ(tracks[0]->path().points[1], (Point{5 * kMm, 0}));
    // 第 2 段：(5mm, 0) → (5mm, 3mm) —— 起点为上一终点
    ASSERT_EQ(tracks[1]->path().points.size(), 2u);
    EXPECT_EQ(tracks[1]->path().points[0], (Point{5 * kMm, 0}));
    EXPECT_EQ(tracks[1]->path().points[1], (Point{5 * kMm, 3 * kMm}));

    // 节点链：3 个节点
    EXPECT_EQ(f.tool.nodes().size(), 3u);
    EXPECT_EQ(f.tool.nodes()[0], (Point{0, 0}));
    EXPECT_EQ(f.tool.nodes()[1], (Point{5 * kMm, 0}));
    EXPECT_EQ(f.tool.nodes()[2], (Point{5 * kMm, 3 * kMm}));
}

TEST(RouteToolThreePointTest, ZeroLengthSecondPointIgnored) {
    RouteFixture f;
    f.tool.add_point(Point{2 * kMm, 2 * kMm});
    // 与起点同位置：忽略，不创建
    const std::string uuid = f.tool.add_point(Point{2 * kMm, 2 * kMm});

    EXPECT_TRUE(uuid.empty());
    EXPECT_EQ(f.board.track_count(), 0u);
    EXPECT_EQ(f.stack.size(), 0u);
    EXPECT_EQ(f.tool.nodes().size(), 1u);  // 仍只有起点
}

// ============================================================
// 3. 栅格吸附
// ============================================================

TEST(RouteToolSnapTest, SnapGridRoundsToNearestGridPoint) {
    RouteFixture f;
    f.tool.set_snap_grid(1 * kMm);  // 1 mm 栅格

    // 1.4mm, 1.6mm → 都吸附到 (1mm or 2mm)；1.4→1mm, 1.6→2mm（round 半数近）
    f.tool.add_point(Point{static_cast<Coord>(1.4 * kMm), static_cast<Coord>(1.6 * kMm)});
    EXPECT_EQ(f.tool.nodes()[0], (Point{1 * kMm, 2 * kMm}));
}

TEST(RouteToolSnapTest, SnapGridAppliesToTrackEndpoint) {
    RouteFixture f;
    f.tool.set_snap_grid(1 * kMm);

    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{static_cast<Coord>(3.45 * kMm), 0});  // 3.45mm → 3mm

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    EXPECT_EQ(tracks[0]->path().points[1].x, 3 * kMm);
}

TEST(RouteToolSnapTest, ZeroGridDisablesSnap) {
    RouteFixture f;
    f.tool.set_snap_grid(0);  // 关闭

    f.tool.add_point(Point{1234, 5678});
    EXPECT_EQ(f.tool.nodes()[0], (Point{1234, 5678}));  // 原样
}

// ============================================================
// 4. 正交 L 形
// ============================================================

TEST(RouteToolOrthogonalTest, DiagonalProducesLCShape) {
    RouteFixture f;
    f.tool.set_orthogonal(true);

    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 3 * kMm});  // 对角线 → L 形

    EXPECT_EQ(f.board.track_count(), 1u);
    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    // L 形 = 3 点：起点、拐角、终点
    ASSERT_EQ(tracks[0]->path().points.size(), 3u);
    EXPECT_EQ(tracks[0]->path().points[0], (Point{0, 0}));
    // 拐角在 (end.x, start.y) = (5mm, 0)
    EXPECT_EQ(tracks[0]->path().points[1], (Point{5 * kMm, 0}));
    EXPECT_EQ(tracks[0]->path().points[2], (Point{5 * kMm, 3 * kMm}));
}

TEST(RouteToolOrthogonalTest, OrthogonalOffKeepsDiagonal) {
    RouteFixture f;
    f.tool.set_orthogonal(false);  // 默认即为关

    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 3 * kMm});

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    ASSERT_EQ(tracks[0]->path().points.size(), 2u);  // 直接 2 点，无拐角
}

TEST(RouteToolOrthogonalTest, OrthogonalAxisAlignedNoCorner) {
    RouteFixture f;
    f.tool.set_orthogonal(true);

    // 已水平共线：不需要拐角
    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 0});

    const auto& tracks = f.board.tracks();
    ASSERT_EQ(tracks.size(), 1u);
    ASSERT_EQ(tracks[0]->path().points.size(), 2u);  // 无拐角插入
}

// ============================================================
// 5. cancel 清空节点列表
// ============================================================

TEST(RouteToolCancelTest, CancelClearsNodes) {
    RouteFixture f;
    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 0});
    ASSERT_EQ(f.board.track_count(), 1u);
    ASSERT_TRUE(f.tool.has_start());

    f.tool.cancel();

    EXPECT_FALSE(f.tool.has_start());
    EXPECT_TRUE(f.tool.nodes().empty());
    // 已提交的 Track 不受影响
    EXPECT_EQ(f.board.track_count(), 1u);
    // 会话仍活跃，可重新开始
    EXPECT_TRUE(f.tool.is_active());
}

TEST(RouteToolCancelTest, CancelThenStartNewRoute) {
    RouteFixture f;
    f.tool.add_point(Point{0, 0});
    f.tool.cancel();

    // 重新布线
    f.tool.add_point(Point{1 * kMm, 1 * kMm});
    f.tool.add_point(Point{2 * kMm, 1 * kMm});
    EXPECT_EQ(f.board.track_count(), 1u);
    EXPECT_EQ(f.tool.nodes().size(), 2u);
}

// ============================================================
// 6. preview 返回正确 Track（路径）
// ============================================================

TEST(RouteToolPreviewTest, PreviewFromStartToMouse) {
    RouteFixture f;
    f.tool.add_point(Point{0, 0});

    const Path p = f.tool.preview(Point{5 * kMm, 0});
    ASSERT_EQ(p.points.size(), 2u);
    EXPECT_EQ(p.points[0], (Point{0, 0}));
    EXPECT_EQ(p.points[1], (Point{5 * kMm, 0}));
    EXPECT_EQ(p.width, kDefaultWidth);
    // preview 不提交
    EXPECT_EQ(f.board.track_count(), 0u);
    EXPECT_EQ(f.stack.size(), 0u);
}

TEST(RouteToolPreviewTest, PreviewContinuesFromLastNode) {
    RouteFixture f;
    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 0});  // 提交第 1 段
    ASSERT_EQ(f.board.track_count(), 1u);

    const Path p = f.tool.preview(Point{5 * kMm, 3 * kMm});
    ASSERT_EQ(p.points.size(), 2u);
    EXPECT_EQ(p.points[0], (Point{5 * kMm, 0}));  // 上一终点
    EXPECT_EQ(p.points[1], (Point{5 * kMm, 3 * kMm}));
    // preview 不改变 board 状态
    EXPECT_EQ(f.board.track_count(), 1u);
}

TEST(RouteToolPreviewTest, PreviewRespectsSnapGrid) {
    RouteFixture f;
    f.tool.set_snap_grid(1 * kMm);
    f.tool.add_point(Point{0, 0});

    const Path p = f.tool.preview(Point{static_cast<Coord>(2.4 * kMm), 0});
    EXPECT_EQ(p.points[1].x, 2 * kMm);  // 吸附
}

TEST(RouteToolPreviewTest, PreviewRespectsOrthogonal) {
    RouteFixture f;
    f.tool.set_orthogonal(true);
    f.tool.add_point(Point{0, 0});

    const Path p = f.tool.preview(Point{4 * kMm, 3 * kMm});
    ASSERT_EQ(p.points.size(), 3u);  // L 形 3 点
    EXPECT_EQ(p.points[1], (Point{4 * kMm, 0}));  // 拐角
}

TEST(RouteToolPreviewTest, PreviewEmptyBeforeStart) {
    RouteFixture f;
    const Path p = f.tool.preview(Point{5 * kMm, 0});
    EXPECT_TRUE(p.points.empty());
}

// ============================================================
// 信号：track_placed
// ============================================================

TEST(RouteToolSignalTest, TrackPlacedEmittedWithUuid) {
    RouteFixture f;
    int count = 0;
    std::string received_uuid;
    // 注：lambda 按值接收 std::string —— Callable 的 unpack_arg 仅特化值类型。
    f.tool.track_placed().connect([&](std::string uuid) {
        ++count;
        received_uuid = uuid;
    });

    f.tool.add_point(Point{0, 0});
    EXPECT_EQ(count, 0);  // 起点不触发

    const std::string uuid = f.tool.add_point(Point{5 * kMm, 0});
    EXPECT_EQ(count, 1);
    EXPECT_EQ(received_uuid, uuid);
    EXPECT_FALSE(uuid.empty());

    f.tool.add_point(Point{5 * kMm, 3 * kMm});
    EXPECT_EQ(count, 2);  // 每次 push 都触发
}

// ============================================================
// 命令化：undo / redo（逻辑隐藏）
// ============================================================

TEST(RouteToolCommandTest, UndoHidesTrackRedoRestores) {
    RouteFixture f;
    f.tool.add_point(Point{0, 0});
    f.tool.add_point(Point{5 * kMm, 0});
    ASSERT_EQ(f.board.track_count(), 1u);
    const std::string uuid = f.board.tracks()[0]->uuid();
    EXPECT_FALSE(f.tool.is_hidden(uuid));

    // undo → 逻辑隐藏
    f.stack.undo();
    EXPECT_TRUE(f.tool.is_hidden(uuid));

    // redo → 恢复可见
    f.stack.redo();
    EXPECT_FALSE(f.tool.is_hidden(uuid));
}

// ============================================================
// 会话生命周期
// ============================================================

TEST(RouteToolLifecycleTest, BeginMakesActive) {
    Board board;
    UndoStack stack;
    RouteTool tool;
    EXPECT_FALSE(tool.is_active());

    tool.begin(&board, &stack, "F.Cu", kDefaultWidth);
    EXPECT_TRUE(tool.is_active());
    EXPECT_EQ(tool.layer_id(), "F.Cu");
    EXPECT_EQ(tool.width(), kDefaultWidth);
}

TEST(RouteToolLifecycleTest, FinishDeactivatesAndClears) {
    RouteFixture f;
    ASSERT_TRUE(f.tool.is_active());
    f.tool.add_point(Point{0, 0});
    ASSERT_TRUE(f.tool.has_start());

    f.tool.finish();

    EXPECT_FALSE(f.tool.is_active());
    EXPECT_FALSE(f.tool.has_start());
    // finish 后 add_point 无副作用
    EXPECT_TRUE(f.tool.add_point(Point{1, 1}).empty());
}

TEST(RouteToolLifecycleTest, NoStackDirectAddTrack) {
    Board board;
    RouteTool tool;
    tool.begin(&board, nullptr, "F.Cu", kDefaultWidth);  // 无 UndoStack

    tool.add_point(Point{0, 0});
    const std::string uuid = tool.add_point(Point{3 * kMm, 0});
    EXPECT_FALSE(uuid.empty());
    EXPECT_EQ(board.track_count(), 1u);  // 直接落地，未入栈
}

TEST(RouteToolLifecycleTest, AddPointBeforeBeginIsNoop) {
    RouteTool tool;  // 未 begin
    EXPECT_TRUE(tool.add_point(Point{1, 1}).empty());
    EXPECT_FALSE(tool.has_start());
}
