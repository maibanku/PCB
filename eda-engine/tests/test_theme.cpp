// Theme —— 主题系统单元测试（P4 Task 3）。
//
// 覆盖：
//   1. LIGHT / DARK / HIGH_CONTRAST 三套预设各 token 存在（颜色 ~21 + 字体 4 + 图标 8 + 间距 9）。
//   2. ThemeManager::set_theme / reload 切换后 theme_changed 信号触发（计数器验证）。
//   3. WCAG 工具：contrast_ratio 公式正确性 + HIGH_CONTRAST 所有文本对 ≥ 4.5:1（AA）。
//   4. 自定义 token 覆盖预设（set_color/set_spacing 不影响其他 token）。
//   5. 查表缺失 key 返回类型默认零值（Color{0,0,0,1} / NIL Variant / 0.0f）。
//   6. 辅助：make_preset 分发正确；ThemeManager.get_current 反映最后一次切换；
//      切换后当前主题的 background 与对应预设一致。

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math/color.h"
#include "core/object/callable.h"
#include "core/object/variant.h"
#include "scene/gui/theme.h"

using namespace eda;

namespace {

// 三套预设共用的颜色 token 键集（与 theme.cpp 的 apply_*_colors 一致）。
const std::vector<std::string>& expected_color_tokens() {
    static const std::vector<std::string> tokens = {
        "background", "foreground", "text",
        "secondary_background", "secondary_foreground",
        "accent", "accent_foreground",
        "primary", "secondary",
        "border", "disabled", "disabled_foreground",
        "success", "warning", "error",
        "highlight", "highlight_foreground",
        "shadow", "overlay",
        "focus_border", "link",
    };
    return tokens;
}

const std::vector<std::string>& expected_font_tokens() {
    static const std::vector<std::string> tokens = {
        "default", "small", "heading", "mono",
    };
    return tokens;
}

const std::vector<std::string>& expected_icon_tokens() {
    static const std::vector<std::string> tokens = {
        "close", "open", "save", "undo", "redo",
        "zoom_in", "zoom_out", "settings",
    };
    return tokens;
}

const std::vector<std::string>& expected_spacing_tokens() {
    static const std::vector<std::string> tokens = {
        "padding_small", "padding", "padding_large",
        "margin_small", "margin", "margin_large",
        "gap", "border_radius", "border_width",
    };
    return tokens;
}

}  // namespace

// ===========================================================================
// 1. 预设 token 完整性
// ===========================================================================

TEST(ThemePresetTest, AllPresetsHaveFullColorTokenSet) {
    for (ThemePreset p : {ThemePreset::LIGHT,
                          ThemePreset::DARK,
                          ThemePreset::HIGH_CONTRAST}) {
        Theme t = Theme::make_preset(p);
        for (const auto& key : expected_color_tokens()) {
            EXPECT_TRUE(t.has_color(key))
                << theme_preset_name(p) << " missing color token: " << key;
        }
    }
}

TEST(ThemePresetTest, AllPresetsHaveFullFontTokenSet) {
    for (ThemePreset p : {ThemePreset::LIGHT,
                          ThemePreset::DARK,
                          ThemePreset::HIGH_CONTRAST}) {
        Theme t = Theme::make_preset(p);
        for (const auto& key : expected_font_tokens()) {
            EXPECT_TRUE(t.has_font(key))
                << theme_preset_name(p) << " missing font token: " << key;
        }
    }
}

TEST(ThemePresetTest, AllPresetsHaveFullIconTokenSet) {
    for (ThemePreset p : {ThemePreset::LIGHT,
                          ThemePreset::DARK,
                          ThemePreset::HIGH_CONTRAST}) {
        Theme t = Theme::make_preset(p);
        for (const auto& key : expected_icon_tokens()) {
            EXPECT_TRUE(t.has_icon(key))
                << theme_preset_name(p) << " missing icon token: " << key;
        }
    }
}

TEST(ThemePresetTest, AllPresetsHaveFullSpacingTokenSet) {
    for (ThemePreset p : {ThemePreset::LIGHT,
                          ThemePreset::DARK,
                          ThemePreset::HIGH_CONTRAST}) {
        Theme t = Theme::make_preset(p);
        for (const auto& key : expected_spacing_tokens()) {
            EXPECT_TRUE(t.has_spacing(key))
                << theme_preset_name(p) << " missing spacing token: " << key;
            // 间距必须 > 0（否则布局坍缩）。
            EXPECT_GT(t.get_spacing(key), 0.0f) << key;
        }
    }
}

TEST(ThemePresetTest, MakePresetDispatchesByEnum) {
    // make_preset(p) 与具名工厂的颜色应一致。
    EXPECT_FLOAT_EQ(
        Theme::make_preset(ThemePreset::LIGHT).get_color("background").r,
        Theme::make_light().get_color("background").r);
    EXPECT_FLOAT_EQ(
        Theme::make_preset(ThemePreset::DARK).get_color("background").r,
        Theme::make_dark().get_color("background").r);
    EXPECT_FLOAT_EQ(
        Theme::make_preset(ThemePreset::HIGH_CONTRAST).get_color("background").r,
        Theme::make_high_contrast().get_color("background").r);
}

// 预设背景调性 sanity check：LIGHT 亮、DARK 暗、HIGH_CONTRAST 纯黑。
TEST(ThemePresetTest, PresetBackgroundToneMatchesIntent) {
    EXPECT_GT(Theme::make_light().get_color("background").r, 0.5f);          // 亮
    EXPECT_LT(Theme::make_dark().get_color("background").r, 0.5f);           // 暗
    EXPECT_FLOAT_EQ(Theme::make_high_contrast().get_color("background").r,
                    0.0f);  // 纯黑
    EXPECT_FLOAT_EQ(Theme::make_high_contrast().get_color("foreground").r,
                    1.0f);  // 纯白
}

// 字体 Variant 结构：{family, size, weight}。
TEST(ThemeFontIconTest, DefaultFontIsDictionaryWithExpectedFields) {
    Theme t = Theme::make_light();
    Variant f = t.get_font("default");
    ASSERT_EQ(f.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(f.dict_at("family").as_string(), std::string("Inter"));
    EXPECT_EQ(f.dict_at("size").as_int(),   static_cast<int64_t>(14));
    EXPECT_EQ(f.dict_at("weight").as_int(), static_cast<int64_t>(400));
}

// 图标 Variant 结构：{name, size}。
TEST(ThemeFontIconTest, IconIsDictionaryWithNameAndSize) {
    Theme t = Theme::make_dark();
    Variant ic = t.get_icon("close");
    ASSERT_EQ(ic.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(ic.dict_at("name").as_string(), std::string("close"));
    EXPECT_EQ(ic.dict_at("size").as_int(),  static_cast<int64_t>(16));
}

// ===========================================================================
// 2. ThemeManager 切换信号
// ===========================================================================
//
// 单例跨测试持久——每个测试 connect 后必须 disconnect，避免悬空 lambda 捕获。

TEST(ThemeManagerTest, SetThemeEmitsThemeChanged) {
    auto& mgr = ThemeManager::get();
    int fired = 0;
    Callable cb([&fired]() { ++fired; });
    mgr.theme_changed().connect(cb);
    ASSERT_EQ(mgr.theme_changed().connection_count(), 1u);

    mgr.set_theme(ThemePreset::DARK);
    EXPECT_EQ(fired, 1);

    mgr.set_theme(ThemePreset::HIGH_CONTRAST);
    EXPECT_EQ(fired, 2);

    // 同预设再次切换也应触发（语义：每次 set_theme 都广播，控件无需 diff）。
    mgr.set_theme(ThemePreset::HIGH_CONTRAST);
    EXPECT_EQ(fired, 3);

    mgr.theme_changed().disconnect(cb);
    EXPECT_EQ(mgr.theme_changed().connection_count(), 0u);
}

TEST(ThemeManagerTest, ReloadEmitsThemeChanged) {
    auto& mgr = ThemeManager::get();
    int fired = 0;
    Callable cb([&fired]() { ++fired; });
    mgr.theme_changed().connect(cb);

    mgr.reload();
    EXPECT_EQ(fired, 1);

    mgr.theme_changed().disconnect(cb);
}

TEST(ThemeManagerTest, SetThemeWithoutConnectionsIsSafe) {
    // 单例初始可能无连接；emit 不应崩溃（快照为空 for-loop 空跑）。
    auto& mgr = ThemeManager::get();
    EXPECT_NO_THROW({
        mgr.set_theme(ThemePreset::LIGHT);
        mgr.set_theme(ThemePreset::DARK);
    });
}

TEST(ThemeManagerTest, GetCurrentReflectsLastSetTheme) {
    auto& mgr = ThemeManager::get();

    mgr.set_theme(ThemePreset::LIGHT);
    ASSERT_EQ(mgr.get_current_preset(), ThemePreset::LIGHT);
    Color light_bg = mgr.get_current().get_color("background");

    mgr.set_theme(ThemePreset::DARK);
    EXPECT_EQ(mgr.get_current_preset(), ThemePreset::DARK);
    Color dark_bg = mgr.get_current().get_color("background");

    // LIGHT bg (#F5F5F5 ≈ 0.96) vs DARK bg (#1E1E1E ≈ 0.118)：r 通道差异显著。
    EXPECT_NE(light_bg.r, dark_bg.r);
    EXPECT_GT(light_bg.r, dark_bg.r);

    // 当前主题应与对应预设工厂产物一致。
    EXPECT_FLOAT_EQ(dark_bg.r, Theme::make_dark().get_color("background").r);
}

TEST(ThemeManagerTest, ReloadRestoresPresetDiscardingOverrides) {
    auto& mgr = ThemeManager::get();
    mgr.set_theme(ThemePreset::LIGHT);
    // 运行时覆盖（应被 reload 清掉）。
    mgr.get_current().set_color("background", Color{0.123f, 0.456f, 0.789f, 1.0f});
    EXPECT_FLOAT_EQ(mgr.get_current().get_color("background").r, 0.123f);

    mgr.reload();
    EXPECT_FLOAT_EQ(mgr.get_current().get_color("background").r,
                    Theme::make_light().get_color("background").r);
}

// ===========================================================================
// 3. WCAG 对比度
// ===========================================================================

TEST(WcagContrastTest, WhiteOnBlackIsTwentyOne) {
    Color white{1.0f, 1.0f, 1.0f, 1.0f};
    Color black{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(contrast_ratio(white, black), 21.0f, 0.05f);
    EXPECT_NEAR(contrast_ratio(black, white), 21.0f, 0.05f);  // 对称
}

TEST(WcagContrastTest, SameColorIsOne) {
    Color gray{0.5f, 0.5f, 0.5f, 1.0f};
    EXPECT_NEAR(contrast_ratio(gray, gray), 1.0f, 0.001f);
}

TEST(WcagContrastTest, RelativeLuminanceBounds) {
    EXPECT_NEAR(relative_luminance(Color{0, 0, 0, 1}), 0.0f, 0.001f);
    EXPECT_NEAR(relative_luminance(Color{1, 1, 1, 1}), 1.0f, 0.001f);
    // 中灰 0.5 → 线性 ≈ 0.214（sRGB 非线性）。
    EXPECT_NEAR(relative_luminance(Color{0.5f, 0.5f, 0.5f, 1}), 0.2140f, 0.005f);
}

TEST(WcagContrastTest, MeetsWcagAaTextHelper) {
    EXPECT_TRUE(meets_wcag_aa_text(Color{1, 1, 1, 1}, Color{0, 0, 0, 1}));
    EXPECT_FALSE(meets_wcag_aa_text(Color{0.5f, 0.5f, 0.5f, 1}, Color{1, 1, 1, 1}));
}

// HIGH_CONTRAST 的所有正常文本 vs 承载背景必须 ≥ 4.5:1（WCAG 2.1 AA）。
TEST(ThemeHighContrastTest, AllTextPairsMeetWcagAa) {
    Theme t = Theme::make_high_contrast();

    struct Pair {
        std::string fg;
        std::string bg;
    };
    const std::vector<Pair> pairs = {
        {"foreground",          "background"},
        {"text",                "background"},
        {"secondary_foreground","secondary_background"},
        {"accent_foreground",   "accent"},
        {"highlight_foreground","highlight"},
        // 状态色常作文本/图标：对 background 检查。
        {"error",               "background"},
        {"success",             "background"},
        {"warning",             "background"},
        {"link",                "background"},
        {"focus_border",        "background"},
    };

    for (const auto& p : pairs) {
        Color fg = t.get_color(p.fg);
        Color bg = t.get_color(p.bg);
        float ratio = contrast_ratio(fg, bg);
        EXPECT_GE(ratio, 4.5f)
            << "HIGH_CONTRAST " << p.fg << " vs " << p.bg
            << " ratio=" << ratio << " (need >= 4.5)";
    }
}

// LIGHT/DARK 主文本也应可读（≥ 4.5:1）——虽非 Task 3 强制项，但保证默认体验。
TEST(ThemeReadableTest, LightAndDarkForegroundReadable) {
    Theme light = Theme::make_light();
    Theme dark  = Theme::make_dark();
    EXPECT_GE(contrast_ratio(light.get_color("foreground"),
                             light.get_color("background")), 4.5f);
    EXPECT_GE(contrast_ratio(dark.get_color("foreground"),
                             dark.get_color("background")), 4.5f);
}

// ===========================================================================
// 4. 自定义 token 覆盖
// ===========================================================================

TEST(ThemeOverrideTest, CustomColorOverridesPresetWithoutAffectingOthers) {
    Theme t = Theme::make_light();
    Color fg_before = t.get_color("foreground");

    Color custom{0.5f, 0.2f, 0.8f, 1.0f};
    t.set_color("background", custom);

    EXPECT_FLOAT_EQ(t.get_color("background").r, custom.r);
    EXPECT_FLOAT_EQ(t.get_color("background").g, custom.g);
    EXPECT_FLOAT_EQ(t.get_color("background").b, custom.b);
    EXPECT_FLOAT_EQ(t.get_color("background").a, custom.a);
    // foreground 未受影响。
    EXPECT_FLOAT_EQ(t.get_color("foreground").r, fg_before.r);
}

TEST(ThemeOverrideTest, CustomSpacingAddedAlongsideDefaults) {
    Theme t = Theme::make_dark();
    EXPECT_FALSE(t.has_spacing("custom_gap"));

    t.set_spacing("custom_gap", 24.0f);
    EXPECT_TRUE(t.has_spacing("custom_gap"));
    EXPECT_FLOAT_EQ(t.get_spacing("custom_gap"), 24.0f);

    // 内置 spacing 仍存在。
    EXPECT_FLOAT_EQ(t.get_spacing("padding"), 8.0f);
}

TEST(ThemeOverrideTest, CustomFontOverridesPreset) {
    Theme t = Theme::make_light();
    std::unordered_map<std::string, Variant> custom_font;
    custom_font.emplace("family", Variant(std::string("Comic Sans")));
    custom_font.emplace("size",   Variant(static_cast<int64_t>(99)));
    custom_font.emplace("weight", Variant(static_cast<int64_t>(700)));
    t.set_font("default", Variant(custom_font));

    Variant v = t.get_font("default");
    EXPECT_EQ(v.dict_at("family").as_string(), std::string("Comic Sans"));
    EXPECT_EQ(v.dict_at("size").as_int(),      static_cast<int64_t>(99));
}

TEST(ThemeOverrideTest, CustomIconAddedAsDictionary) {
    Theme t = Theme::make_light();
    std::unordered_map<std::string, Variant> custom_icon;
    custom_icon.emplace("name", Variant(std::string("route")));
    custom_icon.emplace("size", Variant(static_cast<int64_t>(24)));
    t.set_icon("route", Variant(custom_icon));

    EXPECT_TRUE(t.has_icon("route"));
    EXPECT_EQ(t.get_icon("route").dict_at("name").as_string(),
              std::string("route"));
}

// ===========================================================================
// 5. 缺失 key 返回默认零值
// ===========================================================================

TEST(ThemeMissingKeyTest, MissingColorReturnsOpaqueBlack) {
    Theme t;  // 空 Theme
    EXPECT_FALSE(t.has_color("nonexistent"));
    Color c = t.get_color("nonexistent");
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);  // 与 core/math/color.h 默认 Color 一致
}

TEST(ThemeMissingKeyTest, MissingFontReturnsNilVariant) {
    Theme t;
    EXPECT_FALSE(t.has_font("nonexistent"));
    Variant v = t.get_font("nonexistent");
    EXPECT_EQ(v.get_type(), Variant::NIL);
    EXPECT_TRUE(v.is_nil());
}

TEST(ThemeMissingKeyTest, MissingIconReturnsNilVariant) {
    Theme t;
    EXPECT_FALSE(t.has_icon("nonexistent"));
    Variant v = t.get_icon("nonexistent");
    EXPECT_EQ(v.get_type(), Variant::NIL);
}

TEST(ThemeMissingKeyTest, MissingSpacingReturnsZero) {
    Theme t;
    EXPECT_FALSE(t.has_spacing("nonexistent"));
    EXPECT_FLOAT_EQ(t.get_spacing("nonexistent"), 0.0f);
}

// 已预设的 Theme 查询未登记的 key 同样返回默认（不会误命中其他 token 类）。
TEST(ThemeMissingKeyTest, MissingKeyOnPresetThemeReturnsDefault) {
    Theme t = Theme::make_high_contrast();
    EXPECT_FALSE(t.has_color("totally_unknown"));
    EXPECT_FLOAT_EQ(t.get_color("totally_unknown").a, 1.0f);
    EXPECT_FALSE(t.has_spacing("totally_unknown"));
    EXPECT_FLOAT_EQ(t.get_spacing("totally_unknown"), 0.0f);
}
