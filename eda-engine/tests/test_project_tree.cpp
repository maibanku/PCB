// P5 Task 1 · 工程树数据模型 + project.toml 序列化测试。
//
// 覆盖：
//   - 数据模型构造（Project > Board > Page）+ UUID 自动生成
//   - add / remove / find_by_uuid
//   - TOML 文本往返：创建 → 加板 → 加图页 → 保存 → 重新加载 → 深比较相等
//   - UUID 跨保存/加载稳定（重命名 / 移动不破坏引用）
//   - 确定性输出（同逻辑树 → 字节级一致；列表按 uuid 字典序）
//   - TOML 文本 diff 可读（关键字段、表头、转义）
//   - 旧版本兼容：缺 [dependencies] / modified_time / pages 不崩溃
//   - ProjectLoader::load -> Ref<ProjectTree> + ProjectSaver::save(path, tree*)
//   - Path 工具：join / parent / filename / extension / normalize / 协议识别
//
// 依赖：eda/project/project.h + core/io/path.h（P2 Resource 链路经由 ProjectTree 继承）。
// 注意：本文件不改 CMakeLists；集成步骤由主循环完成（追加 project.cpp 到 eda_project，
//       io/path.cpp 到 eda_core 或独立 eda_io；gtest_discover_tests 由 tests/CMakeLists 注册）。

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/io/path.h"
#include "core/object/ref.h"
#include "eda/project/project.h"

using namespace eda;

namespace {

// 临时文件辅助：返回 temp 目录下的完整路径。
std::string scratch_file(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// 写入原始文本到临时文件（用于"旧版本"工程模拟）。
bool dump_text(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

}  // namespace

// ============================================================================
// 数据模型：构造 / UUID / 类型映射
// ============================================================================

TEST(ProjectModelTest, PageDefaultsAndUuid) {
    Page p;
    EXPECT_TRUE(is_valid_uuid(p.uuid()));
    EXPECT_TRUE(p.uuid().empty() == false);
    EXPECT_EQ(p.type(), PageType::SCHEMATIC);
    EXPECT_EQ(p.modified_time(), 0);
    EXPECT_TRUE(p.path().empty());
    EXPECT_TRUE(p.name().empty());
}

TEST(ProjectModelTest, BoardDefaultsAndUuid) {
    Board b;
    EXPECT_TRUE(is_valid_uuid(b.uuid()));
    EXPECT_EQ(b.type(), BoardType::MAIN);
    EXPECT_EQ(b.page_count(), 0u);
}

TEST(ProjectModelTest, ProjectDefaultsAndSchemaVersion) {
    Project p;
    EXPECT_EQ(p.schema_version(), 1);
    EXPECT_TRUE(is_valid_uuid(p.uuid()));
    EXPECT_EQ(p.board_count(), 0u);
    EXPECT_TRUE(p.dependencies().empty());
}

TEST(ProjectModelTest, UuidGeneratedIsUnique) {
    // 同进程连续生成的 UUID 不重复（统计意义；冲突概率忽略）
    Page p1, p2, p3;
    EXPECT_NE(p1.uuid(), p2.uuid());
    EXPECT_NE(p2.uuid(), p3.uuid());
    EXPECT_NE(p1.uuid(), p3.uuid());
}

TEST(ProjectModelTest, IsValidUuidRejects) {
    EXPECT_FALSE(is_valid_uuid(""));
    EXPECT_FALSE(is_valid_uuid("abc"));
    EXPECT_FALSE(is_valid_uuid("12345678-1234-1234-1234-1234567890ab-extra"));  // 太长
    EXPECT_FALSE(is_valid_uuid("123456781234123412341234567890abz"));  // 非法字符 z
    EXPECT_TRUE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000"));
    EXPECT_TRUE(is_valid_uuid("00000000-0000-0000-0000-000000000000"));
}

TEST(ProjectModelTest, PageTypeRoundTrip) {
    EXPECT_EQ(to_string_view(PageType::SCHEMATIC), "schematic");
    EXPECT_EQ(to_string_view(PageType::PCB), "pcb");
    EXPECT_EQ(parse_page_type("schematic"), PageType::SCHEMATIC);
    EXPECT_EQ(parse_page_type("pcb"), PageType::PCB);
    EXPECT_EQ(parse_page_type("unknown"), PageType::SCHEMATIC);  // 默认降级
}

TEST(ProjectModelTest, BoardTypeRoundTrip) {
    EXPECT_EQ(to_string_view(BoardType::MAIN), "main");
    EXPECT_EQ(to_string_view(BoardType::DAUGHTER), "daughter");
    EXPECT_EQ(parse_board_type("main"), BoardType::MAIN);
    EXPECT_EQ(parse_board_type("daughter"), BoardType::DAUGHTER);
    EXPECT_EQ(parse_board_type("unknown"), BoardType::MAIN);
}

// ============================================================================
// 增删查：add / remove / find_by_uuid
// ============================================================================

TEST(ProjectModelTest, AddBoardReturnsRefAndKeepsUuid) {
    Project p;
    Board& b = p.add_board("MainBoard", BoardType::MAIN);
    EXPECT_EQ(b.name(), "MainBoard");
    EXPECT_EQ(b.type(), BoardType::MAIN);
    EXPECT_TRUE(is_valid_uuid(b.uuid()));
    EXPECT_EQ(p.board_count(), 1u);
    EXPECT_EQ(&p.board(0), &b);
    // 通过 UUID 查回同一对象
    EXPECT_EQ(p.find_board_by_uuid(b.uuid()), &b);
    EXPECT_EQ(p.find_board_by_uuid("non-existent"), nullptr);
}

TEST(ProjectModelTest, AddPageReturnsRefAndFindable) {
    Project p;
    Board& b = p.add_board("MainBoard", BoardType::MAIN);
    Page& s1 = b.add_page("Sheet1", PageType::SCHEMATIC, "res://sheets/sheet1.sch.toml");
    const std::string s1_uuid = s1.uuid();  // 拷贝：vector 后续扩容会令 &s1 失效
    Page& pcb = b.add_page("PCB1", PageType::PCB, "res://boards/pcb1.pcb.toml");
    const std::string pcb_uuid = pcb.uuid();
    EXPECT_EQ(b.page_count(), 2u);
    EXPECT_EQ(b.find_page_by_uuid(s1_uuid)->uuid(), s1_uuid);
    EXPECT_EQ(b.find_page_by_uuid(pcb_uuid)->uuid(), pcb_uuid);
    EXPECT_EQ(b.find_page_by_uuid("non-existent"), nullptr);
}

TEST(ProjectModelTest, RemoveBoardByUuid) {
    Project p;
    Board& b1 = p.add_board("B1", BoardType::MAIN);
    const std::string b1_uuid = b1.uuid();  // 拷贝：vector 扩容会令 &b1 失效
    Board& b2 = p.add_board("B2", BoardType::DAUGHTER);
    const std::string b2_uuid = b2.uuid();
    EXPECT_EQ(p.board_count(), 2u);
    EXPECT_TRUE(p.remove_board_by_uuid(b1_uuid));
    EXPECT_EQ(p.board_count(), 1u);
    EXPECT_EQ(p.board(0).uuid(), b2_uuid);
    EXPECT_FALSE(p.remove_board_by_uuid("non-existent"));
}

TEST(ProjectModelTest, RemovePageByUuid) {
    Project p;
    Board& b = p.add_board("B", BoardType::MAIN);
    Page& p1 = b.add_page("P1", PageType::SCHEMATIC, "res://a.toml");
    const std::string p1_uuid = p1.uuid();  // 拷贝：vector 扩容会令 &p1 失效
    b.add_page("P2", PageType::SCHEMATIC, "res://b.toml");
    EXPECT_EQ(b.page_count(), 2u);
    EXPECT_TRUE(b.remove_page_by_uuid(p1_uuid));
    EXPECT_EQ(b.page_count(), 1u);
    EXPECT_FALSE(b.remove_page_by_uuid(p1_uuid));
}

TEST(ProjectModelTest, DependencyManagement) {
    Project p;
    EXPECT_TRUE(p.dependencies().empty());
    p.add_dependency("lib://standard@1.2.0");
    p.add_dependency("lib://mcu@0.9.1");
    ASSERT_EQ(p.dependencies().size(), 2u);
    EXPECT_TRUE(p.remove_dependency("lib://standard@1.2.0"));
    EXPECT_EQ(p.dependencies().size(), 1u);
    EXPECT_FALSE(p.remove_dependency("lib://not-there"));
}

// ============================================================================
// TOML 往返：核心断言
// ============================================================================

TEST(ProjectTreeRoundTripTest, CreateAddBoardAddPageRoundTrip) {
    // 创建工程 → 加板 → 加图页 → 保存 → 重新加载 → 深比较相等
    // 注：序列化时 boards / pages 按 uuid 字典序输出；解析按文件顺序回填。
    // 为使"原始树"与"加载树"向量同序，这里给每个节点注入"插入顺序 == uuid 字典序"
    // 的 UUID。日常使用中顺序漂移由确定性输出测试单独覆盖。
    ProjectTree tree;
    Project& proj = tree.root();
    proj.set_name("DemoProject");
    proj.set_modified_time(1754496000);
    proj.assign_uuid_for_test("00000000-0000-0000-0000-000000000001");
    Board& main = proj.add_board("MainBoard", BoardType::MAIN);
    main.assign_uuid_for_test("10000000-0000-0000-0000-000000000001");
    main.add_page("Sheet1", PageType::SCHEMATIC, "res://sheets/sheet1.sch.toml")
        .assign_uuid_for_test("20000000-0000-0000-0000-000000000001");
    main.add_page("PCB1", PageType::PCB, "res://boards/pcb1.pcb.toml")
        .assign_uuid_for_test("20000000-0000-0000-0000-000000000002");
    Board& exp = proj.add_board("Expansion", BoardType::DAUGHTER);
    exp.assign_uuid_for_test("10000000-0000-0000-0000-000000000002");
    exp.add_page("Sheet2", PageType::SCHEMATIC, "res://sheets/sheet2.sch.toml")
        .assign_uuid_for_test("20000000-0000-0000-0000-000000000003");
    proj.add_dependency("lib://standard@1.2.0");

    const std::string path = scratch_file("p5_demo_project.toml");
    ASSERT_TRUE(tree.save(path)) << "save 应成功";

    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(path)) << "load 应成功";

    EXPECT_EQ(reloaded.root(), proj);  // 深比较：schema/uuid/name/boards/pages/deps
}

TEST(ProjectTreeRoundTripTest, UuidStableAcrossSaveLoad) {
    ProjectTree tree;
    tree.root().set_name("U");
    Board& b = tree.root().add_board("B", BoardType::MAIN);
    const std::string board_uuid = b.uuid();
    Page& p = b.add_page("S", PageType::SCHEMATIC, "res://s.sch.toml");
    const std::string page_uuid = p.uuid();

    const std::string path = scratch_file("p5_uuid_demo.toml");
    ASSERT_TRUE(tree.save(path));

    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(path));
    const Board* b2 = reloaded.find_board_by_uuid(board_uuid);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(b2->uuid(), board_uuid);
    EXPECT_EQ(b2->name(), "B");
    const Page* p2 = reloaded.find_page_by_uuid(page_uuid);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->uuid(), page_uuid);
    EXPECT_EQ(p2->name(), "S");
}

TEST(ProjectTreeRoundTripTest, FindByUuidOnTree) {
    ProjectTree tree;
    Board& b = tree.root().add_board("MainBoard", BoardType::MAIN);
    Page& p = b.add_page("Sheet1", PageType::SCHEMATIC, "res://s.sch.toml");
    EXPECT_EQ(tree.find_board_by_uuid(b.uuid()), &b);
    EXPECT_EQ(tree.find_page_by_uuid(p.uuid()), &p);
    EXPECT_EQ(tree.find_board_by_uuid("non-existent-uuid"), nullptr);
    EXPECT_EQ(tree.find_page_by_uuid("non-existent-uuid"), nullptr);
}

TEST(ProjectTreeRoundTripTest, SerializeUsesProjectTreeRootUuid) {
    ProjectTree tree;
    tree.root().set_name("X");
    tree.root().assign_uuid_for_test("aaaaaaaa-0000-0000-0000-000000000001");
    const std::string out = tree.serialize_to_string();
    EXPECT_NE(out.find("aaaaaaaa-0000-0000-0000-000000000001"), std::string::npos);
}

// ============================================================================
// 确定性输出（Git-diff 友好）
// ============================================================================

TEST(ProjectDiffTest, DeterministicOutputByteIdentical) {
    // 同一逻辑树（同 UUID）两次序列化 → 字节级一致
    ProjectTree a, b;
    for (auto* t : {&a, &b}) {
        t->root().set_name("DiffProject");
        t->root().set_modified_time(1754496000);
        t->root().assign_uuid_for_test("00000000-0000-0000-0000-000000000001");
        Board& bd = t->root().add_board("MainBoard", BoardType::MAIN);
        bd.assign_uuid_for_test("10000000-0000-0000-0000-000000000001");
        Page& p1 = bd.add_page("Sheet1", PageType::SCHEMATIC, "res://s1.sch.toml");
        p1.assign_uuid_for_test("aaaaaaaa-0000-0000-0000-000000000001");
        p1.set_modified_time(1754496000);
        Page& p2 = bd.add_page("Sheet2", PageType::SCHEMATIC, "res://s2.sch.toml");
        p2.assign_uuid_for_test("bbbbbbbb-0000-0000-0000-000000000002");
        p2.set_modified_time(1754496000);
    }
    EXPECT_EQ(a.serialize_to_string(), b.serialize_to_string());
}

TEST(ProjectDiffTest, PagesSortedByUuidInOutput) {
    // 列表按 uuid 字典序输出，避免插入顺序影响 diff
    ProjectTree t;
    t.root().set_name("S");
    t.root().assign_uuid_for_test("00000000-0000-0000-0000-000000000001");
    Board& b = t.root().add_board("Main", BoardType::MAIN);
    b.assign_uuid_for_test("10000000-0000-0000-0000-000000000001");
    Page& p1 = b.add_page("Z-Page", PageType::SCHEMATIC, "res://z.sch.toml");
    p1.assign_uuid_for_test("bbbbbbbb-0000-0000-0000-000000000000");  // 先注入再 add 下一页
    Page& p2 = b.add_page("A-Page", PageType::SCHEMATIC, "res://a.sch.toml");
    p2.assign_uuid_for_test("aaaaaaaa-0000-0000-0000-000000000000");
    const std::string out = t.serialize_to_string();
    auto pos_a = out.find("aaaaaaaa");
    auto pos_b = out.find("bbbbbbbb");
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_b, std::string::npos);
    EXPECT_LT(pos_a, pos_b);  // a 在 b 之前
}

TEST(ProjectDiffTest, BoardsSortedByUuidInOutput) {
    ProjectTree t;
    t.root().set_name("S");
    t.root().assign_uuid_for_test("00000000-0000-0000-0000-000000000001");
    Board& z = t.root().add_board("Z-Board", BoardType::MAIN);
    z.assign_uuid_for_test("bbbbbbbb-0000-0000-0000-000000000000");  // 先注入再 add 下一板
    Board& a = t.root().add_board("A-Board", BoardType::DAUGHTER);
    a.assign_uuid_for_test("aaaaaaaa-0000-0000-0000-000000000000");
    const std::string out = t.serialize_to_string();
    EXPECT_LT(out.find("aaaaaaaa"), out.find("bbbbbbbb"));
    EXPECT_LT(out.find("A-Board"), out.find("Z-Board"));  // 与 board UUID 同序
}

TEST(ProjectDiffTest, DependenciesSortedInOutput) {
    ProjectTree t;
    t.root().set_name("S");
    t.root().assign_uuid_for_test("00000000-0000-0000-0000-000000000001");
    t.root().add_dependency("lib://z-lib@2.0.0");
    t.root().add_dependency("lib://a-lib@1.0.0");
    t.root().add_dependency("lib://m-lib@1.5.0");
    const std::string out = t.serialize_to_string();
    auto pos_a = out.find("lib://a-lib");
    auto pos_m = out.find("lib://m-lib");
    auto pos_z = out.find("lib://z-lib");
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_m, std::string::npos);
    ASSERT_NE(pos_z, std::string::npos);
    EXPECT_LT(pos_a, pos_m);
    EXPECT_LT(pos_m, pos_z);
}

// ============================================================================
// TOML 文本可读性（diff 友好）
// ============================================================================

TEST(ProjectTomlTextTest, OutputContainsExpectedFields) {
    ProjectTree t;
    t.root().set_name("DemoProject");
    t.root().assign_uuid_for_test("550e8400-e29b-41d4-a716-446655440000");
    t.root().set_modified_time(1754496000);
    Board& b = t.root().add_board("MainBoard", BoardType::MAIN);
    b.assign_uuid_for_test("660e8400-e29b-41d4-a716-446655440001");
    b.add_page("Sheet1", PageType::SCHEMATIC, "res://sheets/sheet1.sch.toml")
        .assign_uuid_for_test("770e8400-e29b-41d4-a716-446655440010");

    const std::string out = t.serialize_to_string();

    EXPECT_NE(out.find("schema_version = 1"), std::string::npos);
    EXPECT_NE(out.find("name = \"DemoProject\""), std::string::npos);
    EXPECT_NE(out.find("uuid = \"550e8400-e29b-41d4-a716-446655440000\""), std::string::npos);
    EXPECT_NE(out.find("modified_time = 1754496000"), std::string::npos);
    EXPECT_NE(out.find("[[boards]]"), std::string::npos);
    EXPECT_NE(out.find("[[boards.pages]]"), std::string::npos);
    EXPECT_NE(out.find("type = \"main\""), std::string::npos);
    EXPECT_NE(out.find("type = \"schematic\""), std::string::npos);
    EXPECT_NE(out.find("path = \"res://sheets/sheet1.sch.toml\""), std::string::npos);
}

TEST(ProjectTomlTextTest, StringValuesAreEscaped) {
    ProjectTree t;
    t.root().set_name(R"(Quote" Backslash\ Newline)");
    const std::string out = t.serialize_to_string();
    EXPECT_NE(out.find("name = \"Quote\\\" Backslash\\\\ Newline\""), std::string::npos);
}

TEST(ProjectTomlTextTest, EmptyDependenciesOmitted) {
    ProjectTree t;
    t.root().set_name("X");
    const std::string out = t.serialize_to_string();
    EXPECT_EQ(out.find("dependencies"), std::string::npos);
}

TEST(ProjectTomlTextTest, StringsWithHashNotMisreadAsCommentOnReload) {
    // path 含 `#`（不是 TOML 注释，因为 # 在字符串内）应能正确往返
    ProjectTree t;
    Board& b = t.root().add_board("B", BoardType::MAIN);
    b.add_page("P", PageType::SCHEMATIC, "res://sheets/sub#v2.sch.toml");

    const std::string path = scratch_file("p5_hash_path.toml");
    ASSERT_TRUE(t.save(path));
    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(path));
    ASSERT_EQ(reloaded.root().board_count(), 1u);
    ASSERT_EQ(reloaded.root().board(0).page_count(), 1u);
    EXPECT_EQ(reloaded.root().board(0).page(0).path(), "res://sheets/sub#v2.sch.toml");
}

// ============================================================================
// 旧版本兼容：缺失字段不崩溃
// ============================================================================

TEST(ProjectLegacyTest, LoadsWithDependenciesAndModifiedTime) {
    // 旧工程可能没有 [dependencies] 与 modified_time；加载不应失败
    const std::string path = scratch_file("p5_legacy_no_deps.toml");
    const std::string text = R"(schema_version = 1
name = "Legacy"
uuid = "11111111-1111-1111-1111-111111111111"

[[boards]]
name = "B"
uuid = "22222222-2222-2222-2222-222222222222"
type = "main"
)";
    ASSERT_TRUE(dump_text(path, text));

    ProjectTree reloaded;
    ASSERT_TRUE(reloaded.load(path));
    EXPECT_EQ(reloaded.root().name(), "Legacy");
    EXPECT_EQ(reloaded.root().uuid(), "11111111-1111-1111-1111-111111111111");
    EXPECT_TRUE(reloaded.root().dependencies().empty());
    EXPECT_EQ(reloaded.root().modified_time(), 0);  // 缺失：保留默认
    ASSERT_EQ(reloaded.root().board_count(), 1u);
    const Board& b = reloaded.root().board(0);
    EXPECT_EQ(b.name(), "B");
    EXPECT_EQ(b.uuid(), "22222222-2222-2222-2222-222222222222");
    EXPECT_EQ(b.type(), BoardType::MAIN);
    EXPECT_EQ(b.page_count(), 0u);
}

TEST(ProjectLegacyTest, LoadsWithoutBoardsOrPages) {
    // 极简工程：只有顶层字段
    const std::string text = R"(schema_version = 1
name = "Empty"
uuid = "33333333-3333-3333-3333-333333333333"
modified_time = 100
)";
    Ref<ProjectTree> tree = ProjectLoader::parse(text);
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().name(), "Empty");
    EXPECT_EQ(tree->root().modified_time(), 100);
    EXPECT_EQ(tree->root().board_count(), 0u);
}

TEST(ProjectLegacyTest, UnknownKeysIgnored) {
    // 未来 schema 新增字段时，旧版本解析器应静默忽略
    const std::string text = R"(schema_version = 1
name = "Future"
uuid = "44444444-4444-4444-4444-444444444444"
future_field = "magic"
modified_time = 7

[[boards]]
name = "B"
uuid = "55555555-5555-5555-5555-555555555555"
type = "main"
new_board_field = 99

  [[boards.pages]]
  name = "S"
  uuid = "66666666-6666-6666-6666-666666666666"
  type = "schematic"
  path = "res://s.toml"
  modified_time = 8
  new_page_field = true
)";
    Ref<ProjectTree> tree = ProjectLoader::parse(text);
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().name(), "Future");
    ASSERT_EQ(tree->root().board_count(), 1u);
    EXPECT_EQ(tree->root().board(0).page_count(), 1u);
    EXPECT_EQ(tree->root().board(0).page(0).path(), "res://s.toml");
}

TEST(ProjectLegacyTest, LoadsEmptyFileAsDefault) {
    Ref<ProjectTree> tree = ProjectLoader::parse("");
    ASSERT_TRUE(tree.valid());
    EXPECT_TRUE(tree->root().name().empty());
    EXPECT_EQ(tree->root().board_count(), 0u);
    // 默认 schema_version = 1
    EXPECT_EQ(tree->root().schema_version(), 1);
}

// ============================================================================
// ProjectLoader / ProjectSaver 门面 API
// ============================================================================

TEST(ProjectFacadeTest, LoaderReturnsRefToProjectTree) {
    const std::string path = scratch_file("p5_facade.toml");
    {
        ProjectTree tree;
        tree.root().set_name("Facade");
        Board& b = tree.root().add_board("B", BoardType::MAIN);
        b.add_page("S", PageType::PCB, "res://p.pcb.toml");
        ASSERT_TRUE(ProjectSaver::save(path, &tree));
    }
    Ref<ProjectTree> loaded = ProjectLoader::load(path);
    ASSERT_TRUE(loaded.valid());
    EXPECT_EQ(loaded->root().name(), "Facade");
    ASSERT_EQ(loaded->root().board_count(), 1u);
    EXPECT_EQ(loaded->root().board(0).page(0).type(), PageType::PCB);
}

TEST(ProjectFacadeTest, LoaderLoadMissingFileReturnsEmptyRef) {
    Ref<ProjectTree> loaded = ProjectLoader::load(scratch_file("p5_does_not_exist.toml"));
    EXPECT_FALSE(loaded.valid());
}

TEST(ProjectFacadeTest, SaverSerializeNullptrReturnsEmpty) {
    EXPECT_TRUE(ProjectSaver::serialize(static_cast<const ProjectTree*>(nullptr)).empty());
    EXPECT_FALSE(ProjectSaver::save(scratch_file("p5_null.toml"), nullptr));
}

TEST(ProjectFacadeTest, ParseRoundTripViaFacade) {
    ProjectTree tree;
    tree.root().set_name("Round");
    tree.root().assign_uuid_for_test("77777777-7777-7777-7777-777777777777");
    Board& b = tree.root().add_board("B", BoardType::DAUGHTER);
    b.assign_uuid_for_test("88888888-8888-8888-8888-888888888888");
    const std::string text = ProjectSaver::serialize(&tree);

    Ref<ProjectTree> reparsed = ProjectLoader::parse(text);
    ASSERT_TRUE(reparsed.valid());
    EXPECT_EQ(reparsed->root(), tree.root());
}

// ============================================================================
// Resource 继承链（P2 对齐）
// ============================================================================

TEST(ProjectTreeResourceTest, IsResourceAndRefCounted) {
    Ref<ProjectTree> tree(new ProjectTree());
    EXPECT_TRUE(tree->is_class("ProjectTree"));
    EXPECT_TRUE(tree->is_class("Resource"));
    EXPECT_TRUE(tree->is_class("RefCounted"));
    EXPECT_TRUE(tree->is_class("Object"));
    EXPECT_EQ(tree->get_class(), "ProjectTree");
    EXPECT_EQ(tree->get_class_static(), "ProjectTree");
    // 引用计数：被 Ref 持有
    EXPECT_GT(tree->get_reference_count(), 0);
}

TEST(ProjectTreeResourceTest, SerializeDeserializeViaResourceInterface) {
    ProjectTree tree;
    tree.root().set_name("ViaResource");
    tree.root().assign_uuid_for_test("99999999-9999-9999-9999-999999999999");
    // 通过 Resource 基类虚函数
    std::string text = tree.serialize();

    Ref<ProjectTree> tree2(new ProjectTree());
    EXPECT_TRUE(tree2->deserialize(text));
    EXPECT_EQ(tree2->root().name(), "ViaResource");
    EXPECT_EQ(tree2->root().uuid(), "99999999-9999-9999-9999-999999999999");
}

// ============================================================================
// Path 工具
// ============================================================================

TEST(PathTest, NormalizeBasic) {
    EXPECT_EQ(Path::normalize("res://sheets//../sheets/x.toml"), "res://sheets/x.toml");
    EXPECT_EQ(Path::normalize("a/./b/../c"), "a/c");
    EXPECT_EQ(Path::normalize("a/b/c"), "a/b/c");
}

TEST(PathTest, NormalizeConvertsBackslashes) {
    EXPECT_EQ(Path::normalize("res://foo\\bar.toml"), "res://foo/bar.toml");
    EXPECT_EQ(Path::normalize("a\\b\\c"), "a/b/c");
}

TEST(PathTest, NormalizeKeepsSchemePrefix) {
    EXPECT_EQ(Path::normalize("user://x"), "user://x");
    EXPECT_EQ(Path::normalize("lib://x"), "lib://x");
    EXPECT_EQ(Path::normalize("res://a/../b"), "res://b");
}

TEST(PathTest, NormalizeAbsoluteAndRelativeLeadingDotDot) {
    EXPECT_EQ(Path::normalize("/a/../b"), "/b");
    EXPECT_EQ(Path::normalize("../a"), "../a");
    EXPECT_EQ(Path::normalize("../../a"), "../../a");
    EXPECT_EQ(Path::normalize("a/../../b"), "../b");
}

TEST(PathTest, JoinHandlesSeparators) {
    EXPECT_EQ(Path::join("res://sheets", "x.toml"), "res://sheets/x.toml");
    EXPECT_EQ(Path::join("res://sheets/", "x.toml"), "res://sheets/x.toml");
    EXPECT_EQ(Path::join("res://sheets", "/x.toml"), "res://sheets/x.toml");
    EXPECT_EQ(Path::join("", "x.toml"), "x.toml");
    EXPECT_EQ(Path::join("a", ""), "a");
    EXPECT_EQ(Path::join("a/b", "c/d"), "a/b/c/d");
}

TEST(PathTest, ParentStripsLeaf) {
    EXPECT_EQ(Path::parent("res://sheets/x.toml"), "res://sheets");
    EXPECT_EQ(Path::parent("res://x.toml"), "res://");
    EXPECT_EQ(Path::parent("x.toml"), "");
    EXPECT_EQ(Path::parent("a/b/c"), "a/b");
}

TEST(PathTest, FilenameReturnsLeaf) {
    EXPECT_EQ(Path::filename("res://sheets/x.toml"), "x.toml");
    EXPECT_EQ(Path::filename("x.toml"), "x.toml");
    EXPECT_EQ(Path::filename("a/b/"), "b");
}

TEST(PathTest, ExtensionLowercaseNoDot) {
    EXPECT_EQ(Path::extension("a/b/x.TOML"), "toml");
    EXPECT_EQ(Path::extension("a/b/x.toml"), "toml");
    EXPECT_EQ(Path::extension("a/b/x"), "");
    EXPECT_EQ(Path::extension("a/.hidden"), "");
    EXPECT_EQ(Path::extension("archive.tar.gz"), "gz");
}

TEST(PathTest, SchemeDetection) {
    EXPECT_TRUE(Path::is_res("res://foo.toml"));
    EXPECT_FALSE(Path::is_res("/abs/foo.toml"));
    EXPECT_FALSE(Path::is_res("user://foo"));
    EXPECT_TRUE(Path::is_user("user://foo"));
    EXPECT_TRUE(Path::is_lib("lib://standard@1.2.0"));
    EXPECT_FALSE(Path::is_lib("res://foo"));
}

TEST(PathTest, ResToNativeMapping) {
    EXPECT_EQ(Path::res_to_native("res://sheets/x.toml", "/proj"), "/proj/sheets/x.toml");
    EXPECT_EQ(Path::res_to_native("res://sheets/x.toml", "/proj/"), "/proj/sheets/x.toml");
    // 非 res:// 原样返回
    EXPECT_EQ(Path::res_to_native("/abs/x.toml", "/proj"), "/abs/x.toml");
}
