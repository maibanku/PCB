// tests/test_gerber.cpp
//
// P13 制造输出 · 写入器骨架测试。
//
// 覆盖：
//   - GerberWriter：单层 Gerber 文本含 FS/MO 头、光圈定义（AD）、坐标（3:4 精度 mm*1e4）、
//                   D01（draw，走线）/ D03（flash，焊盘/过孔）/ M02（结束）。
//   - 光圈去重：同形状+同尺寸（如 via pad 与 circle pad 都是 0.6mm）共享同一 D-code。
//   - write_all：遍历 stackup 各层 + Outline，返回 map。
//   - ExcellonWriter：PTH（via）含 M48 头、T1 刀具（按 drill 直径）、T01 选刀、钻孔坐标、
//                     M30 结束；NPTH 头尾完整（无孔）。
//   - PickPlaceWriter：CSV 表头 + Designator/Footprint/X/Y/Rotation/Layer/Value 行；
//                      坐标 nm→mm、旋转弧度→度、Layer Top/Bot。
//
// 数据模型：
//   - 内部坐标 nm（geometry::Coord）；Gerber 默认 3:4 精度（mm*1e4）；Excellon 默认 3:3（mm*1e3）。
//   - Board 仅 Via 提供 drill_diameter；Pad 无 drill 字段（NPTH 待数据模型扩展）。
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_gerber test_gerber.cpp)
//   target_include_directories(test_gerber PRIVATE ${EDA_ROOT})
//   target_link_libraries(test_gerber PRIVATE eda_manufacturing eda_pcb gtest_main)
//   gtest_discover_tests(test_gerber)

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/manufacturing/excellon_writer.h"
#include "eda/manufacturing/gerber_writer.h"
#include "eda/manufacturing/pick_place_writer.h"
#include "eda/pcb/board.h"
#include "eda/pcb/layer.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"

using namespace eda;
using namespace eda::geometry;
using namespace eda::mfg;
using namespace eda::pcb;

namespace {

// 1mm = 1_000_000 nm
constexpr Coord kMm = 1'000'000;

// π（避免 MSVC 上 M_PI 不定义）
constexpr double kPi = 3.14159265358979323846;

// 构造矩形板框 Polygon（4 顶点闭合）。
Polygon make_rect_outline(Coord x0, Coord y0, Coord x1, Coord y1) {
    Polygon p;
    p.outer = {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1}};
    return p;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// 构造标准测试 Board：50×30mm 板框，F.Cu/B.Cu 两层，
// 1 Track + 2 Pad（1 RECT + 1 CIRCLE）+ 1 Through Via + 1 Footprint。
// 所有数值刻意选整数 mm，便于断言 3:4 / 3:3 精度下的精确字符串。
// 注：Board 拷贝/移动构造已删除（PcbEntity 不可拷贝），故以引用传入就地构建。
void populate_sample_board(Board& b) {
    b.set_outline(make_rect_outline(0, 0, 50 * kMm, 30 * kMm));
    b.add_layer(Layer("F.Cu", LayerType::COPPER, 0));
    b.add_layer(Layer("B.Cu", LayerType::COPPER, 1));

    // Track：F.Cu 上 (1,1) → (10,1)，宽 0.2mm
    b.add_track(
        Path{{Point{1 * kMm, 1 * kMm}, Point{10 * kMm, 1 * kMm}}, 200 * 1000},
        "F.Cu");

    // Pad1：RECT 0.5×0.5mm @ (5,5)
    b.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000,
              PadShape::RECT, "F.Cu", "1");

    // Pad2：CIRCLE 0.6mm 直径 @ (5,25)（与 Via pad 同径 → 共享 D-code）
    b.add_pad(Point{5 * kMm, 25 * kMm}, 600 * 1000, 600 * 1000,
              PadShape::CIRCLE, "F.Cu", "2");

    // Via：Through F.Cu → B.Cu @ (25,15)，drill=0.3mm，pad=0.6mm
    b.add_via(Point{25 * kMm, 15 * kMm}, 300 * 1000, 600 * 1000,
              "F.Cu", "B.Cu");

    // Footprint：U1 @ (20,12)，旋转 90°（π/2 弧度）
    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "SOIC-8";
    fp.refdes = "U1";
    fp.position = Point{20 * kMm, 12 * kMm};
    fp.rotation = 3.14159265358979323846 / 2.0;  // 90°
    fp.flipped = false;
    b.add_footprint(fp);
}

}  // namespace

// ============================================================
// GerberWriter —— 文件头与精度
// ============================================================

TEST(GerberWriterTest, HeaderContainsFsMoAndPolarity) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const std::string g = w.write_layer(b, "F.Cu");

    // FS：3:4 精度，前导零省略（L），绝对坐标（A）
    EXPECT_TRUE(contains(g, "%FSLAX34Y34*%"));
    // 单位 mm
    EXPECT_TRUE(contains(g, "%MOMM*%"));
    // 正片
    EXPECT_TRUE(contains(g, "%LPD*%"));
    // 结束
    EXPECT_TRUE(contains(g, "M02*"));
}

TEST(GerberWriterTest, CoordFormatMmTimes1e4) {
    // 直接验证 format_coord：3:4 精度下 mm*1e4
    GerberWriter w;
    EXPECT_EQ(w.format_coord(1.0), "10000");     // 1mm → 10000
    EXPECT_EQ(w.format_coord(10.0), "100000");   // 10mm → 100000
    EXPECT_EQ(w.format_coord(0.0), "0");
    EXPECT_EQ(w.format_coord(-5.0), "-50000");   // 负号保留
}

TEST(GerberWriterTest, PrecisionConfigurable) {
    GerberWriter w;
    w.set_precision(2, 5);  // 2:5
    EXPECT_EQ(w.integer_digits(), 2);
    EXPECT_EQ(w.decimal_digits(), 5);
    EXPECT_EQ(w.format_coord(1.0), "100000");  // 1mm * 1e5 = 100000
}

// ============================================================
// GerberWriter —— 光圈定义（D-code 自动分配 + 去重）
// ============================================================

TEST(GerberWriterTest, AperturesAutoAssignedWithDedup) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const std::string g = w.write_layer(b, "F.Cu");

    // D10 = Circle 0.200（走线宽度）
    EXPECT_TRUE(contains(g, "%ADD10C,0.200*%"));
    // D11 = Rect 0.500×0.500（Pad1）
    EXPECT_TRUE(contains(g, "%ADD11R,0.500X0.500*%"));
    // D12 = Circle 0.600（Pad2 与 Via pad 同径 → 共享，仅一条 AD 行）
    EXPECT_TRUE(contains(g, "%ADD12C,0.600*%"));

    // 验证 D12 不重复定义（count of "ADD12" == 1）
    size_t pos = 0;
    int count_add12 = 0;
    while ((pos = g.find("ADD12", pos)) != std::string::npos) {
        ++count_add12;
        pos += 5;
    }
    EXPECT_EQ(count_add12, 1);
}

TEST(GerberWriterTest, FormatApertureCircleAndRect) {
    GerberAperture circle{10, GerberApertureShape::CIRCLE, 0.2, 0.0};
    EXPECT_EQ(GerberWriter::format_aperture(circle), "ADD10C,0.200");

    GerberAperture rect{11, GerberApertureShape::RECT, 0.5, 0.5};
    EXPECT_EQ(GerberWriter::format_aperture(rect), "ADD11R,0.500X0.500");
}

// ============================================================
// GerberWriter —— D01（draw）/ D02（move）/ D03（flash）
// ============================================================

TEST(GerberWriterTest, TrackEmitsDrawD01) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const std::string g = w.write_layer(b, "F.Cu");

    // Track (1,1) → (10,1)：D02 move 到 (1,1)，D01 draw 到 (10,1)
    EXPECT_TRUE(contains(g, "X10000Y10000D02*"));
    EXPECT_TRUE(contains(g, "X100000Y10000D01*"));
}

TEST(GerberWriterTest, PadEmitsFlashD03) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const std::string g = w.write_layer(b, "F.Cu");

    // Pad1 @ (5,5)
    EXPECT_TRUE(contains(g, "X50000Y50000D03*"));
    // Pad2 @ (5,25)
    EXPECT_TRUE(contains(g, "X50000Y250000D03*"));
    // Via @ (25,15)
    EXPECT_TRUE(contains(g, "X250000Y150000D03*"));
}

TEST(GerberWriterTest, ViaAppearsOnBothConnectedLayers) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);

    // THROUGH via F.Cu→B.Cu：应在两层 Gerber 中都出现 flash
    const std::string fcu = w.write_layer(b, "F.Cu");
    const std::string bcu = w.write_layer(b, "B.Cu");

    EXPECT_TRUE(contains(fcu, "X250000Y150000D03*"));
    EXPECT_TRUE(contains(bcu, "X250000Y150000D03*"));

    // 走线/焊盘只在 F.Cu，B.Cu 不应出现 Pad1 坐标
    EXPECT_FALSE(contains(bcu, "X50000Y50000D03*"));
}

// ============================================================
// GerberWriter —— 板框 outline
// ============================================================

TEST(GerberWriterTest, OutlineWalksClosedPolygon) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const std::string g = w.write_outline(b);

    // 板框 50×30mm，四个角 + 闭合回 (0,0)
    EXPECT_TRUE(contains(g, "X0Y0D02*"));
    EXPECT_TRUE(contains(g, "X500000Y0D01*"));
    EXPECT_TRUE(contains(g, "X500000Y300000D01*"));
    EXPECT_TRUE(contains(g, "X0Y300000D01*"));
    // 闭合段：(0,300) → (0,0)
    EXPECT_TRUE(contains(g, "X0Y0D01*"));
    EXPECT_TRUE(contains(g, "M02*"));
}

// ============================================================
// GerberWriter —— write_all
// ============================================================

TEST(GerberWriterTest, WriteAllProducesAllLayersPlusOutline) {
    GerberWriter w;
    Board b;
    populate_sample_board(b);
    const auto all = w.write_all(b);

    // F.Cu / B.Cu / Outline 三个键
    ASSERT_EQ(all.count("F.Cu"), 1u);
    ASSERT_EQ(all.count("B.Cu"), 1u);
    ASSERT_EQ(all.count("Outline"), 1u);

    // F.Cu 含走线；B.Cu 不含走线（走线在 F.Cu）
    EXPECT_TRUE(contains(all.at("F.Cu"), "X100000Y10000D01*"));
    EXPECT_FALSE(contains(all.at("B.Cu"), "X100000Y10000D01*"));
}

// ============================================================
// ExcellonWriter —— 刀具分组 + 钻孔坐标
// ============================================================

TEST(ExcellonWriterTest, PthContainsHeaderAndToolList) {
    ExcellonWriter w;
    Board b;
    populate_sample_board(b);
    const std::string pth = w.write_pth(b);

    // M48 头 + METRIC,LZ
    EXPECT_TRUE(contains(pth, "M48*"));
    EXPECT_TRUE(contains(pth, "METRIC,LZ*"));
    // 刀具：drill 0.30mm → T1C0.30
    EXPECT_TRUE(contains(pth, "T1C0.30*"));
    // 头结束符
    EXPECT_TRUE(contains(pth, "%*\n"));
    // 程序结束
    EXPECT_TRUE(contains(pth, "M30*"));
}

TEST(ExcellonWriterTest, PthEmitsDrillHits) {
    ExcellonWriter w;
    Board b;
    populate_sample_board(b);
    const std::string pth = w.write_pth(b);

    // 选刀 T01（3:3 精度下 25mm=25000, 15mm=15000）
    EXPECT_TRUE(contains(pth, "T01*"));
    EXPECT_TRUE(contains(pth, "X25000Y15000*"));
}

TEST(ExcellonWriterTest, PthGroupsToolsByDiameter) {
    // 两个不同 drill 直径 → T1 / T2
    Board b;
    b.add_via(Point{1 * kMm, 1 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");  // 0.3mm
    b.add_via(Point{2 * kMm, 2 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");  // 0.3mm（同 T1）
    b.add_via(Point{3 * kMm, 3 * kMm}, 500 * 1000, 800 * 1000, "F.Cu", "B.Cu");  // 0.5mm（T2）

    ExcellonWriter w;
    const std::string pth = w.write_pth(b);

    EXPECT_TRUE(contains(pth, "T1C0.30*"));
    EXPECT_TRUE(contains(pth, "T2C0.50*"));

    // 0.3mm 钻点：T01 选刀一次后两钻连续（不被 T2 打断）
    EXPECT_TRUE(contains(pth, "T01*"));
    EXPECT_TRUE(contains(pth, "T02*"));
    // (1,1) 与 (2,2)（3:3 精度）
    EXPECT_TRUE(contains(pth, "X1000Y1000*"));
    EXPECT_TRUE(contains(pth, "X2000Y2000*"));
    // (3,3) 是 0.5mm 钻
    EXPECT_TRUE(contains(pth, "X3000Y3000*"));
}

TEST(ExcellonWriterTest, NpthFileHasHeaderFooterEvenWhenEmpty) {
    ExcellonWriter w;
    Board b;
    populate_sample_board(b);
    const std::string npth = w.write_npth(b);

    // 骨架阶段无 NPTH 孔数据，但头尾完整
    EXPECT_TRUE(contains(npth, "M48*"));
    EXPECT_TRUE(contains(npth, "METRIC,LZ*"));
    EXPECT_TRUE(contains(npth, "M30*"));
    // 不应出现 T1 刀具
    EXPECT_FALSE(contains(npth, "T1C"));
}

TEST(ExcellonWriterTest, WriteReturnsBothPthAndNpth) {
    ExcellonWriter w;
    Board b;
    populate_sample_board(b);
    const ExcellonOutput out = w.write(b);

    EXPECT_FALSE(out.pth.empty());
    EXPECT_FALSE(out.npth.empty());
    EXPECT_TRUE(contains(out.pth, "T1C0.30*"));
    EXPECT_TRUE(contains(out.npth, "M30*"));
}

TEST(ExcellonWriterTest, CoordFormatMmTimes1e3) {
    ExcellonWriter w;
    // 默认 3:3 精度
    EXPECT_EQ(w.format_coord(1.0), "1000");
    EXPECT_EQ(w.format_coord(25.0), "25000");
    EXPECT_EQ(w.integer_digits(), 3);
    EXPECT_EQ(w.decimal_digits(), 3);
}

// ============================================================
// PickPlaceWriter —— CSV 表头与行
// ============================================================

TEST(PickPlaceWriterTest, CsvHeaderMatchesSpec) {
    PickPlaceWriter w;
    Board b;
    populate_sample_board(b);
    const std::string csv = w.write(b);

    EXPECT_EQ(csv.substr(0, csv.find('\n')),
              "Designator,Footprint,X(mm),Y(mm),Rotation,Layer,Value");
}

TEST(PickPlaceWriterTest, FootprintRowFieldsCorrect) {
    PickPlaceWriter w;
    Board b;
    populate_sample_board(b);
    const std::string csv = w.write(b);

    // U1 @ (20,12)，旋转 90°（弧度→度），Top（未镜像）
    EXPECT_TRUE(contains(csv, "U1,SOIC-8,20.0000,12.0000,90.0,Top,"));
}

TEST(PickPlaceWriterTest, BottomLayerMarkedWhenFlipped) {
    PickPlaceWriter w;
    Board b;
    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "0603";
    fp.refdes = "R1";
    fp.position = Point{15 * kMm, 10 * kMm};
    fp.rotation = 0.0;
    fp.flipped = true;  // 镜像到底层
    b.add_footprint(fp);

    const std::string csv = w.write(b);
    EXPECT_TRUE(contains(csv, "R1,0603,15.0000,10.0000,0.0,Bottom,"));
}

TEST(PickPlaceWriterTest, EmptyRefdesReplacedWithPlaceholder) {
    PickPlaceWriter w;
    Board b;
    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "DPAK";
    fp.refdes = "";   // 空 refdes
    fp.position = Point{0, 0};
    fp.rotation = 0.0;
    fp.flipped = false;
    b.add_footprint(fp);

    const std::string csv = w.write(b);
    // 空 refdes 用 "?" 占位（避免贴片机解析报错）
    EXPECT_TRUE(contains(csv, "?,DPAK,0.0000,0.0000,0.0,Top,"));
}

TEST(PickPlaceWriterTest, RadiansToDegreesConversion) {
    EXPECT_NEAR(PickPlaceWriter::radians_to_degrees(0.0), 0.0, 1e-9);
    EXPECT_NEAR(PickPlaceWriter::radians_to_degrees(kPi / 2.0), 90.0, 1e-9);
    EXPECT_NEAR(PickPlaceWriter::radians_to_degrees(kPi), 180.0, 1e-9);
    EXPECT_NEAR(PickPlaceWriter::radians_to_degrees(3.0 * kPi / 2.0), 270.0, 1e-9);
}

TEST(PickPlaceWriterTest, EmptyBoardYieldsHeaderOnly) {
    PickPlaceWriter w;
    Board b;
    const std::string csv = w.write(b);
    EXPECT_EQ(csv, "Designator,Footprint,X(mm),Y(mm),Rotation,Layer,Value\n");
}

// ============================================================
// PickPlaceWriter —— 列表入参
// ============================================================

TEST(PickPlaceWriterTest, WriteFromFootprintList) {
    PickPlaceWriter w;
    std::vector<FootprintInstance> fps(2);
    fps[0].uuid = generate_uuid();
    fps[0].footprint_ref = "0402";
    fps[0].refdes = "R1";
    fps[0].position = Point{1 * kMm, 2 * kMm};
    fps[0].rotation = 0.0;
    fps[0].flipped = false;
    fps[1].uuid = generate_uuid();
    fps[1].footprint_ref = "0402";
    fps[1].refdes = "R2";
    fps[1].position = Point{3 * kMm, 4 * kMm};
    fps[1].rotation = kPi;  // 180°
    fps[1].flipped = true;

    const std::string csv = w.write(fps);
    EXPECT_TRUE(contains(csv, "R1,0402,1.0000,2.0000,0.0,Top,"));
    EXPECT_TRUE(contains(csv, "R2,0402,3.0000,4.0000,180.0,Bottom,"));
}
