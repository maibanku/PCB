// Theme —— 主题 token 系统实现（P4 Task 3）。
//
// 含：
//   - Theme 四类 token 查表（color/font/icon/spacing，缺失返回类型零值）。
//   - WCAG 2.1 §1.4.11 对比度工具（srgb_to_linear / relative_luminance / contrast_ratio）。
//   - 三套内置预设 LIGHT / DARK / HIGH_CONTRAST（默认 ~21 颜色 token + 字体/图标/间距若干）。
//   - ThemeManager 单例（Meyers singleton）：set_theme/reload + theme_changed 信号广播。

#include "scene/gui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace eda {

// ===========================================================================
// Theme —— token 查表
// ===========================================================================

void Theme::set_color(const std::string& key, Color value) {
    colors_[key] = value;
}

Color Theme::get_color(const std::string& key) const {
    auto it = colors_.find(key);
    if (it == colors_.end()) {
        // 缺失返回不透明黑（与 core/math/color.h 默认 Color 一致），便于调用方识别。
        return Color{0.0f, 0.0f, 0.0f, 1.0f};
    }
    return it->second;
}

bool Theme::has_color(const std::string& key) const {
    return colors_.find(key) != colors_.end();
}

void Theme::set_font(const std::string& key, const Variant& font) {
    fonts_[key] = font;
}

Variant Theme::get_font(const std::string& key) const {
    auto it = fonts_.find(key);
    if (it == fonts_.end()) {
        return Variant();  // NIL
    }
    return it->second;
}

bool Theme::has_font(const std::string& key) const {
    return fonts_.find(key) != fonts_.end();
}

void Theme::set_icon(const std::string& key, const Variant& icon) {
    icons_[key] = icon;
}

Variant Theme::get_icon(const std::string& key) const {
    auto it = icons_.find(key);
    if (it == icons_.end()) {
        return Variant();  // NIL
    }
    return it->second;
}

bool Theme::has_icon(const std::string& key) const {
    return icons_.find(key) != icons_.end();
}

void Theme::set_spacing(const std::string& key, float value) {
    spacings_[key] = value;
}

float Theme::get_spacing(const std::string& key) const {
    auto it = spacings_.find(key);
    if (it == spacings_.end()) {
        return 0.0f;
    }
    return it->second;
}

bool Theme::has_spacing(const std::string& key) const {
    return spacings_.find(key) != spacings_.end();
}

// ===========================================================================
// WCAG 2.1 §1.4.11 对比度工具
// ===========================================================================
//
// sRGB 分量 c ∈ [0,1] → 线性 RGB。WCAG 给出的分段函数：
//   c <= 0.04045 : c / 12.92
//   c >  0.04045 : ((c + 0.055) / 1.055) ^ 2.4
// （0.04045 是数学上连续点；W3C 原始文档写 0.03928，等价于截止阈值取 0.04045。）

float srgb_to_linear(float c) {
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float relative_luminance(Color c) {
    // 仅 RGB 通道参与（alpha 决定合成而非自发光，WCAG 不计）。
    float r = srgb_to_linear(c.r);
    float g = srgb_to_linear(c.g);
    float b = srgb_to_linear(c.b);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float contrast_ratio(Color a, Color b) {
    float la = relative_luminance(a);
    float lb = relative_luminance(b);
    float lighter = std::max(la, lb);
    float darker  = std::min(la, lb);
    return (lighter + 0.05f) / (darker + 0.05f);
}

bool meets_wcag_aa_text(Color foreground, Color background) {
    return contrast_ratio(foreground, background) >= 4.5f;
}

// ===========================================================================
// 预设工厂
// ===========================================================================
//
// 颜色 token 命名集（三套预设共用键名，便于代码只查 token、换肤零改动）：
//   background / foreground / text
//   secondary_background / secondary_foreground
//   accent / accent_foreground
//   primary / secondary
//   border / disabled / disabled_foreground
//   success / warning / error
//   highlight / highlight_foreground
//   shadow / overlay / focus_border / link
//
// HIGH_CONTRAST：所有文本 vs 其承载背景对比度 ≥ 4.5:1（WCAG 2.1 AA，见 test_theme）。

namespace {

// 便捷：构造字体 Variant（DICTIONARY：family/size/weight）。
Variant make_font_variant(const std::string& family, int size, int weight) {
    std::unordered_map<std::string, Variant> dict;
    dict.emplace("family", Variant(family));
    dict.emplace("size",   Variant(static_cast<int64_t>(size)));
    dict.emplace("weight", Variant(static_cast<int64_t>(weight)));
    return Variant(dict);
}

// 便捷：构造图标 Variant（DICTIONARY：name/size）。
Variant make_icon_variant(const std::string& name, int size) {
    std::unordered_map<std::string, Variant> dict;
    dict.emplace("name", Variant(name));
    dict.emplace("size", Variant(static_cast<int64_t>(size)));
    return Variant(dict);
}

// 三套预设共用的非颜色 token（字体/图标/间距）。换肤不改动这些。
// 字体具体整形（HarfBuzz/FreeType）由 scene/gui/fonts/font_provider（Task 3 协同）落地，
// 本表仅保留 token 描述供控件查表选用。
void apply_common_tokens(Theme& t) {
    // 字体
    t.set_font("default", make_font_variant("Inter",           14, 400));
    t.set_font("small",   make_font_variant("Inter",           12, 400));
    t.set_font("heading", make_font_variant("Inter",           18, 600));
    t.set_font("mono",    make_font_variant("JetBrains Mono",  13, 400));

    // 图标（name 为逻辑标识；前端按主题前景色染色）
    t.set_icon("close",     make_icon_variant("close",     16));
    t.set_icon("open",      make_icon_variant("open",      16));
    t.set_icon("save",      make_icon_variant("save",      16));
    t.set_icon("undo",      make_icon_variant("undo",      16));
    t.set_icon("redo",      make_icon_variant("redo",      16));
    t.set_icon("zoom_in",   make_icon_variant("zoom_in",   16));
    t.set_icon("zoom_out",  make_icon_variant("zoom_out",  16));
    t.set_icon("settings",  make_icon_variant("settings",  16));

    // 间距（逻辑像素；ui_scale 缩放在渲染层统一处理）
    t.set_spacing("padding_small",  4.0f);
    t.set_spacing("padding",        8.0f);
    t.set_spacing("padding_large", 12.0f);
    t.set_spacing("margin_small",   4.0f);
    t.set_spacing("margin",         8.0f);
    t.set_spacing("margin_large",  16.0f);
    t.set_spacing("gap",            8.0f);
    t.set_spacing("border_radius",  4.0f);
    t.set_spacing("border_width",   1.0f);
}

// 便捷：从十六进制 #RRGGBB 构造 Color（alpha 默认 1.0）。
// 仅预设工厂内部使用——业务代码严禁用它构造颜色（Global Constraints：禁硬编码颜色，
// 必须走 Theme::get_color 查 token）。
Color hex(uint32_t rgb, float alpha = 1.0f) {
    Color c;
    c.r = static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f;
    c.g = static_cast<float>((rgb >> 8)  & 0xFFu) / 255.0f;
    c.b = static_cast<float>( rgb        & 0xFFu) / 255.0f;
    c.a = alpha;
    return c;
}

void apply_light_colors(Theme& t) {
    t.set_color("background",           hex(0xF5F5F5));
    t.set_color("foreground",           hex(0x1A1A1A));
    t.set_color("text",                 hex(0x1A1A1A));
    t.set_color("secondary_background", hex(0xFFFFFF));
    t.set_color("secondary_foreground", hex(0x333333));
    t.set_color("accent",               hex(0x0066CC));
    t.set_color("accent_foreground",    hex(0xFFFFFF));
    t.set_color("primary",              hex(0x0066CC));
    t.set_color("secondary",            hex(0x666666));
    t.set_color("border",               hex(0xC0C0C0));
    t.set_color("disabled",             hex(0xE0E0E0));
    t.set_color("disabled_foreground",  hex(0x999999));
    t.set_color("success",              hex(0x2E7D32));
    t.set_color("warning",              hex(0xED6C02));
    t.set_color("error",                hex(0xC62828));
    t.set_color("highlight",            hex(0x0078D4));
    t.set_color("highlight_foreground", hex(0xFFFFFF));
    t.set_color("shadow",               Color{0.0f, 0.0f, 0.0f, 0.15f});
    t.set_color("overlay",              Color{0.0f, 0.0f, 0.0f, 0.50f});
    t.set_color("focus_border",         hex(0x0066CC));
    t.set_color("link",                 hex(0x0066CC));
}

void apply_dark_colors(Theme& t) {
    t.set_color("background",           hex(0x1E1E1E));
    t.set_color("foreground",           hex(0xE0E0E0));
    t.set_color("text",                 hex(0xE0E0E0));
    t.set_color("secondary_background", hex(0x2D2D2D));
    t.set_color("secondary_foreground", hex(0xCCCCCC));
    t.set_color("accent",               hex(0x4FC3F7));
    t.set_color("accent_foreground",    hex(0x000000));
    t.set_color("primary",              hex(0x4FC3F7));
    t.set_color("secondary",            hex(0xAAAAAA));
    t.set_color("border",               hex(0x3C3C3C));
    t.set_color("disabled",             hex(0x2A2A2A));
    t.set_color("disabled_foreground",  hex(0x6E6E6E));
    t.set_color("success",              hex(0x81C784));
    t.set_color("warning",              hex(0xFFB74D));
    t.set_color("error",                hex(0xEF5350));
    t.set_color("highlight",            hex(0x264F78));
    t.set_color("highlight_foreground", hex(0xFFFFFF));
    t.set_color("shadow",               Color{0.0f, 0.0f, 0.0f, 0.40f});
    t.set_color("overlay",              Color{0.0f, 0.0f, 0.0f, 0.70f});
    t.set_color("focus_border",         hex(0x4FC3F7));
    t.set_color("link",                 hex(0x4FC3F7));
}

void apply_high_contrast_colors(Theme& t) {
    // WCAG 2.1 AA：所有正常文本 vs 承载背景 ≥ 4.5:1。黑底纯色组合普遍达 5:1 以上。
    // 关键对（见 test_theme 校验）：
    //   foreground/background          = #FFFFFF/#000000 ≈ 21:1
    //   accent_foreground/accent       = #000000/#FFFF00 ≈ 19.6:1
    //   error/background               = #FF0000/#000000 ≈ 5.25:1
    //   success, warning 背景投影      = 10:1+
    t.set_color("background",           hex(0x000000));
    t.set_color("foreground",           hex(0xFFFFFF));
    t.set_color("text",                 hex(0xFFFFFF));
    t.set_color("secondary_background", hex(0x1A1A1A));
    t.set_color("secondary_foreground", hex(0xFFFFFF));
    t.set_color("accent",               hex(0xFFFF00));
    t.set_color("accent_foreground",    hex(0x000000));
    t.set_color("primary",              hex(0xFFFF00));
    t.set_color("secondary",            hex(0xFFFFFF));
    t.set_color("border",               hex(0xFFFFFF));
    t.set_color("disabled",             hex(0x555555));
    t.set_color("disabled_foreground",  hex(0xCCCCCC));
    t.set_color("success",              hex(0x00FF00));
    t.set_color("warning",              hex(0xFFA500));
    t.set_color("error",                hex(0xFF0000));
    t.set_color("highlight",            hex(0xFFFF00));
    t.set_color("highlight_foreground", hex(0x000000));
    t.set_color("shadow",               Color{0.0f, 0.0f, 0.0f, 0.60f});
    t.set_color("overlay",              Color{0.0f, 0.0f, 0.0f, 0.85f});
    t.set_color("focus_border",         hex(0xFFFF00));
    t.set_color("link",                 hex(0xFFFF00));
}

}  // namespace

Theme Theme::make_light() {
    Theme t;
    apply_light_colors(t);
    apply_common_tokens(t);
    return t;
}

Theme Theme::make_dark() {
    Theme t;
    apply_dark_colors(t);
    apply_common_tokens(t);
    return t;
}

Theme Theme::make_high_contrast() {
    Theme t;
    apply_high_contrast_colors(t);
    apply_common_tokens(t);
    return t;
}

Theme Theme::make_preset(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::LIGHT:         return make_light();
        case ThemePreset::DARK:          return make_dark();
        case ThemePreset::HIGH_CONTRAST: return make_high_contrast();
    }
    return make_light();  // 兜底（理论不可达）
}

// ===========================================================================
// ThemeManager —— 单例
// ===========================================================================

ThemeManager::ThemeManager() {
    // 初始主题为 LIGHT（最常用默认；可后续由用户偏好覆盖）。
    current_ = Theme::make_light();
    // 注意：构造期不 emit theme_changed——首批控件尚未 connect，emit 也是空跑。
}

ThemeManager& ThemeManager::get() {
    // C++11 函数内 static：线程安全初始化（Meyers singleton）。
    static ThemeManager instance;
    return instance;
}

void ThemeManager::set_theme(ThemePreset preset) {
    preset_ = preset;
    current_ = Theme::make_preset(preset);
    // 广播：控件在槽里 queue_redraw 完成换肤重绘。
    theme_changed_.emit();
}

void ThemeManager::reload() {
    // 用当前预设重建——丢弃运行时 set_color 等覆盖，恢复内置调色。
    current_ = Theme::make_preset(preset_);
    theme_changed_.emit();
}

}  // namespace eda
