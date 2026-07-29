// tests/test_geometry_integration.cpp
//
// P3 Task 9 集成验收：跨模块交叉验证（geometry + command 端到端）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 9 Step 1：
//   1. 图元 CRUD 经命令栈可撤销（Object + ClassDB + SetPropertyCommand + UndoStack）
//   2. 布尔 unite/subtract/intersect + 含孔面积守恒
//   3. R-tree 10k 邻域查询 vs 暴力 O(n²) 交叉验证（用存储 Box 作查询）
//   4. 偏移 + courtyard（器件外轮廓 inflate → courtyard 包含原轮廓）
//   5. 三角剖分面积守恒（Polygon → 三角形面积之和 == 原多边形面积）
//   6. 求交（两线段相交 → 交点坐标精确）
//   7. 端到端铺铜（板框 subtract 焊盘 → triangulate → RTree → UndoStack macro）
//
// 集成时由主循环在 tests/CMakeLists.txt 追加：
//   add_executable(test_geometry_integration test_geometry_integration.cpp)
//   target_link_libraries(test_geometry_integration PRIVATE eda_geometry eda_command gtest_main)
//   gtest_discover_tests(test_geometry_integration)

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "eda/geometry/intersect.h"
#include "eda/geometry/measure.h"
#include "eda/geometry/offset.h"
#include "eda/geometry/triangulate.h"
#include "eda/geometry/types.h"
#include "eda/geometry/units.h"

using namespace eda::geometry;
using namespace eda::command;

namespace {

// ---------- 面积 / 几何辅助 ----------

// 单环绝对面积（nm²）。long double 累加规避 int64 乘积溢出（板级坐标量级 10^16~10^18）。
double ring_area_abs(const std::vector<Point>& ring) {
    const std::size_t n = ring.size();
    if (n < 3) return 0.0;
    long double s = 0.0L;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& a = ring[i];
        const Point& b = ring[(i + 1) % n];
        s += static_cast<long double>(a.x) * static_cast<long double>(b.y);
        s -= static_cast<long double>(b.x) * static_cast<long double>(a.y);
    }
    return std::fabs(static_cast<double>(s)) * 0.5;
}

// Polygon 面积 = |外环| - Σ|孔|（下界 0）。
double polygon_area(const Polygon& p) {
    double a = ring_area_abs(p.outer);
    for (const auto& hole : p.holes) a -= ring_area_abs(hole);
    return a < 0.0 ? 0.0 : a;
}

// 一组多边形总面积。
double total_area(const std::vector<Polygon>& polys) {
    double a = 0.0;
    for (const auto& p : polys) a += polygon_area(p);
    return a;
}

// 单三角形面积（叉积取半绝对值）。
double triangle_area(const Point& a, const Point& b, const Point& c) {
    const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y);
    const double bx = static_cast<double>(b.x), by = static_cast<double>(b.y);
    const double cx = static_cast<double>(c.x), cy = static_cast<double>(c.y);
    return std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) / 2.0;
}

// 剖分结果所有三角形面积之和。
double total_triangle_area(const TriangulationResult& tri) {
    double sum = 0.0;
    for (std::size_t i = 0; i + 2 < tri.indices.size(); i += 3) {
        sum += triangle_area(tri.vertices[tri.indices[i]],
                             tri.vertices[tri.indices[i + 1]],
                             tri.vertices[tri.indices[i + 2]]);
    }
    return sum;
}

// 构造 CCW 矩形 Polygon（外环 4 点，无孔）。
Polygon make_square(Coord x0, Coord y0, Coord x1, Coord y1) {
    Polygon p;
    p.outer = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return p;
}

// 构造 CCW 矩形外环 + CW 矩形孔。
Polygon make_square_with_hole(Coord x0, Coord y0, Coord x1, Coord y1,
                              Coord hx0, Coord hy0, Coord hx1, Coord hy1) {
    Polygon p;
    p.outer = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    p.holes.push_back({{hx0, hy0}, {hx0, hy1}, {hx1, hy1}, {hx1, hy0}});
    return p;
}

// 点是否在单环内（射线法；边界容差忽略，仅用于 courtyard 包含性粗检）。
bool point_in_ring(Point p, const std::vector<Point>& ring) {
    bool inside = false;
    const std::size_t n = ring.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point& a = ring[i];
        const Point& b = ring[j];
        const bool yk = (a.y > p.y) != (b.y > p.y);
        if (!yk) continue;
        const double dy = static_cast<double>(b.y - a.y);
        if (std::fabs(dy) < 1e-9) continue;
        const double x_at = static_cast<double>(a.x) +
                            static_cast<double>(p.y - a.y) *
                            static_cast<double>(b.x - a.x) / dy;
        if (static_cast<double>(p.x) < x_at) inside = !inside;
    }
    return inside;
}

// 暴力 O(n) 范围查询：返回与 q 相交的 id（升序）。
std::vector<int64_t> brute_query(
    const std::vector<std::pair<Box, int64_t>>& data, Box q) {
    std::vector<int64_t> out;
    for (const auto& [b, id] : data) {
        if (b.intersects(q)) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
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

// ---------- 图元对象（ClassDB 注册属性，供命令栈编辑）----------

// 把几何图元包装成 Object，属性经 ClassDB 暴露，SetPropertyCommand 即可通用编辑。
// 这里用"点图元"做演示：x/y 是 ClassDB 注册属性；layer 演示字符串属性。
class PointNode : public eda::Object {
public:
    Coord x = 0;
    Coord y = 0;
    std::string layer = "F.Cu";

    Coord get_x() const { return x; }
    void set_x(Coord v) { x = v; }
    Coord get_y() const { return y; }
    void set_y(Coord v) { y = v; }
    std::string get_layer() const { return layer; }
    void set_layer(const std::string& v) { layer = v; }

    static std::string get_class_static() { return "PointNode"; }
    std::string get_class() const override { return "PointNode"; }
    bool is_class(const std::string& n) const override {
        return n == "PointNode" || Object::is_class(n);
    }
};

void RegisterIntegrationClasses() {
    GDREGISTER_CLASS(PointNode);
    eda::ClassDB::bind_property("x", &PointNode::get_x, &PointNode::set_x);
    eda::ClassDB::bind_property("y", &PointNode::get_y, &PointNode::set_y);
    eda::ClassDB::bind_property("layer", &PointNode::get_layer, &PointNode::set_layer);
}

std::unique_ptr<SetPropertyCommand> SetCoord(PointNode* obj, const std::string& key, int64_t v) {
    return std::make_unique<SetPropertyCommand>(obj, key, eda::Variant(v));
}

}  // namespace

// 统一 fixture：每个用例在干净 ClassDB 状态下运行。
class GeometryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        eda::ClassDB::cleanup();
        RegisterIntegrationClasses();
    }
    void TearDown() override { eda::ClassDB::cleanup(); }
};

// ==========================================================================
// 1. 图元 CRUD 经命令栈可撤销
// ==========================================================================

TEST_F(GeometryIntegrationTest, PrimitiveCrudViaCommandStackIsUndoable) {
    // "创建"图元：实例化 PointNode 并设初值。
    PointNode node;
    node.x = mm_to_nm(10.0);
    node.y = mm_to_nm(20.0);
    node.layer = "F.Cu";
    const Coord orig_x = node.x;

    UndoStack stack;

    // 改属性：x 移动到 30mm。
    stack.push(SetCoord(&node, "x", mm_to_nm(30.0)));
    EXPECT_EQ(node.x, mm_to_nm(30.0));

    // undo 恢复原值。
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(node.x, orig_x);

    // redo 重放。
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(node.x, mm_to_nm(30.0));

    // 字符串属性（layer）也走同一条 SetPropertyCommand 通道。
    stack.push(std::make_unique<SetPropertyCommand>(
        &node, "layer", eda::Variant(std::string("B.Cu"))));
    EXPECT_EQ(node.layer, "B.Cu");
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(node.layer, "F.Cu");
}

TEST_F(GeometryIntegrationTest, MacroCombinesMultiplePrimitiveEdits) {
    // 一次"批量移动"= macro 内多个 SetPropertyCommand，一次 undo 全部回退。
    PointNode a;
    a.x = 0;
    a.y = 0;
    PointNode b;
    b.x = 0;
    b.y = 0;

    UndoStack stack;
    stack.begin_macro("batch move");
    stack.push(SetCoord(&a, "x", 100));
    stack.push(SetCoord(&a, "y", 200));
    stack.push(SetCoord(&b, "x", 300));
    stack.push(SetCoord(&b, "y", 400));
    stack.end_macro();

    EXPECT_EQ(a.x, 100);
    EXPECT_EQ(a.y, 200);
    EXPECT_EQ(b.x, 300);
    EXPECT_EQ(b.y, 400);
    EXPECT_EQ(stack.size(), 1u);  // macro 在主栈为单个撤销单元

    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(a.x, 0);
    EXPECT_EQ(a.y, 0);
    EXPECT_EQ(b.x, 0);
    EXPECT_EQ(b.y, 0);

    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(a.x, 100);
    EXPECT_EQ(b.y, 400);
}

// ==========================================================================
// 2. 布尔 unite/subtract/intersect + 含孔面积守恒
// ==========================================================================

TEST_F(GeometryIntegrationTest, BooleanOpsConserveAreaWithHoles) {
    // 两个含孔多边形（相同）：外环 0..100，孔 30..70。
    //   单个净面积 = 10000 - 1600 = 8400。
    const Polygon a = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);
    const Polygon b = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);

    // unite：形状不变，孔保留，面积守恒。
    auto u = unite(a, b);
    ASSERT_EQ(u.size(), 1u);
    EXPECT_EQ(u[0].holes.size(), 1u);
    EXPECT_NEAR(polygon_area(u[0]), 8400.0, 1e-6);

    // intersect：a ∩ b = a（二者相同），孔保留。
    auto inter = intersect(a, b);
    ASSERT_EQ(inter.size(), 1u);
    EXPECT_EQ(inter[0].holes.size(), 1u);
    EXPECT_NEAR(polygon_area(inter[0]), 8400.0, 1e-6);

    // subtract：a \ b（完全重合）→ 空集。
    auto sub = subtract(a, b);
    EXPECT_TRUE(sub.empty());

    // 含孔多边形 a 与完全覆盖其孔的实心多边形 c 的 unite：
    // 孔被 c 填满 → 结果无孔，面积 = 外环面积（10000）。验证孔填充语义。
    const Polygon a_holed = make_square_with_hole(0, 0, 100, 100, 30, 30, 70, 70);
    const Polygon c = make_square(0, 0, 100, 100);
    auto u2 = unite(a_holed, c);
    ASSERT_EQ(u2.size(), 1u);
    EXPECT_EQ(u2[0].holes.size(), 0u);  // 孔被填满
    EXPECT_NEAR(polygon_area(u2[0]), 10000.0, 1e-6);
}

// ==========================================================================
// 3. R-tree 10k 邻域查询 vs 暴力 O(n²) 交叉验证
// ==========================================================================

TEST_F(GeometryIntegrationTest, RTree10kNeighborQueryMatchesBruteForce) {
    constexpr int64_t N = 10000;
    constexpr int Q = 500;  // 用前 500 个存储 Box 作查询（验证"每个 Box 邻域"）

    std::mt19937_64 rng(0xBEEF1234ULL);
    std::vector<std::pair<Box, int64_t>> entries;
    entries.reserve(static_cast<std::size_t>(N));
    for (int64_t i = 0; i < N; ++i) {
        entries.push_back({random_box(rng, 0, 1'000'000, 100, 5000), i});
    }

    RTree<int64_t> tree;
    tree.build(entries);
    ASSERT_EQ(tree.size(), static_cast<std::size_t>(N));

    int total_matches = 0;
    for (int q = 0; q < Q; ++q) {
        // 用存储的 Box 本身作查询：R-tree 与暴力结果集必须完全一致。
        const Box qb = entries[static_cast<std::size_t>(q)].first;
        auto rtree_res = tree.query(qb);
        const auto brute_res = brute_query(entries, qb);
        std::sort(rtree_res.begin(), rtree_res.end());
        EXPECT_EQ(rtree_res, brute_res) << "query " << q;
        total_matches += static_cast<int>(rtree_res.size());
    }
    // 防"永远返回空"退化：邻域查询至少应命中若干。
    EXPECT_GT(total_matches, 0);
}

// ==========================================================================
// 4. 偏移 + courtyard（器件外轮廓 inflate → courtyard 包含原轮廓）
// ==========================================================================

TEST_F(GeometryIntegrationTest, CourtyardContainsDeviceOutline) {
    // 10mm × 6mm 器件外轮廓，courtyard 间距 1mm。
    Polygon device = make_square(0, 0, mm_to_nm(10.0), mm_to_nm(6.0));
    const Coord gap = mm_to_nm(1.0);

    auto cy = courtyard(device, gap);
    ASSERT_EQ(cy.size(), 1u);

    // 面积核对：12mm × 8mm = 96 mm²。
    EXPECT_NEAR(polygon_area(cy[0]), 96e12, 1e10);

    // 包含性：原外轮廓 4 个角点必须全部落在 courtyard 外环内。
    for (const Point& corner : device.outer) {
        EXPECT_TRUE(point_in_ring(corner, cy[0].outer))
            << "corner (" << corner.x << "," << corner.y << ") not in courtyard";
    }

    // 对称验证：courtyard == inflate(device, gap)。
    auto inf = inflate(device, gap);
    ASSERT_EQ(inf.size(), 1u);
    EXPECT_DOUBLE_EQ(polygon_area(cy[0]), polygon_area(inf[0]));
}

// ==========================================================================
// 5. 三角剖分面积守恒（Polygon → 三角形面积之和 == 原多边形面积）
// ==========================================================================

TEST_F(GeometryIntegrationTest, TriangulationConservesArea) {
    // 外环 200×200 + 方孔 100×100：净面积 = 40000 - 10000 = 30000 nm²。
    Polygon poly;
    poly.outer = {{0, 0}, {200, 0}, {200, 200}, {0, 200}};
    poly.holes.push_back({{50, 50}, {150, 50}, {150, 150}, {50, 150}});

    const auto tri = triangulate(poly);

    // 顶点数 = 外环 4 + 孔 4 = 8；三角形数 = n - 2 + 2h = 8。
    EXPECT_EQ(tri.vertices.size(), 8u);
    EXPECT_EQ(tri.indices.size() % 3u, 0u);
    EXPECT_EQ(tri.indices.size() / 3u, 8u);
    for (uint32_t idx : tri.indices) {
        EXPECT_LT(idx, tri.vertices.size());
    }

    // 面积守恒不变量：Σ|三角形面积| == |area(Polygon)|。
    EXPECT_NEAR(total_triangle_area(tri), 30000.0, 1e-6);
    EXPECT_NEAR(total_triangle_area(tri), area(poly), 1e-6);
}

// ==========================================================================
// 6. 求交（两线段相交 → 交点坐标精确）
// ==========================================================================

TEST_F(GeometryIntegrationTest, SegmentIntersectionIsExact) {
    // X 形：两条对角线交于 (5, 5)——整数交点，零误差。
    Segment a{Point{0, 0}, Point{10, 10}};
    Segment b{Point{0, 10}, Point{10, 0}};
    const auto ip = segment_segment_intersect(a, b);
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(*ip, (Point{5, 5}));

    // T 接：共享端点 (10, 10)。
    Segment c{Point{0, 0}, Point{10, 10}};
    Segment d{Point{10, 10}, Point{20, 0}};
    const auto ip2 = segment_segment_intersect(c, d);
    ASSERT_TRUE(ip2.has_value());
    EXPECT_EQ(*ip2, (Point{10, 10}));

    // 平行：无交点。
    Segment e{Point{0, 0}, Point{10, 0}};
    Segment f{Point{0, 5}, Point{10, 5}};
    EXPECT_FALSE(segment_segment_intersect(e, f).has_value());
}

// ==========================================================================
// 7. 端到端铺铜：板框 subtract 焊盘 → triangulate → RTree → UndoStack macro
// ==========================================================================

TEST_F(GeometryIntegrationTest, CopperPourEndToEndPipeline) {
    // 场景：20mm × 20mm 板框，中央挖 3 个 1mm × 1mm 焊盘避让。
    Polygon board = make_square(0, 0, mm_to_nm(20.0), mm_to_nm(20.0));
    const double board_area = polygon_area(board);

    // 3 个焊盘多边形 + 它们的包围盒（供 RTree 索引）。
    std::vector<Polygon> pads = {
        make_square(mm_to_nm(5.0),  mm_to_nm(5.0),  mm_to_nm(6.0),  mm_to_nm(6.0)),
        make_square(mm_to_nm(10.0), mm_to_nm(10.0), mm_to_nm(11.0), mm_to_nm(11.0)),
        make_square(mm_to_nm(15.0), mm_to_nm(15.0), mm_to_nm(16.0), mm_to_nm(16.0)),
    };

    // 几何管线：逐步从板框挖去每个焊盘 → 铺铜（含 3 个孔）。
    std::vector<Polygon> pour{board};
    for (const auto& pad : pads) {
        std::vector<Polygon> next;
        for (const auto& region : pour) {
            auto diff = subtract(region, pad);
            for (auto& r : diff) next.push_back(std::move(r));
        }
        pour = std::move(next);
    }
    ASSERT_EQ(pour.size(), 1u);
    EXPECT_EQ(pour[0].holes.size(), 3u);

    // 面积守恒：板框面积 - 3 × 焊盘面积（1 mm² = 1e12 nm²）。
    EXPECT_NEAR(polygon_area(pour[0]), board_area - 3.0e12, 1e9);

    // 三角剖分面积守恒（与 area(pour) 交叉验证）。
    const auto tri = triangulate(pour[0]);
    EXPECT_GT(tri.indices.size(), 0u);
    EXPECT_NEAR(total_triangle_area(tri), area(pour[0]), 1e-6);

    // RTree 索引 3 个焊盘：查询每个焊盘邻域应命中其本身。
    std::vector<std::pair<Box, int64_t>> pad_entries;
    for (std::size_t i = 0; i < pads.size(); ++i) {
        std::vector<Point> outer_span(pads[i].outer.begin(), pads[i].outer.end());
        pad_entries.emplace_back(Box::from_points(outer_span), static_cast<int64_t>(i));
    }
    RTree<int64_t> pad_index;
    pad_index.build(pad_entries);
    EXPECT_EQ(pad_index.size(), 3u);

    // 中央焊盘邻域查询：应命中索引 1。
    std::vector<Point> mid_span(pads[1].outer.begin(), pads[1].outer.end());
    auto hit = pad_index.query(Box::from_points(mid_span));
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0], 1);

    // 命令栈 macro：把铺铜图元的 layer 与一个焊盘图元的 layer 一起改，一次 undo 全退。
    PointNode pour_node;
    pour_node.layer = "F.Cu";
    PointNode pad_node;
    pad_node.layer = "F.Cu";

    UndoStack stack;
    stack.begin_macro("relocate copper pour");
    stack.push(std::make_unique<SetPropertyCommand>(
        &pour_node, "layer", eda::Variant(std::string("In1.Cu"))));
    stack.push(std::make_unique<SetPropertyCommand>(
        &pad_node, "layer", eda::Variant(std::string("In1.Cu"))));
    stack.end_macro();

    EXPECT_EQ(pour_node.layer, "In1.Cu");
    EXPECT_EQ(pad_node.layer, "In1.Cu");
    EXPECT_EQ(stack.size(), 1u);

    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(pour_node.layer, "F.Cu");
    EXPECT_EQ(pad_node.layer, "F.Cu");

    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(pour_node.layer, "In1.Cu");
    EXPECT_EQ(pad_node.layer, "In1.Cu");
}
