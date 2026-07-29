// P5 · 工程新建 / 保存 / 加载命令化测试。
//
// 覆盖：
//   - NewProjectCommand：树非空、含 1 板(MAIN) + 2 页(SCHEMATIC + PCB) + created 信号
//   - SaveProjectCommand：文件存在、TOML 可读（schema_version / [[boards]] / pages）
//                         + saved/failed 信号 + 空 tree 触发 failed
//   - LoadProjectCommand：loaded 信号 + 结构与原树相等 + 版本兼容标志
//                         + 缺文件 / schema 不兼容 触发 failed
//   - New → Save → Load 往返：结构深比较相等
//   - CommandRegistry 集成：file.new handler 执行 → current_tree 更新 + tree_changed emit
//                         + file.save/file.save_as/file.open 四条命令均注册
//   - file.save_as → file.open 端到端（临时 CWD 隔离，不污染源码树）
//
// 依赖：eda_project（含 project_commands）+ eda_editor（CommandRegistry）+ eda_core（Signal）。

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/object/ref.h"
#include "core/object/signal.h"
#include "editor/command_palette/command_registry.h"
#include "eda/project/project.h"
#include "eda/project/project_commands.h"

using namespace eda;

namespace {

// 临时文件路径（系统 temp 目录下）。
std::string scratch(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// 写入原始文本到临时文件（用于版本不兼容 / 旧版本工程模拟）。
bool dump_text(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

// 读取文件全部文本（用于校验落盘内容）。
std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// ============================================================================
// NewProjectCommand
// ============================================================================

TEST(NewProjectCommandTest, CreatesBlankProjectWithOneMainBoardTwoPages) {
    NewProjectCommand cmd;
    bool created_fired = false;
    cmd.created().connect([&] { created_fired = true; });

    Ref<ProjectTree> tree = cmd.execute();

    ASSERT_TRUE(tree.valid());
    EXPECT_TRUE(created_fired);
    ASSERT_EQ(tree->root().board_count(), 1u);
    const Board& b = tree->root().board(0);
    EXPECT_EQ(b.type(), BoardType::MAIN);
    ASSERT_EQ(b.page_count(), 2u);
    EXPECT_EQ(b.page(0).type(), PageType::SCHEMATIC);
    EXPECT_EQ(b.page(1).type(), PageType::PCB);
}

TEST(NewProjectCommandTest, BlankTreeHasNonEmptyUuids) {
    NewProjectCommand cmd;
    Ref<ProjectTree> tree = cmd.execute();
    EXPECT_TRUE(is_valid_uuid(tree->root().uuid()));
    ASSERT_EQ(tree->root().board_count(), 1u);
    EXPECT_TRUE(is_valid_uuid(tree->root().board(0).uuid()));
    ASSERT_EQ(tree->root().board(0).page_count(), 2u);
    EXPECT_TRUE(is_valid_uuid(tree->root().board(0).page(0).uuid()));
    EXPECT_TRUE(is_valid_uuid(tree->root().board(0).page(1).uuid()));
}

// ============================================================================
// SaveProjectCommand
// ============================================================================

TEST(SaveProjectCommandTest, WritesFileAndEmitsSavedSignal) {
    Ref<ProjectTree> tree = NewProjectCommand{}.execute();
    const std::string path = scratch("p5_cmd_save.toml");
    std::remove(path.c_str());

    SaveProjectCommand cmd(tree);
    bool saved_fired = false, failed_fired = false;
    cmd.saved().connect([&] { saved_fired = true; });
    cmd.failed().connect([&] { failed_fired = true; });

    EXPECT_TRUE(cmd.execute(path));
    EXPECT_TRUE(saved_fired);
    EXPECT_FALSE(failed_fired);
    EXPECT_TRUE(std::filesystem::exists(path));

    // TOML 可读：关键字段、表头都在
    const std::string text = read_text(path);
    EXPECT_NE(text.find("schema_version = 1"), std::string::npos);
    EXPECT_NE(text.find("[[boards]]"), std::string::npos);
    EXPECT_NE(text.find("[[boards.pages]]"), std::string::npos);
    EXPECT_NE(text.find("type = \"schematic\""), std::string::npos);
    EXPECT_NE(text.find("type = \"pcb\""), std::string::npos);
    EXPECT_NE(text.find("type = \"main\""), std::string::npos);
}

TEST(SaveProjectCommandTest, NullTreeEmitsFailedSignal) {
    Ref<ProjectTree> null_tree;  // 默认构造：空 Ref
    SaveProjectCommand cmd(null_tree);
    bool saved_fired = false, failed_fired = false;
    cmd.saved().connect([&] { saved_fired = true; });
    cmd.failed().connect([&] { failed_fired = true; });

    EXPECT_FALSE(cmd.execute(scratch("p5_cmd_null.toml")));
    EXPECT_FALSE(saved_fired);
    EXPECT_TRUE(failed_fired);
}

// ============================================================================
// LoadProjectCommand
// ============================================================================

TEST(LoadProjectCommandTest, LoadsAndEmitsLoadedSignal) {
    Ref<ProjectTree> original = NewProjectCommand{}.execute();
    const std::string path = scratch("p5_cmd_load.toml");
    ASSERT_TRUE(ProjectSaver::save(path, original.get()));

    LoadProjectCommand cmd;
    bool loaded_fired = false, failed_fired = false;
    cmd.loaded().connect([&] { loaded_fired = true; });
    cmd.failed().connect([&] { failed_fired = true; });

    Ref<ProjectTree> tree = cmd.execute(path);
    ASSERT_TRUE(tree.valid());
    EXPECT_TRUE(loaded_fired);
    EXPECT_FALSE(failed_fired);
    EXPECT_TRUE(cmd.version_compatible());
    EXPECT_EQ(cmd.file_schema_version(), 1);
}

TEST(LoadProjectCommandTest, MissingFileEmitsFailedSignal) {
    LoadProjectCommand cmd;
    bool loaded_fired = false, failed_fired = false;
    cmd.loaded().connect([&] { loaded_fired = true; });
    cmd.failed().connect([&] { failed_fired = true; });

    const std::string missing = scratch("p5_cmd_does_not_exist.toml");
    std::remove(missing.c_str());

    Ref<ProjectTree> tree = cmd.execute(missing);
    EXPECT_FALSE(tree.valid());
    EXPECT_FALSE(loaded_fired);
    EXPECT_TRUE(failed_fired);
    EXPECT_FALSE(cmd.version_compatible());
}

TEST(LoadProjectCommandTest, VersionMismatchEmitsFailedSignal) {
    // 未来 schema（version=999）：旧解析器应拒绝，而不是被前向兼容降级
    const std::string path = scratch("p5_cmd_version_mismatch.toml");
    ASSERT_TRUE(dump_text(path, R"(schema_version = 999
name = "Future"
uuid = "11111111-1111-1111-1111-111111111111"
)"));

    LoadProjectCommand cmd;
    bool loaded_fired = false, failed_fired = false;
    cmd.loaded().connect([&] { loaded_fired = true; });
    cmd.failed().connect([&] { failed_fired = true; });

    Ref<ProjectTree> tree = cmd.execute(path);
    EXPECT_FALSE(tree.valid());
    EXPECT_FALSE(loaded_fired);
    EXPECT_TRUE(failed_fired);
    EXPECT_FALSE(cmd.version_compatible());
    EXPECT_EQ(cmd.file_schema_version(), 999);
}

TEST(LoadProjectCommandTest, MissingSchemaVersionEmitsFailed) {
    // 没有 schema_version 字段（来源不明的文件）：拒绝加载
    const std::string path = scratch("p5_cmd_no_version.toml");
    ASSERT_TRUE(dump_text(path, R"(name = "NoVersion"
uuid = "22222222-2222-2222-2222-222222222222"
)"));

    LoadProjectCommand cmd;
    bool failed_fired = false;
    cmd.failed().connect([&] { failed_fired = true; });

    Ref<ProjectTree> tree = cmd.execute(path);
    EXPECT_FALSE(tree.valid());
    EXPECT_TRUE(failed_fired);
    EXPECT_FALSE(cmd.version_compatible());
    EXPECT_EQ(cmd.file_schema_version(), -1);
}

// ============================================================================
// New → Save → Load 往返：结构相等
// ============================================================================

TEST(ProjectCommandsRoundTripTest, NewSaveLoadStructureEqual) {
    Ref<ProjectTree> original = NewProjectCommand{}.execute();
    const std::string path = scratch("p5_cmd_roundtrip.toml");

    SaveProjectCommand scmd(original);
    ASSERT_TRUE(scmd.execute(path));

    LoadProjectCommand lcmd;
    Ref<ProjectTree> reloaded = lcmd.execute(path);
    ASSERT_TRUE(reloaded.valid());

    // 深比较：板数、页数、类型、UUID 完整往返
    ASSERT_EQ(reloaded->root().board_count(), original->root().board_count());
    const Board& a = reloaded->root().board(0);
    const Board& b = original->root().board(0);
    EXPECT_EQ(a.uuid(), b.uuid());
    EXPECT_EQ(a.name(), b.name());
    EXPECT_EQ(a.type(), b.type());
    ASSERT_EQ(a.page_count(), b.page_count());

    // ProjectSaver 按 UUID 字典序输出 pages，加载顺序与插入顺序可能不同。
    // 按 UUID 配对比较，避免依赖下标顺序。
    auto find_page_by_uuid = [](const Board& board,
                                const std::string& uuid) -> const Page* {
        for (size_t i = 0; i < board.page_count(); ++i) {
            if (board.page(i).uuid() == uuid) return &board.page(i);
        }
        return nullptr;
    };
    int schematic_count = 0, pcb_count = 0;
    for (size_t i = 0; i < b.page_count(); ++i) {
        const Page* orig = &b.page(i);
        const Page* got = find_page_by_uuid(a, orig->uuid());
        ASSERT_NE(got, nullptr) << "丢失 page uuid=" << orig->uuid();
        EXPECT_EQ(got->name(), orig->name());
        EXPECT_EQ(got->type(), orig->type());
        EXPECT_EQ(got->path(), orig->path());
        if (got->type() == PageType::SCHEMATIC) ++schematic_count;
        if (got->type() == PageType::PCB) ++pcb_count;
    }
    EXPECT_EQ(schematic_count, 1);
    EXPECT_EQ(pcb_count, 1);

    // 路径完整往返：不依赖下标
    bool found_sch_path = false, found_pcb_path = false;
    for (size_t i = 0; i < a.page_count(); ++i) {
        if (a.page(i).path() == "res://sheets/sheet1.sch.toml") found_sch_path = true;
        if (a.page(i).path() == "res://boards/pcb1.pcb.toml") found_pcb_path = true;
    }
    EXPECT_TRUE(found_sch_path);
    EXPECT_TRUE(found_pcb_path);
}

// ============================================================================
// CommandRegistry 集成
// ============================================================================

TEST(ProjectCommandsRegistryTest, AllFourCommandsRegisteredWithHandlers) {
    CommandRegistry registry;
    Ref<ProjectTree> current_tree;
    Signal<> tree_changed;
    register_project_commands(registry, current_tree, tree_changed);

    for (const char* id : {builtin::FILE_NEW, builtin::FILE_SAVE,
                           builtin::FILE_SAVE_AS, builtin::FILE_OPEN}) {
        const Command* c = registry.get(id);
        ASSERT_NE(c, nullptr) << "missing command " << id;
        EXPECT_TRUE(c->handler) << "empty handler for " << id;
    }
}

TEST(ProjectCommandsRegistryTest, FileNewHandlerUpdatesTreeAndEmitsTreeChanged) {
    CommandRegistry registry;
    Ref<ProjectTree> current_tree;
    Signal<> tree_changed;
    int change_count = 0;
    tree_changed.connect([&] { ++change_count; });
    register_project_commands(registry, current_tree, tree_changed);

    const Command* c = registry.get(builtin::FILE_NEW);
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(c->handler);
    c->handler();

    ASSERT_TRUE(current_tree.valid());
    EXPECT_EQ(current_tree->root().board_count(), 1u);
    EXPECT_EQ(current_tree->root().board(0).page_count(), 2u);
    EXPECT_GT(change_count, 0);
}

TEST(ProjectCommandsRegistryTest, FileNewHandlerReplacesExistingTree) {
    CommandRegistry registry;
    Ref<ProjectTree> current_tree = NewProjectCommand{}.execute();  // 初始非空
    Signal<> tree_changed;
    register_project_commands(registry, current_tree, tree_changed);

    const Project* old_root = &current_tree->root();
    registry.get(builtin::FILE_NEW)->handler();

    ASSERT_TRUE(current_tree.valid());
    EXPECT_NE(&current_tree->root(), old_root);  // 树被替换
    EXPECT_EQ(current_tree->root().board_count(), 1u);
}

// file.save_as → file.open 端到端：临时隔离 CWD，避免污染源码树。
TEST(ProjectCommandsRegistryTest, FileSaveAsThenOpenRoundTrip) {
    const auto orig_cwd = std::filesystem::current_path();
    const auto tmp_cwd = std::filesystem::temp_directory_path() / "p5_cmd_cwd";
    std::filesystem::create_directories(tmp_cwd);
    std::filesystem::current_path(tmp_cwd);
    const std::string default_path = kDefaultProjectFileName;
    std::remove(default_path.c_str());

    CommandRegistry registry;
    Ref<ProjectTree> current_tree;
    Signal<> tree_changed;
    int change_count = 0;
    tree_changed.connect([&] { ++change_count; });
    register_project_commands(registry, current_tree, tree_changed);

    // 1) new → current_tree 非空
    registry.get(builtin::FILE_NEW)->handler();
    ASSERT_TRUE(current_tree.valid());
    const int changes_after_new = change_count;
    EXPECT_GT(changes_after_new, 0);

    // 2) save_as → 落盘到 CWD/untitled.pcbproj
    registry.get(builtin::FILE_SAVE_AS)->handler();
    EXPECT_TRUE(std::filesystem::exists(default_path));
    // save_as 不 emit tree_changed（树未变）
    EXPECT_EQ(change_count, changes_after_new);

    // 3) 清空 current_tree 再 open → 从磁盘恢复 + emit tree_changed
    current_tree.reset();
    ASSERT_FALSE(current_tree.valid());
    registry.get(builtin::FILE_OPEN)->handler();
    ASSERT_TRUE(current_tree.valid());
    EXPECT_EQ(current_tree->root().board_count(), 1u);
    ASSERT_EQ(current_tree->root().board(0).page_count(), 2u);
    // pages 按 UUID 字典序落盘 → 加载顺序可能与插入顺序不同；按类型计数断言
    int schematic_count = 0, pcb_count = 0;
    for (size_t i = 0; i < current_tree->root().board(0).page_count(); ++i) {
        PageType t = current_tree->root().board(0).page(i).type();
        if (t == PageType::SCHEMATIC) ++schematic_count;
        else if (t == PageType::PCB) ++pcb_count;
    }
    EXPECT_EQ(schematic_count, 1);
    EXPECT_EQ(pcb_count, 1);
    EXPECT_GT(change_count, changes_after_new);

    // 清理 + 还原 CWD
    std::remove(default_path.c_str());
    std::filesystem::current_path(orig_cwd);
}

// file.save（无既有路径）→ 回落默认路径，落盘成功
TEST(ProjectCommandsRegistryTest, FileSaveFallsBackToDefaultPath) {
    const auto orig_cwd = std::filesystem::current_path();
    const auto tmp_cwd = std::filesystem::temp_directory_path() / "p5_cmd_save_cwd";
    std::filesystem::create_directories(tmp_cwd);
    std::filesystem::current_path(tmp_cwd);
    const std::string default_path = kDefaultProjectFileName;
    std::remove(default_path.c_str());

    CommandRegistry registry;
    Ref<ProjectTree> current_tree;
    Signal<> tree_changed;
    register_project_commands(registry, current_tree, tree_changed);

    registry.get(builtin::FILE_NEW)->handler();
    ASSERT_TRUE(current_tree.valid());

    // current_path 为空 → save 回落默认路径
    registry.get(builtin::FILE_SAVE)->handler();
    EXPECT_TRUE(std::filesystem::exists(default_path));

    // 再次 save（current_path 已被前一次 save? 不，save 不写 current_path）
    // save 仅在 current_path 非空时沿用；此处 current_path 仍为空 → 再次默认路径
    std::remove(default_path.c_str());
    registry.get(builtin::FILE_SAVE)->handler();
    EXPECT_TRUE(std::filesystem::exists(default_path));

    std::remove(default_path.c_str());
    std::filesystem::current_path(orig_cwd);
}
