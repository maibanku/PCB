#pragma once

// eda/project/template_def.h
//
// P5 Task 3 · 工程模板定义（TemplateDef）+ template.toml 加载。
//
// 范围：
//   - TemplateDef：模板元信息（id / name / category / description）+ 板类型 +
//     参数化变量（params）+ 默认设计规则（间距 / 线宽 / 过孔）+ 叠层预设（2/4 层）。
//   - TemplateLoader：从 template.toml 文本 / 文件加载（最小 TOML 子集，行扫描状态机，
//     与 ProjectLoader 风格一致——不引 toml++ 依赖）。
//
// template.toml schema（首版 schema_version = 1）：
//   schema_version = 1
//   id = "blank"
//   name = "Blank"
//   category = "Basic"
//   description = "Empty project starting point."
//   board_type = "main"
//
//   [default_rules]
//   clearance_mm  = 0.20
//   track_width_mm = 0.20
//   via_drill_mm  = 0.30
//
//   [stackup]
//   layer_count = 2
//
//   [[params]]
//   name = "board_width"
//   type = "int"
//   default = "100"
//   min = "10"
//   max = "1000"
//
//   [[params]]
//   name = "color"
//   type = "enum"
//   default = "red"
//   options = ["red", "green", "blue"]
//
// 设计：
//   - TemplateDef / TemplateParam / TemplateRules / StackupPreset 全部为值类型，
//     可拷贝、深比较（operator== default），便于 list / find 返回值。
//   - 参数值统一以字符串承载（portable，跨 INT/FLOAT/BOOL/ENUM 类型不丢失原义）；
//     min/max 同样用字符串，避免浮点精度在序列化往返中漂移。
//   - 加载器忽略未识别字段（前向兼容）；缺字段沿用默认值。
//
// 依赖：eda/project/project.h（BoardType / parse_board_type）。
// 不改 CMakeLists.txt：eda/project/template_def.cpp 由主循环加入 eda_project target。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "eda/project/project.h"

namespace eda {

// ============================================================================
// 模板参数类型
// ============================================================================

enum class TemplateParamType {
    STRING,
    INT,
    FLOAT,
    BOOL,
    ENUM,
};

// 序列化用字符串名：STRING -> "string"，INT -> "int"，FLOAT -> "float"，
// BOOL -> "bool"，ENUM -> "enum"。
std::string_view to_string_view(TemplateParamType t);
// 反序列化：未识别字符串默认 STRING（前向兼容）。
TemplateParamType parse_template_param_type(std::string_view s);

// ============================================================================
// 模板参数定义（值类型，深比较）
// ============================================================================

struct TemplateParam {
    std::string name;
    TemplateParamType type = TemplateParamType::STRING;
    std::string default_value;       // 字符串形式承载（portable）
    bool has_min = false;
    std::string min_value;           // 字符串形式（避免浮点精度漂移）
    bool has_max = false;
    std::string max_value;
    std::vector<std::string> options;  // ENUM 候选值

    bool operator==(const TemplateParam&) const = default;
};

// ============================================================================
// 默认设计规则（间距 / 线宽 / 过孔，单位 mm）
// ============================================================================

struct TemplateRules {
    double clearance_mm = 0.20;   // 最小间距
    double track_width_mm = 0.20; // 默认线宽
    double via_drill_mm = 0.30;   // 默认过孔钻孔径

    bool operator==(const TemplateRules&) const = default;
};

// ============================================================================
// 叠层预设
// ============================================================================

struct StackupPreset {
    int layer_count = 2;  // 常用 2 / 4

    bool operator==(const StackupPreset&) const = default;
};

// ============================================================================
// TemplateDef —— 单个工程模板的完整定义
// ============================================================================

class TemplateDef {
public:
    TemplateDef() = default;
    ~TemplateDef() = default;
    TemplateDef(const TemplateDef&) = default;
    TemplateDef& operator=(const TemplateDef&) = default;
    TemplateDef(TemplateDef&&) noexcept = default;
    TemplateDef& operator=(TemplateDef&&) noexcept = default;

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& category() const { return category_; }
    const std::string& description() const { return description_; }
    BoardType board_type() const { return board_type_; }
    const std::vector<TemplateParam>& params() const { return params_; }
    const TemplateRules& default_rules() const { return default_rules_; }
    const StackupPreset& stackup_preset() const { return stackup_preset_; }

    void set_id(std::string s) { id_ = std::move(s); }
    void set_name(std::string s) { name_ = std::move(s); }
    void set_category(std::string s) { category_ = std::move(s); }
    void set_description(std::string s) { description_ = std::move(s); }
    void set_board_type(BoardType t) { board_type_ = t; }
    void set_default_rules(TemplateRules r) { default_rules_ = r; }
    void set_stackup_preset(StackupPreset s) { stackup_preset_ = s; }

    // 新增一个参数槽，返回引用便于链式配置。
    TemplateParam& add_param();
    // 按名查找参数（线性扫描；模板参数数量级小，无需 hash）。
    const TemplateParam* find_param(const std::string& name) const;

    bool operator==(const TemplateDef&) const = default;

private:
    std::string id_;
    std::string name_;
    std::string category_;
    std::string description_;
    BoardType board_type_ = BoardType::MAIN;
    std::vector<TemplateParam> params_;
    TemplateRules default_rules_;
    StackupPreset stackup_preset_;
};

// ============================================================================
// TemplateLoader —— template.toml 读写门面（最小 TOML 子集）
// ============================================================================

class TemplateLoader {
public:
    // 解析 template.toml 文本，填充到 def（解析前重置 def 为空白状态）。
    // 解析失败（严重语法错）仍返回 true 并尽量填充——失败容忍，与 ProjectLoader 一致。
    static bool parse_into(TemplateDef& def, const std::string& text);
    // 加载 template.toml 文件，填充到 def。文件不可读返回 false。
    static bool load_into(TemplateDef& def, const std::string& path);
    // 解析文本 -> 新 TemplateDef。
    static std::optional<TemplateDef> parse(const std::string& text);
    // 加载文件 -> 新 TemplateDef。
    static std::optional<TemplateDef> load(const std::string& path);
};

}  // namespace eda
