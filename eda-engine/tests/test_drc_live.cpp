// tests/test_drc_live.cpp
//
// P11 实时 DRC 联动测试（11-02 实时校验 / dirty 标记 + 自动 DRC + 信号通知）。
//
// 覆盖：
//   - 初始无 Board → run → 0 errors / 0 warnings
//   - 设置 Board + DrcOptions → run_if_dirty → 正确 error_count
//   - mark_dirty → run_if_dirty 重算（结果随 Board 变化刷新）
//   - 缓存语义：未脏时 run_if_dirty 不重算（results_changed 不触发）
//   - results_changed 信号触发（携带 error_count）
//   - auto_run=false → mark_dirty + run_if_dirty 不重跑；run_now 仍可强制刷新
//   - 两 Track 间距不足 → error_count=1；修复后 → 0
//   - warning 通道：Net 含 Pad 但无 Track → UnroutedNetRule WARNING → warning_count=1
//
// 数据：直接构造 pcb::Board（Track/Net），与 test_drc.cpp 复用同一辅助。
// ValidationContext{&board} 由控制器内部构造（ctx.board = board_）。
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_drc_live test_drc_live.cpp)
//   target_link_libraries(test_drc_live PRIVATE eda_validation gtest_main)
//   gtest_discover_tests(test_drc_live)

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/net.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/validation/drc.h"
#include "eda/validation/drc_live.h"
#include "eda/validation/validation.h"

using namespace eda;
using namespace eda::pcb;
using namespace eda::geometry;
using namespace eda::validation;

namespace {

// 1 mm = 1_000_000 nm（Coord 单位为 nm）
constexpr Coord kMm = 1'000'000;

// 构造一条水平 Track（path 沿 x 轴，给定 y / width / net）。
Track& add_horizontal_track(Board& board, Coord x0, Coord x1, Coord y,
                            Coord width, const std::string& layer,
                            const std::string& net_uuid) {
    Path p;
    p.points = {Point{x0, y}, Point{x1, y}};
    p.width = width;
    return board.add_track(p, layer, net_uuid);
}

}  // namespace

// ============================================================
// 初始状态
// ============================================================

TEST(DrcLiveTest, InitialNoBoardHasZeroErrors) {
    DrcLiveController c;
    const auto& r = c.run_if_dirty();
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(c.error_count(), 0u);
    EXPECT_EQ(c.warning_count(), 0u);
}

TEST(DrcLiveTest, InitialAutoRunDefaultTrue) {
    DrcLiveController c;
    EXPECT_TRUE(c.auto_run());
}

TEST(DrcLiveTest, ResultsReferenceStableAcrossRuns) {
    DrcLiveController c;
    const auto& r1 = c.results();
    const auto* addr1 = r1.data();
    c.run_if_dirty();
    const auto& r2 = c.results();
    // 不重算（无 Board，仍 dirty，但第一次已 do_run 清 dirty；第二次 do_run 不发生）
    // 地址不要求稳定，但 results() 返回的引用应与 cached_results_ 一致。
    EXPECT_EQ(&r1, &r2);
    (void)addr1;
}

// ============================================================
// 设置 Board + DrcOptions → run → error_count
// ============================================================

TEST(DrcLiveTest, RunIfDirtyComputesViolations) {
    DrcLiveController c;
    Board board;
    // 两 Track 中心线 y=0 / y=300k，width=200k → 铜距 100k < 200k → 1 clearance ERROR
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});

    const auto& r = c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 1u);
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.size(), 1u);
}

// ============================================================
// mark_dirty → run_if_dirty 重算
// ============================================================

TEST(DrcLiveTest, MarkDirtyTriggersRerunOnRunIfDirty) {
    DrcLiveController c;
    Board board;
    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.run_if_dirty();  // 首次计算：空板 → 0 errors
    EXPECT_EQ(c.error_count(), 0u);

    // 修改 Board：加两条过近 Track
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");
    c.mark_dirty();

    const auto& r = c.run_if_dirty();  // dirty → 重算
    EXPECT_EQ(c.error_count(), 1u);
    EXPECT_EQ(r.size(), 1u);
}

TEST(DrcLiveTest, CleanRunIfDirtyReturnsCacheNoRerun) {
    DrcLiveController c;
    Board board;
    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.run_if_dirty();  // 首次计算 → 0 errors，清 dirty

    int signal_count = 0;
    c.results_changed().connect([&signal_count](int) { ++signal_count; });

    // 未 mark_dirty → 第二次 run_if_dirty 返回缓存，不重算、不 emit
    c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 0u);
    EXPECT_EQ(signal_count, 0);
}

// ============================================================
// results_changed 信号触发
// ============================================================

TEST(DrcLiveTest, ResultsChangedSignalFiresWithErrorCode) {
    DrcLiveController c;
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");

    int last_error_count = -1;
    int emit_count = 0;
    c.results_changed().connect([&](int n) {
        last_error_count = n;
        ++emit_count;
    });

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.run_if_dirty();

    EXPECT_EQ(emit_count, 1);
    EXPECT_EQ(last_error_count, 1);
    EXPECT_EQ(c.error_count(), 1u);
}

TEST(DrcLiveTest, RunNowEmitsEvenWhenClean) {
    DrcLiveController c;
    Board board;
    c.set_board(&board);
    c.run_if_dirty();  // 清 dirty

    int emit_count = 0;
    c.results_changed().connect([&emit_count](int) { ++emit_count; });

    c.run_now();  // 即便未脏也 emit
    EXPECT_EQ(emit_count, 1);
}

// ============================================================
// auto_run=false → mark_dirty 不触发 run
// ============================================================

TEST(DrcLiveTest, AutoRunFalseSkipsRunIfDirty) {
    DrcLiveController c;
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.set_auto_run(false);
    c.mark_dirty();
    EXPECT_FALSE(c.auto_run());

    int signal_count = 0;
    c.results_changed().connect([&signal_count](int) { ++signal_count; });

    // auto_run=false → run_if_dirty 不重跑（缓存仍空，0 errors）
    c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 0u);
    EXPECT_EQ(signal_count, 0);

    // run_now 强制重跑
    c.run_now();
    EXPECT_EQ(c.error_count(), 1u);
    EXPECT_EQ(signal_count, 1);
}

TEST(DrcLiveTest, AutoRunFalseThenTrueResumesAuto) {
    DrcLiveController c;
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.set_auto_run(false);
    c.run_if_dirty();  // 不重跑
    EXPECT_EQ(c.error_count(), 0u);

    // 切回 auto_run=true 并 mark_dirty
    c.set_auto_run(true);
    c.mark_dirty();
    c.run_if_dirty();  // 重跑
    EXPECT_EQ(c.error_count(), 1u);
}

// ============================================================
// 两 Track 间距不足 → error_count=1；修复后 → 0
// ============================================================

TEST(DrcLiveTest, ClearanceViolationFixedClearsErrorCount) {
    DrcLiveController c;
    Board board;
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 300'000, 200'000, "F.Cu", "net-B");
    // 铜距 = 300k - 200k = 100k < 200k → 1 clearance ERROR

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.run_if_dirty();
    ASSERT_EQ(c.error_count(), 1u);

    // 修复：切换到一块无违例的 Board（铜距 300k ≥ 200k）
    Board fixed_board;
    add_horizontal_track(fixed_board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(fixed_board, 0, kMm, 500'000, 200'000, "F.Cu", "net-B");

    c.set_board(&fixed_board);
    const auto& r = c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 0u);
    EXPECT_TRUE(r.empty());
}

// ============================================================
// warning 通道：UnroutedNetRule → WARNING 计数
// ============================================================

TEST(DrcLiveTest, UnroutedNetReportsWarningCount) {
    DrcLiveController c;
    Board board;
    Net& net = board.add_net("N1");
    Pad& pad = board.add_pad(Point{0, 0}, 300'000, 300'000, PadShape::RECT, "F.Cu");
    pad.set_net_uuid(net.uuid());
    net.add_member(pad.uuid());
    // Net 有 Pad 成员、0 Track → UnroutedNetRule WARNING

    c.set_board(&board);
    c.run_if_dirty();

    EXPECT_EQ(c.error_count(), 0u);
    EXPECT_EQ(c.warning_count(), 1u);
}

// ============================================================
// set_options 变化触发重算（更严格的 min_clearance 显露违例）
// ============================================================

TEST(DrcLiveTest, SetOptionsTightensClearanceAndMarksDirty) {
    DrcLiveController c;
    Board board;
    // 两 Track 铜距 300k
    add_horizontal_track(board, 0, kMm, 0, 200'000, "F.Cu", "net-A");
    add_horizontal_track(board, 0, kMm, 500'000, 200'000, "F.Cu", "net-B");

    c.set_board(&board);
    c.set_options(DrcOptions{.min_clearance_nm = 200'000, .min_width_nm = 150'000});
    c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 0u);  // 300k ≥ 200k 通过

    // 收紧到 400k → 铜距 300k < 400k 违例
    c.set_options(DrcOptions{.min_clearance_nm = 400'000, .min_width_nm = 150'000});
    c.run_if_dirty();
    EXPECT_EQ(c.error_count(), 1u);
}
