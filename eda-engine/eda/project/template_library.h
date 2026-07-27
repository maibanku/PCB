#pragma once

// eda/project/template_library.h
//
// P5 Task 3 · 工程模板库（TemplateLibrary）。
//
// 范围：
//   - 内置模板清单（硬编码：Blank / Arduino_Uno / ESP32_DevKit / RPi_Hat，
//     简化为板尺寸 + 层数预设 + 默认规则）。
//   - 扫描用户模板目录（默认 ~/.eda/templates/，每份子目录一个 template.toml）。
//   - list()：返回内置 + 用户模板全集（内置在前）。
//   - find(id)：按 id 查找（用户模板优先于内置，允许覆盖定制）。
//   - resolve_params(def, overrides)：合并默认值与覆盖，供向导 UI 预览。
//   - instantiate(def, overrides)：实例化为新 ProjectTree（填充板 / 图页）。
//
// 实例化语义：
//   - 新工程 / 板 / 页 UUID 全部新生成（不复用模板固定 UUID）。
//   - Project 名 = 模板 name + " Project"。
//   - 加 1 块 board_type() 指定类型的板；板名嵌入解析后的尺寸（若模板声明了
//     board_width / board_height 参数），形如 "Main (100x80)"。
//   - 板下挂 1 张原理图页（res://sheets/main.sch.toml）+ 1 张 PCB 页
//     （res://boards/main.pcb.toml）。
//   - 解析后的默认规则 / 叠层不写入 ProjectTree（Project/Board/Page 当前不携带
//     规则字段）；由调用方（向导 UI）通过 def.default_rules() / resolved_params
//     单独取出，写入独立的规则文件（P7 / P8 落地）。
//
// 依赖：eda/project/template_def.h、eda/project/project.h、core/object/ref.h。
// 不改 CMakeLists.txt：eda/project/template_library.cpp 由主循环加入 eda_project。

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/object/ref.h"
#include "eda/project/project.h"
#include "eda/project/template_def.h"

namespace eda {

// ============================================================================
// TemplateLibrary —— 模板清单 + 实例化入口
// ============================================================================

class TemplateLibrary {
public:
    // 内置模板 id 常量（顺序稳定；list() 内置段按此顺序输出）。
    static constexpr const char* kBuiltinIdBlank       = "blank";
    static constexpr const char* kBuiltinIdArduinoUno  = "arduino_uno";
    static constexpr const char* kBuiltinIdEsp32DevKit = "esp32_devkit";
    static constexpr const char* kBuiltinIdRpiHat      = "rpi_hat";

    // 默认用户模板目录：~/.eda/templates/（跨平台 HOME / USERPROFILE 解析；
    // 无法解析时返回空串，list 退化为仅返回内置模板）。
    static std::string default_user_templates_dir();

    // 使用 default_user_templates_dir() 作为用户模板目录。
    TemplateLibrary();
    // 指定用户模板目录（测试 / 自定义安装路径用）。
    explicit TemplateLibrary(std::string user_templates_dir);

    // 列出所有模板：内置在前，用户模板在后。用户模板可包含同 id 内置（覆盖语义）。
    // 用户目录不存在 / 为空时仅返回内置。
    std::vector<TemplateDef> list() const;

    // 按 id 查找。用户模板优先于内置（允许覆盖）；找不到返回空 optional。
    std::optional<TemplateDef> find(const std::string& id) const;

    // 合并参数默认值与覆盖。仅写入 def.params() 中声明的键；
    // overrides 中未声明的键被忽略（避免向导 UI 注入无关字段）。
    static std::map<std::string, std::string> resolve_params(
        const TemplateDef& def,
        const std::map<std::string, std::string>& overrides);

    // 实例化为新工程树。def.id / def.name 为空或板未生成时返回空 Ref。
    // overrides 语义同 resolve_params。
    Ref<ProjectTree> instantiate(
        const TemplateDef& def,
        const std::map<std::string, std::string>& overrides) const;

    const std::string& user_templates_dir() const { return user_templates_dir_; }

private:
    // 构造内置模板（硬编码 4 份）。
    static std::vector<TemplateDef> builtins();
    // 扫描用户模板目录（每份子目录一个 template.toml）。
    std::vector<TemplateDef> scan_user_dir() const;

    std::string user_templates_dir_;
};

}  // namespace eda
