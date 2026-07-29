// Label —— 文本显示控件（P4 Task 4 基础 Widget）。
//
// 在 Control（锚点 / 几何 / 自绘）之上叠加：
//   - 文本内容 text_ + 字体 token font_token_（"default" / "small" / "heading"），
//     字号在 get_minimum_size 估值与 _draw 文本位置计算中作为粗粒度度量来源。
//   - get_minimum_size 按 font_token 字号 + 文本长度给估值（真实度量由 font_provider
//     在后续 Task 落地；此处仅给容器布局可用估值，避免 0 尺寸挤压）。
//   - _draw 策略：
//       * draw_rect_filled 画背景（secondary_background token；查表禁硬编码颜色）
//       * draw_text 在左上内边距处占位写文本（前景 foreground token）
//   - set_text / set_font_token 均触发 update_minimum_size（向父容器冒泡重排）+
//     queue_redraw（重绘）。
//
// 对齐 Godot scene/gui/label.h（最小子集：无自动换行 / 省略 / 富文本，留待后续）。

#pragma once

#include "scene/gui/control.h"

#include <string>

namespace eda {

class Label : public Control {
public:
    Label();

    Label(const Label&) = delete;
    Label& operator=(const Label&) = delete;

    // —— 文本内容 ——
    void set_text(const std::string& text);
    const std::string& get_text() const { return text_; }

    // —— 字体 token（"default" / "small" / "heading"；缺省 "default"）——
    void set_font_token(const std::string& token);
    const std::string& get_font_token() const { return font_token_; }

    // —— 最小尺寸（容器布局消费；字号 × 字符数估值）——
    Vector2 get_minimum_size() const override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "Label"; }
    std::string get_class() const override { return "Label"; }
    bool is_class(const std::string& name) const override {
        return name == "Label" || Control::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::string text_;
    std::string font_token_ = "default";

    // 由 font_token 推字号（与 theme.cpp apply_common_tokens 一致：14 / 12 / 18）。
    int font_size_for_token_() const;
};

}  // namespace eda
