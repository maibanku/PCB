// Separator —— 分隔线控件（P4 Task 4 基础 Widget）。
//
// 在 Control 之上叠加：
//   - 方向 orientation_（HORIZONTAL 水平 / VERTICAL 垂直）。
//   - _draw 策略：在控件矩形中线位置画 1px 厚的方向线，颜色 = border token
//     （查表禁硬编码）。水平线铺满 width，垂直线铺满 height。
//   - get_minimum_size：方向轴上 0（容器拉伸），交叉轴 1px。
//
// 实现层用 draw_rect(filled=true) 画 1px 填充矩形作为线段（ControlCanvas 当前
// 仅暴露 draw_rect / draw_text；"draw_line" 语义经 1px 填充矩形落地，等价视觉）。
//
// 对齐 Godot scene/gui/separator.h（HSeparator / VSeparator 合并为一类 + 方向枚举）。

#pragma once

#include "scene/gui/control.h"

namespace eda {

// 分隔线方向。
enum class Orientation {
    HORIZONTAL,  // 水平线（铺满 width，高 1px）
    VERTICAL,    // 垂直线（铺满 height，宽 1px）
};

class Separator : public Control {
public:
    Separator();

    Separator(const Separator&) = delete;
    Separator& operator=(const Separator&) = delete;

    void set_orientation(Orientation o);
    Orientation get_orientation() const { return orientation_; }

    Vector2 get_minimum_size() const override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "Separator"; }
    std::string get_class() const override { return "Separator"; }
    bool is_class(const std::string& name) const override {
        return name == "Separator" || Control::is_class(name);
    }

protected:
    void _draw() override;

private:
    Orientation orientation_ = Orientation::HORIZONTAL;
};

}  // namespace eda
