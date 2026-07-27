// eda/project/template_library.cpp
//
// P5 Task 3 · 工程模板库实现。
//
// 设计要点：
//   - 内置模板硬编码（4 份）：blank / arduino_uno / esp32_devkit / rpi_hat。
//     每份的 board_width / board_height 默认值取对应开发板的名义尺寸（mm，向下取整）；
//     叠层层数取社区推荐（Arduino Uno / ESP32 -> 2 层；RPi HAT 推荐 4 层）。
//   - 用户模板目录扫描：遍历 user_templates_dir_ 下的子目录，每个子目录读
//     template.toml（用 TemplateLoader::load）。无法解析的模板直接跳过。
//   - 跨平台 HOME 解析：$HOME（Unix） / %USERPROFILE%（Windows） / %HOMEDRIVE%%HOMEPATH%（兜底）。
//   - 实例化：每模板都生成 1 板 + 2 页（SCHEMATIC + PCB）；板名按解析后的尺寸美化。
//   - 默认规则 / 叠层不写入 ProjectTree（Project/Board/Page 当前无对应字段）；
//     由调用方通过 def.default_rules() / def.stackup_preset() 单独取出。

#include "eda/project/template_library.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

namespace eda {

namespace {

// ============================================================================
// 常量
// ============================================================================

constexpr const char* kParamBoardWidth  = "board_width";
constexpr const char* kParamBoardHeight = "board_height";

constexpr const char* kSheetPath = "res://sheets/main.sch.toml";
constexpr const char* kPcbPath   = "res://boards/main.pcb.toml";

// ============================================================================
// 跨平台 HOME 目录解析
// ============================================================================

std::string home_dir() {
    // Unix 优先 $HOME；Windows 用 %USERPROFILE%；最后兜底 HOMEDRIVE + HOMEPATH。
    if (const char* h = std::getenv("HOME")) {
        if (h[0] != '\0') return std::string(h);
    }
    if (const char* up = std::getenv("USERPROFILE")) {
        if (up[0] != '\0') return std::string(up);
    }
    const char* hd = std::getenv("HOMEDRIVE");
    const char* hp = std::getenv("HOMEPATH");
    if (hd && hp && hd[0] != '\0' && hp[0] != '\0') {
        return std::string(hd) + std::string(hp);
    }
    return std::string();
}

// ============================================================================
// 内置模板构造辅助
// ============================================================================

TemplateParam make_range_param(const std::string& name,
                               TemplateParamType type,
                               const std::string& default_value,
                               const std::string& min_value,
                               const std::string& max_value) {
    TemplateParam p;
    p.name = name;
    p.type = type;
    p.default_value = default_value;
    p.min_value = min_value;
    p.has_min = true;
    p.max_value = max_value;
    p.has_max = true;
    return p;
}

TemplateDef make_builtin_blank() {
    TemplateDef d;
    d.set_id(TemplateLibrary::kBuiltinIdBlank);
    d.set_name("Blank");
    d.set_category("Basic");
    d.set_description("Empty project starting point with one board and default rules.");
    d.set_board_type(BoardType::MAIN);
    d.set_default_rules(TemplateRules{0.20, 0.20, 0.30});
    d.set_stackup_preset(StackupPreset{2});
    d.add_param() = make_range_param(kParamBoardWidth,  TemplateParamType::INT,
                                     "100", "10", "1000");
    d.add_param() = make_range_param(kParamBoardHeight, TemplateParamType::INT,
                                     "80",  "10", "1000");
    return d;
}

TemplateDef make_builtin_arduino_uno() {
    TemplateDef d;
    d.set_id(TemplateLibrary::kBuiltinIdArduinoUno);
    d.set_name("Arduino Uno");
    d.set_category("Maker");
    d.set_description("Arduino Uno compatible footprint (68.6 x 53.4 mm, 2 layers).");
    d.set_board_type(BoardType::MAIN);
    d.set_default_rules(TemplateRules{0.20, 0.20, 0.30});
    d.set_stackup_preset(StackupPreset{2});
    d.add_param() = make_range_param(kParamBoardWidth,  TemplateParamType::INT,
                                     "68", "10", "1000");
    d.add_param() = make_range_param(kParamBoardHeight, TemplateParamType::INT,
                                     "53", "10", "1000");
    return d;
}

TemplateDef make_builtin_esp32_devkit() {
    TemplateDef d;
    d.set_id(TemplateLibrary::kBuiltinIdEsp32DevKit);
    d.set_name("ESP32 DevKit");
    d.set_category("Maker");
    d.set_description("ESP32 DevKit board footprint (51 x 28 mm, 2 layers).");
    d.set_board_type(BoardType::MAIN);
    d.set_default_rules(TemplateRules{0.18, 0.18, 0.25});
    d.set_stackup_preset(StackupPreset{2});
    d.add_param() = make_range_param(kParamBoardWidth,  TemplateParamType::INT,
                                     "51", "10", "1000");
    d.add_param() = make_range_param(kParamBoardHeight, TemplateParamType::INT,
                                     "28", "10", "1000");
    return d;
}

TemplateDef make_builtin_rpi_hat() {
    TemplateDef d;
    d.set_id(TemplateLibrary::kBuiltinIdRpiHat);
    d.set_name("Raspberry Pi HAT");
    d.set_category("Maker");
    d.set_description("Raspberry Pi HAT footprint (65 x 56 mm, 4 layers recommended).");
    d.set_board_type(BoardType::MAIN);
    d.set_default_rules(TemplateRules{0.15, 0.15, 0.20});
    d.set_stackup_preset(StackupPreset{4});
    d.add_param() = make_range_param(kParamBoardWidth,  TemplateParamType::INT,
                                     "65", "10", "1000");
    d.add_param() = make_range_param(kParamBoardHeight, TemplateParamType::INT,
                                     "56", "10", "1000");
    return d;
}

}  // namespace

// ============================================================================
// 静态构造 / 默认目录
// ============================================================================

std::string TemplateLibrary::default_user_templates_dir() {
    const std::string home = home_dir();
    if (home.empty()) return std::string();
    // 统一正斜杠（与 res:// / user:// 协议前缀风格一致）。
    return home + "/.eda/templates";
}

std::vector<TemplateDef> TemplateLibrary::builtins() {
    std::vector<TemplateDef> out;
    out.reserve(4);
    out.push_back(make_builtin_blank());
    out.push_back(make_builtin_arduino_uno());
    out.push_back(make_builtin_esp32_devkit());
    out.push_back(make_builtin_rpi_hat());
    return out;
}

TemplateLibrary::TemplateLibrary()
    : user_templates_dir_(default_user_templates_dir()) {}

TemplateLibrary::TemplateLibrary(std::string user_templates_dir)
    : user_templates_dir_(std::move(user_templates_dir)) {}

// ============================================================================
// 用户模板目录扫描
// ============================================================================

std::vector<TemplateDef> TemplateLibrary::scan_user_dir() const {
    namespace fs = std::filesystem;
    std::vector<TemplateDef> out;
    if (user_templates_dir_.empty()) return out;

    std::error_code exist_ec;
    if (!fs::exists(user_templates_dir_, exist_ec)) return out;

    try {
        for (const fs::directory_entry& entry :
             fs::directory_iterator(user_templates_dir_)) {
            std::error_code rec;
            if (!entry.is_directory(rec)) continue;

            const fs::path toml_path = entry.path() / "template.toml";
            std::error_code toml_ec;
            if (!fs::exists(toml_path, toml_ec)) continue;

            auto def = TemplateLoader::load(toml_path.string());
            if (def.has_value()) {
                out.push_back(std::move(*def));
            }
        }
    } catch (const std::exception&) {
        // 中途异常：返回已收集的部分（不崩溃）。
    }
    return out;
}

// ============================================================================
// list / find
// ============================================================================

std::vector<TemplateDef> TemplateLibrary::list() const {
    std::vector<TemplateDef> out = builtins();
    std::vector<TemplateDef> user = scan_user_dir();
    for (auto& d : user) out.push_back(std::move(d));
    return out;
}

std::optional<TemplateDef> TemplateLibrary::find(const std::string& id) const {
    if (id.empty()) return std::nullopt;
    // 用户模板优先（允许覆盖内置）。
    std::vector<TemplateDef> user = scan_user_dir();
    for (const auto& d : user) {
        if (d.id() == id) return d;
    }
    for (const auto& d : builtins()) {
        if (d.id() == id) return d;
    }
    return std::nullopt;
}

// ============================================================================
// resolve_params
// ============================================================================

std::map<std::string, std::string> TemplateLibrary::resolve_params(
    const TemplateDef& def,
    const std::map<std::string, std::string>& overrides) {
    std::map<std::string, std::string> out;
    // 1. 默认值打底（仅写入 def 声明的参数）
    for (const TemplateParam& p : def.params()) {
        out[p.name] = p.default_value;
    }
    // 2. overrides 覆盖（仅对已声明的参数生效）
    for (const auto& [key, value] : overrides) {
        auto it = out.find(key);
        if (it != out.end()) {
            it->second = value;
        }
    }
    return out;
}

// ============================================================================
// instantiate
// ============================================================================

Ref<ProjectTree> TemplateLibrary::instantiate(
    const TemplateDef& def,
    const std::map<std::string, std::string>& overrides) const {
    if (def.id().empty() || def.name().empty()) return Ref<ProjectTree>();

    const std::map<std::string, std::string> resolved =
        resolve_params(def, overrides);

    Ref<ProjectTree> tree(new ProjectTree());
    tree->root().set_name(def.name() + " Project");

    // 板名：默认 "Main"；声明了 board_width / board_height 时嵌入尺寸
    // （使参数覆盖对调用方可观察——树结构本身不携带尺寸字段）。
    std::string board_name = "Main";
    auto bw = resolved.find(kParamBoardWidth);
    auto bh = resolved.find(kParamBoardHeight);
    if (bw != resolved.end() && bh != resolved.end() &&
        !bw->second.empty() && !bh->second.empty()) {
        board_name = "Main (" + bw->second + "x" + bh->second + ")";
    }

    Board& b = tree->root().add_board(board_name, def.board_type());
    b.add_page("Sheet1", PageType::SCHEMATIC, kSheetPath);
    b.add_page("PCB1",   PageType::PCB,       kPcbPath);

    return tree;
}

}  // namespace eda
