// TextRenderer 实现。

#include "scene/gui/fonts/text_renderer.h"

#include "core/math/rect2.h"
#include "scene/gui/fonts/font_provider.h"
#include "scene/gui/theme.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace eda {

namespace {

// 从 Theme font token Variant 拆出 family / size。缺键给默认值（Inter 14）。
struct FontTokenSpec {
    std::string family = "Inter";
    int         size   = 14;
};

FontTokenSpec resolve_font_token(const std::string& token) {
    FontTokenSpec spec;
    const Variant v = ThemeManager::get().get_current().get_font(token);
    if (v.get_type() != Variant::DICTIONARY) {
        return spec;  // 缺失 token：默认值
    }
    const Variant& fam = v.dict_at("family");
    const Variant& sz  = v.dict_at("size");
    if (fam.get_type() == Variant::STRING) {
        spec.family = fam.as_string();
    }
    if (sz.get_type() == Variant::INT) {
        spec.size = static_cast<int>(sz.as_int());
    }
    return spec;
}

}  // namespace

size_t TextRenderer::render_text(Canvas2D& canvas,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color,
                                 const std::string& font_token) {
    if (text.empty()) {
        return 0;
    }

    FontTokenSpec spec = resolve_font_token(font_token);
    FontProvider& fp = FontProvider::get();
    if (!fp.ensure_loaded(spec.family)) {
        // 字体不可用：静默不画（不抛异常）。
        return 0;
    }

    canvas.set_color(color);
    const size_t before = canvas.vertices().size();

    Vector2 pen = position;
    for (unsigned char ch : text) {
        if (ch == ' ') {
            // 空格无 bitmap，仅推进（按 size 的 1/4 近似，避免依赖空格 glyph 度量）。
            pen.x += std::max(1.0f, spec.size * 0.25f);
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            // 控制字符首版跳过（多行 / 制表留作后续）。
            continue;
        }

        const GlyphInfo& g = fp.get_glyph(spec.family, static_cast<uint32_t>(ch), spec.size);
        if (g.width > 0 && g.height > 0) {
            // 首版占位：画 bounding rect。bitmap 渲染留作后续升级。
            Rect2 r{pen, {static_cast<float>(g.width), static_cast<float>(g.height)}};
            canvas.draw_rect_filled(r);
        }
        // 笔触水平推进；advance 为 0（罕见）时退化为 size/2 防止字符重叠。
        pen.x += (g.advance > 0.0f) ? g.advance : spec.size * 0.5f;
    }

    return canvas.vertices().size() - before;
}

}  // namespace eda
