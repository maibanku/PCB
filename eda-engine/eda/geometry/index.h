#pragma once

// eda/geometry/index.h
//
// P3 几何内核 · Task 6：自研 R-tree（空间索引）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 6 / 子功能 README「05-空间索引」。
//
// 设计目标（D5：自研，**不引 Boost**）：
//   - STR（Sort-Tile-Recursive）bulk-load 批量建树，叶子层与内部层均按中心坐标
//     先 x 后 y 切片打包，得到重叠度低、查询性能稳定的结构。
//   - 范围查询 query(Box)：返回所有与查询框相交的叶子条目（拾取 / 视图剔除 / DRC 邻域预筛选）。
//   - kNN 查询 nearest(Point, k)：best-first 最小堆，按 Box 到 point 的最小平方距离排序。
//   - 增量 insert / remove：Guttman 1984 ChooseSubtree + QuadraticSplit + CondenseTree。
//   - 点包含 contains(Point)：左闭右开语义，与 Box::contains 一致。
//
// 接口契约：
//   - 叶子只存 Box + T，**不持有图元本体**（T 通常是图元 id：uint32_t / int64_t）。
//   - 模板参数 T 由业务层指定；核心算法（RTreeCore）非模板、type-erased，
//     payload 以 int64_t handle 表示（= 外层 RTree<T>::values_ 的索引）。
//     好处：算法在 index.cpp 中编译一次，避免模板膨胀。
//
// 复杂度（N 条目、节点容量 M，默认 16）：
//   - STR bulk-load ：O(N log N)（每层一次排序 × 树高 log_M N）。
//   - query(Box)    ：期望 O(log_M N + k)，k 为命中数；最坏 O(N)（极端重叠）。
//   - nearest(p, k) ：O((log_M N + k) · log(log_M N + k))，堆操作主导。
//   - insert        ：O(log_M N · M²)（分裂启发式 PickSeeds / PickNext 主导）。
//   - remove        ：O(log_M N · M + 孤儿重插)。
//
// 坐标基础（D1）：Box 顶点为 int64_t nm，永不参与浮点运算。面积与距离比较用
// long double 规避 int64 溢出（板级坐标面积可达 10^16~10^18 nm²）。

#include "eda/geometry/types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace eda::geometry {

namespace detail {

// 非模板核心：存储 (Box, int64_t payload_id) 并执行 STR bulk-load + 查询 + 增删。
// payload_id 是不透明 handle，由外层 RTree<T> 映射到具体载荷 T。
// pImpl：Node 为不完整嵌套类型，定义在 index.cpp。
class RTreeCore {
public:
    RTreeCore();
    explicit RTreeCore(std::size_t node_capacity);
    ~RTreeCore();
    RTreeCore(const RTreeCore&) = delete;
    RTreeCore& operator=(const RTreeCore&) = delete;
    RTreeCore(RTreeCore&&) noexcept;
    RTreeCore& operator=(RTreeCore&&) noexcept;

    // STR bulk-load：清空当前状态后批量建树。entries 必须有唯一 payload_id（由调用方保证）。
    void build(std::span<const std::pair<Box, int64_t>> entries);

    // 增量插入：ChooseSubtree 选路 + QuadraticSplit 处理溢出。
    void insert(Box box, int64_t payload_id);

    // 删除首个 (box, payload_id) 匹配的叶子条目；CondenseTree 收集孤儿重插。
    // 命中返回 true；未找到返回 false（不改 size_）。
    bool remove(Box box, int64_t payload_id);

    // 范围查询：返回所有 box 与查询框相交的 payload_id（顺序未指定）。
    void query(Box box, std::vector<int64_t>& out) const;

    // kNN：按 Box 到 point 的最小平方距离升序，返回至多 k 个 payload_id。
    void nearest(Point point, std::size_t k, std::vector<int64_t>& out) const;

    // 点包含：返回所有 box.contains(point) 的 payload_id（左闭右开）。
    void contains_query(Point point, std::vector<int64_t>& out) const;

    std::size_t size() const noexcept;
    void clear();

public:
    struct Node;
private:
    std::unique_ptr<Node> root_;
    std::size_t node_capacity_;
    std::size_t size_;

    // 内部结构性插入（不修改 size_），供 remove 重插孤儿复用。
    void insert_entry(Box box, int64_t payload_id);
};

}  // namespace detail

// 自研 R-tree 模板（D5：无 Boost 依赖）。
// 模板参数 T 通常是图元 id（uint32_t / int64_t）。
// 叶子只存 Box + T，**不持有图元本体**；T 由 RTree<T>::values_ 持有。
// 节点 fanout 默认 16（可构造时传入）。
template <typename T>
class RTree {
public:
    RTree() = default;
    explicit RTree(std::size_t node_capacity) : core_(node_capacity) {}

    // move-only：core_ 不可拷贝，values_ 可移动。默认 move 足够。
    RTree(const RTree&) = delete;
    RTree& operator=(const RTree&) = delete;
    RTree(RTree&&) noexcept = default;
    RTree& operator=(RTree&&) noexcept = default;

    // STR bulk-load：清空当前树，按 entries 批量建树。
    void build(std::span<const std::pair<Box, T>> entries);

    // 增量插入。value 复制到内部载荷表；payload_id = 载荷表索引。
    void insert(Box box, T value);

    // 删除首个 box 匹配且载荷等于 value 的叶子条目；返回是否命中。
    bool remove(Box box, const T& value);

    // 范围查询：返回所有 box 与查询框相交的载荷（顺序未指定）。
    std::vector<T> query(Box box) const;

    // 点包含：返回所有 box.contains(point) 的载荷（左闭右开语义）。
    std::vector<T> contains(Point point) const;

    // kNN：返回距 point 最近的 k 个载荷（按 Box 最小平方距离升序）。
    std::vector<T> nearest(Point point, std::size_t k) const;

    std::size_t size() const noexcept { return core_.size(); }
    void clear();

private:
    detail::RTreeCore core_;
    std::vector<T> values_;  // 载荷表：core 的 payload_id ↔ T
};

// ===================== 模板实现（in-header）=====================

template <typename T>
void RTree<T>::build(std::span<const std::pair<Box, T>> entries) {
    values_.clear();
    values_.reserve(entries.size());
    std::vector<std::pair<Box, int64_t>> core_entries;
    core_entries.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        values_.push_back(entries[i].second);
        core_entries.emplace_back(entries[i].first, static_cast<int64_t>(i));
    }
    core_.build(core_entries);
}

template <typename T>
void RTree<T>::insert(Box box, T value) {
    const int64_t id = static_cast<int64_t>(values_.size());
    values_.push_back(std::move(value));
    core_.insert(box, id);
}

template <typename T>
bool RTree<T>::remove(Box box, const T& value) {
    // 找到首个 values_[i] == value 且 core 含 (box, i) 的条目并删除。
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (values_[i] == value) {
            const int64_t id = static_cast<int64_t>(i);
            if (core_.remove(box, id)) return true;
        }
    }
    return false;
}

template <typename T>
std::vector<T> RTree<T>::query(Box box) const {
    std::vector<int64_t> ids;
    core_.query(box, ids);
    std::vector<T> out;
    out.reserve(ids.size());
    for (auto id : ids) out.push_back(values_[static_cast<std::size_t>(id)]);
    return out;
}

template <typename T>
std::vector<T> RTree<T>::contains(Point point) const {
    std::vector<int64_t> ids;
    core_.contains_query(point, ids);
    std::vector<T> out;
    out.reserve(ids.size());
    for (auto id : ids) out.push_back(values_[static_cast<std::size_t>(id)]);
    return out;
}

template <typename T>
std::vector<T> RTree<T>::nearest(Point point, std::size_t k) const {
    std::vector<int64_t> ids;
    core_.nearest(point, k, ids);
    std::vector<T> out;
    out.reserve(ids.size());
    for (auto id : ids) out.push_back(values_[static_cast<std::size_t>(id)]);
    return out;
}

template <typename T>
void RTree<T>::clear() {
    core_.clear();
    values_.clear();
}

}  // namespace eda::geometry
