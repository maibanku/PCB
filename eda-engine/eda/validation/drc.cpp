// eda/validation/drc.cpp
//
// P11 DRC 基础规则实现（11-01 统一校验中心 / PCB 物理规则）。
//
// 数据访问：规则在 .cpp 内 include pcb 头，经 ctx.board->is_class("Board") 判定后
// static_cast 下转成 const pcb::Board*，直查 Track/Pad/Via/Net（validation.h 顶部预留的
// 集成路径）。非 Board 或 board=nullptr → 安全返回空。
//
// 几何算法要点：
//   - 所有距离走 P3 geometry::measure（point_to_segment / distance）+ geometry::intersect
//     （segment_segment_intersect）。结果为 nm Coord，整数比较无精度误差。
//   - segment_segment_distance：先 segment_segment_intersect 判 0，再取四端点
//     point_to_segment 的最小（覆盖端点 + 平行段最近点两种情形）。
//   - segment_to_box：先判段与 4 条盒边相交（→ 0），再取端点→盒 + 4 角→段的最小（精确）。
//   - box_to_box：各轴间隙取 max(0) 后勾股（AABB-AABB 精确）。
//
// R-tree 邻域预筛选（ClearanceRule）：
//   - 把每条 Track / 每个 Pad 的 AABB 装入 geometry::RTree<int>（载荷 = primitive 索引）。
//   - 对 primitive i，用 expand(box_i, min_clearance) 查询邻居，仅对 j > i 且同层异 Net
//     的候选跑精确铜距，避免 O(N²) 全配对。
//
// 命名规范：namespace eda::validation、snake_case、成员尾下划线、include 从仓库根。

#include "eda/validation/drc.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/object/object.h"
#include "eda/geometry/index.h"
#include "eda/geometry/intersect.h"
#include "eda/geometry/measure.h"
#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/net.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"

namespace eda::validation {

namespace {

// ============================================================================
// 几何辅助（nm 坐标，全部基于 P3 geometry 内核）
// ============================================================================

using geometry::Box;
using geometry::Coord;
using geometry::Point;
using geometry::Segment;

constexpr Coord kMaxCoord = std::numeric_limits<Coord>::max();

// AABB 四方向外扩 amount（nm）。amount ≤ 0 时原样返回。
Box expand_box(const Box& b, Coord amount) {
    if (amount <= 0) return b;
    return Box{{b.min.x - amount, b.min.y - amount},
               {b.max.x + amount, b.max.y + amount}};
}

// 点到 AABB 最小距离（点在盒内返回 0）。
Coord point_to_box(Point p, const Box& b) {
    const Coord dx = std::max({b.min.x - p.x, p.x - b.max.x, static_cast<Coord>(0)});
    const Coord dy = std::max({b.min.y - p.y, p.y - b.max.y, static_cast<Coord>(0)});
    return geometry::distance(Point{dx, dy}, Point{0, 0});
}

// 两个 AABB 之间的最小距离（相交返回 0）。
Coord box_to_box(const Box& a, const Box& b) {
    const Coord dx = std::max({b.min.x - a.max.x, a.min.x - b.max.x, static_cast<Coord>(0)});
    const Coord dy = std::max({b.min.y - a.max.y, a.min.y - b.max.y, static_cast<Coord>(0)});
    return geometry::distance(Point{dx, dy}, Point{0, 0});
}

// 两线段最小距离：先 segment_segment_intersect 判 0（含 T 接 / 端点相切），
// 否则取四端点到对方段距离的最小（端点 + 平行两种情形的最近点都被覆盖）。
Coord segment_segment_distance(const Segment& a, const Segment& b) {
    if (geometry::segment_segment_intersect(a, b).has_value()) return 0;
    const Coord d1 = geometry::point_to_segment(a.a, b);
    const Coord d2 = geometry::point_to_segment(a.b, b);
    const Coord d3 = geometry::point_to_segment(b.a, a);
    const Coord d4 = geometry::point_to_segment(b.b, a);
    return std::min({d1, d2, d3, d4});
}

// 线段到 AABB 最小距离（精确）：
//   - 段与盒 4 条边任一相交 → 0（含段穿过盒内部）
//   - 否则取 min(端点→盒, 4 角→段)
Coord segment_to_box(const Segment& s, const Box& b) {
    const Segment edges[4] = {
        Segment{{b.min.x, b.min.y}, {b.max.x, b.min.y}},  // 下边
        Segment{{b.max.x, b.min.y}, {b.max.x, b.max.y}},  // 右边
        Segment{{b.max.x, b.max.y}, {b.min.x, b.max.y}},  // 上边
        Segment{{b.min.x, b.max.y}, {b.min.x, b.min.y}},  // 左边
    };
    for (const auto& e : edges) {
        if (geometry::segment_segment_intersect(s, e).has_value()) return 0;
    }
    Coord best = std::min(point_to_box(s.a, b), point_to_box(s.b, b));
    const Point corners[4] = {
        b.min,
        Point{b.max.x, b.min.y},
        b.max,
        Point{b.min.x, b.max.y},
    };
    for (const auto& c : corners) {
        best = std::min(best, geometry::point_to_segment(c, s));
    }
    return best;
}

// 从 pcb::Track 的 Path 拆出段列表。
std::vector<Segment> path_segments(const geometry::Path& path) {
    std::vector<Segment> segs;
    segs.reserve(path.points.empty() ? 0 : path.points.size() - 1);
    for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
        segs.push_back(Segment{path.points[i], path.points[i + 1]});
    }
    return segs;
}

// 两 Path 中心线最小距离（无段返回 kMaxCoord）。
Coord path_path_distance(const geometry::Path& a, const geometry::Path& b) {
    const auto sa = path_segments(a);
    const auto sb = path_segments(b);
    if (sa.empty() || sb.empty()) return kMaxCoord;
    Coord best = kMaxCoord;
    for (const auto& s1 : sa) {
        for (const auto& s2 : sb) {
            const Coord d = segment_segment_distance(s1, s2);
            if (d < best) best = d;
        }
    }
    return best;
}

// 两 Path 中心线是否相交（用于短路判定）。
bool path_path_intersect(const geometry::Path& a, const geometry::Path& b) {
    const auto sa = path_segments(a);
    const auto sb = path_segments(b);
    for (const auto& s1 : sa) {
        for (const auto& s2 : sb) {
            if (geometry::segment_segment_intersect(s1, s2).has_value()) return true;
        }
    }
    return false;
}

// 两 Net 是否视为"同一节点"（都非空且相等）。任一为空（未分配）→ 视为不同。
bool same_net(const std::string& a, const std::string& b) {
    return !a.empty() && a == b;
}

// 两 Net 是否视为"明确不同"（都非空且不等）。
bool different_net(const std::string& a, const std::string& b) {
    return !a.empty() && !b.empty() && a != b;
}

// ============================================================================
// 下转：ctx.board → const pcb::Board*
// ============================================================================
const pcb::Board* board_from_context(const ValidationContext& ctx) {
    if (ctx.board == nullptr) return nullptr;
    if (!ctx.board->is_class("Board")) return nullptr;  // 非 Board，安全跳过
    return static_cast<const pcb::Board*>(ctx.board);
}

// ============================================================================
// Primitive —— 把 Board 上的 Track / Pad 投影成统一描述，便于 R-tree 配对。
// ============================================================================
struct Primitive {
    enum class Kind { Track, Pad };
    Kind kind;
    std::size_t index;       // board.tracks()[index] 或 board.pads()[index]
    Box box;                 // 自身包围盒（未经 clearance 外扩）
    std::string layer_id;
    std::string net_uuid;
};

// 取 primitive 的中心几何（Track: 第一段中点；Pad: position）——用于违例 location。
Point primitive_anchor(const Primitive& p, const pcb::Board& board) {
    if (p.kind == Primitive::Kind::Track) {
        const auto& track = board.tracks()[p.index];
        const auto& pts = track->path().points;
        if (pts.size() >= 2) {
            return Point{(pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2};
        }
        if (!pts.empty()) return pts[0];
        return track->bounding_box().min;
    }
    return board.pads()[p.index]->position();
}

// 把 Board 的 Track + Pad 投影成 Primitive 列表。
std::vector<Primitive> collect_primitives(const pcb::Board& board) {
    std::vector<Primitive> out;
    out.reserve(board.track_count() + board.pad_count());
    for (std::size_t i = 0; i < board.track_count(); ++i) {
        const auto& t = board.tracks()[i];
        out.push_back(Primitive{Primitive::Kind::Track, i, t->bounding_box(),
                                t->layer_id(), t->net_uuid()});
    }
    for (std::size_t i = 0; i < board.pad_count(); ++i) {
        const auto& p = board.pads()[i];
        out.push_back(Primitive{Primitive::Kind::Pad, i, p->bounding_box(),
                                p->layer_id(), p->net_uuid()});
    }
    return out;
}

// ============================================================================
// 精确铜距：primitive i → primitive j（同层前提，已由调用方保证）。
// 返回 <铜到铜距离, 违例代表点>；不可比时返回 <kMaxCoord, {}>。
// ============================================================================
struct ClearanceProbe {
    Coord copper_distance = kMaxCoord;   // 铜→铜（已扣除双方自宽度）
    Point representative{0, 0};           // 违例代表点（中点）
};

ClearanceProbe probe_clearance(const pcb::Board& board,
                               const Primitive& a,
                               const Primitive& b) {
    ClearanceProbe probe;
    const Point ca = primitive_anchor(a, board);
    const Point cb = primitive_anchor(b, board);
    probe.representative = Point{(ca.x + cb.x) / 2, (ca.y + cb.y) / 2};

    auto from_track_pad = [&](const Primitive& tk, const Primitive& pd) {
        const auto& track = board.tracks()[tk.index];
        const auto& pad = board.pads()[pd.index];
        const auto segs = path_segments(track->path());
        if (segs.empty()) {
            probe.copper_distance = kMaxCoord;
            return;
        }
        Coord best = kMaxCoord;
        for (const auto& s : segs) {
            best = std::min(best, segment_to_box(s, pad->bounding_box()));
        }
        // Track 有宽度：扣除半宽（Pad box 已是完整铜区，不再扣 pad 半径）
        const Coord half_w = track->width() / 2;
        probe.copper_distance = best - half_w;
    };

    if (a.kind == Primitive::Kind::Track && b.kind == Primitive::Kind::Track) {
        const auto& ta = board.tracks()[a.index];
        const auto& tb = board.tracks()[b.index];
        const Coord center_dist = path_path_distance(ta->path(), tb->path());
        probe.copper_distance = center_dist - ta->width() / 2 - tb->width() / 2;
    } else if (a.kind == Primitive::Kind::Pad && b.kind == Primitive::Kind::Pad) {
        const auto& pa = board.pads()[a.index];
        const auto& pb = board.pads()[b.index];
        probe.copper_distance = box_to_box(pa->bounding_box(), pb->bounding_box());
    } else {
        // Track-Pad（两个方向归一）
        if (a.kind == Primitive::Kind::Track) from_track_pad(a, b);
        else from_track_pad(b, a);
    }
    return probe;
}

}  // namespace

// ============================================================================
// ClearanceRule
// ============================================================================

std::vector<ValidationResult> ClearanceRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    const pcb::Board* board = board_from_context(ctx);
    if (board == nullptr) return results;

    const auto prims = collect_primitives(*board);
    if (prims.size() < 2) return results;

    // R-tree 邻域索引：装自身 AABB（不含 clearance 外扩）。
    geometry::RTree<int> rtree;
    std::vector<std::pair<Box, int>> entries;
    entries.reserve(prims.size());
    for (std::size_t i = 0; i < prims.size(); ++i) {
        entries.emplace_back(prims[i].box, static_cast<int>(i));
    }
    rtree.build(entries);

    const Coord min_clr = options_.min_clearance_nm;

    for (std::size_t i = 0; i < prims.size(); ++i) {
        const auto& pi = prims[i];
        // 用外扩 min_clearance 的盒查邻居（AABB 邻域预筛选）。
        const Box query = expand_box(pi.box, min_clr);
        const auto candidates = rtree.query(query);
        for (int j_raw : candidates) {
            const std::size_t j = static_cast<std::size_t>(j_raw);
            if (j <= i) continue;  // 每对只查一次
            const auto& pj = prims[j];

            // 同层才需间距（跨层由叠层专项规则负责）。
            if (pi.layer_id != pj.layer_id) continue;
            // 同 Net 不查（同节点无间距要求）。
            if (same_net(pi.net_uuid, pj.net_uuid)) continue;

            const auto probe = probe_clearance(*board, pi, pj);
            if (probe.copper_distance < min_clr) {
                ValidationResult r;
                r.severity = Severity::ERROR;
                r.category = "DRC";
                r.fixable = false;  // 需用户决定移动哪条
                r.location = probe.representative;
                r.entity_uuid = pj.kind == Primitive::Kind::Track
                                    ? board->tracks()[pj.index]->uuid()
                                    : board->pads()[pj.index]->uuid();

                const char* kind_str =
                    (pi.kind == Primitive::Kind::Track && pj.kind == Primitive::Kind::Track)
                        ? "Track-track"
                    : (pi.kind == Primitive::Kind::Pad && pj.kind == Primitive::Kind::Pad)
                        ? "Pad-pad"
                        : "Track-pad";
                r.message = std::string(kind_str) + " clearance " +
                            std::to_string(probe.copper_distance) + " nm < min " +
                            std::to_string(min_clr) + " nm";
                results.push_back(std::move(r));
            }
        }
    }
    return results;
}

// ============================================================================
// MinWidthRule
// ============================================================================

std::vector<ValidationResult> MinWidthRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    const pcb::Board* board = board_from_context(ctx);
    if (board == nullptr) return results;

    const Coord min_w = options_.min_width_nm;
    results.reserve(board->track_count());

    for (const auto& track : board->tracks()) {
        const Coord w = track->width();
        if (w < min_w) {
            ValidationResult r;
            r.severity = Severity::ERROR;
            r.category = "DRC";
            r.fixable = true;  // 可加宽
            r.entity_uuid = track->uuid();
            // location: 第一段中点（无段时退化到 bbox.min）
            const auto& pts = track->path().points;
            if (pts.size() >= 2) {
                r.location = Point{(pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2};
            } else if (!pts.empty()) {
                r.location = pts[0];
            } else {
                r.location = track->bounding_box().min;
            }
            r.message = "Track width " + std::to_string(w) + " nm < min " +
                        std::to_string(min_w) + " nm";
            results.push_back(std::move(r));
        }
    }
    return results;
}

// ============================================================================
// ShortCircuitRule
// ============================================================================

std::vector<ValidationResult> ShortCircuitRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    const pcb::Board* board = board_from_context(ctx);
    if (board == nullptr) return results;

    const auto prims = collect_primitives(*board);

    // O(N²) 配对（短路是稀疏事件；骨架版不引入额外索引）。
    for (std::size_t i = 0; i < prims.size(); ++i) {
        for (std::size_t j = i + 1; j < prims.size(); ++j) {
            const auto& pi = prims[i];
            const auto& pj = prims[j];
            if (pi.layer_id != pj.layer_id) continue;           // 跨层不算短
            if (!different_net(pi.net_uuid, pj.net_uuid)) continue;  // 需明确异 Net

            bool shorted = false;
            Point where{0, 0};

            if (pi.kind == Primitive::Kind::Track && pj.kind == Primitive::Kind::Track) {
                const auto& ta = board->tracks()[pi.index];
                const auto& tb = board->tracks()[pj.index];
                shorted = path_path_intersect(ta->path(), tb->path());
                if (shorted) {
                    // 用两中心线第一段中点的平均作代表点
                    const auto& pa = ta->path().points;
                    const auto& pb = tb->path().points;
                    Point ma = pa.empty() ? ta->bounding_box().min
                                          : Point{(pa[0].x + pa[1 % pa.size()].x) / 2,
                                                  (pa[0].y + pa[1 % pa.size()].y) / 2};
                    Point mb = pb.empty() ? tb->bounding_box().min
                                          : Point{(pb[0].x + pb[1 % pb.size()].x) / 2,
                                                  (pb[0].y + pb[1 % pb.size()].y) / 2};
                    where = Point{(ma.x + mb.x) / 2, (ma.y + mb.y) / 2};
                }
            } else if (pi.kind == Primitive::Kind::Pad && pj.kind == Primitive::Kind::Pad) {
                const auto& pa = board->pads()[pi.index];
                const auto& pb = board->pads()[pj.index];
                shorted = pa->bounding_box().intersects(pb->bounding_box());
                if (shorted) {
                    const Point ca = pa->position();
                    const Point cb = pb->position();
                    where = Point{(ca.x + cb.x) / 2, (ca.y + cb.y) / 2};
                }
            } else {
                // Track-Pad：Track 任一段穿入 Pad-Box → 短路
                const Primitive* tk_p;
                const Primitive* pd_p;
                if (pi.kind == Primitive::Kind::Track) {
                    tk_p = &pi;
                    pd_p = &pj;
                } else {
                    tk_p = &pj;
                    pd_p = &pi;
                }
                const auto& track = board->tracks()[tk_p->index];
                const auto& pad = board->pads()[pd_p->index];
                const auto segs = path_segments(track->path());
                for (const auto& s : segs) {
                    if (segment_to_box(s, pad->bounding_box()) == 0) {
                        shorted = true;
                        where = Point{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2};
                        break;
                    }
                }
            }

            if (!shorted) continue;

            ValidationResult r;
            r.severity = Severity::ERROR;
            r.category = "DRC";
            r.fixable = false;  // 需用户判断电气意图
            r.location = where;
            r.entity_uuid = pj.kind == Primitive::Kind::Track
                                ? board->tracks()[pj.index]->uuid()
                                : board->pads()[pj.index]->uuid();
            r.message = "Short circuit: different-net primitives overlap";
            results.push_back(std::move(r));
        }
    }
    return results;
}

// ============================================================================
// UnroutedNetRule
// ============================================================================

std::vector<ValidationResult> UnroutedNetRule::check(const ValidationContext& ctx) const {
    std::vector<ValidationResult> results;
    const pcb::Board* board = board_from_context(ctx);
    if (board == nullptr) return results;

    for (const auto& net : board->nets()) {
        std::size_t pad_via = 0;
        std::size_t tracks = 0;
        Point first_pin{0, 0};
        bool has_pin_location = false;

        // Board::find_track/find_pad/find_via 仅有非 const 重载，故走 find_by_uuid
        //（const 重载返回 const PcbEntity*），再用 is_class 判成员类型。
        for (const auto& uuid : net->member_uuids()) {
            const pcb::PcbEntity* entity = board->find_by_uuid(uuid);
            if (entity == nullptr) continue;
            if (entity->is_class("Track")) {
                ++tracks;
            } else if (entity->is_class("Pad")) {
                ++pad_via;
                if (!has_pin_location) {
                    first_pin = static_cast<const pcb::Pad*>(entity)->position();
                    has_pin_location = true;
                }
            } else if (entity->is_class("Via")) {
                ++pad_via;
                if (!has_pin_location) {
                    first_pin = static_cast<const pcb::Via*>(entity)->position();
                    has_pin_location = true;
                }
            }
        }

        if (pad_via > 0 && tracks == 0) {
            ValidationResult r;
            r.severity = Severity::WARNING;
            r.category = "DRC";
            r.fixable = true;  // 可走 auto-router / 手工连线
            r.entity_uuid = net->uuid();
            if (has_pin_location) r.location = first_pin;
            r.message = "Net '" + net->name() + "' has " +
                        std::to_string(pad_via) + " pin(s) but no track routing";
            results.push_back(std::move(r));
        }
    }
    return results;
}

}  // namespace eda::validation
