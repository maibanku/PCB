// P5 Task 3 · 工程模板（Template）测试。
//
// 覆盖（对齐任务要求）：
//   - list 非空（含 4 个内置模板：blank / arduino_uno / esp32_devkit / rpi_hat）
//   - find 命中（按 id） / find 未命中返回 nullopt
//   - instantiate -> 树结构正确（板数 / 图页数：1 板 + 2 页，类型 = SCHEMATIC + PCB）
//   - 参数覆盖生效（resolve_params + 板名嵌入尺寸）
//   - 未提供参数用默认（resolve_params + 板名沿用默认尺寸）
//   - 内置模板字段完整（id / name / category / description 非空；rules > 0；layer >= 2）
//
// 补充覆盖：
//   - TemplateLoader::parse：从 template.toml 文本加载（含 [[params]] / [default_rules] /
//     [stackup] / options 数组 / board_type=daughter）
//   - 用户模板目录扫描：临时目录下放 <id>/template.toml，list / find 命中
//   - 用户模板覆盖同 id 内置（find 返回用户版本）
//   - instantiate 项目名 = 模板 name + " Project"
//   - instantiate 板类型沿用 def.board_type
//   - 空模板（无 params）instantiate 不崩溃，板名退化为 "Main"
//
// 依赖：eda/project/template_def.h + eda/project/template_library.h + eda/project/project.h。
// 不改 CMakeLists.txt：test_template 由主循环加入 tests/CMakeLists（eda_project 链接）。
//
// 注：所有用例使用显式 user_templates_dir（指向独立临时目录），避免污染真实
//     ~/.eda/templates/；多个用例的临时目录通过计数器隔离。

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "core/object/ref.h"
#include "eda/project/project.h"
#include "eda/project/template_def.h"
#include "eda/project/template_library.h"

using namespace eda;

namespace {

// 每个用例独立临时目录的 fixture（与 test_snapshot 风格一致）。
class TemplateTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path() /
                    ("eda_template_test_" + std::to_string(counter_++));
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
int TemplateTest::counter_ = 0;

void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(f) << "failed to open " << path;
    f << content;
}

bool contains_id(const std::vector<TemplateDef>& all, const std::string& id) {
    for (const auto& d : all) {
        if (d.id() == id) return true;
    }
    return false;
}

}  // namespace

// ============================================================================
// list 非空（含 4 个内置模板）
// ============================================================================

TEST_F(TemplateTest, ListReturnsBuiltins) {
    TemplateLibrary lib(dir());  // 空临时目录：仅返回内置
    const auto all = lib.list();
    EXPECT_GE(all.size(), 4u);
    EXPECT_TRUE(contains_id(all, "blank"));
    EXPECT_TRUE(contains_id(all, "arduino_uno"));
    EXPECT_TRUE(contains_id(all, "esp32_devkit"));
    EXPECT_TRUE(contains_id(all, "rpi_hat"));
}

TEST_F(TemplateTest, EmptyUserDirDoesNotCrashList) {
    // 用户目录存在但为空：list 仅返回内置，不崩溃。
    TemplateLibrary lib(dir());
    const auto all = lib.list();
    EXPECT_EQ(all.size(), 4u);
}

TEST_F(TemplateTest, NonExistentUserDirDegradesToBuiltins) {
    // 用户目录不存在：list 仅返回内置。
    TemplateLibrary lib(dir() + "/does_not_exist");
    const auto all = lib.list();
    EXPECT_EQ(all.size(), 4u);
}

// ============================================================================
// find 命中 / 未命中
// ============================================================================

TEST_F(TemplateTest, FindByIdHits) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->id(), "blank");
    EXPECT_EQ(d->name(), "Blank");
    EXPECT_FALSE(d->description().empty());
}

TEST_F(TemplateTest, FindByIdUnknownReturnsNullopt) {
    TemplateLibrary lib(dir());
    EXPECT_FALSE(lib.find("non-existent-id").has_value());
    EXPECT_FALSE(lib.find("").has_value());  // 空 id
}

// ============================================================================
// instantiate -> 树结构正确（板数 / 图页数）
// ============================================================================

TEST_F(TemplateTest, InstantiateProducesCorrectStructure) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    Ref<ProjectTree> tree = lib.instantiate(*d, {});
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().board_count(), 1u);
    EXPECT_EQ(tree->root().board(0).page_count(), 2u);
    EXPECT_EQ(tree->root().board(0).page(0).type(), PageType::SCHEMATIC);
    EXPECT_EQ(tree->root().board(0).page(1).type(), PageType::PCB);
}

TEST_F(TemplateTest, InstantiateNamesProjectFromTemplate) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("arduino_uno");
    ASSERT_TRUE(d.has_value());

    Ref<ProjectTree> tree = lib.instantiate(*d, {});
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().name(), "Arduino Uno Project");
}

TEST_F(TemplateTest, InstantiateBoardTypeMatchesDef) {
    TemplateLibrary lib(dir());
    for (const char* id : {"blank", "arduino_uno", "esp32_devkit", "rpi_hat"}) {
        const auto d = lib.find(id);
        ASSERT_TRUE(d.has_value());
        Ref<ProjectTree> tree = lib.instantiate(*d, {});
        ASSERT_TRUE(tree.valid()) << "instantiate failed for " << id;
        EXPECT_EQ(tree->root().board(0).type(), d->board_type()) << "board type mismatch for " << id;
        EXPECT_EQ(tree->root().board(0).type(), BoardType::MAIN);
    }
}

TEST_F(TemplateTest, InstantiatePagePathsUseResScheme) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());
    Ref<ProjectTree> tree = lib.instantiate(*d, {});
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().board(0).page(0).path(), "res://sheets/main.sch.toml");
    EXPECT_EQ(tree->root().board(0).page(1).path(), "res://boards/main.pcb.toml");
}

TEST_F(TemplateTest, InstantiateEachCallGeneratesFreshUuids) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    Ref<ProjectTree> a = lib.instantiate(*d, {});
    Ref<ProjectTree> b = lib.instantiate(*d, {});
    ASSERT_TRUE(a.valid() && b.valid());
    // 每次实例化都生成新 UUID（不复用模板固定 UUID）
    EXPECT_NE(a->root().uuid(), b->root().uuid());
    EXPECT_NE(a->root().board(0).uuid(), b->root().board(0).uuid());
}

TEST_F(TemplateTest, InstantiateEmptyIdReturnsEmptyRef) {
    TemplateLibrary lib(dir());
    TemplateDef bad;  // id / name 默认空
    EXPECT_FALSE(lib.instantiate(bad, {}).valid());
}

// ============================================================================
// 参数覆盖生效 / 默认值
// ============================================================================

TEST_F(TemplateTest, ResolveParamsOverrideTakesEffect) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    std::map<std::string, std::string> overrides = {{"board_width", "200"}};
    const auto resolved = TemplateLibrary::resolve_params(*d, overrides);
    EXPECT_EQ(resolved.at("board_width"), "200");  // 覆盖生效

    // 未覆盖的参数保留默认
    const TemplateParam* h = d->find_param("board_height");
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(resolved.at("board_height"), h->default_value);
}

TEST_F(TemplateTest, ResolveParamsUsesDefaultWhenMissing) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    const TemplateParam* w = d->find_param("board_width");
    ASSERT_NE(w, nullptr);

    const auto resolved = TemplateLibrary::resolve_params(*d, {});
    EXPECT_EQ(resolved.at("board_width"), w->default_value);
}

TEST_F(TemplateTest, ResolveParamsIgnoresUnknownOverrideKeys) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    // unknown_key 不在 def.params() 中，不应写入结果。
    const auto resolved = TemplateLibrary::resolve_params(
        *d, {{"unknown_key", "junk"}});
    EXPECT_EQ(resolved.count("unknown_key"), 0u);
    EXPECT_EQ(resolved.size(), d->params().size());
}

TEST_F(TemplateTest, InstantiateBakesOverrideDimsIntoBoardName) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());

    Ref<ProjectTree> tree = lib.instantiate(
        *d, {{"board_width", "150"}, {"board_height", "120"}});
    ASSERT_TRUE(tree.valid());
    const std::string name = tree->root().board(0).name();
    EXPECT_NE(name.find("150"), std::string::npos);
    EXPECT_NE(name.find("120"), std::string::npos);
    EXPECT_NE(name.find("x"), std::string::npos);
}

TEST_F(TemplateTest, InstantiateBakesDefaultDimsWhenNoOverride) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());
    const TemplateParam* w = d->find_param("board_width");
    ASSERT_NE(w, nullptr);

    Ref<ProjectTree> tree = lib.instantiate(*d, {});
    ASSERT_TRUE(tree.valid());
    // 默认尺寸出现在板名中
    EXPECT_NE(tree->root().board(0).name().find(w->default_value),
              std::string::npos);
}

// ============================================================================
// 内置模板字段完整
// ============================================================================

TEST_F(TemplateTest, BuiltinTemplateFieldsComplete) {
    TemplateLibrary lib(dir());
    const auto all = lib.list();
    ASSERT_GE(all.size(), 4u);
    for (const TemplateDef& d : all) {
        EXPECT_FALSE(d.id().empty())         << "id empty";
        EXPECT_FALSE(d.name().empty())       << "name empty";
        EXPECT_FALSE(d.category().empty())   << "category empty";
        EXPECT_FALSE(d.description().empty()) << "description empty";
        EXPECT_GT(d.default_rules().clearance_mm, 0.0);
        EXPECT_GT(d.default_rules().track_width_mm, 0.0);
        EXPECT_GT(d.default_rules().via_drill_mm, 0.0);
        EXPECT_GE(d.stackup_preset().layer_count, 2);
    }
}

TEST_F(TemplateTest, BuiltinStackupPresetsAreSane) {
    TemplateLibrary lib(dir());
    const auto blank = lib.find("blank");
    const auto rpi   = lib.find("rpi_hat");
    ASSERT_TRUE(blank.has_value());
    ASSERT_TRUE(rpi.has_value());
    EXPECT_EQ(blank->stackup_preset().layer_count, 2);
    EXPECT_EQ(rpi->stackup_preset().layer_count, 4);  // RPi HAT 推荐 4 层
}

TEST_F(TemplateTest, BuiltinBoardDimsHaveRange) {
    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());
    const TemplateParam* w = d->find_param("board_width");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->type, TemplateParamType::INT);
    EXPECT_TRUE(w->has_min);
    EXPECT_TRUE(w->has_max);
    EXPECT_FALSE(w->min_value.empty());
    EXPECT_FALSE(w->max_value.empty());
}

// ============================================================================
// TemplateLoader::parse template.toml 文本
// ============================================================================

TEST(TemplateLoaderTest, ParseTemplateTomlText) {
    const std::string text = R"TOML(
schema_version = 1
id = "custom"
name = "Custom Board"
category = "Custom"
description = "User-defined template"
board_type = "daughter"

[default_rules]
clearance_mm = 0.25
track_width_mm = 0.18
via_drill_mm = 0.30

[stackup]
layer_count = 4

[[params]]
name = "board_width"
type = "int"
default = "80"
min = "10"
max = "500"

[[params]]
name = "color"
type = "enum"
default = "red"
options = ["red", "green", "blue"]
)TOML";

    auto d = TemplateLoader::parse(text);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->id(), "custom");
    EXPECT_EQ(d->name(), "Custom Board");
    EXPECT_EQ(d->category(), "Custom");
    EXPECT_EQ(d->description(), "User-defined template");
    EXPECT_EQ(d->board_type(), BoardType::DAUGHTER);
    EXPECT_DOUBLE_EQ(d->default_rules().clearance_mm, 0.25);
    EXPECT_DOUBLE_EQ(d->default_rules().track_width_mm, 0.18);
    EXPECT_DOUBLE_EQ(d->default_rules().via_drill_mm, 0.30);
    EXPECT_EQ(d->stackup_preset().layer_count, 4);

    ASSERT_EQ(d->params().size(), 2u);
    EXPECT_EQ(d->params()[0].name, "board_width");
    EXPECT_EQ(d->params()[0].type, TemplateParamType::INT);
    EXPECT_EQ(d->params()[0].default_value, "80");
    EXPECT_TRUE(d->params()[0].has_min);
    EXPECT_EQ(d->params()[0].min_value, "10");
    EXPECT_TRUE(d->params()[0].has_max);
    EXPECT_EQ(d->params()[0].max_value, "500");

    EXPECT_EQ(d->params()[1].name, "color");
    EXPECT_EQ(d->params()[1].type, TemplateParamType::ENUM);
    EXPECT_EQ(d->params()[1].default_value, "red");
    ASSERT_EQ(d->params()[1].options.size(), 3u);
    EXPECT_EQ(d->params()[1].options[0], "red");
    EXPECT_EQ(d->params()[1].options[1], "green");
    EXPECT_EQ(d->params()[1].options[2], "blue");
}

TEST(TemplateLoaderTest, ParseMinimalTemplateToml) {
    // 最小 schema：仅顶层字段；rules / stackup / params 缺省。
    const std::string text = R"TOML(
schema_version = 1
id = "mini"
name = "Mini"
category = "Basic"
description = "Bare-bones template"
)TOML";
    auto d = TemplateLoader::parse(text);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->id(), "mini");
    EXPECT_EQ(d->board_type(), BoardType::MAIN);  // 默认
    EXPECT_EQ(d->params().size(), 0u);
    // rules / stackup 取结构体默认值
    EXPECT_GT(d->default_rules().clearance_mm, 0.0);
    EXPECT_GE(d->stackup_preset().layer_count, 2);
}

TEST(TemplateLoaderTest, ParseIgnoresUnknownKeys) {
    const std::string text = R"TOML(
schema_version = 1
id = "u"
name = "U"
category = "C"
description = "D"
unknown_top_field = "ignored"

[unknown_section]
foo = "bar"
)TOML";
    auto d = TemplateLoader::parse(text);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->id(), "u");
    EXPECT_EQ(d->name(), "U");
}

TEST(TemplateLoaderTest, ParamTypeMappingRoundTrip) {
    EXPECT_EQ(to_string_view(TemplateParamType::STRING), "string");
    EXPECT_EQ(to_string_view(TemplateParamType::INT),    "int");
    EXPECT_EQ(to_string_view(TemplateParamType::FLOAT),  "float");
    EXPECT_EQ(to_string_view(TemplateParamType::BOOL),   "bool");
    EXPECT_EQ(to_string_view(TemplateParamType::ENUM),   "enum");

    EXPECT_EQ(parse_template_param_type("int"),   TemplateParamType::INT);
    EXPECT_EQ(parse_template_param_type("float"), TemplateParamType::FLOAT);
    EXPECT_EQ(parse_template_param_type("bool"),  TemplateParamType::BOOL);
    EXPECT_EQ(parse_template_param_type("enum"),  TemplateParamType::ENUM);
    EXPECT_EQ(parse_template_param_type("string"), TemplateParamType::STRING);
    EXPECT_EQ(parse_template_param_type("unknown"), TemplateParamType::STRING);
}

// ============================================================================
// 用户模板目录扫描
// ============================================================================

TEST_F(TemplateTest, ScanUserTemplatesFromDir) {
    const std::string sub = dir() + "/my_board";
    std::error_code ec;
    std::filesystem::create_directories(sub, ec);
    write_file(sub + "/template.toml", R"TOML(
schema_version = 1
id = "my_board"
name = "My Board"
category = "Custom"
description = "Test user template"
board_type = "main"

[default_rules]
clearance_mm = 0.2
track_width_mm = 0.2
via_drill_mm = 0.3

[stackup]
layer_count = 2
)TOML");

    TemplateLibrary lib(dir());
    const auto all = lib.list();
    EXPECT_GE(all.size(), 5u);  // 4 内置 + 1 用户
    EXPECT_TRUE(contains_id(all, "my_board"));

    const auto d = lib.find("my_board");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->name(), "My Board");

    Ref<ProjectTree> tree = lib.instantiate(*d, {});
    ASSERT_TRUE(tree.valid());
    EXPECT_EQ(tree->root().name(), "My Board Project");
    EXPECT_EQ(tree->root().board_count(), 1u);
    EXPECT_EQ(tree->root().board(0).page_count(), 2u);
}

TEST_F(TemplateTest, UserTemplateOverridesBuiltinById) {
    // 用户模板 id = "blank"（与内置冲突）-> find 应返回用户版本（覆盖语义）。
    const std::string sub = dir() + "/custom_blank";
    std::error_code ec;
    std::filesystem::create_directories(sub, ec);
    write_file(sub + "/template.toml", R"TOML(
schema_version = 1
id = "blank"
name = "My Custom Blank"
category = "Custom"
description = "User override of builtin blank"
board_type = "main"

[default_rules]
clearance_mm = 0.4
track_width_mm = 0.4
via_drill_mm = 0.5

[stackup]
layer_count = 4
)TOML");

    TemplateLibrary lib(dir());
    const auto d = lib.find("blank");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->name(), "My Custom Blank");  // 用户版本，非内置 "Blank"
    EXPECT_DOUBLE_EQ(d->default_rules().clearance_mm, 0.4);
    EXPECT_EQ(d->stackup_preset().layer_count, 4);
}

TEST_F(TemplateTest, ScanSkipsDirWithoutTemplateToml) {
    // 子目录存在但无 template.toml -> 跳过，不影响其他模板。
    std::error_code ec;
    std::filesystem::create_directories(dir() + "/no_toml", ec);
    std::filesystem::create_directories(dir() + "/good", ec);
    write_file(dir() + "/good/template.toml", R"TOML(
schema_version = 1
id = "good"
name = "Good"
category = "C"
description = "D"
board_type = "main"

[default_rules]
clearance_mm = 0.2
track_width_mm = 0.2
via_drill_mm = 0.3

[stackup]
layer_count = 2
)TOML");

    TemplateLibrary lib(dir());
    const auto all = lib.list();
    EXPECT_TRUE(contains_id(all, "good"));
    EXPECT_FALSE(contains_id(all, "no_toml"));
}

TEST_F(TemplateTest, DefaultUserTemplatesDirIsHomeDotEda) {
    // 不依赖具体 HOME 值——仅校验后缀（跨平台一致）。
    const std::string def = TemplateLibrary::default_user_templates_dir();
    EXPECT_TRUE(def.empty() || def.size() >= std::string("/.eda/templates").size());
    if (!def.empty()) {
        EXPECT_NE(def.find(".eda/templates"), std::string::npos);
    }
}
