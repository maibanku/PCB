// Theme —— 主题 token 系统（P4 Task 3）。
//
// 在 Control（P4 Task 1）之上提供统一样式查表：
//   - 四类 token：color / font / icon / spacing，每类一条 key->Variant 查表。
//     业务/控件代码只查 get_color("background") 等 token，严禁硬编码 RGB
//     （Global Constraints：主题查表禁硬编码颜色）——换肤/暗色/高对比零业务改动。
//   - 三套内置预设 LIGHT / DARK / HIGH_CONTRAST：
//       * LIGHT / DARK 满足一般可读性；
//       * HIGH_CONTRAST 文本对比度满足 WCAG 2.1 AA ≥ 4.5:1（Task 3 验收点）。
//   - 默认 token 集 ~21 项颜色 + 字体/图标/间距若干（见 theme.cpp 三套预设）。
//   - 自定义覆盖：set_color/set_font/set_icon/set_spacing 在预设之上覆盖（不破坏预设）。
//   - 缺失 key：get_* 返回类型默认零值（Color{0,0,0,1} / NIL Variant / 0.0f），不抛异常，
//     与 Variant 的 dict_at 缺键策略一致（core/object/variant.cpp）。
//
// WCAG 对比度工具（relative_luminance / contrast_ratio）——对齐 WCAG 2.1 §1.4.11；
// 仅供主题/测试消费（验证 HIGH_CONTRAST 文本对比度阈值）。
//
// ThemeManager —— 进程级单例：
//   - set_theme(ThemePreset) 构建对应预设 → 广播 theme_changed → 控件在槽里
//     queue_redraw 完成换肤重绘。
//   - get_current() 返回当前 Theme 引用；reload() 用当前预设重建（丢弃运行时覆盖）。
//   - theme_changed 是 Signal<>（无参）；控件典型用法：
//       ThemeManager::get().theme_changed().connect(&MyControl::on_theme_changed, this);
//       void on_theme_changed() { queue_redraw(); }

#pragma once

#include "core/math/color.h"
#include "core/object/signal.h"
#include "core/object/variant.h"

#include <string>
#include <unordered_map>

namespace eda {

// 主题预设。
enum class ThemePreset {
    LIGHT,
    DARK,
    HIGH_CONTRAST,
};

// 字符串化（日志/调试用）。
inline const char* theme_preset_name(ThemePreset p) {
    switch (p) {
        case ThemePreset::LIGHT:         return "LIGHT";
        case ThemePreset::DARK:          return "DARK";
        case ThemePreset::HIGH_CONTRAST: return "HIGH_CONTRAST";
    }
    return "UNKNOWN";
}

class ThemeManager;  // 前向

// ---------------------------------------------------------------------------
// Theme —— 主题 token 容器（值语义，可拷贝）。
// ---------------------------------------------------------------------------
//
// 设计与 Godot Theme Resource 对齐，但简化为四类扁平查表：
//   - colors_   : key -> Color        （通过 Variant 兼容 key->Variant 的总描述）
//   - fonts_    : key -> Variant      （DICTIONARY：{family, size, weight}）
//   - icons_    : key -> Variant      （DICTIONARY：{name, size} 或 STRING）
//   - spacings_ : key -> float        （通过 Variant 兼容 key->Variant）
//
// 缺失 key 返回该类型零值（Color{0,0,0,1} / NIL Variant / 0.0f），与 Variant 的
// 缺键策略一致；调用方可通过 has_* 先验查询。

class Theme {
public:
    Theme() = default;
    ~Theme() = default;
    Theme(const Theme&) = default;
    Theme& operator=(const Theme&) = default;
    Theme(Theme&&) noexcept = default;
    Theme& operator=(Theme&&) noexcept = default;

    // —— 颜色 token ——
    void  set_color(const std::string& key, Color value);
    Color get_color(const std::string& key) const;        // 缺失返回 Color{0,0,0,1}
    bool  has_color(const std::string& key) const;

    // —— 字体 token（DICTIONARY：family/size/weight）——
    void    set_font(const std::string& key, const Variant& font);
    Variant get_font(const std::string& key) const;       // 缺失返回 NIL Variant
    bool    has_font(const std::string& key) const;

    // —— 图标 token（DICTIONARY：name/size 或 STRING）——
    void    set_icon(const std::string& key, const Variant& icon);
    Variant get_icon(const std::string& key) const;       // 缺失返回 NIL Variant
    bool    has_icon(const std::string& key) const;

    // —— 间距 token（像素值）——
    void  set_spacing(const std::string& key, float value);
    float get_spacing(const std::string& key) const;      // 缺失返回 0.0f
    bool  has_spacing(const std::string& key) const;

    // —— token 清单只读视图（测试/调试用）——
    const std::unordered_map<std::string, Color>&   colors()   const { return colors_; }
    const std::unordered_map<std::string, Variant>& fonts()    const { return fonts_; }
    const std::unordered_map<std::string, Variant>& icons()    const { return icons_; }
    const std::unordered_map<std::string, float>&   spacings() const { return spacings_; }

    // —— 预设工厂：构建内置三套预设 ——
    static Theme make_light();
    static Theme make_dark();
    static Theme make_high_contrast();
    static Theme make_preset(ThemePreset preset);

private:
    std::unordered_map<std::string, Color>   colors_;
    std::unordered_map<std::string, Variant> fonts_;
    std::unordered_map<std::string, Variant> icons_;
    std::unordered_map<std::string, float>   spacings_;
};

// ---------------------------------------------------------------------------
// WCAG 2.1 对比度工具（§1.4.11）。
// ---------------------------------------------------------------------------

// sRGB [0,1] → 线性 RGB [0,1]（WCAG 公式）。
float srgb_to_linear(float c);

// 相对亮度 L ∈ [0,1]：0.2126·R + 0.7152·G + 0.0722·B（线性空间，忽略 alpha）。
float relative_luminance(Color c);

// 对比度比值 ∈ [1.0, 21.0]：(L_lighter + 0.05) / (L_darker + 0.05)。
float contrast_ratio(Color a, Color b);

// 便捷：是否符合 WCAG AA 正常文本 ≥ 4.5:1。
bool  meets_wcag_aa_text(Color foreground, Color background);

// ---------------------------------------------------------------------------
// ThemeManager —— 进程级单例（Meyers singleton：函数内 static，线程安全初始化）。
// ---------------------------------------------------------------------------

class ThemeManager {
public:
    static ThemeManager& get();

    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
    ThemeManager(ThemeManager&&) = delete;
    ThemeManager& operator=(ThemeManager&&) = delete;

    // 切换预设：构建对应主题 + 广播 theme_changed。
    void set_theme(ThemePreset preset);

    // 用当前预设重建主题（丢弃运行时 set_color 等覆盖）+ 广播 theme_changed。
    void reload();

    // 当前主题（只读 / 可写：直接改当前主题但不广播，需控件主动 queue_redraw；
    // 推荐改完后手动调 theme_changed().emit()）。
    Theme&       get_current()       { return current_; }
    const Theme& get_current() const { return current_; }

    ThemePreset get_current_preset() const { return preset_; }

    // 主题切换信号。控件 connect 后在槽里 queue_redraw 完成重绘。
    Signal<>& theme_changed() { return theme_changed_; }

private:
    ThemeManager();

    Theme      current_;
    ThemePreset preset_ = ThemePreset::LIGHT;
    Signal<>   theme_changed_;
};

}  // namespace eda
