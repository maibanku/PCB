// P5 Task 5 · 工程打包归档（.edapkg）测试。
//
// 覆盖（对齐任务要求）：
//   - pack -> unpack -> 树结构相等（project.toml 字节级 round-trip）
//   - 含全部依赖：lib:// 引用冻结到 manifest.libraries[]；libs/ 下文件作为普通条目嵌入
//   - manifest 完整：schema_version / eda_version / project_uuid / signature / libs / resources 齐全
//   - verify 通过：fresh archive 一致性校验返回 true；corrupted 返回 false
//   - 离线可打开：解包后 project.toml 引用的全部 res:// 文件均存在
//   - 三种模式（FULL / PRODUCTION / SNAPSHOT）按预期筛选
//   - ArchiveManifest 纯序列化：round-trip / 确定性输出 / 字典序 / lib 引用解析
//
// 依赖：eda/project/archive.h + archive_manifest.h + project.h。
// 不改 CMakeLists.txt：test_archive 由主循环加入 tests/CMakeLists（eda_project 链接）。
//
// 注：所有用例使用独立临时工程目录，避免相互污染（与 test_snapshot 风格一致）。

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "eda/project/archive.h"
#include "eda/project/archive_manifest.h"
#include "eda/project/project.h"

using namespace eda;

namespace {

// ============================================================================
// 辅助：每个用例独立临时目录的 fixture
// ============================================================================

class ArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path() /
                    ("eda_archive_test_" + std::to_string(counter_++));
        std::filesystem::create_directories(base, ec);
        dir_ = base.string();
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    const std::string& dir() const { return dir_; }

private:
    std::string dir_;
    static int counter_;
};
int ArchiveTest::counter_ = 0;

// ============================================================================
// 辅助：文件写入 / 读取
// ============================================================================

void write_file(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    if (fs::path p(path); p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// ============================================================================
// 辅助：构造示例工程目录（project.toml + sheets/ + boards/ + attachments/ + libs/）
// ============================================================================

struct SampleProject {
    std::string dir;             // 工程根
    std::string proj_toml_path;  // project.toml 全路径
    int board_count = 0;
    int page_count = 0;
    int dependency_count = 0;
};

SampleProject make_sample_project(const std::string& root,
                                  const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(root, ec);

    // 构造工程树
    ProjectTree tree;
    tree.root().set_name(name);
    tree.root().add_dependency("lib://standard@1.2.0");
    tree.root().add_dependency("lib://mcu@0.4.1");
    auto& b = tree.root().add_board("Main", BoardType::MAIN);
    b.add_page("S1", PageType::SCHEMATIC, "res://sheets/s1.sch.toml");
    b.add_page("P1", PageType::PCB, "res://boards/p1.pcb.toml");

    const std::string proj_toml = (fs::path(root) / "project.toml").string();
    tree.save(proj_toml);

    // 子目录 + 引用文件
    fs::create_directories(fs::path(root) / "sheets", ec);
    fs::create_directories(fs::path(root) / "boards", ec);
    fs::create_directories(fs::path(root) / "attachments", ec);
    fs::create_directories(fs::path(root) / "libs", ec);
    fs::create_directories(fs::path(root) / "output", ec);

    write_file((fs::path(root) / "sheets/s1.sch.toml").string(),
               "# schematic s1\n");
    write_file((fs::path(root) / "boards/p1.pcb.toml").string(),
               "# pcb p1\n");
    write_file((fs::path(root) / "attachments/datasheet.pdf").string(),
               "%PDF-1.4 fake datasheet\n");
    write_file((fs::path(root) / "libs/standard.edalib").string(),
               "standard library content v1.2.0\n");
    write_file((fs::path(root) / "output/board.gbr").string(),
               "G76*\n");  // 生产输出（Gerber 片段）

    SampleProject info;
    info.dir = root;
    info.proj_toml_path = proj_toml;
    info.board_count = 1;
    info.page_count = 2;
    info.dependency_count = 2;
    return info;
}

// 构造示例 ArchiveManifest（纯值，无 FS 依赖）
ArchiveManifest make_sample_manifest() {
    ArchiveManifest m;
    m.set_eda_version("0.1.0");
    m.set_created_time(1754496000);
    m.set_project_uuid("550e8400-e29b-41d4-a716-446655440000");
    m.set_signature(std::string());  // 占位空串
    m.add_library_from_ref("lib://standard@1.2.0");
    m.add_library_from_ref("lib://mcu@0.4.1");
    m.add_resource("res://project.toml", 412);
    m.add_resource("res://sheets/s1.sch.toml", 56);
    return m;
}

}  // namespace

// ============================================================================
// ArchiveMode 字符串映射
// ============================================================================

TEST(ArchiveModeTest, ToStringView) {
    EXPECT_EQ(to_string_view(ArchiveMode::FULL), "full");
    EXPECT_EQ(to_string_view(ArchiveMode::PRODUCTION), "production");
    EXPECT_EQ(to_string_view(ArchiveMode::SNAPSHOT), "snapshot");
}

TEST(ArchiveModeTest, ParseRoundTrip) {
    EXPECT_EQ(parse_archive_mode("full"), ArchiveMode::FULL);
    EXPECT_EQ(parse_archive_mode("production"), ArchiveMode::PRODUCTION);
    EXPECT_EQ(parse_archive_mode("snapshot"), ArchiveMode::SNAPSHOT);
}

TEST(ArchiveModeTest, ParseUnknownDefaultsToFull) {
    EXPECT_EQ(parse_archive_mode("unknown"), ArchiveMode::FULL);
    EXPECT_EQ(parse_archive_mode(""), ArchiveMode::FULL);
}

// ============================================================================
// ArchiveManifest —— 纯序列化（不涉 FS）
// ============================================================================

TEST(ArchiveManifestTest, SerializeDeserializeRoundTrip) {
    const ArchiveManifest a = make_sample_manifest();
    const std::string text = a.serialize();
    EXPECT_FALSE(text.empty());

    ArchiveManifest b;
    ASSERT_TRUE(b.deserialize(text));

    // 序列化为规范形式后比较（确定性输出：字段顺序固定、libraries/resources
    // 按字典序）。注意 operator== 对 libraries / resources 顺序敏感，而
    // serialize 会重排为字典序——故 round-trip 用规范形式比较最稳健。
    EXPECT_EQ(a.serialize(), b.serialize());

    // 关键标量字段直接核对
    EXPECT_EQ(a.schema_version(), b.schema_version());
    EXPECT_EQ(a.eda_version(), b.eda_version());
    EXPECT_EQ(a.created_time(), b.created_time());
    EXPECT_EQ(a.project_uuid(), b.project_uuid());
    EXPECT_EQ(a.signature(), b.signature());
    EXPECT_EQ(a.libraries().size(), b.libraries().size());
    EXPECT_EQ(a.resources().size(), b.resources().size());
}

TEST(ArchiveManifestTest, SerializeDeterministicOutput) {
    const ArchiveManifest a = make_sample_manifest();
    const ArchiveManifest b = make_sample_manifest();
    EXPECT_EQ(a.serialize(), b.serialize());  // 字节级一致
}

TEST(ArchiveManifestTest, SchemaVersionDefaultsToOne) {
    ArchiveManifest m;
    EXPECT_EQ(m.schema_version(), 1);
}

TEST(ArchiveManifestTest, SignaturePlaceholderEmpty) {
    ArchiveManifest m;
    EXPECT_TRUE(m.signature().empty());
    const std::string text = m.serialize();
    EXPECT_NE(text.find("signature = \"\""), std::string::npos);
}

TEST(ArchiveManifestTest, LibrariesSortedByName) {
    ArchiveManifest m;
    m.add_library_from_ref("lib://zeta@1.0");
    m.add_library_from_ref("lib://alpha@2.0");
    m.add_library_from_ref("lib://mid@0.1");

    const std::string text = m.serialize();
    const auto pos_a = text.find("alpha");
    const auto pos_m = text.find("\"mid");
    const auto pos_z = text.find("zeta");
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_m, std::string::npos);
    ASSERT_NE(pos_z, std::string::npos);
    EXPECT_LT(pos_a, pos_m);
    EXPECT_LT(pos_m, pos_z);
}

TEST(ArchiveManifestTest, ResourcesSortedByPath) {
    ArchiveManifest m;
    m.add_resource("res://zzz.toml", 10);
    m.add_resource("res://aaa.toml", 20);
    m.add_resource("res://mmm.toml", 30);

    const std::string text = m.serialize();
    const auto pos_a = text.find("res://aaa.toml");
    const auto pos_m = text.find("res://mmm.toml");
    const auto pos_z = text.find("res://zzz.toml");
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_m, std::string::npos);
    ASSERT_NE(pos_z, std::string::npos);
    EXPECT_LT(pos_a, pos_m);
    EXPECT_LT(pos_m, pos_z);
}

TEST(ArchiveManifestTest, ParseLibRefWithVersion) {
    ArchiveManifest m;
    m.add_library_from_ref("lib://standard@1.2.0");
    ASSERT_EQ(m.libraries().size(), 1u);
    EXPECT_EQ(m.libraries()[0].name, "standard");
    EXPECT_EQ(m.libraries()[0].version, "1.2.0");
    EXPECT_EQ(m.libraries()[0].ref, "lib://standard@1.2.0");
}

TEST(ArchiveManifestTest, ParseLibRefWithoutVersion) {
    ArchiveManifest m;
    m.add_library_from_ref("lib://unversioned");
    ASSERT_EQ(m.libraries().size(), 1u);
    EXPECT_EQ(m.libraries()[0].name, "unversioned");
    EXPECT_TRUE(m.libraries()[0].version.empty());
    EXPECT_EQ(m.libraries()[0].ref, "lib://unversioned");
}

TEST(ArchiveManifestTest, ManifestLoaderSaveLoadFile) {
    static int manifest_counter = 0;
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("eda_manifest_io_" + std::to_string(manifest_counter++) + ".toml"))
            .string();
    const ArchiveManifest a = make_sample_manifest();
    ASSERT_TRUE(ArchiveManifestLoader::save(a, path));

    ArchiveManifest b;
    ASSERT_TRUE(ArchiveManifestLoader::load_into(b, path));
    // 用规范序列化形式比较（operator== 对 libraries 顺序敏感；serialize 重排为字典序）
    EXPECT_EQ(a.serialize(), b.serialize());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ============================================================================
// Archive —— pack / unpack / verify（FS 集成）
// ============================================================================

TEST_F(ArchiveTest, PackUnpackRoundTripTreeEqual) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "DemoProject");

    ProjectTree original;
    ASSERT_TRUE(original.load(sp.proj_toml_path));

    const std::string archive = dir() + "/proj.edapkg";
    ArchiveOptions opts;
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));

    const std::string unpack_dir = dir() + "/unpacked";
    ASSERT_TRUE(Archive::unpack(archive, unpack_dir));

    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(unpack_dir + "/project.toml"));

    // 字节级 round-trip：project.toml 内容相同 -> 树深比较相等
    EXPECT_EQ(reloaded.root(), original.root());
}

TEST_F(ArchiveTest, ArchiveMagicCorrect) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "MagicProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    std::ifstream f(archive, std::ios::binary);
    ASSERT_TRUE(f.good());
    char magic[Archive::kMagicLen];
    f.read(magic, static_cast<std::streamsize>(Archive::kMagicLen));
    ASSERT_EQ(f.gcount(), static_cast<std::streamsize>(Archive::kMagicLen));
    EXPECT_EQ(std::memcmp(magic, Archive::kMagic, Archive::kMagicLen), 0);
    EXPECT_EQ(Archive::kMagicLen, 8u);
}

TEST_F(ArchiveTest, VerifyPassesOnFreshArchive) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "VerifyProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));
    EXPECT_TRUE(Archive::verify(archive));
}

TEST_F(ArchiveTest, VerifyFailsOnMissingFile) {
    EXPECT_FALSE(Archive::verify(dir() + "/nonexistent.edapkg"));
}

TEST_F(ArchiveTest, VerifyFailsOnCorruptedMagic) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "CorruptProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    // 改写首字节（破坏 magic）
    std::string bytes = read_file(archive);
    ASSERT_GT(bytes.size(), 4u);
    bytes[0] = 'X';
    write_file(archive, bytes);

    EXPECT_FALSE(Archive::verify(archive));
    EXPECT_FALSE(Archive::unpack(archive, dir() + "/bad_unpack"));
}

TEST_F(ArchiveTest, VerifyFailsOnTruncated) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "TruncProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    // 截断到 magic + 4 字节（仅 entry_count，无任何条目）
    std::string bytes = read_file(archive);
    ASSERT_GT(bytes.size(), Archive::kMagicLen + 4u);
    bytes.resize(Archive::kMagicLen + 4);
    const std::string trunc = dir() + "/trunc.edapkg";
    write_file(trunc, bytes);

    EXPECT_FALSE(Archive::verify(trunc));
}

TEST_F(ArchiveTest, ReadManifestReturnsParsedManifest) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "ManifestProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    ArchiveManifest m;
    ASSERT_TRUE(Archive::read_manifest(archive, m));

    ProjectTree tree;
    ASSERT_TRUE(tree.load(sp.proj_toml_path));
    EXPECT_EQ(m.project_uuid(), tree.root().uuid());
}

TEST_F(ArchiveTest, ManifestComplete) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "CompleteProj");
    const std::string archive = dir() + "/proj.edapkg";
    ArchiveOptions opts;
    opts.eda_version = "9.9.9";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));

    ArchiveManifest m;
    ASSERT_TRUE(Archive::read_manifest(archive, m));

    EXPECT_EQ(m.schema_version(), 1);
    EXPECT_EQ(m.eda_version(), "9.9.9");
    EXPECT_GT(m.created_time(), 0);
    EXPECT_FALSE(m.project_uuid().empty());
    EXPECT_TRUE(m.signature().empty());  // 占位

    // 全部依赖都登记
    EXPECT_EQ(m.libraries().size(),
              static_cast<size_t>(sp.dependency_count));

    // 全部工程文件都登记为资源（不含 manifest.toml 自身）
    EXPECT_EQ(m.resources().size(), 6u);  // project.toml + 2 sch/pcb + pdf + lib + gbr
}

TEST_F(ArchiveTest, AllDependenciesFrozenInManifest) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "DepProj");
    const std::string archive = dir() + "/dep.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    ArchiveManifest m;
    ASSERT_TRUE(Archive::read_manifest(archive, m));

    bool found_standard = false;
    bool found_mcu = false;
    for (const LibraryVersion& lib : m.libraries()) {
        if (lib.name == "standard" && lib.version == "1.2.0" &&
            lib.ref == "lib://standard@1.2.0") {
            found_standard = true;
        }
        if (lib.name == "mcu" && lib.version == "0.4.1" &&
            lib.ref == "lib://mcu@0.4.1") {
            found_mcu = true;
        }
    }
    EXPECT_TRUE(found_standard) << "standard@1.2.0 未冻结到 manifest";
    EXPECT_TRUE(found_mcu) << "mcu@0.4.1 未冻结到 manifest";
}

TEST_F(ArchiveTest, AllDependenciesEmbeddedAsFiles) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "EmbedProj");
    const std::string archive = dir() + "/embed.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    const std::string unpack_dir = dir() + "/unpacked";
    ASSERT_TRUE(Archive::unpack(archive, unpack_dir));

    // libs/ 下文件作为普通条目嵌入，解包后存在
    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(unpack_dir + "/libs/standard.edalib"));
    EXPECT_TRUE(fs::exists(unpack_dir + "/attachments/datasheet.pdf"));
    EXPECT_TRUE(fs::exists(unpack_dir + "/sheets/s1.sch.toml"));
    EXPECT_TRUE(fs::exists(unpack_dir + "/boards/p1.pcb.toml"));
}

TEST_F(ArchiveTest, OfflineOpenableAllResPathsExist) {
    // 解包后无外部依赖：project.toml 内全部 res:// 引用都能在解包目录内解析到文件
    const SampleProject sp = make_sample_project(dir() + "/proj", "OfflineProj");
    const std::string archive = dir() + "/offline.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    const std::string unpack_dir = dir() + "/unpacked";
    ASSERT_TRUE(Archive::unpack(archive, unpack_dir));

    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(unpack_dir + "/project.toml"));

    namespace fs = std::filesystem;
    int checked = 0;
    for (const Board& b : reloaded.root().boards()) {
        for (const Page& p : b.pages()) {
            const std::string& res_path = p.path();
            ASSERT_TRUE(res_path.starts_with("res://"));
            const std::string rel = res_path.substr(6);
            const fs::path native = fs::path(unpack_dir) / rel;
            EXPECT_TRUE(fs::exists(native))
                << "离线解包后缺失: " << native.string();
            ++checked;
        }
    }
    EXPECT_GT(checked, 0) << "测试未实际校验任何 res:// 路径";
}

TEST_F(ArchiveTest, PackFailsWithoutProjectToml) {
    namespace fs = std::filesystem;
    const std::string empty_proj = dir() + "/empty";
    std::error_code ec;
    fs::create_directories(empty_proj, ec);
    // 没有 project.toml
    EXPECT_FALSE(Archive::pack(empty_proj, dir() + "/x.edapkg",
                               ArchiveOptions{}));
}

TEST_F(ArchiveTest, PackFailsOnNonexistentDir) {
    EXPECT_FALSE(Archive::pack(dir() + "/no_such_dir",
                               dir() + "/x.edapkg",
                               ArchiveOptions{}));
}

TEST_F(ArchiveTest, UnpackCreatesOutputDir) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "MkDirProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    namespace fs = std::filesystem;
    const std::string unpack_dir = dir() + "/nested/deep/unpacked";
    EXPECT_FALSE(fs::exists(unpack_dir));
    ASSERT_TRUE(Archive::unpack(archive, unpack_dir));
    EXPECT_TRUE(fs::exists(unpack_dir + "/project.toml"));
}

// ============================================================================
// 三种归档模式
// ============================================================================

TEST_F(ArchiveTest, FullModeIncludesAllFiles) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "FullProj");
    const std::string archive = dir() + "/full.edapkg";
    ArchiveOptions opts;
    opts.mode = ArchiveMode::FULL;
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));

    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(dir() + "/out/project.toml"));
    EXPECT_TRUE(fs::exists(dir() + "/out/sheets/s1.sch.toml"));
    EXPECT_TRUE(fs::exists(dir() + "/out/boards/p1.pcb.toml"));
    EXPECT_TRUE(fs::exists(dir() + "/out/attachments/datasheet.pdf"));
    EXPECT_TRUE(fs::exists(dir() + "/out/libs/standard.edalib"));
    EXPECT_TRUE(fs::exists(dir() + "/out/output/board.gbr"));
}

TEST_F(ArchiveTest, SnapshotModeOnlyProjectToml) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "SnapProj");
    const std::string archive = dir() + "/snap.edapkg";
    ArchiveOptions opts;
    opts.mode = ArchiveMode::SNAPSHOT;
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));

    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(dir() + "/out/project.toml"));
    // 其余文件不应出现
    EXPECT_FALSE(fs::exists(dir() + "/out/sheets/s1.sch.toml"));
    EXPECT_FALSE(fs::exists(dir() + "/out/libs/standard.edalib"));

    // manifest 资源应只含 project.toml
    ArchiveManifest m;
    ASSERT_TRUE(Archive::read_manifest(archive, m));
    EXPECT_EQ(m.resources().size(), 1u);
    EXPECT_EQ(m.resources()[0].path, "res://project.toml");
}

TEST_F(ArchiveTest, ProductionModeIncludesOutputAndProjectToml) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "ProdProj");
    const std::string archive = dir() + "/prod.edapkg";
    ArchiveOptions opts;
    opts.mode = ArchiveMode::PRODUCTION;
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));

    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(dir() + "/out/project.toml"));
    EXPECT_TRUE(fs::exists(dir() + "/out/output/board.gbr"));  // 生产输出
    // 非生产文件被排除
    EXPECT_FALSE(fs::exists(dir() + "/out/sheets/s1.sch.toml"));
    EXPECT_FALSE(fs::exists(dir() + "/out/libs/standard.edalib"));
}

// ============================================================================
// 隐藏目录过滤
// ============================================================================

TEST_F(ArchiveTest, HiddenSnapshotsExcludedByDefault) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "HiddenProj");
    // 工程内放一个 .eda-snapshots 快照文件（应被默认排除）
    write_file(dir() + "/proj/.eda-snapshots/0001-first.toml", "# snapshot\n");
    write_file(dir() + "/proj/.git/config", "[core]\n");

    const std::string archive = dir() + "/proj.edapkg";
    ArchiveOptions opts;  // 默认 include_hidden = false
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));

    namespace fs = std::filesystem;
    EXPECT_FALSE(fs::exists(dir() + "/out/.eda-snapshots/0001-first.toml"));
    EXPECT_FALSE(fs::exists(dir() + "/out/.git/config"));
}

TEST_F(ArchiveTest, IncludeHiddenCapturesSnapshots) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "HiddenInProj");
    write_file(dir() + "/proj/.eda-snapshots/0001-first.toml", "# snapshot\n");

    const std::string archive = dir() + "/proj.edapkg";
    ArchiveOptions opts;
    opts.include_hidden = true;
    ASSERT_TRUE(Archive::pack(sp.dir, archive, opts));
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));

    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(dir() + "/out/.eda-snapshots/0001-first.toml"));
}

// ============================================================================
// 资源清单交叉校验（manifest.resources ↔ 容器条目）
// ============================================================================

TEST_F(ArchiveTest, ManifestResourcesMatchUnpackedFiles) {
    const SampleProject sp = make_sample_project(dir() + "/proj", "MatchProj");
    const std::string archive = dir() + "/proj.edapkg";
    ASSERT_TRUE(Archive::pack(sp.dir, archive, ArchiveOptions{}));

    ArchiveManifest m;
    ASSERT_TRUE(Archive::read_manifest(archive, m));

    // 每个 res:// 资源对应一个实际解包文件，且尺寸匹配
    ASSERT_TRUE(Archive::unpack(archive, dir() + "/out"));
    namespace fs = std::filesystem;
    for (const ArchiveResource& r : m.resources()) {
        ASSERT_TRUE(r.path.starts_with("res://"));
        const fs::path native = fs::path(dir() + "/out") / r.path.substr(6);
        EXPECT_TRUE(fs::exists(native)) << "缺失资源: " << r.path;
        std::error_code ec;
        const auto sz = static_cast<int64_t>(fs::file_size(native, ec));
        EXPECT_FALSE(ec) << "无法读取大小: " << r.path;
        EXPECT_EQ(sz, r.size) << "尺寸不匹配: " << r.path;
    }
}
