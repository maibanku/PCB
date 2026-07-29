// test_font —— FontProvider + TextRenderer 验收测试（P4 文本渲染首版）。
//
// 验收点：
//   1. load_font 从真实 TTF 加载，失败路径返回 false。
//   2. get_glyph 返回非零 width/height/advance + alpha bitmap 非空。
//   3. glyph cache 命中：相同 (family, codepoint, size) 返回同一引用（指针相等）。
//   4. get_font_metrics 行度量 > 0。
//   5. ensure_loaded 走 Windows 系统兜底。
//   6. TextRenderer::render_text：每个非空格字符 6 顶点（2 三角形），缺失 token 静默 0。
//
// 单例隔离：每个测试 SetUp 调 unload_all()，避免 family / 缓存串扰。
// 字体源：C:/Windows/Fonts/arial.ttf（Windows 默认字体；CI 在 Windows 11 上跑）。

#include <gtest/gtest.h>

#include "scene/2d/canvas_2d.h"
#include "scene/gui/fonts/font_provider.h"
#include "scene/gui/fonts/text_renderer.h"
#include "scene/gui/theme.h"

#include <cstddef>
#include <string>

using namespace eda;

namespace {

constexpr const char* kArialPath = "C:/Windows/Fonts/arial.ttf";
constexpr const char* kConsolaPath = "C:/Windows/Fonts/consola.ttf";

bool arial_available() {
    return FontProvider::get().load_font("__probe__", kArialPath);
}

class FontFixture : public ::testing::Test {
protected:
    void SetUp() override { FontProvider::get().unload_all(); }
    void TearDown() override { FontProvider::get().unload_all(); }
};

bool probe_font_file(const char* path) {
    return FontProvider::get().load_font("__probe__", path);
}

}  // namespace

// ===========================================================================
// load_font
// ===========================================================================

TEST_F(FontFixture, LoadFontReturnsFalseForMissingFile) {
    EXPECT_FALSE(FontProvider::get().load_font("X", "Z:/nope/missing.ttf"));
    EXPECT_FALSE(FontProvider::get().has_family("X"));
}

TEST_F(FontFixture, LoadFontFromArialTtf) {
    ASSERT_TRUE(probe_font_file(kArialPath)) << "arial.ttf not available";
    EXPECT_TRUE(FontProvider::get().load_font("serif", kArialPath));
    EXPECT_TRUE(FontProvider::get().has_family("serif"));
}

// ===========================================================================
// get_glyph
// ===========================================================================

TEST_F(FontFixture, GlyphHasNonZeroMetricsAndBitmap) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    ASSERT_TRUE(FontProvider::get().load_font("Inter", kArialPath));
    const GlyphInfo& g = FontProvider::get().get_glyph("Inter", 'A', 14);
    EXPECT_GT(g.width, 0);
    EXPECT_GT(g.height, 0);
    EXPECT_GT(g.advance, 0.0f);
    // alpha bitmap 大小 = width × height
    EXPECT_EQ(g.bitmap.size(), static_cast<size_t>(g.width) * g.height);
    // 至少有一个非零 alpha 像素（'A' 实际栅格化必然有着色像素）
    bool any_nonzero = false;
    for (uint8_t a : g.bitmap) {
        if (a != 0) { any_nonzero = true; break; }
    }
    EXPECT_TRUE(any_nonzero);
}

TEST_F(FontFixture, GlyphCacheReturnsSameReference) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    ASSERT_TRUE(FontProvider::get().load_font("Inter", kArialPath));
    const GlyphInfo& a = FontProvider::get().get_glyph("Inter", 'B', 16);
    const GlyphInfo& b = FontProvider::get().get_glyph("Inter", 'B', 16);
    EXPECT_EQ(&a, &b);  // 同一缓存项 → 同一地址
}

TEST_F(FontFixture, GlyphCacheDifferentiatesSizeAndCodepoint) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    ASSERT_TRUE(FontProvider::get().load_font("Inter", kArialPath));
    const GlyphInfo& a14 = FontProvider::get().get_glyph("Inter", 'A', 14);
    const GlyphInfo& a28 = FontProvider::get().get_glyph("Inter", 'A', 28);
    const GlyphInfo& b14 = FontProvider::get().get_glyph("Inter", 'B', 14);
    EXPECT_NE(&a14, &a28);
    EXPECT_NE(&a14, &b14);
    // 大字号通常像素面积更大
    EXPECT_GE(a28.width * a28.height, a14.width * a14.height);
}

TEST_F(FontFixture, GlyphForMissingFamilyReturnsEmpty) {
    const GlyphInfo& g = FontProvider::get().get_glyph("NoSuchFamily", 'A', 14);
    EXPECT_EQ(g.width, 0);
    EXPECT_EQ(g.height, 0);
    EXPECT_TRUE(g.bitmap.empty());
}

// ===========================================================================
// get_font_metrics
// ===========================================================================

TEST_F(FontFixture, FontMetricsNonZero) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    ASSERT_TRUE(FontProvider::get().load_font("Inter", kArialPath));
    FontMetrics m = FontProvider::get().get_font_metrics("Inter", 14);
    EXPECT_GT(m.ascent, 0.0f);
    EXPECT_GE(m.descent, 0.0f);
    EXPECT_GT(m.line_height(), 0.0f);
}

TEST_F(FontFixture, FontMetricsScaleWithSize) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    ASSERT_TRUE(FontProvider::get().load_font("Inter", kArialPath));
    FontMetrics m14 = FontProvider::get().get_font_metrics("Inter", 14);
    FontMetrics m28 = FontProvider::get().get_font_metrics("Inter", 28);
    EXPECT_GT(m28.line_height(), m14.line_height());
}

// ===========================================================================
// ensure_loaded / load_default_system_fonts
// ===========================================================================

TEST_F(FontFixture, EnsureLoadedUsesSystemFallback) {
    bool any_ok = FontProvider::get().ensure_loaded("Inter");
    if (!arial_available()) {
        GTEST_SKIP() << "system arial.ttf not available";
    }
    EXPECT_TRUE(any_ok);
    EXPECT_TRUE(FontProvider::get().has_family("Inter"));
}

TEST_F(FontFixture, LoadDefaultSystemFontsLoadsAtLeastOne) {
    bool any_ok = FontProvider::get().load_default_system_fonts();
    if (!probe_font_file(kArialPath)) {
        GTEST_SKIP() << "no system fonts available";
    }
    EXPECT_TRUE(any_ok);
    // Inter / JetBrains Mono 都应在加载后可见
    EXPECT_TRUE(FontProvider::get().has_family("Inter"));
    EXPECT_TRUE(FontProvider::get().has_family("JetBrains Mono"));
}

// ===========================================================================
// TextRenderer::render_text
// ===========================================================================

TEST_F(FontFixture, RenderTextProducesSixVerticesPerNonSpaceChar) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    Canvas2D canvas;
    size_t n = TextRenderer::render_text(canvas, "Hi",
                                         {0.0f, 0.0f},
                                         Color{1.0f, 1.0f, 1.0f, 1.0f},
                                         "default");
    // 2 个非空格字符 × 6 顶点 = 12
    EXPECT_EQ(n, 12u);
    EXPECT_EQ(canvas.vertices().size(), 12u);
}

TEST_F(FontFixture, RenderTextSkipsSpace) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    Canvas2D canvas;
    // "A B" → 仅 A、B 两个矩形（空格仅推进笔触）
    size_t n = TextRenderer::render_text(canvas, "A B",
                                         {0.0f, 0.0f},
                                         Color{1.0f, 1.0f, 1.0f, 1.0f},
                                         "default");
    EXPECT_EQ(n, 12u);
}

TEST_F(FontFixture, RenderTextEmptyStringNoVertices) {
    Canvas2D canvas;
    size_t n = TextRenderer::render_text(canvas, "",
                                         {0.0f, 0.0f},
                                         Color{1.0f, 1.0f, 1.0f, 1.0f},
                                         "default");
    EXPECT_EQ(n, 0u);
    EXPECT_TRUE(canvas.vertices().empty());
}

TEST_F(FontFixture, RenderTextWithUnknownTokenStillRendersViaDefaultFamily) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    Canvas2D canvas;
    // 未知 token → resolve 走默认 family "Inter" + ensure_loaded 兜底成功 → 仍渲染
    size_t n = TextRenderer::render_text(canvas, "X",
                                         {0.0f, 0.0f},
                                         Color{1.0f, 1.0f, 1.0f, 1.0f},
                                         "nonexistent_token");
    EXPECT_EQ(n, 6u);
}

TEST_F(FontFixture, RenderTextGlyphsAdvanceHorizontally) {
    if (!arial_available()) GTEST_SKIP() << "arial.ttf not available";

    Canvas2D canvas;
    TextRenderer::render_text(canvas, "ABC",
                              {10.0f, 20.0f},
                              Color{1.0f, 1.0f, 1.0f, 1.0f},
                              "default");
    // 3 个字符 → 18 顶点；从顶点里收集所有 x，应出现至少 3 个不同的矩形左边缘 ≥ 10。
    // 简化断言：第一个顶点位于 (10,20)（第一个矩形的左上角之一）。
    EXPECT_EQ(canvas.vertices().size(), 18u);
    bool has_origin = false;
    for (const auto& v : canvas.vertices()) {
        if (v.pos.x == 10.0f && v.pos.y == 20.0f) {
            has_origin = true;
            break;
        }
    }
    EXPECT_TRUE(has_origin);
    // 笔触推进：所有 x 不应小于 10（首字符左边缘）。
    for (const auto& v : canvas.vertices()) {
        EXPECT_GE(v.pos.x, 10.0f);
    }
}
