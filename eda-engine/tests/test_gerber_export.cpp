// tests/test_gerber_export.cpp
//
// P13 制造输出 · GerberExporter 端到端导出测试。
//
// 覆盖：
//   - export_all：简单 Board（2 Track + 4 Pad + 2 Via + outline）→ ≥6 文件
//     · 每个文件存在且非空（verify_roundtrip）
//     · outline.gbr 含板框角点坐标
//     · Excellon .drl 含刀具声明（T1C...）
//     · Pick&Place .csv 含 "Designator" 表头
//     · 文件名约定：{project}_{layer}.gbr（如 myboard_F_Cu.gbr）
//   - Signal：file_written 每文件触发一次；export_completed 触发一次（success=true）
//   - verify_roundtrip：从未导出返回 false；导出后返回 true
//   - 目录自动创建：output_dir 不存在时由 export_all 创建
//   - sanitize_layer_id：点号 → 下划线
//
// 数据模型：
//   - Board 内部坐标 nm（1mm = 1_000_000nm）；Gerber 输出 3:4 精度（mm*1e4）。
//   - 测试 Board：50×30mm 板框 + F.Cu/B.Cu + 2 Track + 4 Pad（F.Cu）+ 2 Through Via + 1 Footprint。
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_gerber_export test_gerber_export.cpp)
//   target_include_directories(test_gerber_export PRIVATE ${EDA_ROOT})
//   target_link_libraries(test_gerber_export PRIVATE eda_manufacturing eda_pcb gtest_main)
//   gtest_discover_tests(test_gerber_export)

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "core/object/callable.h"
#include "eda/geometry/types.h"
#include "eda/manufacturing/excellon_writer.h"
#include "eda/manufacturing/gerber_export.h"
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

// 构造矩形板框 Polygon（4 顶点闭合）。
Polygon make_rect_outline(Coord x0, Coord y0, Coord x1, Coord y1) {
    Polygon p;
    p.outer = {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1}};
    return p;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// 构造标准测试 Board：50×30mm 板框 + F.Cu/B.Cu + 2 Track + 4 Pad + 2 Via + 1 Footprint。
// 刻意用整数 mm 坐标，便于精确断言 Gerber 3:4 精度字符串。
void populate_sample_board(Board& b) {
    b.set_name("myboard");
    b.set_outline(make_rect_outline(0, 0, 50 * kMm, 30 * kMm));
    b.add_layer(Layer("F.Cu", LayerType::COPPER, 0));
    b.add_layer(Layer("B.Cu", LayerType::COPPER, 1));

    // Track 1：F.Cu 上 (1,1) → (10,1)，宽 0.2mm
    b.add_track(
        Path{{Point{1 * kMm, 1 * kMm}, Point{10 * kMm, 1 * kMm}}, 200 * 1000},
        "F.Cu");
    // Track 2：F.Cu 上 (10,1) → (10,20)，宽 0.2mm
    b.add_track(
        Path{{Point{10 * kMm, 1 * kMm}, Point{10 * kMm, 20 * kMm}}, 200 * 1000},
        "F.Cu");

    // Pad 1/2：RECT 0.5×0.5mm @ (5,5) / (5,10)
    b.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000,
              PadShape::RECT, "F.Cu", "1");
    b.add_pad(Point{5 * kMm, 10 * kMm}, 500 * 1000, 500 * 1000,
              PadShape::RECT, "F.Cu", "2");
    // Pad 3/4：CIRCLE 0.6mm @ (40,5) / (40,25)
    b.add_pad(Point{40 * kMm, 5 * kMm}, 600 * 1000, 600 * 1000,
              PadShape::CIRCLE, "F.Cu", "3");
    b.add_pad(Point{40 * kMm, 25 * kMm}, 600 * 1000, 600 * 1000,
              PadShape::CIRCLE, "F.Cu", "4");

    // Via 1：Through @ (25,15)，drill=0.3mm，pad=0.6mm
    b.add_via(Point{25 * kMm, 15 * kMm}, 300 * 1000, 600 * 1000,
              "F.Cu", "B.Cu");
    // Via 2：Through @ (30,15)，drill=0.4mm，pad=0.7mm（不同直径 → T2）
    b.add_via(Point{30 * kMm, 15 * kMm}, 400 * 1000, 700 * 1000,
              "F.Cu", "B.Cu");

    // Footprint：U1 @ (20,12)，旋转 0°，Top 层
    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "SOIC-8";
    fp.refdes = "U1";
    fp.position = Point{20 * kMm, 12 * kMm};
    fp.rotation = 0.0;
    fp.flipped = false;
    b.add_footprint(fp);
}

// 生成一个保证唯一的临时目录路径（不创建）。
std::string make_unique_temp_dir(const char* tag) {
    // 使用进程级随机源（gtest 可并行运行，避免固定路径冲突）
    static std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "gerber_export_" << tag << "_" << rng();
    const auto tmp = std::filesystem::temp_directory_path() / oss.str();
    return tmp.string();
}

// 读小文件全文到 string（测试辅助）。
std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// RAII：测试结束自动清理临时目录。
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::string p) : path(std::move(p)) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

// ============================================================
// sanitize_layer_id
// ============================================================

TEST(GerberExportSanitizeTest, DotReplacedWithUnderscore) {
    EXPECT_EQ(GerberExporter::sanitize_layer_id("F.Cu"), "F_Cu");
    EXPECT_EQ(GerberExporter::sanitize_layer_id("B.Cu"), "B_Cu");
    EXPECT_EQ(GerberExporter::sanitize_layer_id("F.SilkS"), "F_SilkS");
    EXPECT_EQ(GerberExporter::sanitize_layer_id("F.Mask"), "F_Mask");
}

TEST(GerberExportSanitizeTest, SpacesAndSpecialCharsReplaced) {
    EXPECT_EQ(GerberExporter::sanitize_layer_id("my board"), "my_board");
    EXPECT_EQ(GerberExporter::sanitize_layer_id("In1.Cu"), "In1_Cu");
    // 已安全字符保留
    EXPECT_EQ(GerberExporter::sanitize_layer_id("F_Cu-1"), "F_Cu-1");
}

TEST(GerberExportSanitizeTest, EmptyBecomesPlaceholder) {
    EXPECT_EQ(GerberExporter::sanitize_layer_id(""), "layer");
}

// ============================================================
// export_all —— 文件落盘与命名约定
// ============================================================

TEST(GerberExportTest, ExportAllWritesAllExpectedFiles) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("all");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);

    ASSERT_TRUE(result.success) << "error: " << result.error;
    EXPECT_GT(result.total_files, 0);
    EXPECT_EQ(result.total_files, static_cast<int>(result.files_written.size()));

    // 预期至少 6 个文件：2 copper + 2 mask + 1 silk + 1 outline + 1 drill + 1 csv = 8
    EXPECT_GE(result.files_written.size(), 6u);

    // 关键文件应按命名约定出现在列表中
    const auto has = [&](const std::string& suffix) {
        for (const auto& f : result.files_written) {
            if (f.find(suffix) != std::string::npos) return true;
        }
        return false;
    };
    EXPECT_TRUE(has("myboard_F_Cu.gbr"));
    EXPECT_TRUE(has("myboard_B_Cu.gbr"));
    EXPECT_TRUE(has("myboard_F_Mask.gbr"));
    EXPECT_TRUE(has("myboard_B_Mask.gbr"));
    EXPECT_TRUE(has("myboard_F_SilkS.gbr"));
    EXPECT_TRUE(has("myboard_outline.gbr"));
    EXPECT_TRUE(has("myboard_drl.drl"));
    EXPECT_TRUE(has("myboard_pickplace.csv"));
}

TEST(GerberExportTest, AllWrittenFilesAreNonEmpty) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("nonempty");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success) << "error: " << result.error;

    for (const auto& name : result.files_written) {
        const auto path = std::filesystem::path(dir_str) / name;
        ASSERT_TRUE(std::filesystem::exists(path))
            << "missing file: " << path.string();
        const std::uintmax_t sz = std::filesystem::file_size(path);
        EXPECT_GT(sz, 0u) << "empty file: " << path.string();
    }
}

TEST(GerberExportTest, OutlineGerberContainsBoardCorners) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("outline");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success);

    const std::string outline =
        read_file(std::filesystem::path(dir_str) / "myboard_outline.gbr");
    // 板框 50×30mm，四个角点（3:4 精度：50mm=500000, 30mm=300000）
    EXPECT_TRUE(contains(outline, "X500000Y0"));
    EXPECT_TRUE(contains(outline, "X500000Y300000"));
    EXPECT_TRUE(contains(outline, "X0Y300000"));
    EXPECT_TRUE(contains(outline, "M02*"));
}

TEST(GerberExportTest, ExcellonDrillContainsToolDeclaration) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("drl");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success);

    const std::string drl =
        read_file(std::filesystem::path(dir_str) / "myboard_drl.drl");
    // 两把刀：drill=0.3mm (T1C0.30) / drill=0.4mm (T2C0.40)
    EXPECT_TRUE(contains(drl, "M48*"));
    EXPECT_TRUE(contains(drl, "T1C0.30*"));
    EXPECT_TRUE(contains(drl, "T2C0.40*"));
    EXPECT_TRUE(contains(drl, "M30*"));
}

TEST(GerberExportTest, PickPlaceCsvContainsDesignatorHeader) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("csv");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success);

    const std::string csv =
        read_file(std::filesystem::path(dir_str) / "myboard_pickplace.csv");
    EXPECT_TRUE(contains(csv, "Designator"));
    EXPECT_TRUE(contains(csv, "U1,SOIC-8"));
}

TEST(GerberExportTest, CopperLayerGerberContainsTracksAndPads) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("copper");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success);

    const std::string fcu =
        read_file(std::filesystem::path(dir_str) / "myboard_F_Cu.gbr");
    // 走线 D01 draw（Track 起点 (1,1) → 终点 (10,1)）
    EXPECT_TRUE(contains(fcu, "X10000Y10000D02*"));
    EXPECT_TRUE(contains(fcu, "X100000Y10000D01*"));
    // Pad1 @ (5,5) D03 flash
    EXPECT_TRUE(contains(fcu, "X50000Y50000D03*"));
}

// ============================================================
// verify_roundtrip
// ============================================================

TEST(GerberExportTest, VerifyRoundtripTrueAfterExport) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("verify_ok");
    TempDir guard(dir_str);

    GerberExporter ex;
    ASSERT_TRUE(ex.export_all(b, dir_str).success);

    EXPECT_TRUE(ex.verify_roundtrip(dir_str));
}

TEST(GerberExportTest, VerifyRoundtripFalseWhenNeverExported) {
    GerberExporter ex;
    EXPECT_FALSE(ex.verify_roundtrip("."));
}

TEST(GerberExportTest, VerifyRoundtripFalseWhenFileTruncated) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("verify_bad");
    TempDir guard(dir_str);

    GerberExporter ex;
    ASSERT_TRUE(ex.export_all(b, dir_str).success);
    ASSERT_TRUE(ex.verify_roundtrip(dir_str));

    // 把其中一个文件清空 → verify 应失败
    const auto target =
        std::filesystem::path(dir_str) / "myboard_pickplace.csv";
    std::ofstream truncate(target, std::ios::trunc);
    truncate.close();
    EXPECT_FALSE(ex.verify_roundtrip(dir_str));
}

// ============================================================
// 信号
// ============================================================

TEST(GerberExportSignalTest, FileWrittenFiresPerFile) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("sig_file");
    TempDir guard(dir_str);

    GerberExporter ex;
    std::vector<std::string> seen;
    ex.file_written.connect([&seen](std::string f) {
        seen.push_back(f);
    });

    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success);

    // file_written 触发次数 = 写入文件数
    EXPECT_EQ(seen.size(), result.files_written.size());
    // 内容一致（按写入顺序）
    EXPECT_EQ(seen, result.files_written);
}

TEST(GerberExportSignalTest, ExportCompletedFiresOnceWithSuccess) {
    Board b;
    populate_sample_board(b);

    const std::string dir_str = make_unique_temp_dir("sig_done");
    TempDir guard(dir_str);

    GerberExporter ex;
    int call_count = 0;
    ExportResult captured{};
    ex.export_completed = [&](const ExportResult& r) {
        ++call_count;
        captured = r;
    };

    const ExportResult result = ex.export_all(b, dir_str);

    EXPECT_EQ(call_count, 1);
    EXPECT_TRUE(captured.success);
    EXPECT_EQ(captured.total_files, result.total_files);
    EXPECT_EQ(captured.files_written, result.files_written);
}

// ============================================================
// 目录自动创建
// ============================================================

TEST(GerberExportTest, OutputDirAutoCreatedWhenMissing) {
    Board b;
    populate_sample_board(b);

    // 嵌套不存在的目录
    const std::string dir_str =
        (std::filesystem::path(make_unique_temp_dir("nested")) / "sub" / "deep").string();
    TempDir guard(dir_str);

    ASSERT_FALSE(std::filesystem::exists(dir_str));

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);
    ASSERT_TRUE(result.success) << "error: " << result.error;
    EXPECT_TRUE(std::filesystem::exists(dir_str));
    EXPECT_TRUE(ex.verify_roundtrip(dir_str));
}

// ============================================================
// 空板导出（仍应成功，文件数较少但非零）
// ============================================================

TEST(GerberExportTest, EmptyBoardStillProducesOutlineDrillAndCsv) {
    Board b;  // 默认空板：无 outline / 无 layer / 无图元
    b.set_name("empty");

    const std::string dir_str = make_unique_temp_dir("empty");
    TempDir guard(dir_str);

    GerberExporter ex;
    const ExportResult result = ex.export_all(b, dir_str);

    // 空板也应成功：至少写出 outline + drl + csv 三个文件
    EXPECT_TRUE(result.success) << "error: " << result.error;
    EXPECT_GE(result.files_written.size(), 3u);

    // 文件名前缀来自 Board.name() = "empty"
    bool has_outline = false;
    for (const auto& f : result.files_written) {
        if (f.find("empty_outline.gbr") != std::string::npos) has_outline = true;
    }
    EXPECT_TRUE(has_outline);

    // verify 通过（空 Gerber 头尾仍非空）
    EXPECT_TRUE(ex.verify_roundtrip(dir_str));
}
