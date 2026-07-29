// StatusBar —— 状态栏（P4 widgets · 编辑器底栏）。
//
// 继承 HBox：左侧 status_label（状态文本，默认 "Ready"）；中间 EXPAND spacer 推
// 右侧两个标签；右侧 drc_label（"DRC: N"）+ zoom_label（"Zoom: NN%"）。
// 固定高度 24px。
//
// 三个 set_* 入口同步更新对应 Label 的文本：
//   - set_status(text)     → status_label
//   - set_drc_count(int)   → drc_label  （"DRC: <N>"）
//   - set_zoom(float)      → zoom_label （"Zoom: <NN>%"，1.0 = 100%）
// 业务在 DRC 运行 / 视图缩放 / 状态切换时调用，实时反映到底栏。
//
// 配色：背景 secondary_background token（任务规范）。固定高度经 get_minimum_size
// 返回 {0, kFixedHeight=24}。
//
// 所有权：Label 堆分配挂树，~Node 级联释放。

#pragma once

#include "scene/gui/container.h"  // HBox

#include <string>

namespace eda {

class Label;

class StatusBar : public HBox {
public:
    StatusBar();

    StatusBar(const StatusBar&) = delete;
    StatusBar& operator=(const StatusBar&) = delete;

    // —— 状态文本 ——
    void              set_status(const std::string& text);
    const std::string& get_status() const { return status_; }

    // —— DRC 计数 ——
    void set_drc_count(int count);
    int  get_drc_count() const { return drc_count_; }

    // —— 缩放（1.0 = 100%）——
    void  set_zoom(float zoom);
    float get_zoom() const { return zoom_; }

    // —— 标签访问（测试 / 自定义样式用）——
    Label* get_status_label() const { return status_label_; }
    Label* get_drc_label()    const { return drc_label_; }
    Label* get_zoom_label()   const { return zoom_label_; }

    // 固定高度（像素）。
    static constexpr float kFixedHeight = 24.0f;

    Vector2 get_minimum_size() const override;

    // —— 类型查询 ——
    static std::string get_class_static() { return "StatusBar"; }
    std::string get_class() const override { return "StatusBar"; }
    bool is_class(const std::string& name) const override {
        return name == "StatusBar" || HBox::is_class(name);
    }

protected:
    void _draw() override;

private:
    // 由当前状态重算三个 Label 的显示文本。
    void update_labels_();

    std::string status_    = "Ready";
    int         drc_count_ = 0;
    float       zoom_      = 1.0f;

    Label* status_label_;
    Label* drc_label_;
    Label* zoom_label_;
};

}  // namespace eda
