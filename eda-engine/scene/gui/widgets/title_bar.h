// TitleBar —— 应用标题栏（P4 widgets · 窗口顶栏）。
//
// 继承 HBox：左侧 Label("EDA Engine") + Label(project_name)；中间 EXPAND spacer 把
// 后续控件推到右侧；右侧 3 Button(─/□/× 最小化 / 最大化 / 关闭)。固定高度 32px。
//
// 窗口按钮信号（业务由 Window / OS 层消费，框架只 emit）：
//   - minimize_requested  最小化按钮点击
//   - maximize_requested  最大化按钮点击
//   - close_requested     关闭按钮点击
// 实现路径：Button::clicked → TitleBar lambda → emit 对应 Signal<>。
//
// 配色：背景 background token（任务规范）。固定高度经 get_minimum_size 返回
// {0, kFixedHeight=32}，交由父容器布局消费。
//
// 所有权：标签 / 按钮 堆分配，挂树由 ~Node 级联释放。lambda 捕获 this 安全——
// 按钮是 TitleBar 的子节点，析构顺序保证按钮先于 TitleBar 主体销毁，回调永不悬空。

#pragma once

#include "scene/gui/container.h"  // HBox
#include "core/object/signal.h"

#include <string>

namespace eda {

class Label;
class Button;

class TitleBar : public HBox {
public:
    TitleBar();

    TitleBar(const TitleBar&) = delete;
    TitleBar& operator=(const TitleBar&) = delete;

    // —— 项目名（左二标签；空串显示 "(untitled)"）——
    void              set_project_name(const std::string& name);
    const std::string& get_project_name() const { return project_name_; }

    // —— 窗口按钮访问（测试 / 自定义样式用）——
    Button* get_minimize_button() const { return minimize_btn_; }
    Button* get_maximize_button() const { return maximize_btn_; }
    Button* get_close_button()    const { return close_btn_; }

    // —— 标签访问 ——
    Label*  get_title_label()   const { return title_label_; }
    Label*  get_project_label() const { return project_label_; }

    // —— 窗口操作信号（业务消费）——
    Signal<>& close_requested()    { return close_requested_; }
    Signal<>& minimize_requested() { return minimize_requested_; }
    Signal<>& maximize_requested() { return maximize_requested_; }

    // 固定高度（像素）。
    static constexpr float kFixedHeight = 32.0f;

    Vector2 get_minimum_size() const override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "TitleBar"; }
    std::string get_class() const override { return "TitleBar"; }
    bool is_class(const std::string& name) const override {
        return name == "TitleBar" || HBox::is_class(name);
    }

protected:
    void _draw() override;

private:
    std::string project_name_;
    Label*  title_label_;
    Label*  project_label_;
    Button* minimize_btn_;
    Button* maximize_btn_;
    Button* close_btn_;

    Signal<> close_requested_;
    Signal<> minimize_requested_;
    Signal<> maximize_requested_;
};

}  // namespace eda
