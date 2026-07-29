// TextRenderer —— 把字符串渲染到 Canvas2D（P4 Task 协同：文本渲染首版）。
//
// 与 FontProvider（scene/gui/fonts/font_provider.h）+ Canvas2D + Theme 协同：
//   render_text(canvas, text, position, color, font_token)
//     → ThemeManager::get_current().get_font(token)  // 拿 family/size Variant
//     → FontProvider::ensure_loaded(family)          // 懒加载系统兜底字体
//     → 逐字符 FontProvider::get_glyph               // 查 alpha bitmap + 度量
//     → Canvas2D::draw_rect_filled 画 glyph bounding rect（首版占位策略）
//
// 首版"色块条"策略：
//   每个非空格字符画一个 bounding rect（实心矩形）= 2 三角形 / 6 顶点，
//   不渲染 alpha bitmap。Label 因此可辨识文本长度/行进位置；bitmap 渲染留作后续升级。
//   - color 由调用方传入（通常 = Theme::get_color("foreground")）。
//   - 笔触沿 +x 推进，y 取 position.y；矩形顶 = position.y（不接 baseline）。
//   - 空格不画矩形，仅按 advance 推进。
//
// 缺失 token / family 时静默不画（返回 0 顶点），与 Variant 缺键策略一致。

#pragma once

#include "core/math/color.h"
#include "core/math/vector2.h"
#include "scene/2d/canvas_2d.h"

#include <string>

namespace eda {

class TextRenderer {
public:
    // 渲染 text 到 canvas。返回生成的顶点数（便于测试断言）。
    //
    //   canvas      : 目标画布
    //   text        : UTF-8 文本（首版按字节处理；中文多字节暂作多字符）
    //   position    : 文本左上角（不是 baseline；首版 baseline 对齐留给后续）
    //   color       : 文本色（已着色每个 glyph 矩形）
    //   font_token  : Theme font token，如 "default" / "small" / "heading" / "mono"
    static size_t render_text(Canvas2D& canvas,
                              const std::string& text,
                              Vector2 position,
                              Color color,
                              const std::string& font_token);
};

}  // namespace eda
