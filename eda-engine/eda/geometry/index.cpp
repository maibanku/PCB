// eda/geometry/index.cpp
//
// P3 几何内核 · Task 6 实现：自研 R-tree（STR bulk-load + 范围/kNN 查询 + 增量增删）。
//
// 算法参考：
//   - STR（Sort-Tile-Recursive）bulk-load：Leutenegger, Lopez, Edgington 1997。
//     每层先按中心 x 切成 ceil(sqrt(P)) 个垂直切片，切片内按中心 y 排序后按 M 打包。
//   - Guttman 1984 R-tree：
//       ChooseSubtree  沿 enlargement 最小路径下钻到叶。
//       QuadraticSplit PickSeeds（最大 dead area）+ PickNext（最大 |Δenl|）平分溢出节点。
//       CondenseTree   删除后下溢节点摘除，孤叶子条目重插。
//   - kNN：best-first 最小堆，按 Box 到 point 的最小平方距离（避免 sqrt）排序。
//
// 决策对齐 D5：自研，不依赖 Boost.Geometry。
// 坐标基础 D1：内部 int64_t nm，面积/距离用 long double 规避溢出。

#include "eda/geometry/index.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace eda::geometry::detail {

// ===================== Node 定义（pImpl 完成）=====================

struct RTreeCore::Node {
    Box mbr{};
    bool is_leaf = true;
    std::vector<std::pair<Box, int64_t>> entries;   // 叶子层：box + payload_id
    std::vector<std::unique_ptr<Node>> children;    // 内部层：孩子节点

    // 重算本节点 MBR（插入/分裂/删除后调用）。
    void update_mbr() {
        if (is_leaf) {
            if (entries.empty()) { mbr = Box{}; return; }
            mbr = entries.front().first;
            for (const auto& e : entries) mbr = mbr.merge(e.first);
        } else {
            if (children.empty()) { mbr = Box{}; return; }
            mbr = children.front()->mbr;
            for (const auto& c : children) mbr = mbr.merge(c->mbr);
        }
    }

    // 当前条目/孩子数（用于溢出 / 下溢判定）。
    std::size_t count() const {
        return is_leaf ? entries.size() : children.size();
    }
};

namespace {

// 默认 fanout（plan「几何与命令栈实现计划.md」约定 16）。
constexpr std::size_t kDefaultNodeCapacity = 16;

// 节点容量下限：低于 2 时 QuadraticSplit 无法分成两组。
constexpr std::size_t kMinNodeCapacity = 2;

// Box 中心坐标（STR 切片排序键）。
Coord center_x(const Box& b) { return (b.min.x + b.max.x) / 2; }
Coord center_y(const Box& b) { return (b.min.y + b.max.y) / 2; }

// Box 面积（long double 规避 int64 溢出）。
// 注意：MSVC 上 long double == double（53 位尾数），超 ~9×10^15 nm² 会掉精度。
// 本实现只用面积做相对比较（ChooseSubtree / Split 的 enlargement 启发式），
// 不影响查询正确性；生产级若需更高精度可换 __int128。
long double area(const Box& b) {
    const long double w = static_cast<long double>(b.max.x) -
                          static_cast<long double>(b.min.x);
    const long double h = static_cast<long double>(b.max.y) -
                          static_cast<long double>(b.min.y);
    return w * h;
}

// 点到 Box 的最小平方距离（kNN 排序键）。点在 Box 内（含边界）返回 0。
long double min_sq_dist(const Box& b, Point p) {
    long double dx = 0.0L, dy = 0.0L;
    if (p.x < b.min.x) dx = static_cast<long double>(b.min.x - p.x);
    else if (p.x > b.max.x) dx = static_cast<long double>(p.x - b.max.x);
    if (p.y < b.min.y) dy = static_cast<long double>(b.min.y - p.y);
    else if (p.y > b.max.y) dy = static_cast<long double>(p.y - b.max.y);
    return dx * dx + dy * dy;
}

// 范围查询递归体：与 box 不相交的子树直接剪枝。
void query_recursive(const RTreeCore::Node& node, Box box,
                     std::vector<int64_t>& out) {
    if (!node.mbr.intersects(box)) return;
    if (node.is_leaf) {
        for (const auto& e : node.entries) {
            if (e.first.intersects(box)) out.push_back(e.second);
        }
    } else {
        for (const auto& child : node.children) {
            query_recursive(*child, box, out);
        }
    }
}

// 点包含递归体：左闭右开语义（与 Box::contains 一致）。
// 下行判定 child.mbr.contains(p)：若为 false，则子树内无任何 entry 包含 p
// （entry.box ⊆ child.mbr，右界相等时同被排除），剪枝安全。
void contains_query_recursive(const RTreeCore::Node& node, Point p,
                              std::vector<int64_t>& out) {
    if (!node.mbr.contains(p)) return;
    if (node.is_leaf) {
        for (const auto& e : node.entries) {
            if (e.first.contains(p)) out.push_back(e.second);
        }
    } else {
        for (const auto& child : node.children) {
            contains_query_recursive(*child, p, out);
        }
    }
}

// ChooseSubtree：从 root 起逐层选 enlargement 最小（次选 area 最小）的孩子，
// 直至抵达叶子。path 记录路径（含 root 与 leaf），供上行分裂检查使用。
RTreeCore::Node* choose_subtree(RTreeCore::Node& root, Box box,
                                std::vector<RTreeCore::Node*>& path) {
    RTreeCore::Node* cur = &root;
    path.push_back(cur);
    while (!cur->is_leaf) {
        RTreeCore::Node* best = cur->children.front().get();
        long double best_area = area(best->mbr);
        long double best_enl = area(best->mbr.merge(box)) - best_area;
        for (std::size_t i = 1; i < cur->children.size(); ++i) {
            RTreeCore::Node* cand = cur->children[i].get();
            const long double cand_area = area(cand->mbr);
            const long double cand_enl = area(cand->mbr.merge(box)) - cand_area;
            if (cand_enl < best_enl ||
                (cand_enl == best_enl && cand_area < best_area)) {
                best = cand;
                best_enl = cand_enl;
                best_area = cand_area;
            }
        }
        cur = best;
        path.push_back(cur);
    }
    return cur;
}

// QuadraticSplit（Guttman 1984）：将 items 平分为两组。
// 原 vector 收缩为 a 组；返回 b 组（用于新节点）。
// GetBox 从 Item 取 Box 引用（叶子 .first / 内部 ->mbr）。
template <typename Item, typename GetBox>
std::vector<Item> quadratic_split(std::vector<Item>& items, GetBox get_box,
                                  std::size_t capacity) {
    const std::size_t n = items.size();
    // 每组下限 ceil(capacity/2)，保证分裂后两组均不空。
    const std::size_t min_alloc = (capacity + 1) / 2;

    // 1) PickSeeds：合并后 dead area 最大的两项作为两组种子。
    std::size_t seed_a = 0, seed_b = 1;
    long double worst_d = -1.0L;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const Box merged = get_box(items[i]).merge(get_box(items[j]));
            const long double d = area(merged) -
                                  area(get_box(items[i])) -
                                  area(get_box(items[j]));
            if (d > worst_d) { worst_d = d; seed_a = i; seed_b = j; }
        }
    }

    std::vector<char> assigned(n, 0);
    assigned[seed_a] = 1;
    assigned[seed_b] = 1;
    std::vector<std::size_t> idx_a, idx_b;
    idx_a.push_back(seed_a);
    idx_b.push_back(seed_b);
    Box mbr_a = get_box(items[seed_a]);
    Box mbr_b = get_box(items[seed_b]);
    std::size_t remaining = (n >= 2) ? n - 2 : 0;

    while (remaining > 0) {
        // 若某组不吃光剩余就无法达下限 → 一次性灌入，避免破坏最小填充约束。
        if (idx_a.size() + remaining <= min_alloc) {
            for (std::size_t i = 0; i < n; ++i) {
                if (!assigned[i]) { idx_a.push_back(i); assigned[i] = 1; --remaining; }
            }
            break;
        }
        if (idx_b.size() + remaining <= min_alloc) {
            for (std::size_t i = 0; i < n; ++i) {
                if (!assigned[i]) { idx_b.push_back(i); assigned[i] = 1; --remaining; }
            }
            break;
        }

        // 2) PickNext：选 |enl_a - enl_b| 最大的项，分入 enlargement 较小组。
        long double best_diff = -1.0L;
        std::size_t best_idx = n;
        long double best_enl_a = 0.0L, best_enl_b = 0.0L;
        for (std::size_t i = 0; i < n; ++i) {
            if (assigned[i]) continue;
            const Box ib = get_box(items[i]);
            const long double enl_a = area(mbr_a.merge(ib)) - area(mbr_a);
            const long double enl_b = area(mbr_b.merge(ib)) - area(mbr_b);
            const long double diff = std::fabs(enl_a - enl_b);
            if (diff > best_diff) {
                best_diff = diff;
                best_idx = i;
                best_enl_a = enl_a;
                best_enl_b = enl_b;
            }
        }
        if (best_idx == n) break;  // 安全兜底（不应触发）

        const bool to_a = (best_enl_a < best_enl_b) ||
                          (best_enl_a == best_enl_b && area(mbr_a) < area(mbr_b)) ||
                          (best_enl_a == best_enl_b && idx_a.size() < idx_b.size());
        if (to_a) {
            idx_a.push_back(best_idx);
            mbr_a = mbr_a.merge(get_box(items[best_idx]));
        } else {
            idx_b.push_back(best_idx);
            mbr_b = mbr_b.merge(get_box(items[best_idx]));
        }
        assigned[best_idx] = 1;
        --remaining;
    }

    // 按索引搬运到两组新 vector（避免 move 后再通过 stale 索引访问）。
    std::vector<Item> a, b;
    a.reserve(idx_a.size());
    b.reserve(idx_b.size());
    for (auto i : idx_a) a.push_back(std::move(items[i]));
    for (auto i : idx_b) b.push_back(std::move(items[i]));
    items = std::move(a);
    return b;
}

// 拆分一个溢出节点：返回新建的兄弟节点（同 is_leaf）。
std::unique_ptr<RTreeCore::Node>
split_overflow(RTreeCore::Node& node, std::size_t capacity) {
    auto sibling = std::make_unique<RTreeCore::Node>();
    sibling->is_leaf = node.is_leaf;
    if (node.is_leaf) {
        auto b = quadratic_split(
            node.entries,
            [](const std::pair<Box, int64_t>& e) -> const Box& { return e.first; },
            capacity);
        sibling->entries = std::move(b);
    } else {
        auto b = quadratic_split(
            node.children,
            [](const std::unique_ptr<RTreeCore::Node>& c) -> const Box& {
                return c->mbr;
            },
            capacity);
        sibling->children = std::move(b);
    }
    node.update_mbr();
    sibling->update_mbr();
    return sibling;
}

// FindLeaf：DFS 定位含 (box, payload_id) 的叶子，path 累积成功路径。
// 失败时 path 回滚到入口状态。
RTreeCore::Node* find_leaf(RTreeCore::Node& node, Box box, int64_t payload_id,
                           std::vector<RTreeCore::Node*>& path) {
    path.push_back(&node);
    if (node.is_leaf) {
        for (const auto& e : node.entries) {
            if (e.second == payload_id &&
                e.first.min.x == box.min.x && e.first.min.y == box.min.y &&
                e.first.max.x == box.max.x && e.first.max.y == box.max.y) {
                return &node;
            }
        }
        path.pop_back();
        return nullptr;
    }
    for (const auto& child : node.children) {
        if (!child->mbr.intersects(box)) continue;
        if (RTreeCore::Node* found = find_leaf(*child, box, payload_id, path)) {
            return found;
        }
    }
    path.pop_back();
    return nullptr;
}

// 把子树所有叶子条目平铺到 out（Condense 后用于重插）。
void flatten_entries(const RTreeCore::Node& node,
                     std::vector<std::pair<Box, int64_t>>& out) {
    if (node.is_leaf) {
        for (const auto& e : node.entries) out.push_back(e);
    } else {
        for (const auto& child : node.children) {
            flatten_entries(*child, out);
        }
    }
}

}  // namespace

// ===================== 构造 / 析构 / 移动 =====================

RTreeCore::RTreeCore()
    : root_(std::make_unique<Node>()),
      node_capacity_(kDefaultNodeCapacity),
      size_(0) {}

RTreeCore::RTreeCore(std::size_t node_capacity)
    : root_(std::make_unique<Node>()),
      node_capacity_(std::max(node_capacity, kMinNodeCapacity)),
      size_(0) {}

RTreeCore::~RTreeCore() = default;
RTreeCore::RTreeCore(RTreeCore&&) noexcept = default;
RTreeCore& RTreeCore::operator=(RTreeCore&&) noexcept = default;

std::size_t RTreeCore::size() const noexcept { return size_; }

void RTreeCore::clear() {
    root_ = std::make_unique<Node>();
    size_ = 0;
}

// ===================== STR bulk-load =====================

void RTreeCore::build(std::span<const std::pair<Box, int64_t>> entries) {
    // 重置为空树叶根。
    root_ = std::make_unique<Node>();
    root_->is_leaf = true;
    size_ = entries.size();
    if (entries.empty()) return;

    const std::size_t N = entries.size();
    const std::size_t M = node_capacity_;

    // ---- 叶层 STR 划分 ----
    std::vector<std::pair<Box, int64_t>> sorted(entries.begin(), entries.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return center_x(a.first) < center_x(b.first);
              });

    const std::size_t P = (N + M - 1) / M;                       // 期望叶节点数
    const std::size_t S = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<long double>(P))));      // 垂直切片数
    const std::size_t slice_target = S * M;                      // 每切片条目数上限

    std::vector<std::unique_ptr<Node>> level;
    level.reserve(P);

    std::size_t i = 0;
    while (i < N) {
        const std::size_t this_slice = std::min(slice_target, N - i);
        const auto sb = sorted.begin() + static_cast<std::ptrdiff_t>(i);
        const auto se =
            sorted.begin() + static_cast<std::ptrdiff_t>(i + this_slice);
        // 切片内按中心 y 排序，再按 M 打包成叶节点。
        std::sort(sb, se, [](const auto& a, const auto& b) {
            return center_y(a.first) < center_y(b.first);
        });
        for (std::size_t j = 0; j < this_slice; j += M) {
            auto leaf = std::make_unique<Node>();
            leaf->is_leaf = true;
            const std::size_t take = std::min(M, this_slice - j);
            leaf->entries.reserve(take);
            for (std::size_t k = 0; k < take; ++k) {
                leaf->entries.push_back(*(sb + static_cast<std::ptrdiff_t>(j + k)));
            }
            leaf->update_mbr();
            level.push_back(std::move(leaf));
        }
        i += this_slice;
    }

    // ---- 自底向上构建内部层（每层复用同样的 STR 划分）----
    while (level.size() > 1) {
        const std::size_t n = level.size();
        const std::size_t parent_count = (n + M - 1) / M;
        const std::size_t sp = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<long double>(parent_count))));
        const std::size_t slice_target_inner = sp * M;

        std::sort(level.begin(), level.end(),
                  [](const auto& a, const auto& b) {
                      return center_x(a->mbr) < center_x(b->mbr);
                  });

        std::vector<std::unique_ptr<Node>> next_level;
        next_level.reserve(parent_count);

        std::size_t p = 0;
        while (p < n) {
            const std::size_t this_slice = std::min(slice_target_inner, n - p);
            const auto sb = level.begin() + static_cast<std::ptrdiff_t>(p);
            const auto se =
                level.begin() + static_cast<std::ptrdiff_t>(p + this_slice);
            std::sort(sb, se, [](const auto& a, const auto& b) {
                return center_y(a->mbr) < center_y(b->mbr);
            });
            for (std::size_t j = 0; j < this_slice; j += M) {
                auto parent = std::make_unique<Node>();
                parent->is_leaf = false;
                const std::size_t take = std::min(M, this_slice - j);
                parent->children.reserve(take);
                for (std::size_t k = 0; k < take; ++k) {
                    parent->children.push_back(
                        std::move(*(sb + static_cast<std::ptrdiff_t>(j + k))));
                }
                parent->update_mbr();
                next_level.push_back(std::move(parent));
            }
            p += this_slice;
        }

        level = std::move(next_level);
    }

    root_ = std::move(level.front());
    root_->update_mbr();
}

// ===================== 增量插入（ChooseSubtree + QuadraticSplit）=====================

void RTreeCore::insert(Box box, int64_t payload_id) {
    if (size_ == 0) {
        // 空树：根直接作为叶子，放入唯一条目。
        root_->is_leaf = true;
        root_->entries.push_back({box, payload_id});
        root_->mbr = box;
        size_ = 1;
        return;
    }
    insert_entry(box, payload_id);
    ++size_;
}

void RTreeCore::insert_entry(Box box, int64_t payload_id) {
    std::vector<Node*> path;
    Node* leaf = choose_subtree(*root_, box, path);
    leaf->entries.push_back({box, payload_id});

    // 自底向上：更新 mbr、必要时分裂并把兄弟挂到父节点。
    for (std::size_t i = path.size(); i-- > 0; ) {
        Node* node = path[i];
        node->update_mbr();
        if (node->count() <= node_capacity_) continue;

        auto sibling = split_overflow(*node, node_capacity_);

        if (i == 0) {
            // 根节点分裂 → 新根，旧根与兄弟作为两个孩子。
            auto new_root = std::make_unique<Node>();
            new_root->is_leaf = false;
            new_root->children.reserve(2);
            new_root->children.push_back(std::move(root_));
            new_root->children.push_back(std::move(sibling));
            new_root->update_mbr();
            root_ = std::move(new_root);
            return;
        }
        // 非根节点分裂：兄弟挂到父节点；下一轮 i-1 检查父节点是否溢出。
        path[i - 1]->children.push_back(std::move(sibling));
    }
}

// ===================== 删除（FindLeaf + CondenseTree）=====================

bool RTreeCore::remove(Box box, int64_t payload_id) {
    if (size_ == 0) return false;

    std::vector<Node*> path;
    Node* leaf = find_leaf(*root_, box, payload_id, path);
    if (!leaf) return false;

    const auto it = std::find_if(
        leaf->entries.begin(), leaf->entries.end(),
        [&](const std::pair<Box, int64_t>& e) {
            return e.second == payload_id &&
                   e.first.min.x == box.min.x && e.first.min.y == box.min.y &&
                   e.first.max.x == box.max.x && e.first.max.y == box.max.y;
        });
    if (it == leaf->entries.end()) return false;
    leaf->entries.erase(it);
    --size_;

    // CondenseTree：自底向上摘除下溢节点（根除外），收集孤叶子条目重插。
    // 简化策略：把下溢子树整棵平铺为叶子条目再重插；丢失部分树质量但保证正确性。
    const std::size_t min_entries = node_capacity_ / 2;
    std::vector<std::pair<Box, int64_t>> orphans;

    for (std::size_t i = path.size(); i-- > 0; ) {
        Node* node = path[i];
        if (i == 0) {
            // 根不淘汰；循环末尾单独处理"根单孩子提升"。
            node->update_mbr();
            break;
        }
        if (node->count() < min_entries) {
            flatten_entries(*node, orphans);
            auto& parent_children = path[i - 1]->children;
            parent_children.erase(
                std::remove_if(parent_children.begin(), parent_children.end(),
                               [&](const std::unique_ptr<Node>& c) {
                                   return c.get() == node;
                               }),
                parent_children.end());
        } else {
            node->update_mbr();
        }
    }

    // 根为内部节点且只剩一个孩子 → 提升该孩子为新根（降低树高）。
    while (!root_->is_leaf && root_->children.size() == 1) {
        root_ = std::move(root_->children.front());
    }
    root_->update_mbr();

    if (size_ == 0) {
        root_ = std::make_unique<Node>();
    } else {
        // 重插孤儿条目（不改 size_：它们原本就在树中，被摘除时未减 size_）。
        for (const auto& [b, pid] : orphans) {
            insert_entry(b, pid);
        }
    }

    return true;
}

// ===================== 范围查询 / kNN / 点包含 =====================

void RTreeCore::query(Box box, std::vector<int64_t>& out) const {
    out.clear();
    if (size_ == 0) return;
    query_recursive(*root_, box, out);
}

void RTreeCore::contains_query(Point point, std::vector<int64_t>& out) const {
    out.clear();
    if (size_ == 0) return;
    contains_query_recursive(*root_, point, out);
}

void RTreeCore::nearest(Point point, std::size_t k,
                        std::vector<int64_t>& out) const {
    out.clear();
    if (size_ == 0 || k == 0) return;

    // best-first 最小堆：节点与叶子条目共用，按 Box 到 point 的最小平方距离排序。
    struct HeapItem {
        long double dist = 0.0L;
        bool is_node = true;
        const Node* node = nullptr;
        Box box{};
        int64_t payload = 0;
    };
    struct Greater {
        bool operator()(const HeapItem& a, const HeapItem& b) const {
            return a.dist > b.dist;
        }
    };

    std::priority_queue<HeapItem, std::vector<HeapItem>, Greater> heap;
    heap.push({min_sq_dist(root_->mbr, point), true, root_.get(), Box{}, 0});

    while (!heap.empty() && out.size() < k) {
        const HeapItem top = heap.top();
        heap.pop();
        if (top.is_node) {
            if (top.node->is_leaf) {
                for (const auto& e : top.node->entries) {
                    heap.push({min_sq_dist(e.first, point), false, nullptr,
                               e.first, e.second});
                }
            } else {
                for (const auto& child : top.node->children) {
                    heap.push({min_sq_dist(child->mbr, point), true,
                               child.get(), Box{}, 0});
                }
            }
        } else {
            out.push_back(top.payload);
        }
    }
}

}  // namespace eda::geometry::detail
