// tests/test_geometry_benchmark.cpp
//
// P3 Task 9 性能基准：测时延，不断言 PASS/FAIL，只打印到 stdout。
//
// 对应 plan「几何与命令栈实现计划.md」Task 9 Step 2：
//   1. 10k Box R-tree build + 1000 次 query 时延
//   2. 两多边形布尔（100 顶点）时延
//   3. 10k 顶点多边形三角剖分时延
//   4. 命令栈 1000 次 push/undo/redo 时延
//
// 这是独立可执行程序（自带 main，不走 gtest），输出直接走 stdout，
// 不被 GoogleTest 输出捕获，便于人工 / CI 读取时延数据。
//
// 集成时由主循环在 tests/CMakeLists.txt 追加（不入 ctest，仅编译运行）：
//   add_executable(test_geometry_benchmark test_geometry_benchmark.cpp)
//   target_link_libraries(test_geometry_benchmark PRIVATE eda_geometry eda_command)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/variant.h"
#include "eda/command/command.h"
#include "eda/command/undo_stack.h"
#include "eda/geometry/boolean.h"
#include "eda/geometry/index.h"
#include "eda/geometry/triangulate.h"
#include "eda/geometry/types.h"
#include "eda/geometry/units.h"

using namespace eda::geometry;
using namespace eda::command;

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Clock = std::chrono::steady_clock;

template <typename F>
double elapsed_ms(F&& f) {
    const auto t0 = Clock::now();
    f();
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// 圆环近似多边形（CCW，n 顶点）。
Polygon make_circle_polygon(Coord cx, Coord cy, Coord r_nm, int n) {
    Polygon p;
    p.outer.reserve(static_cast<std::size_t>(n));
    const double r = static_cast<double>(r_nm);
    for (int i = 0; i < n; ++i) {
        const double ang = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
        const Coord x = cx + static_cast<Coord>(std::llround(r * std::cos(ang)));
        const Coord y = cy + static_cast<Coord>(std::llround(r * std::sin(ang)));
        p.outer.push_back({x, y});
    }
    return p;
}

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

// ---------- 基准 1：10k Box R-tree build + 1000 query ----------
double bench_rtree_build_and_query() {
    constexpr int64_t N = 10000;
    constexpr int Q = 1000;

    std::mt19937_64 rng(0xC0FFEEULL);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(static_cast<std::size_t>(N));
    for (int64_t i = 0; i < N; ++i) {
        entries.push_back({random_box(rng, 0, 1'000'000, 100, 5000), i});
    }

    RTree<int64_t> tree;
    const double build_ms = elapsed_ms([&] {
        tree.build(entries);
    });

    std::vector<Box> queries;
    queries.reserve(static_cast<std::size_t>(Q));
    for (int q = 0; q < Q; ++q) {
        queries.push_back(random_box(rng, 0, 1'000'000, 500, 20000));
    }

    volatile long long hits = 0;  // volatile 防优化掉查询循环
    const double query_ms = elapsed_ms([&] {
        for (const Box& qb : queries) {
            auto r = tree.query(qb);
            hits += static_cast<long long>(r.size());
        }
    });

    std::cout << "[bench] R-tree 10k build: " << build_ms << " ms\n";
    std::cout << "[bench] R-tree 1000 query: " << query_ms << " ms"
              << " (avg " << query_ms / Q << " ms/query, hits=" << hits << ")\n";
    return query_ms;
}

// ---------- 基准 2：两 100 顶点多边形布尔 ----------
double bench_boolean_100_vertices() {
    // 两个 100 顶点圆环，部分重叠。
    const Polygon a = make_circle_polygon(mm_to_nm(0.0), mm_to_nm(0.0), mm_to_nm(10.0), 100);
    const Polygon b = make_circle_polygon(mm_to_nm(15.0), mm_to_nm(0.0), mm_to_nm(10.0), 100);

    const double t_unite = elapsed_ms([&] { volatile auto r = unite(a, b); (void)r; });
    const double t_sub = elapsed_ms([&] { volatile auto r = subtract(a, b); (void)r; });
    const double t_inter = elapsed_ms([&] { volatile auto r = intersect(a, b); (void)r; });

    std::cout << "[bench] boolean 100-vertex unite:    " << t_unite << " ms\n";
    std::cout << "[bench] boolean 100-vertex subtract: " << t_sub << " ms\n";
    std::cout << "[bench] boolean 100-vertex intersect: " << t_inter << " ms\n";
    return t_unite + t_sub + t_inter;
}

// ---------- 基准 3：10k 顶点多边形三角剖分 ----------
double bench_triangulate_10k_vertices() {
    constexpr int N = 10000;
    const Polygon poly = make_circle_polygon(0, 0, mm_to_nm(100.0), N);

    double tri_ms = 0.0;
    std::size_t tri_count = 0;
    double area_sum = 0.0;
    {
        const auto t0 = Clock::now();
        const auto tri = triangulate(poly);
        const auto t1 = Clock::now();
        tri_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        tri_count = tri.indices.size() / 3u;
        double s = 0.0;
        for (std::size_t i = 0; i + 2 < tri.indices.size(); i += 3) {
            const Point& p0 = tri.vertices[tri.indices[i]];
            const Point& p1 = tri.vertices[tri.indices[i + 1]];
            const Point& p2 = tri.vertices[tri.indices[i + 2]];
            s += std::fabs(static_cast<double>((p1.x - p0.x) * (p2.y - p0.y) -
                                               (p2.x - p0.x) * (p1.y - p0.y))) / 2.0;
        }
        area_sum = s;
    }

    std::cout << "[bench] triangulate 10k-vertex polygon: " << tri_ms << " ms"
              << " (triangles=" << tri_count << ", area=" << area_sum << " nm^2)\n";
    return tri_ms;
}

// ---------- 基准 4：命令栈 1000 次 push/undo/redo ----------

class BenchNode : public eda::Object {
public:
    int64_t value = 0;
    int64_t get_value() const { return value; }
    void set_value(int64_t v) { value = v; }

    static std::string get_class_static() { return "BenchNode"; }
    std::string get_class() const override { return "BenchNode"; }
    bool is_class(const std::string& n) const override {
        return n == "BenchNode" || Object::is_class(n);
    }
};

double bench_command_stack_1000_ops() {
    eda::ClassDB::cleanup();
    GDREGISTER_CLASS(BenchNode);
    eda::ClassDB::bind_property("value", &BenchNode::get_value, &BenchNode::set_value);

    constexpr int N = 1000;
    BenchNode node;
    node.value = 0;

    UndoStack stack;
    stack.set_memory_limit(0);  // 无限，保留全部 1000 步

    const double push_ms = elapsed_ms([&] {
        for (int i = 1; i <= N; ++i) {
            stack.push(std::make_unique<SetPropertyCommand>(
                &node, "value", eda::Variant(static_cast<int64_t>(i))));
        }
    });

    const double undo_ms = elapsed_ms([&] {
        for (int i = 0; i < N; ++i) stack.undo();
    });

    const double redo_ms = elapsed_ms([&] {
        for (int i = 0; i < N; ++i) stack.redo();
    });

    std::cout << "[bench] command stack " << N << " push:  " << push_ms
              << " ms (final value=" << node.value << ")\n";
    std::cout << "[bench] command stack " << N << " undo:  " << undo_ms << " ms\n";
    std::cout << "[bench] command stack " << N << " redo:  " << redo_ms << " ms\n";

    eda::ClassDB::cleanup();
    return push_ms + undo_ms + redo_ms;
}

}  // namespace

int main() {
    std::cout << "=== P3 Task 9 Geometry & Command Performance Benchmark ===\n";
    std::cout.setf(std::ios::fixed);
    std::cout.precision(3);

    bench_rtree_build_and_query();
    bench_boolean_100_vertices();
    bench_triangulate_10k_vertices();
    bench_command_stack_1000_ops();

    std::cout << "=== benchmark complete ===\n";
    return 0;
}
