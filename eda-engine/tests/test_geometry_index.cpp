// tests/test_geometry_index.cpp
//
// P3 几何内核 · Task 6 测试：自研 R-tree。
//
// 对应 plan「几何与命令栈实现计划.md」Task 6：
//   - 空树 / 小数据集范围查询正确性
//   - 点包含（左闭右开语义）
//   - nearest(k) 最近邻
//   - STR bulk-load 与逐插入查询结果等价
//   - **10k 随机 Box 邻域查询 vs 暴力 O(n) 每查询结果集一致**（核心交叉验证）
//   - **10k kNN vs 暴力结果一致**
//   - insert / remove 增量一致性
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_index test_geometry_index.cpp)
//   target_link_libraries(test_geometry_index PRIVATE eda_geometry gtest_main)
//   gtest_discover_tests(test_geometry_index)

#include <gtest/gtest.h>

#include "eda/geometry/types.h"
#include "eda/geometry/index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

using namespace eda::geometry;

namespace {

// 暴力 O(n) 范围查询：返回 box 与查询框相交的 id（升序，去重前的集合语义）。
std::vector<int64_t> brute_query(
    const std::vector<std::pair<Box, int64_t>>& data, Box query) {
    std::vector<int64_t> out;
    for (const auto& [b, id] : data) {
        if (b.intersects(query)) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// 暴力 kNN：按 Box 到 point 的最小平方距离升序，返回前 k 个 id（集合语义，已排序去序）。
// 与 RTree 比较时用排序后 id 集合（容忍等距 tie-break 次序差异）。
std::vector<int64_t> brute_nearest_set(
    const std::vector<std::pair<Box, int64_t>>& data, Point p,
    std::size_t k) {
    struct Score {
        long double dist;
        int64_t id;
    };
    std::vector<Score> scored;
    scored.reserve(data.size());
    for (const auto& [b, id] : data) {
        long double dx = 0.0L, dy = 0.0L;
        if (p.x < b.min.x) dx = static_cast<long double>(b.min.x - p.x);
        else if (p.x > b.max.x) dx = static_cast<long double>(p.x - b.max.x);
        if (p.y < b.min.y) dy = static_cast<long double>(b.min.y - p.y);
        else if (p.y > b.max.y) dy = static_cast<long double>(p.y - b.max.y);
        scored.push_back({dx * dx + dy * dy, id});
    }
    std::sort(scored.begin(), scored.end(),
              [](const Score& a, const Score& b) {
                  if (a.dist != b.dist) return a.dist < b.dist;
                  return a.id < b.id;  // 确定性 tie-break，便于集合比较
              });
    std::vector<int64_t> out;
    for (std::size_t i = 0; i < scored.size() && i < k; ++i) {
        out.push_back(scored[i].id);
    }
    std::sort(out.begin(), out.end());  // 集合语义：排序后与 RTree 结果比较
    return out;
}

// 随机 AABB 生成（固定种子，可复现）。
Box random_box(std::mt19937_64& rng, Coord span_min, Coord span_max,
               Coord size_min, Coord size_max) {
    std::uniform_int_distribution<Coord> span_dist(span_min, span_max);
    std::uniform_int_distribution<Coord> size_dist(size_min, size_max);
    const Coord x0 = span_dist(rng);
    const Coord y0 = span_dist(rng);
    const Coord w = size_dist(rng);
    const Coord h = size_dist(rng);
    return Box{Point{x0, y0}, Point{x0 + w, y0 + h}};
}

}  // namespace

// ---------- 空树 ----------
TEST(RTreeTest, EmptyTreeBehaviors) {
    RTree<int64_t> tree;
    EXPECT_EQ(tree.size(), 0u);
    EXPECT_TRUE(tree.query(Box{Point{0, 0}, Point{10, 10}}).empty());
    EXPECT_TRUE(tree.nearest(Point{0, 0}, 3).empty());
    EXPECT_TRUE(tree.contains(Point{0, 0}).empty());
    // remove 在空树上应返回 false。
    EXPECT_FALSE(tree.remove(Box{Point{0, 0}, Point{1, 1}}, 0));
}

// ---------- 小数据集范围查询 ----------
TEST(RTreeTest, SmallRangeQuery) {
    RTree<int64_t> tree;
    std::vector<std::pair<Box, int64_t>> entries = {
        {{{0, 0}, {10, 10}}, 1},
        {{{20, 20}, {30, 30}}, 2},
        {{{5, 5}, {15, 15}}, 3},
        {{{100, 100}, {110, 110}}, 4},
    };
    tree.build(entries);
    EXPECT_EQ(tree.size(), 4u);

    // 查询 (0,0)-(15,15)：命中 1 与 3（2 是 (20,20)-(30,30) 不相交）。
    auto r = tree.query(Box{Point{0, 0}, Point{15, 15}});
    std::sort(r.begin(), r.end());
    EXPECT_EQ(r, (std::vector<int64_t>{1, 3}));

    // 全空间查询：4 个全部命中。
    auto all = tree.query(Box{Point{-1000, -1000}, Point{1000, 1000}});
    EXPECT_EQ(all.size(), 4u);

    // 不相交查询：空。
    auto none = tree.query(Box{Point{200, 200}, Point{300, 300}});
    EXPECT_TRUE(none.empty());
}

// ---------- 点包含（左闭右开语义）----------
TEST(RTreeTest, PointContains) {
    RTree<int64_t> tree;
    std::vector<std::pair<Box, int64_t>> entries = {
        {{{0, 0}, {10, 10}}, 1},     // 含 (5,5)，不含 (10,10)（右开）
        {{{10, 10}, {20, 20}}, 2},   // 含 (10,10)（左闭）
    };
    tree.build(entries);

    auto a = tree.contains(Point{5, 5});
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(a[0], 1);

    auto b = tree.contains(Point{10, 10});
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0], 2);

    EXPECT_TRUE(tree.contains(Point{50, 50}).empty());
}

// ---------- nearest(1)：原点在 box1 内 → 距离 0 ----------
TEST(RTreeTest, NearestOne) {
    RTree<int64_t> tree;
    std::vector<std::pair<Box, int64_t>> entries = {
        {{{0, 0}, {2, 2}}, 1},
        {{{100, 100}, {102, 102}}, 2},
        {{{10, 10}, {12, 12}}, 3},
    };
    tree.build(entries);

    auto n = tree.nearest(Point{0, 0}, 1);
    ASSERT_EQ(n.size(), 1u);
    EXPECT_EQ(n[0], 1);

    // 查询点远离 box1，最近应切换到 box3。
    auto n2 = tree.nearest(Point{9, 9}, 1);
    ASSERT_EQ(n2.size(), 1u);
    EXPECT_EQ(n2[0], 3);
}

// ---------- nearest(k)：返回数量上限 ----------
TEST(RTreeTest, NearestMoreThanAvailable) {
    RTree<int64_t> tree;
    std::vector<std::pair<Box, int64_t>> entries = {
        {{{0, 0}, {1, 1}}, 1},
        {{{10, 10}, {11, 11}}, 2},
    };
    tree.build(entries);
    // k > size：返回全部 2 个。
    auto n = tree.nearest(Point{0, 0}, 10);
    EXPECT_EQ(n.size(), 2u);
}

// ---------- STR bulk-load 与逐插入查询结果等价 ----------
TEST(RTreeTest, BulkLoadMatchesInsertion) {
    std::mt19937_64 rng(42);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(500);
    for (int64_t i = 0; i < 500; ++i) {
        entries.push_back({random_box(rng, -100000, 100000, 10, 1000), i});
    }

    RTree<int64_t> bulk_tree;
    bulk_tree.build(entries);

    RTree<int64_t> ins_tree;
    for (const auto& [b, id] : entries) ins_tree.insert(b, id);

    EXPECT_EQ(bulk_tree.size(), ins_tree.size());

    // 同一组随机查询：两者结果集合必须一致。
    for (int q = 0; q < 50; ++q) {
        const Box qb = random_box(rng, -100000, 100000, 100, 5000);
        auto a = bulk_tree.query(qb);
        auto b = ins_tree.query(qb);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        EXPECT_EQ(a, b) << "query " << q;
    }
}

// ---------- 10k 随机 Box 邻域查询 vs 暴力 O(n) 结果一致（核心交叉验证）----------
TEST(RTreeTest, Random10kNeighborQueryMatchesBruteForce) {
    constexpr int64_t N = 10000;
    constexpr int Q = 200;

    std::mt19937_64 rng(0xC0FFEEULL);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(N);
    for (int64_t i = 0; i < N; ++i) {
        entries.push_back({random_box(rng, 0, 1'000'000, 100, 5000), i});
    }

    RTree<int64_t> tree;
    tree.build(entries);
    ASSERT_EQ(tree.size(), static_cast<std::size_t>(N));

    int total_matches = 0;
    for (int q = 0; q < Q; ++q) {
        const Box qb = random_box(rng, 0, 1'000'000, 500, 20000);
        auto rtree_res = tree.query(qb);
        const auto brute_res = brute_query(entries, qb);
        std::sort(rtree_res.begin(), rtree_res.end());
        EXPECT_EQ(rtree_res, brute_res) << "query " << q;
        total_matches += static_cast<int>(rtree_res.size());
    }
    // 防"永远返回空"退化：200 次随机大窗口查询至少应命中若干。
    EXPECT_GT(total_matches, 0);
}

// ---------- 10k kNN vs 暴力（集合语义，容忍等距 tie-break）----------
TEST(RTreeTest, Random10kNearestMatchesBruteForce) {
    constexpr int64_t N = 10000;
    constexpr int Q = 100;
    constexpr std::size_t K = 5;

    std::mt19937_64 rng(0xCAFEULL);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(N);
    for (int64_t i = 0; i < N; ++i) {
        entries.push_back({random_box(rng, 0, 1'000'000, 100, 5000), i});
    }

    RTree<int64_t> tree;
    tree.build(entries);

    for (int q = 0; q < Q; ++q) {
        std::uniform_int_distribution<Coord> pdist(-100000, 1'100'000);
        const Point p{pdist(rng), pdist(rng)};
        auto rtree_res = tree.nearest(p, K);
        const auto brute_res = brute_nearest_set(entries, p, K);
        ASSERT_EQ(rtree_res.size(), brute_res.size()) << "q=" << q;
        std::sort(rtree_res.begin(), rtree_res.end());
        EXPECT_EQ(rtree_res, brute_res) << "q=" << q;
    }
}

// ---------- insert / remove 增量一致性（与暴力对照剩余集）----------
TEST(RTreeTest, InsertRemoveConsistency) {
    std::mt19937_64 rng(2024);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(1000);
    for (int64_t i = 0; i < 1000; ++i) {
        entries.push_back({random_box(rng, 0, 100'000, 10, 500), i});
    }

    RTree<int64_t> tree;
    tree.build(entries);

    // 删除所有偶数 id。
    for (int64_t i = 0; i < 1000; i += 2) {
        const bool removed = tree.remove(entries[i].first, entries[i].second);
        EXPECT_TRUE(removed) << "remove i=" << i;
    }
    EXPECT_EQ(tree.size(), 500u);

    // 剩余集合 = 奇数 id。
    std::vector<std::pair<Box, int64_t>> remaining;
    remaining.reserve(500);
    for (int64_t i = 1; i < 1000; i += 2) remaining.push_back(entries[i]);

    for (int q = 0; q < 50; ++q) {
        const Box qb = random_box(rng, 0, 100'000, 100, 5000);
        auto rtree_res = tree.query(qb);
        const auto brute_res = brute_query(remaining, qb);
        std::sort(rtree_res.begin(), rtree_res.end());
        EXPECT_EQ(rtree_res, brute_res) << "q=" << q;
    }

    // 再插回 i % 4 == 0 的偶数（与已删的偶数重叠一半）。
    for (int64_t i = 0; i < 1000; i += 4) {
        tree.insert(entries[i].first, entries[i].second);
    }

    // 当前剩余 = 奇数 id + (i%4==0 的偶数 id)。
    std::vector<std::pair<Box, int64_t>> remaining2;
    for (int64_t i = 0; i < 1000; ++i) {
        if (i % 2 == 1 || i % 4 == 0) remaining2.push_back(entries[i]);
    }
    for (int q = 0; q < 50; ++q) {
        const Box qb = random_box(rng, 0, 100'000, 100, 5000);
        auto rtree_res = tree.query(qb);
        const auto brute_res = brute_query(remaining2, qb);
        std::sort(rtree_res.begin(), rtree_res.end());
        EXPECT_EQ(rtree_res, brute_res) << "q2=" << q;
    }
}

// ---------- remove 未命中 ----------
TEST(RTreeTest, RemoveNonExistent) {
    RTree<int64_t> tree;
    tree.insert(Box{Point{0, 0}, Point{10, 10}}, 100);
    EXPECT_FALSE(tree.remove(Box{Point{0, 0}, Point{10, 10}}, 999));
    EXPECT_FALSE(tree.remove(Box{Point{20, 20}, Point{30, 30}}, 100));
    EXPECT_EQ(tree.size(), 1u);
}

// ---------- uint32_t 模板参数可用 ----------
TEST(RTreeTest, Uint32Payload) {
    RTree<uint32_t> tree;
    std::vector<std::pair<Box, uint32_t>> entries = {
        {{{0, 0}, {10, 10}}, 7u},
        {{{20, 20}, {30, 30}}, 42u},
    };
    tree.build(entries);
    EXPECT_EQ(tree.size(), 2u);
    auto r = tree.query(Box{Point{0, 0}, Point{10, 10}});
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 7u);
}
