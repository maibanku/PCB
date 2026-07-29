// eda/pcb/editor/route_tool.cpp
//
// PCB 布线交互工具实现。
//
// 核心数据流：节点列表（vector<Point>） → 每两个相邻节点生成一段 Track → 命令化入栈。
//
// 命令：detail::AddRouteTrackCommand
//   - execute 首次：board.add_track(path, layer) → 记录 UUID；
//     非首次（redo）→ mark_visible（把 undo 时隐藏的 UUID 恢复可见）。
//   - undo：mark_hidden（视觉上消失，hit_test/渲染跳过）。
//   - redo：转发到 execute。
//   （Board 无 remove_*，撤销走 RouteTool 的 hidden_uuids_ 集合。）

#include "eda/pcb/editor/route_tool.h"

#include <cmath>
#include <utility>

#include "eda/command/command.h"
#include "eda/command/undo_stack.h"
#include "eda/pcb/board.h"
#include "eda/pcb/track.h"

namespace eda::pcb {

// ============================================================================
// detail::AddRouteTrackCommand —— 布线 push 命令（经 UndoStack 入栈，可撤销）。
// ============================================================================

namespace detail {

class AddRouteTrackCommand : public command::Command {
public:
    AddRouteTrackCommand(RouteTool* tool, Board* board, geometry::Path path,
                         std::string layer_id, std::string name = "Add Route Track")
        : Command(std::move(name)),
          tool_(tool),
          board_(board),
          path_(std::move(path)),
          layer_id_(std::move(layer_id)) {}

    void execute() override {
        if (!created_) {
            Track& t = board_->add_track(path_, layer_id_);
            created_uuid_ = t.uuid();
            created_ = true;
        } else {
            tool_->mark_visible(created_uuid_);
        }
    }
    void undo() override { tool_->mark_hidden(created_uuid_); }
    void redo() override { execute(); }

    const std::string& created_uuid() const { return created_uuid_; }

    static std::string get_class_static() { return "AddRouteTrackCommand"; }
    std::string get_class() const override { return "AddRouteTrackCommand"; }
    bool is_class(const std::string& n) const override {
        return n == "AddRouteTrackCommand" || Command::is_class(n);
    }

private:
    RouteTool* tool_;
    Board* board_;
    geometry::Path path_;
    std::string layer_id_;
    std::string created_uuid_;
    bool created_ = false;
};

}  // namespace detail

// ============================================================================
// RouteTool 公共 API
// ============================================================================

RouteTool::RouteTool() = default;
RouteTool::~RouteTool() = default;

void RouteTool::begin(Board* board, command::UndoStack* stack, std::string layer_id,
                      geometry::Coord default_width_nm) {
    board_ = board;
    undo_stack_ = stack;
    layer_id_ = std::move(layer_id);
    width_ = default_width_nm;
    nodes_.clear();
}

void RouteTool::finish() {
    nodes_.clear();
    board_ = nullptr;
    undo_stack_ = nullptr;
    layer_id_.clear();
    width_ = 0;
}

void RouteTool::cancel() {
    nodes_.clear();
}

void RouteTool::set_snap_grid(geometry::Coord nm) { snap_grid_ = nm; }

void RouteTool::set_orthogonal(bool enabled) { orthogonal_ = enabled; }

// ============================================================================
// 节点输入：add_point / preview
// ============================================================================

std::string RouteTool::add_point(geometry::Point board_pos) {
    if (!board_) return "";
    const geometry::Point p = snap_point(board_pos);

    // 第 1 个节点 = 起点：仅记录。
    if (nodes_.empty()) {
        nodes_.push_back(p);
        return "";
    }

    const geometry::Point prev = nodes_.back();
    // 零长度段：忽略，避免误触产生空 Track。
    if (p.x == prev.x && p.y == prev.y) return "";

    const geometry::Path path = build_segment(prev, p);

    std::string uuid;
    if (undo_stack_) {
        auto cmd =
            std::make_unique<detail::AddRouteTrackCommand>(this, board_, path, layer_id_);
        const detail::AddRouteTrackCommand* raw = cmd.get();
        undo_stack_->push(std::move(cmd));
        uuid = raw->created_uuid();
    } else {
        Track& t = board_->add_track(path, layer_id_);
        uuid = t.uuid();
    }

    // 新节点入栈，成为下一段的"上一节点"。
    nodes_.push_back(p);

    track_placed_.emit(uuid);
    return uuid;
}

geometry::Path RouteTool::preview(geometry::Point current_mouse) const {
    if (!board_ || nodes_.empty()) return geometry::Path{};
    const geometry::Point p = snap_point(current_mouse);
    return build_segment(nodes_.back(), p);
}

// ============================================================================
// 内部工具
// ============================================================================

geometry::Point RouteTool::snap_point(geometry::Point p) const {
    if (snap_grid_ <= 0) return p;
    const double g = static_cast<double>(snap_grid_);
    return geometry::Point{
        static_cast<geometry::Coord>(std::round(static_cast<double>(p.x) / g) * g),
        static_cast<geometry::Coord>(std::round(static_cast<double>(p.y) / g) * g)};
}

geometry::Path RouteTool::build_segment(geometry::Point a, geometry::Point b) const {
    geometry::Path path;
    path.width = width_;
    path.points.push_back(a);
    // 正交模式且非共线：插入 90° 拐角（先水平后垂直，拐角在 (b.x, a.y)）。
    if (orthogonal_ && a.x != b.x && a.y != b.y) {
        path.points.push_back(geometry::Point{b.x, a.y});
    }
    path.points.push_back(b);
    return path;
}

}  // namespace eda::pcb
