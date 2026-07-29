#pragma once

// eda/pcb/editor/route_tool.h
//
// PCB 布线交互工具（Track 工具模式）。
//
// 职责：
//   把"鼠标点击放置线段"翻译为对 Board 的 Track 编辑动作（命令化，经 UndoStack 入栈）。
//   典型交互（PcbEditor 的 Tool::TRACK 调用本工具）：
//     1. 用户进入 TRACK 模式 → route_tool.begin(board, stack, layer, width)；
//     2. 鼠标点击 → route_tool.add_point(board_pos)：
//        - 第 1 次点击 = 起点（仅记录，不创建图元）；
//        - 第 2 次点击 = 终点 → 从起点生成本段 Track（含正交 L 形拐角）→ push 入栈；
//        - 此后每次点击 = 新终点 → 从"上一终点"生成本段 Track → push 入栈；
//     3. 鼠标移动 → route_tool.preview(mouse) 取得"橡皮筋"预览路径（不提交）；
//     4. 右键 / Esc → route_tool.cancel() 清空已记录节点（会话绑定保留）；
//     5. 退出 TRACK 模式 → route_tool.finish() 解除绑定。
//
// 栅格吸附（set_snap_grid）：每次 add_point / preview 的输入先吸附到最近栅格点。
//
// 正交模式（set_orthogonal）：开启后，两端点非共线时自动插入 90° 拐角形成 L 形走线
//   （简化版：固定"先水平后垂直"，拐角落在 (end.x, start.y)）。
//
// 命令化策略（与 PcbEditor 一致）：
//   Board 当前只暴露 add_track，没有 remove_*。撤销因此实现为"逻辑隐藏"——
//   RouteTool 自身维护 hidden_uuids_ 集合，AddRouteTrackCommand 的 execute/undo/redo
//   通过 mark_visible / mark_hidden 切换可见性。is_hidden() 供 hit_test / 渲染跳过。
//
// 不可拷贝/移动：内部持有 Signal<>，需稳定地址。
//
// 依赖：eda_pcb（Board/Track）+ eda_command（UndoStack/Command）+ eda_geometry（Point/Path）。
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <string>
#include <unordered_set>
#include <vector>

#include "core/object/signal.h"
#include "eda/geometry/types.h"

namespace eda::command {
class UndoStack;
}  // namespace eda::command

namespace eda::pcb {

class Board;

namespace detail {
class AddRouteTrackCommand;
}  // namespace detail

// ============================================================================
// RouteTool —— PCB 布线交互工具（节点列表 → Track 序列）。
// ============================================================================
class RouteTool {
public:
    RouteTool();
    ~RouteTool();

    RouteTool(const RouteTool&) = delete;
    RouteTool& operator=(const RouteTool&) = delete;
    RouteTool(RouteTool&&) = delete;
    RouteTool& operator=(RouteTool&&) = delete;

    // ---------------- 会话 ----------------

    // 开始布线会话：绑定 Board / UndoStack + 当前层 / 默认宽度，重置节点列表。
    void begin(Board* board, command::UndoStack* stack, std::string layer_id,
               geometry::Coord default_width_nm);

    // 结束布线会话：清空节点 + 解除 Board/Stack 绑定。
    void finish();

    // 取消当前布线（不清除已提交的 Track）：仅清空未提交的节点列表。
    void cancel();

    // ---------------- 节点输入 ----------------

    // 添加一个布线节点（自动栅格吸附）。
    //   - 第 1 个节点 = 起点：仅记录，返回空串（不创建图元）。
    //   - 第 2 个节点起 = 新终点：从"上一节点"生成本段 Track 并 push 入栈；
    //     与上一节点重合（零长度）时忽略，返回空串。
    //   返回值 = 新建 Track 的 UUID（无创建返回空串）。
    std::string add_point(geometry::Point board_pos);

    // 取得"橡皮筋"预览路径（从最后节点到 current_mouse，含正交拐角，不提交）。
    // 无活跃会话或无起点时返回空 Path（points 为空）。
    geometry::Path preview(geometry::Point current_mouse) const;

    // ---------------- 配置 ----------------

    // 栅格吸附间距（nm）。设 0 / 负数 = 关闭吸附。默认关闭。
    void set_snap_grid(geometry::Coord nm);
    geometry::Coord snap_grid() const { return snap_grid_; }

    // 正交模式（true = 自动 90° L 形拐角）。默认关闭。
    void set_orthogonal(bool enabled);
    bool is_orthogonal() const { return orthogonal_; }

    // ---------------- 状态查询 ----------------

    bool is_active() const { return board_ != nullptr; }      // 会话已 begin 且未 finish
    bool has_start() const { return !nodes_.empty(); }         // 已有起点（至少 1 个节点）
    const std::vector<geometry::Point>& nodes() const { return nodes_; }
    geometry::Coord width() const { return width_; }
    const std::string& layer_id() const { return layer_id_; }

    // ---------------- 可见性（"逻辑隐藏"标记，供 hit_test/渲染跳过）----------------

    bool is_hidden(const std::string& uuid) const { return hidden_uuids_.count(uuid) > 0; }

    // ---------------- 信号 ----------------

    // 每次 Track push 入栈时 emit（参数 = 新建 Track 的 UUID）。
    // 注：按值传递 std::string —— Variant 的 unpack_arg 仅特化值类型 std::string。
    Signal<std::string>& track_placed() { return track_placed_; }

private:
    Board* board_ = nullptr;
    command::UndoStack* undo_stack_ = nullptr;
    std::string layer_id_;
    geometry::Coord width_ = 0;

    geometry::Coord snap_grid_ = 0;  // 0 = 关闭
    bool orthogonal_ = false;

    std::vector<geometry::Point> nodes_;  // 已记录的布线节点（含起点）

    std::unordered_set<std::string> hidden_uuids_;  // 撤销"逻辑隐藏"集合

    Signal<std::string> track_placed_;

    // 把板坐标按 snap_grid_ 吸附到最近栅格点（关闭时原样返回）。
    geometry::Point snap_point(geometry::Point p) const;
    // 计算从 a 到 b 的中心线路径（正交模式下含 L 形拐角）。width 已写入。
    geometry::Path build_segment(geometry::Point a, geometry::Point b) const;

    // 供 AddRouteTrackCommand 实现命令化（撤销 = 逻辑隐藏）。
    void mark_hidden(const std::string& uuid) { hidden_uuids_.insert(uuid); }
    void mark_visible(const std::string& uuid) { hidden_uuids_.erase(uuid); }

    friend class detail::AddRouteTrackCommand;
};

}  // namespace eda::pcb
