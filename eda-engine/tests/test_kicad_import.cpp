// tests/test_kicad_import.cpp
//
// P16 格式兼容 · KiCad .kicad_pcb 导入测试（骨架）。
//
// 覆盖：
//   - 内嵌最小 .kicad_pcb S-expression 文本 → parse_string → Board
//   - Track / Pad / Via / Net 计数与字段正确（含 net 码 → UUID 映射）
//   - 坐标 mm → nm 转换（segment / via / pad 全路径）
//   - footprint pad 局部→世界坐标变换（含 0° / 90° 旋转场景）
//   - Edge.Cuts gr_line → 板框 Polygon 链式拼接 + bounding_box
//   - FidelityReport 计数与 summary 字符串
//   - 失败路径：非 S-expression / 非 kicad_pcb 顶层 → false + error_message
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_kicad_import test_kicad_import.cpp)
//   target_link_libraries(test_kicad_import PRIVATE eda_formats gtest_main)
//   gtest_discover_tests(test_kicad_import)

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "eda/formats/kicad_importer.h"
#include "eda/formats/kicad_pcb_parser.h"
#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/pcb/board.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"

using namespace eda;
using namespace eda::formats;
using namespace eda::pcb;
using namespace eda::geometry;

namespace {

// 1mm = 1_000_000 nm
constexpr Coord kMm = 1'000'000;

// 最小可用的 .kicad_pcb 样本（KiCad v6+ S-expression 风格）：
//   - 3 nets（含 net 0 ""）→ 2 个具名网络（VCC / GND）
//   - 1 个 segment（F.Cu，net 1=VCC）
//   - 1 个 via（F.Cu→B.Cu，net 2=GND）
//   - 1 个 footprint，2 个 pad（pad1→VCC，pad2→GND）
//   - 4 条 Edge.Cuts gr_line 围成 100×100mm 矩形板框
const char* kSampleKicadPcb = R"SX(
(kicad_pcb
  (version 20221018)
  (generator "eda-engine-test")
  (general (thickness 1.6))
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (32 "B.Adhes" user "B.Adhesive")
    (33 "F.Adhes" user "F.Adhesive")
    (34 "B.Paste" user)
    (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings")
    (44 "Edge.Cuts" user)
  )

  (net 0 "")
  (net 1 "VCC")
  (net 2 "GND")

  (footprint "Resistor:R_0805"
    (layer "F.Cu")
    (at 100.0 100.0)
    (property "Reference" "R1" (at 0 -1.5) (layer "F.SilkS"))
    (property "Value" "10k" (at 0 1.5) (layer "F.SilkS"))
    (pad "1" smd rect
      (at -1.0 0.0)
      (size 0.5 0.6)
      (layers "F.Cu" "F.Mask")
      (net 1 "VCC"))
    (pad "2" smd rect
      (at 1.0 0.0)
      (size 0.5 0.6)
      (layers "F.Cu" "F.Mask")
      (net 2 "GND"))
  )

  (segment
    (start 100.0 80.0)
    (end 100.0 50.0)
    (width 0.25)
    (layer "F.Cu")
    (net 1 "VCC"))

  (via
    (at 50.0 50.0)
    (size 0.6)
    (drill 0.3)
    (layers "F.Cu" "B.Cu")
    (net 2 "GND"))

  (gr_line (start 0 0) (end 100.0 0) (layer "Edge.Cuts"))
  (gr_line (start 100.0 0) (end 100.0 100.0) (layer "Edge.Cuts"))
  (gr_line (start 100.0 100.0) (end 0 100.0) (layer "Edge.Cuts"))
  (gr_line (start 0 100.0) (end 0 0) (layer "Edge.Cuts"))
)
)SX";

// 用于 footprint 旋转测试的样本：footprint 旋转 90°，pad 局部 (1, 0)
// 应在世界坐标 (100 - 0, 100 + 1) = (100, 101) mm（绕原点逆时针旋转后再平移）。
// 旋转矩阵 R(90°) = [[0, -1], [1, 0]]：(1,0) → (0, 1)；平移 (+100, +100) → (100, 101)。
const char* kRotatedFootprintPcb = R"SX(
(kicad_pcb
  (version 20221018)
  (net 1 "SIG")
  (footprint "Cap:C0402"
    (layer "F.Cu")
    (at 100.0 100.0 90.0)
    (pad "1" smd rect
      (at 1.0 0.0)
      (size 0.5 0.5)
      (layers "F.Cu" "F.Mask")
      (net 1 "SIG")))
)
)SX";

}  // namespace

// ============================================================
// KiCadPcbParser 直接调用
// ============================================================

TEST(KiCadPcbParserTest, ParsesSampleIntoBoard) {
    KiCadPcbParser parser;
    Board board;
    KiCadPcbParseStats stats;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board, &stats));

    // 顶层元素计数
    EXPECT_EQ(board.track_count(), 1u);
    EXPECT_EQ(board.via_count(), 1u);
    EXPECT_EQ(board.pad_count(), 2u);
    EXPECT_EQ(board.footprint_count(), 1u);
    // net 0 "" 跳过；仅 VCC / GND 入树
    EXPECT_EQ(board.net_count(), 2u);
}

TEST(KiCadPcbParserTest, StatsReflectSampleCounts) {
    KiCadPcbParser parser;
    Board board;
    KiCadPcbParseStats stats;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board, &stats));

    EXPECT_EQ(stats.nets_total, 3u);          // (net 0 "") + 1 + 2
    EXPECT_EQ(stats.nets_imported, 2u);       // VCC / GND
    EXPECT_EQ(stats.segments_total, 1u);
    EXPECT_EQ(stats.segments_imported, 1u);
    EXPECT_EQ(stats.vias_total, 1u);
    EXPECT_EQ(stats.vias_imported, 1u);
    EXPECT_EQ(stats.pads_total, 2u);
    EXPECT_EQ(stats.pads_imported, 2u);
    EXPECT_EQ(stats.footprints_total, 1u);
    EXPECT_EQ(stats.footprints_imported, 1u);
    EXPECT_EQ(stats.outline_segments_total, 4u);
    EXPECT_EQ(stats.outline_segments_imported, 4u);
}

TEST(KiCadPcbParserTest, TrackFieldsAndCoordinatesNm) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    ASSERT_EQ(board.tracks().size(), 1u);
    const Track& t = *board.tracks().front();
    ASSERT_EQ(t.path().points.size(), 2u);
    EXPECT_EQ(t.path().points[0].x, 100 * kMm);
    EXPECT_EQ(t.path().points[0].y, 80 * kMm);
    EXPECT_EQ(t.path().points[1].x, 100 * kMm);
    EXPECT_EQ(t.path().points[1].y, 50 * kMm);
    EXPECT_EQ(t.width(), mm_to_nm(0.25));     // 250_000 nm
    EXPECT_EQ(t.layer_id(), "F.Cu");
}

TEST(KiCadPcbParserTest, ViaFieldsAndCoordinatesNm) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    ASSERT_EQ(board.vias().size(), 1u);
    const Via& v = *board.vias().front();
    EXPECT_EQ(v.position().x, 50 * kMm);
    EXPECT_EQ(v.position().y, 50 * kMm);
    EXPECT_EQ(v.pad_diameter(), mm_to_nm(0.6));
    EXPECT_EQ(v.drill_diameter(), mm_to_nm(0.3));
    EXPECT_EQ(v.layer_from(), "F.Cu");
    EXPECT_EQ(v.layer_to(), "B.Cu");
    EXPECT_EQ(v.via_type(), ViaType::THROUGH);
}

TEST(KiCadPcbParserTest, NetCodeToUuidMapping) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    // 收集 net name → uuid
    std::string vcc_uuid, gnd_uuid;
    for (const auto& n : board.nets()) {
        if (n->name() == "VCC") vcc_uuid = n->uuid();
        if (n->name() == "GND") gnd_uuid = n->uuid();
    }
    ASSERT_FALSE(vcc_uuid.empty());
    ASSERT_FALSE(gnd_uuid.empty());

    // segment → net 1 = VCC
    const Track& t = *board.tracks().front();
    EXPECT_EQ(t.net_uuid(), vcc_uuid);

    // via → net 2 = GND
    const Via& v = *board.vias().front();
    EXPECT_EQ(v.net_uuid(), gnd_uuid);

    // pad1 → VCC, pad2 → GND
    ASSERT_EQ(board.pads().size(), 2u);
    EXPECT_EQ(board.pads()[0]->net_uuid(), vcc_uuid);
    EXPECT_EQ(board.pads()[1]->net_uuid(), gnd_uuid);
}

TEST(KiCadPcbParserTest, PadWorldPositionAndSize) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    // footprint at (100, 100) 0°；pad1 local (-1, 0) → world (99, 100)
    // pad2 local (+1, 0) → world (101, 100)
    ASSERT_EQ(board.pads().size(), 2u);
    const Pad& p1 = *board.pads()[0];
    const Pad& p2 = *board.pads()[1];

    EXPECT_EQ(p1.position().x, 99 * kMm);
    EXPECT_EQ(p1.position().y, 100 * kMm);
    EXPECT_EQ(p1.width(), mm_to_nm(0.5));
    EXPECT_EQ(p1.height(), mm_to_nm(0.6));
    EXPECT_EQ(p1.shape(), PadShape::RECT);
    EXPECT_EQ(p1.layer_id(), "F.Cu");
    EXPECT_EQ(p1.number(), "1");

    EXPECT_EQ(p2.position().x, 101 * kMm);
    EXPECT_EQ(p2.position().y, 100 * kMm);
    EXPECT_EQ(p2.number(), "2");
}

TEST(KiCadPcbParserTest, PadWorldPositionWithFootprintRotation) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kRotatedFootprintPcb, board));

    ASSERT_EQ(board.pads().size(), 1u);
    const Pad& p = *board.pads().front();
    // R(90°)*(1,0) = (0,1)；平移 (100,100) → (100, 101)
    EXPECT_EQ(p.position().x, 100 * kMm);
    EXPECT_EQ(p.position().y, 101 * kMm);
}

TEST(KiCadPcbParserTest, FootprintInstanceMetadata) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    ASSERT_EQ(board.footprints().size(), 1u);
    const FootprintInstance& fp = board.footprints()[0];
    EXPECT_EQ(fp.footprint_ref, "Resistor:R_0805");
    EXPECT_EQ(fp.refdes, "R1");
    EXPECT_FALSE(fp.flipped);            // F.Cu → not flipped
    EXPECT_EQ(fp.position.x, 100 * kMm);
    EXPECT_EQ(fp.position.y, 100 * kMm);
    EXPECT_NEAR(fp.rotation, 0.0, 1e-9);
}

TEST(KiCadPcbParserTest, OutlinePolygonChainedFromEdgeCuts) {
    KiCadPcbParser parser;
    Board board;
    ASSERT_TRUE(parser.parse_string(kSampleKicadPcb, board));

    const Polygon& outline = board.outline();
    EXPECT_EQ(outline.outer.size(), 4u);  // 闭合矩形 4 顶点

    // 板框 AABB 由 bounding_box 暴露：0..100mm
    const Box bb = board.bounding_box();
    EXPECT_EQ(bb.min.x, 0);
    EXPECT_EQ(bb.min.y, 0);
    EXPECT_EQ(bb.max.x, 100 * kMm);
    EXPECT_EQ(bb.max.y, 100 * kMm);
}

TEST(KiCadPcbParserTest, FailureBadSexpr) {
    KiCadPcbParser parser;
    Board board;
    KiCadPcbParseStats stats;
    EXPECT_FALSE(parser.parse_string("(unterminated (nested", board, &stats));
    EXPECT_FALSE(stats.error_message.empty());
}

TEST(KiCadPcbParserTest, FailureWrongTopLevelHead) {
    KiCadPcbParser parser;
    Board board;
    KiCadPcbParseStats stats;
    EXPECT_FALSE(parser.parse_string("(not_a_pcb_file)", board, &stats));
    EXPECT_EQ(stats.error_message, "top-level head is not 'kicad_pcb'");
}

// ============================================================
// KiCadImporter + FidelityReport
// ============================================================

TEST(KiCadImporterTest, ImportStringProducesFidelityReport) {
    KiCadImporter importer;
    Board board;
    FidelityReport report;
    ASSERT_TRUE(importer.import_string(kSampleKicadPcb, board, report));

    EXPECT_TRUE(report.parsed_ok);
    EXPECT_EQ(report.source_format, "kicad_pcb");
    EXPECT_EQ(report.nets_imported, 2u);
    EXPECT_EQ(report.segments_imported, 1u);
    EXPECT_EQ(report.vias_imported, 1u);
    EXPECT_EQ(report.pads_imported, 2u);
    EXPECT_EQ(report.footprints_imported, 1u);
    EXPECT_EQ(report.outline_segments_imported, 4u);

    const std::string s = report.summary();
    EXPECT_NE(s.find("kicad_pcb: OK"), std::string::npos);
    EXPECT_NE(s.find("nets 2/3"), std::string::npos);
    EXPECT_NE(s.find("segs 1/1"), std::string::npos);
}

TEST(KiCadImporterTest, ImportFailurePopulatesError) {
    KiCadImporter importer;
    Board board;
    FidelityReport report;
    EXPECT_FALSE(importer.import_string("garbage not sexpr", board, report));
    EXPECT_FALSE(report.parsed_ok);
    EXPECT_FALSE(report.error_message.empty());

    const std::string s = report.summary();
    EXPECT_NE(s.find("FAILED"), std::string::npos);
}

// ============================================================
// KiCadParser：圆/八边形/椭圆 pad 形状映射 + 不支持图元清单
// ============================================================

TEST(KiCadPcbParserTest, PadShapeMappingAndUnsupportedKinds) {
    // 显式覆盖 circle/oval/octagon 形状；同时混入 gr_text 与 gr_arc（应列入 unsupported）。
    const std::string text = R"SX(
(kicad_pcb
  (net 1 "N1")
  (footprint "Demo:shapes" (layer "F.Cu") (at 0 0)
    (pad "A" thru_hole circle (at 0 0) (size 1.0 1.0) (layers "F.Cu") (net 1 "N1"))
    (pad "B" thru_hole oval   (at 2 0) (size 1.0 1.0) (layers "F.Cu") (net 1 "N1"))
    (pad "C" thru_hole octagon (at 4 0) (size 1.0 1.0) (layers "F.Cu") (net 1 "N1"))
    (pad "D" smd roundrect     (at 6 0) (size 1.0 1.0) (layers "F.Cu") (net 1 "N1"))
  )
  (gr_text "hello" (at 0 0) (layer "F.SilkS"))
  (gr_arc (start 0 0) (end 5 5) (angle 90) (layer "Edge.Cuts"))
)
)SX";

    KiCadPcbParser parser;
    Board board;
    KiCadPcbParseStats stats;
    ASSERT_TRUE(parser.parse_string(text, board, &stats));

    ASSERT_EQ(board.pads().size(), 4u);
    EXPECT_EQ(board.pads()[0]->shape(), PadShape::CIRCLE);
    EXPECT_EQ(board.pads()[1]->shape(), PadShape::OVAL);
    EXPECT_EQ(board.pads()[2]->shape(), PadShape::OCTAGON);
    EXPECT_EQ(board.pads()[3]->shape(), PadShape::RECT);  // roundrect → RECT

    // CIRCLE pad：高度应被强制等于宽度
    EXPECT_EQ(board.pads()[0]->height(), board.pads()[0]->width());

    // unsupported_kinds 含 gr_text 与 gr_arc
    bool has_text = false, has_arc = false;
    for (const auto& u : stats.unsupported_kinds) {
        if (u.find("gr_text") != std::string::npos) has_text = true;
        if (u.find("gr_arc")  != std::string::npos) has_arc  = true;
    }
    EXPECT_TRUE(has_text);
    EXPECT_TRUE(has_arc);
}
