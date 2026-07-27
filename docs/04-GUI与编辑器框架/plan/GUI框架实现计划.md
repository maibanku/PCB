---
title: GUI框架实现计划（GUI & Editor Framework）
lang: zh-CN
status: 权威
created: 2026-07-27
tags:
  - EDA
  - GUI
  - C++
  - 实现计划
  - Control
  - 编辑器框架
related:
  - "[[04-GUI与编辑器框架]]"
  - "[[01-引擎地基/plan/引擎地基实现计划]]"
  - "[[00-总览/03-项目目录结构]]"
  - "[[00-总览/04-术语表]]"
---

# GUI框架实现计划（GUI & Editor Framework）

> 📊 **实现进度**：`[█░░░░░░░░░] 12%` · 1/8 Task · ⚙️ 实现中 · 追踪：[[00-总览/06-前期搭建项目路线]] · 最后更新：2026-07-27

> **For agentic workers:** REQUIRED SUB-SKILL: 使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 按任务逐个实现。步骤使用 `- [ ]` 复选框跟踪。Task 1 为核心交付（完整 TDD），Task 2..8 为待展开骨架。

**Goal:** 用自研的 Control 控件体系 + 布局容器 + 主题 + 可停靠 Dock/属性 Inspector/命令面板/快捷键 + i18n/a11y，搭出一个**能运行、能交互、能换肤、能多语言切换**的专业 EDA 编辑器外壳（不依赖任何系统控件）。

**Architecture:** 完全对齐 Godot 的 `scene/gui` 与 `editor/` 职责划分。`scene/gui/` 放自绘 Control 节点体系（`Control : CanvasItem : Node`，所有控件经 `_draw()` 自绘，不经系统按钮/输入框）；布局由容器（`Container : Control`）在 `_notification(SORT_CHILDREN)` 时统一接管子控件几何；外观由 `Theme` Resource 统一查表（`get_theme_color/font_size/constant/stylebox`），换肤零业务改动；`editor/dock|inspector|command_palette|shortcuts` 放编辑器外壳（Dock 拖拽停靠、Inspector 反射驱动属性编辑、命令面板模糊搜索、快捷键三套预设）；`scene/gui/{i18n,a11y,l10n}` 放国际化与无障碍（`tr()` 走 `.po` + ICU，控件暴露 `accessibility_*` 给平台 AT API）。头文件与源文件同目录（Godot 风格）。

**与 Godot 的映射：** `Control` ↔ `scene/gui/control.h`；`Container/BoxContainer/SplitContainer/ScrollContainer/TabContainer/GridContainer` ↔ `scene/gui/container.h` + `box_container.h` 等；`Theme/StyleBox` ↔ `scene/gui/theme/theme.h` + `stylebox.h`；`EditorInspector/EditorInspectorPlugin` ↔ `editor/inspector/inspector.h`；`InputMap` ↔ `editor/shortcuts/input_map.h`；`tr()` ↔ Godot 的 `TranslationServer` + `Object::tr()`。

**Tech Stack:** C++20（继承 P1）、CMake ≥ 3.20（继承 P1）、GoogleTest（继承 P1，所有布局/焦点/主题/翻译逻辑纯 CPU 可测）；i18n/l10n 选 **ICU**（`MessageFormat` 统一复数/性别/选择 + 数字/日期/货币/单位区域格式）+ **gettext `.po`** 作翻译资源容器（复数/上下文/译者注释/模糊标记、Crowdin/Weblate 兼容、`xgettext` 抽取成熟）；HarfBuzz/FreeType 字体整形（Task 3 主题系统引入）。

---

## Global Constraints

### 继承自 P1（继续生效）

- **语言**：C++20（`-std=c++20`），MSVC 19.29+ 或 GCC 11+
- **构建**：CMake ≥ 3.20，Release/Debug 双配置，各模块自带 `CMakeLists.txt`
- **目录/命名（对齐 Godot）**：头文件 `.h` 与源 `.cpp` 同目录；文件名 `snake_case.h`；类型 `PascalCase`（`Control`/`Container`/`Theme`/`DockManager`）；命名空间 `eda::`；成员变量带尾下划线 `name_`；枚举值 `SCREAMING_CASE`
- **include 路径**：从仓库根解析，形如 `#include "scene/gui/control.h"`、`#include "editor/dock/dock_manager.h"`
- **提交**：约定式提交（`feat:` / `fix:` / `test:` / `docs:` / `chore:`），每个任务结束提交一次
- **测试**：纯逻辑（锚点布局、容器排序、主题查表、翻译查表、模糊匹配、快捷键映射）用 GoogleTest 单元测试；控件 `_draw()` 用伪渲染器（`RecordingControlCanvas`）记录绘制调用验证；GPU/窗口集成用烟雾测试

### P4 特有约束（本 plan 新增）

- **控件完全自绘**：所有控件经 `_draw()` 调 `ControlCanvas::draw_*`（生产环境绑 `RenderingServer` 的 `canvas_item_add_*`），**严禁依赖系统按钮/输入框/对话框**——保证 Win/macOS/Linux 视觉与行为完全一致；IME（中日韩输入）经 `platform/DisplayServer` 回调接入 LineEdit/TextEdit
- **UI 文本零硬编码**：所有面向用户的文本必须经 `tr("english source")` 查表，**严禁硬编码中文字面量**（CI 扫描 `tr()` 抽取 `.pot` + 阻断裸中文）；`tr()` source 取自 [[00-总览/04-术语表]] §四"英文 source"列（全小写）；固有名词（IPC-2581/Gerber/courtyard/DRC 等，§三清单）不进 `tr()` 池，直接字面量
- **主题查表禁硬编码颜色**：业务/控件代码只查 `get_theme_color("error_color", "LineEdit")` 等，**严禁硬编码 RGB**；换肤/暗色模式/高对比度零业务改动
- **控件接入无障碍属性**：每个面向用户的 Control 必须设置 `accessibility_label`（屏幕阅读器朗读文本）与 `accessibility_role`；焦点控件必须有可视高亮（2px 对比色边框，不依赖 hover）；键盘可达（Tab/Shift+Tab 焦点遍历、模态焦点陷阱、Esc 关闭）
- **WCAG 2.1 AA 对齐**：文本对比度 ≥ 4.5:1（正常文本）/ ≥ 3:1（大文本与 UI 组件边界）；非文本对比度 ≥ 3:1；高对比度主题变体满足阈值（与 Task 3 主题系统协作）
- **DPI 适配**：界面缩放 `ui_scale`（0.8x ~ 2.0x）走渲染层统一缩放，不靠各控件自调字号；主题尺寸按 `Scale` 缩放
- **术语一致**：UI 文案、代码符号、文档术语、翻译 key 四层映射以 [[00-总览/04-术语表]] 为 SSOT（`dock`=可停靠面板、`inspector`=属性检查器、`footprint`=封装、`pad`=焊盘、`via`=过孔……）
- **上游契约（强前置）**：`Control` 继承 `CanvasItem`（P2 节点系统提供 `scene/main/canvas_item.h`，含 `Node::get_parent/get_children`、`CanvasItem` 的变换/可见性/`queue_redraw` 钩子）；`RenderingServer.canvas_item_add_*`（P1 渲染服务的扩展，P4 起接入 RID 风格 canvas_item 接口）；**P2 未完成时本 plan 的 Task 1 无法编译——需先落 P2**

### 范围边界（不在本 plan）

- 具体业务视图（原理图画布/PCB 画布/库编辑器面板）由 P7/P8/P6 各业务域实现，本 plan 只提供控件与外壳基础设施
- 完整的 `ClassDB` 反射（P2 提供，本 plan 的 Inspector 消费它，不重新实现）
- `modules/` 插件接口（P18，本 plan 命令面板预留注册扩展点但不落地沙箱）

---

## 文件结构（对齐 Godot `scene/gui` + `editor/`）

```
eda-engine/
├── scene/
│   ├── 2d/                         # [P1] Canvas2D（已存在）
│   ├── main/                       # [P2] Node / CanvasItem（上游契约）
│   └── gui/                        # ══ [P4] 自绘 GUI 控件体系 ══
│       ├── CMakeLists.txt          #  eda_scene_gui target
│       ├── control.h / .cpp        #  Control 基类（Task 1，核心）
│       ├── control_canvas.h        #  ControlCanvas 绘制接口 + RecordingControlCanvas（Task 1）
│       ├── control_canvas.cpp      #
│       ├── containers/             #  容器（Task 2）
│       │   ├── container.h / .cpp           # Container 基类（_notification SORT_CHILDREN）
│       │   ├── box_container.h / .cpp       # HBox / VBox
│       │   ├── split_container.h / .cpp     # HSplit / VSplit
│       │   ├── scroll_container.h / .cpp
│       │   ├── tab_container.h / .cpp
│       │   ├── grid_container.h / .cpp
│       │   ├── margin_container.h / .cpp
│       │   ├── center_container.h / .cpp
│       │   ├── aspect_ratio_container.h / .cpp
│       │   └── flow_container.h / .cpp
│       ├── widgets/                #  内置自绘控件集（Task 2 随容器补足，按需）
│       │   ├── button.h / .cpp
│       │   ├── label.h / .cpp
│       │   ├── line_edit.h / .cpp          # 接入 IME
│       │   ├── text_edit.h / .cpp          # 含语法高亮基础
│       │   ├── tree.h / .cpp               # 多列树/表（工程树/器件树）
│       │   ├── item_list.h / .cpp
│       │   ├── scroll_container.h / .cpp
│       │   ├── popup_menu.h / .cpp
│       │   ├── combo_box.h / .cpp
│       │   ├── check_box.h / .cpp
│       │   ├── spin_box.h / .cpp
│       │   └── color_picker.h / .cpp
│       ├── theme/                  #  主题系统（Task 3）
│       │   ├── theme.h / .cpp               # Theme Resource（键值查表）
│       │   ├── style_box.h / .cpp           # StyleBox（flat/line/texture/nine-patch）
│       │   ├── default_theme.h / .cpp       # 明/暗/高对比度三套预设
│       │   └── theme_loader.h / .cpp        # .theme.toml 导入导出
│       ├── i18n/                   #  翻译系统（Task 8）
│       │   ├── translation.h / .cpp         # tr() / tr_n / tr_ctxt
│       │   ├── translation_server.h / .cpp  # locale 切换 + 信号广播
│       │   ├── po_loader.h / .cpp           # .po / .mo 加载
│       │   └── po_extractor.h / .cpp        # CI 抽取 .pot（工具）
│       ├── a11y/                   #  无障碍（Task 8）
│       │   ├── accessibility.h / .cpp       # accessibility_label/role/value/state
│       │   ├── accessibility_tree.h / .cpp  # 无障碍树构建
│       │   └── at_api.h                     # 平台 AT 后端接口（UIA/NSAccessibility/AT-SPI2）
│       ├── l10n/                   #  本地化（Task 8）
│       │   ├── locale.h / .cpp              # locale 三层回退（用户>工程>系统）
│       │   ├── number_format.h / .cpp       # ICU 数字/日期/货币
│       │   └── units.h / .cpp               # mm/mil/in 归一化（EDA 特有）
│       └── fonts/                  #  字体管理（Task 3 协同）
│           └── font_provider.h / .cpp       # HarfBuzz/FreeType 集成
│
├── editor/                         # ══ [P4] 通用编辑器框架 ══
│   ├── CMakeLists.txt              #  eda_editor target
│   ├── dock/                       #  可停靠面板（Task 4）
│   │   ├── dock_manager.h / .cpp           # 左/右/下/上/中心区域管理
│   │   ├── dock_container.h / .cpp         # 多 Dock 标签页
│   │   ├── dock_panel.h / .cpp             # 单个可拖拽面板基类
│   │   ├── floating_window.h / .cpp        # 拖出主窗口的浮动窗口
│   │   └── dock_layout.h / .cpp            # .layout.toml 序列化/恢复
│   ├── inspector/                  #  属性检查器（Task 5）
│   │   ├── inspector.h / .cpp              # 编辑主体
│   │   ├── inspector_plugin.h / .cpp       # 插件式属性编辑器注入
│   │   ├── property_editors.h / .cpp       # 内置属性编辑器（line/spin/color/enum/vector2...）
│   │   └── inspector_search.h / .cpp       # 属性搜索/过滤
│   ├── command_palette/            #  命令面板（Task 6）
│   │   ├── command_registry.h / .cpp       # Command 注册中心（单一真相源）
│   │   ├── command.h / .cpp                # Command{id,name,shortcut,icon,category,callback}
│   │   ├── command_palette.h / .cpp        # 唤起 UI（Ctrl+Shift+P）
│   │   └── fuzzy_match.h / .cpp            # 子序列 + 拼音首字母 + 最近使用加权
│   ├── shortcuts/                  #  快捷键体系（Task 7）
│   │   ├── input_map.h / .cpp              # Action ↔ 物理键映射
│   │   ├── keymap_loader.h / .cpp          # .keymap.toml 导入导出
│   │   └── presets/                        #  kicad/altium/jlceda/default 四套预设 .toml
│   ├── tools/                      #  工具/模式切换（P4 骨架，P7/P8 协同扩展）
│   │   └── tool_manager.h / .cpp           # 工具注册 + 当前工具 + 作用域切换
│   └── undo/                      #  编辑器 UndoRedo 集成（衔接 P3）
│       └── editor_undo_redo.h / .cpp       # 与 eda/command/undo_redo 桥接
│
└── tests/
    ├── test_control.cpp            #  Task 1（核心）
    ├── test_containers.cpp         #  Task 2
    ├── test_theme.cpp              #  Task 3
    ├── test_dock.cpp               #  Task 4
    ├── test_inspector.cpp          #  Task 5
    ├── test_command_palette.cpp    #  Task 6
    ├── test_shortcuts.cpp          #  Task 7
    └── test_i18n.cpp               #  Task 8（翻译/a11y/l10n）
```

**职责边界：** `scene/gui/control` 只定义控件基类与几何/锚点/焦点/自绘入口，**不懂容器排序**（归 `containers/`）、**不懂外观数值**（归 `theme/`）、**不懂翻译查询的 locale 来源**（归 `i18n/`）；`containers/` 只接管子控件几何，不替子控件绘制；`theme/` 只存查表数据，不持有控件；`editor/*` 只组装 `scene/gui` 控件成编辑器外壳，不新增基础控件类型。

---

## Task 1: Control 基类与锚点布局（核心 TDD）

**Files:**
- Create: `scene/gui/control.h`、`scene/gui/control.cpp`
- Create: `scene/gui/control_canvas.h`、`scene/gui/control_canvas.cpp`
- Create: `scene/gui/CMakeLists.txt`
- Modify: `scene/CMakeLists.txt`（追加 `add_subdirectory(gui)`）
- Create: `tests/test_control.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `eda::CanvasItem`（P2 上游）、`eda::Vector2/Rect2/Color`（P1）
- Produces:
  - `eda::Side/FocusMode/MouseFilter/GrowDirection/LayoutPreset` 枚举
  - `eda::ControlCanvas`（绘制接口，`draw_rect/draw_text`）+ `eda::RecordingControlCanvas`（测试）+ `eda::ControlCanvasScope`（RAII 设置 thread-local 当前画布）+ `eda::current_control_canvas()`
  - `eda::Control : CanvasItem`：锚点 `set_anchor/get_anchor/set_offset/get_offset/set_anchors_preset`；几何 `get_position/get_size/get_rect/set_position/set_size/set_rect`；视口 `set_viewport_size`；最小尺寸 `get_minimum_size/get_combined_minimum_size/update_minimum_size`；自绘 `current_canvas/queue_redraw/draw_if_dirty/_draw`；焦点 `set_focus_mode/set_focus/has_focus/find_next_focus`；无障碍 `set_accessibility_label/get_accessibility_label`

- [ ] **Step 1: 写失败测试（Control 锚点 / 几何 / 最小尺寸 / 焦点 / 自绘）**

`tests/test_control.cpp`：

```cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "scene/gui/control.h"
#include "scene/gui/control_canvas.h"
using namespace eda;

// —— 锚点 / 几何 ——

TEST(ControlAnchorTest, DefaultAnchorsFillParent) {
    Control parent;
    parent.set_size({100, 100});              // 无父：viewport 默认 (0,0)，offsets 退化为绝对
    Control c;
    c.set_parent(&parent);                    // 默认 anchors {0,0,1,1} offsets {0,0,0,0}
    EXPECT_FLOAT_EQ(c.get_size().x, 100.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 100.0f);
    EXPECT_FLOAT_EQ(c.get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(c.get_position().y, 0.0f);
}

TEST(ControlAnchorTest, CenterPresetKeepsSizeAtCenter) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_size({20, 20});
    c.set_anchors_preset(LayoutPreset::CENTER);  // anchors 0.5 + offsets ±10
    EXPECT_NEAR(c.get_position().x, 40.0f, 1e-4f);  // (100-20)/2
    EXPECT_NEAR(c.get_position().y, 40.0f, 1e-4f);
    EXPECT_FLOAT_EQ(c.get_size().x, 20.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 20.0f);
}

TEST(ControlAnchorTest, SetSizeKeepsPosition) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_position({10, 10});
    c.set_size({40, 40});
    EXPECT_FLOAT_EQ(c.get_position().x, 10.0f);
    EXPECT_FLOAT_EQ(c.get_position().y, 10.0f);
    EXPECT_FLOAT_EQ(c.get_size().x, 40.0f);
    c.set_size({60, 20});
    EXPECT_FLOAT_EQ(c.get_position().x, 10.0f);  // 位置不变
    EXPECT_FLOAT_EQ(c.get_size().x, 60.0f);
}

TEST(ControlAnchorTest, SetPositionKeepsSize) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_position({10, 10});
    c.set_size({30, 30});
    c.set_position({50, 50});                  // 移动后尺寸不变
    EXPECT_FLOAT_EQ(c.get_position().x, 50.0f);
    EXPECT_FLOAT_EQ(c.get_size().x, 30.0f);
}

TEST(ControlAnchorTest, RightAnchorStaysPinnedOnParentResize) {
    Control parent; parent.set_size({100, 100});
    Control c; c.set_parent(&parent);
    c.set_size({20, 20});
    c.set_anchors_preset(LayoutPreset::BOTTOM_RIGHT);  // 锚到右下角
    EXPECT_NEAR(c.get_position().x, 80.0f, 1e-4f);     // 100-20
    parent.set_size({200, 200});                        // 父尺寸变化
    EXPECT_NEAR(c.get_position().x, 180.0f, 1e-4f);    // 仍锚在右下：200-20
    EXPECT_FLOAT_EQ(c.get_size().x, 20.0f);
}

TEST(ControlAnchorTest, FullRectPresetFillsParent) {
    Control parent; parent.set_size({80, 60});
    Control c; c.set_parent(&parent);
    c.set_anchors_preset(LayoutPreset::FULL_RECT);
    EXPECT_FLOAT_EQ(c.get_size().x, 80.0f);
    EXPECT_FLOAT_EQ(c.get_size().y, 60.0f);
}

// —— 最小尺寸（容器布局消费）——

namespace {
class FixedSizeControl : public Control {
public:
    void set_minimum_size(Vector2 s) { min_ = s; update_minimum_size(); }
    Vector2 get_minimum_size() const override { return min_; }
private:
    Vector2 min_{0, 0};
};
}  // namespace

TEST(ControlMinSizeTest, DefaultIsZero) {
    Control c;
    EXPECT_FLOAT_EQ(c.get_minimum_size().x, 0.0f);
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 0.0f);
}

TEST(ControlMinSizeTest, CombinedCachesUntilDirty) {
    FixedSizeControl c;
    c.set_minimum_size({30, 20});
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 30.0f);
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().y, 20.0f);
    c.set_minimum_size({50, 10});
    EXPECT_FLOAT_EQ(c.get_combined_minimum_size().x, 50.0f);  // 失效后重算
}

// —— 焦点 ——

TEST(ControlFocusTest, SetFocusTogglesState) {
    Control c;
    c.set_focus_mode(FocusMode::CLICK);
    EXPECT_FALSE(c.has_focus());
    c.set_focus(true);
    EXPECT_TRUE(c.has_focus());
    c.set_focus(false);
    EXPECT_FALSE(c.has_focus());
}

TEST(ControlFocusTest, NoneModeRefusesFocus) {
    Control c;  // 默认 NONE
    c.set_focus(true);
    EXPECT_FALSE(c.has_focus());
}

TEST(ControlFocusTest, FindNextFocusWalksSiblings) {
    Control parent;
    Control a, b, d;
    for (auto* ch : {&a, &b, &d}) {
        ch->set_parent(&parent);
        ch->set_focus_mode(FocusMode::CLICK);
    }
    a.set_focus(true);
    Control* next = a.find_next_focus(/*reverse=*/false);
    EXPECT_EQ(next, &b);
    Control* wrap = d.find_next_focus(/*reverse=*/false);
    EXPECT_EQ(wrap, &a);  // 末尾回绕到首
}

TEST(ControlFocusTest, NonFocusableSiblingSkipped) {
    Control parent;
    Control a, b, d;
    a.set_parent(&parent); a.set_focus_mode(FocusMode::CLICK);
    b.set_parent(&parent);  // NONE，跳过
    d.set_parent(&parent); d.set_focus_mode(FocusMode::ALL);
    Control* next = a.find_next_focus(false);
    EXPECT_EQ(next, &d);
}

// —— 自绘（伪渲染器验证）——

namespace {
class StubControl : public Control {
protected:
    void _draw() override {
        current_canvas()->draw_rect(Rect2{{0, 0}, {10, 10}}, Color{1, 0, 0, 1}, /*filled=*/true);
    }
};
}  // namespace

TEST(ControlDrawTest, DrawDispatchedOnRedraw) {
    StubControl c;
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.queue_redraw();
    EXPECT_TRUE(c.is_draw_dirty());
    c.draw_if_dirty();
    EXPECT_FALSE(c.is_draw_dirty());
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 10.0f);
    EXPECT_TRUE(rec.rects[0].filled);
}

TEST(ControlDrawTest, CleanControlNotRedrawn) {
    StubControl c;
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.draw_if_dirty();  // 初始 dirty=true，首帧会画一次
    rec.rects.clear();
    c.draw_if_dirty();  // 已 clean，不再画
    EXPECT_EQ(rec.rects.size(), 0u);
}

TEST(ControlFocusTest, FocusedControlDrawsHighlight) {
    StubControl c;
    c.set_focus_mode(FocusMode::CLICK);
    c.set_focus(true);
    RecordingControlCanvas rec;
    ControlCanvasScope scope(rec);
    c.queue_redraw();
    c.draw_if_dirty();
    // StubControl 自绘 1 个填充矩形 + 框架绘制 1 个非填充焦点高亮边框
    ASSERT_EQ(rec.rects.size(), 2u);
    EXPECT_TRUE(rec.rects[0].filled);    // 控件自绘
    EXPECT_FALSE(rec.rects[1].filled);   // 框架焦点高亮（无障碍：焦点可视指示）
}

// —— 无障碍 ——

TEST(ControlA11yTest, AccessibilityLabelStored) {
    Control c;
    c.set_accessibility_label("delete footprint");
    EXPECT_EQ(c.get_accessibility_label(), "delete footprint");
}
```

`tests/CMakeLists.txt` 追加：

```cmake
add_executable(test_control test_control.cpp)
target_include_directories(test_control PRIVATE ${EDA_ROOT})
target_link_libraries(test_control PRIVATE eda_scene_gui gtest_main)
gtest_discover_tests(test_control)
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build && ctest --test-dir build -R "ControlAnchorTest|ControlMinSizeTest|ControlFocusTest|ControlDrawTest|ControlA11yTest" --output-on-failure`

Expected: FAIL（`scene/gui/control.h` 不存在；`eda_scene_gui` target 未定义）

- [ ] **Step 3: 实现 ControlCanvas 接口与 RecordingControlCanvas**

`scene/gui/control_canvas.h`：

```cpp
#pragma once
#include "core/math/rect2.h"
#include "core/math/color.h"
#include "core/math/vector2.h"
#include <string>
#include <vector>

namespace eda {

// Control 自绘入口使用的绘制接口。
// 生产环境：实现包装 RenderingServer.canvas_item_add_*；
// 测试环境：RecordingControlCanvas 记录调用以断言。
class ControlCanvas {
public:
    virtual ~ControlCanvas() = default;
    virtual void draw_rect(Rect2 rect, Color color, bool filled) = 0;
    virtual void draw_text(const std::string& text, Vector2 pos, Color color) = 0;
    // 后续 Task 按需补：draw_line / draw_style_box / draw_texture / draw_circle ...
};

// 返回当前线程绑定的 ControlCanvas（由 ControlCanvasScope RAII 设置）。
// 遍历器在调 control->draw_if_dirty() 前用 scope 绑定真实画布。
ControlCanvas* current_control_canvas();

// RAII：在作用域内将 c 设为当前画布，析构恢复前值。
class ControlCanvasScope {
public:
    explicit ControlCanvasScope(ControlCanvas& c);
    ~ControlCanvasScope();
    ControlCanvasScope(const ControlCanvasScope&) = delete;
    ControlCanvasScope& operator=(const ControlCanvasScope&) = delete;
private:
    ControlCanvas* prev_;
};

// 测试用：记录所有绘制调用。
struct RecordedRect { Rect2 rect; Color color; bool filled; };

class RecordingControlCanvas : public ControlCanvas {
public:
    std::vector<RecordedRect> rects;
    std::vector<std::pair<std::string, Vector2>> texts;
    void draw_rect(Rect2 r, Color c, bool f) override { rects.push_back({r, c, f}); }
    void draw_text(const std::string& t, Vector2 p, Color) override { texts.emplace_back(t, p); }
};

}  // namespace eda
```

`scene/gui/control_canvas.cpp`：

```cpp
#include "control_canvas.h"

namespace eda {
namespace detail {
thread_local ControlCanvas* g_current = nullptr;
}

ControlCanvas* current_control_canvas() { return detail::g_current; }

ControlCanvasScope::ControlCanvasScope(ControlCanvas& c)
    : prev_(detail::g_current) { detail::g_current = &c; }
ControlCanvasScope::~ControlCanvasScope() { detail::g_current = prev_; }

}  // namespace eda
```

- [ ] **Step 4: 实现 Control 类**

`scene/gui/control.h`：

```cpp
#pragma once
#include "scene/main/canvas_item.h"   // P2 上游：CanvasItem（含 Node/Transform2D/可见性）
#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "core/math/color.h"
#include "scene/gui/control_canvas.h"
#include <string>

namespace eda {

enum class Side : int { LEFT = 0, TOP = 1, RIGHT = 2, BOTTOM = 3 };
enum class FocusMode   { NONE, CLICK, ALL };
enum class MouseFilter { STOP, PASS, IGNORE };
enum class GrowDirection { BEGIN, END, BOTH };

// 锚点预设（对齐 Godot LayoutPreset，常用子集；其余 Task 2 按同构补全）
enum class LayoutPreset {
    TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT,
    CENTER,
    LEFT_WIDE, TOP_WIDE, RIGHT_WIDE, BOTTOM_WIDE,
    FULL_RECT
};

// 通知标识（对齐 Godot Notification；CanvasItem 既有者不重复定义）
inline constexpr int NOTIFICATION_DRAW         = 30;
inline constexpr int NOTIFICATION_FOCUS_ENTER  = 31;
inline constexpr int NOTIFICATION_FOCUS_EXIT   = 32;
inline constexpr int NOTIFICATION_RESIZED      = 33;
inline constexpr int NOTIFICATION_SORT_CHILDREN = 34;

class Control : public CanvasItem {
public:
    Control();

    // —— 锚点 / 偏移（0..1 相对父尺寸 + 像素偏移）——
    void  set_anchor(Side side, float value);
    float get_anchor(Side side) const { return anchors_[static_cast<int>(side)]; }
    void  set_offset(Side side, float value);
    float get_offset(Side side) const { return offsets_[static_cast<int>(side)]; }
    void  set_anchors_preset(LayoutPreset preset);  // 按当前尺寸重算 offsets

    // —— 几何（由锚点 + 父尺寸计算）——
    Vector2 get_position() const;
    Vector2 get_size() const;
    Rect2   get_rect() const { return {get_position(), get_size()}; }
    void    set_position(Vector2 p);      // 保持尺寸平移
    void    set_size(Vector2 s);          // 保持位置调整右下偏移
    void    set_rect(Rect2 r);            // 位置 + 尺寸一起设

    // 顶层 Control（父非 Control）的视口尺寸，由 Window/SceneTree 注入
    void    set_viewport_size(Vector2 s) { viewport_size_ = s; queue_redraw(); }
    Vector2 get_viewport_size() const { return viewport_size_; }

    // —— 最小尺寸（容器布局消费）——
    virtual Vector2 get_minimum_size() const { return {0, 0}; }
    Vector2 get_combined_minimum_size() const;  // 带缓存
    void    update_minimum_size();              // 标记失效 + 向父冒泡

    // —— 自绘入口 ——
    static ControlCanvas* current_canvas() { return current_control_canvas(); }
    bool is_draw_dirty() const { return draw_dirty_; }
    void queue_redraw();
    void draw_if_dirty();   // 遍历器调：dirty 则 _draw() + 焦点高亮
    virtual void _draw() {}

    // —— 焦点（无障碍前置）——
    void      set_focus_mode(FocusMode m) { focus_mode_ = m; }
    FocusMode get_focus_mode() const { return focus_mode_; }
    void      set_focus(bool p_focus);
    bool      has_focus() const { return has_focus_; }
    Control*  find_next_focus(bool reverse) const;  // DFS 遍历下一个可聚焦控件

    // —— 无障碍（08-02）——
    void              set_accessibility_label(const std::string& s) { a11y_label_ = s; }
    const std::string& get_accessibility_label() const { return a11y_label_; }

protected:
    void _notification(int what) override;  // 接 CanvasItem 通知流（P2 上游）

private:
    float anchors_[4]  = {0.0f, 0.0f, 1.0f, 1.0f};  // L T R B，默认铺满父
    float offsets_[4]  = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector2 viewport_size_{0, 0};
    FocusMode focus_mode_ = FocusMode::NONE;
    bool has_focus_      = false;
    bool draw_dirty_     = true;
    mutable bool min_size_dirty_ = true;
    mutable Vector2 cached_min_size_{0, 0};
    std::string a11y_label_;

    Vector2 parent_size_() const;   // 父 Control 的 size 或 viewport_size_
};

}  // namespace eda
```

`scene/gui/control.cpp`：

```cpp
#include "control.h"
#include <algorithm>
#include <vector>

namespace eda {

Control::Control() = default;

Vector2 Control::parent_size_() const {
    if (auto* p = get_parent()) {
        if (auto* cp = dynamic_cast<Control*>(p)) return cp->get_size();
    }
    return viewport_size_;
}

Vector2 Control::get_position() const {
    Vector2 ps = parent_size_();
    return {anchors_[0] * ps.x + offsets_[0], anchors_[1] * ps.y + offsets_[1]};
}

Vector2 Control::get_size() const {
    Vector2 ps = parent_size_();
    float left   = anchors_[0] * ps.x + offsets_[0];
    float top    = anchors_[1] * ps.y + offsets_[1];
    float right  = anchors_[2] * ps.x + offsets_[2];
    float bottom = anchors_[3] * ps.y + offsets_[3];
    return {right - left, bottom - top};
}

void Control::set_position(Vector2 p) {
    Vector2 d = p - get_position();
    offsets_[0] += d.x; offsets_[2] += d.x;   // LEFT/RIGHT 跟随 x
    offsets_[1] += d.y; offsets_[3] += d.y;   // TOP/BOTTOM 跟随 y
    _notification(NOTIFICATION_RESIZED);
    update_minimum_size();
}

void Control::set_size(Vector2 s) {
    Vector2 d = s - get_size();
    offsets_[2] += d.x;   // RIGHT 调整宽
    offsets_[3] += d.y;   // BOTTOM 调整高（保持左上位置）
    _notification(NOTIFICATION_RESIZED);
    update_minimum_size();
}

void Control::set_rect(Rect2 r) {
    set_position(r.pos);
    set_size(r.size);
}

void Control::set_anchor(Side side, float value) {
    anchors_[static_cast<int>(side)] = value;
    queue_redraw();
}

void Control::set_offset(Side side, float value) {
    offsets_[static_cast<int>(side)] = value;
    queue_redraw();
}

void Control::set_anchors_preset(LayoutPreset preset) {
    Vector2 s = get_size();
    float w = s.x, h = s.y;
    auto off = [&](float l, float t, float r, float b) {
        offsets_[0]=l; offsets_[1]=t; offsets_[2]=r; offsets_[3]=b;
    };
    auto anc = [&](float l, float t, float r, float b) {
        anchors_[0]=l; anchors_[1]=t; anchors_[2]=r; anchors_[3]=b;
    };
    switch (preset) {
        case LayoutPreset::FULL_RECT:
            anc(0, 0, 1, 1); off(0, 0, 0, 0); break;
        case LayoutPreset::LEFT_WIDE:   anc(0, 0, 0, 1); off(0, 0, 0, 0); break;
        case LayoutPreset::RIGHT_WIDE:  anc(1, 0, 1, 1); off(0, 0, 0, 0); break;
        case LayoutPreset::TOP_WIDE:    anc(0, 0, 1, 0); off(0, 0, 0, 0); break;
        case LayoutPreset::BOTTOM_WIDE: anc(0, 1, 1, 1); off(0, 0, 0, 0); break;
        case LayoutPreset::CENTER:
            anc(0.5f, 0.5f, 0.5f, 0.5f); off(-w / 2, -h / 2, w / 2, h / 2); break;
        case LayoutPreset::TOP_LEFT:
            anc(0, 0, 0, 0); off(0, 0, w, h); break;
        case LayoutPreset::TOP_RIGHT:
            anc(1, 0, 1, 0); off(-w, 0, 0, h); break;
        case LayoutPreset::BOTTOM_LEFT:
            anc(0, 1, 0, 1); off(0, -h, w, 0); break;
        case LayoutPreset::BOTTOM_RIGHT:
            anc(1, 1, 1, 1); off(-w, -h, 0, 0); break;
    }
    queue_redraw();
    update_minimum_size();
}

Vector2 Control::get_combined_minimum_size() const {
    if (min_size_dirty_) {
        cached_min_size_ = get_minimum_size();
        min_size_dirty_ = false;
    }
    return cached_min_size_;
}

void Control::update_minimum_size() {
    if (!min_size_dirty_) {
        min_size_dirty_ = true;
        if (auto* p = dynamic_cast<Control*>(get_parent())) p->update_minimum_size();
    }
}

void Control::queue_redraw() { draw_dirty_ = true; }

void Control::draw_if_dirty() {
    if (!draw_dirty_) return;
    draw_dirty_ = false;
    _notification(NOTIFICATION_DRAW);
    _draw();   // 子类自绘（默认空）
    if (has_focus_) {
        // 无障碍：焦点可视指示（WCAG 2.1 AA：不依赖 hover）
        if (auto* c = current_canvas()) {
            c->draw_rect(get_rect(), Color{0.40f, 0.62f, 1.00f, 1.00f}, /*filled=*/false);
        }
    }
}

void Control::_notification(int what) {
    // Task 1 仅占位；后续接信号分发（focus_enter/exit → 信号系统 P2）
    (void)what;
}

void Control::set_focus(bool f) {
    if (focus_mode_ == FocusMode::NONE) f = false;
    if (has_focus_ == f) return;
    has_focus_ = f;
    _notification(f ? NOTIFICATION_FOCUS_ENTER : NOTIFICATION_FOCUS_EXIT);
    queue_redraw();
}

namespace {
void collect_focusable(const Node* n, std::vector<Control*>& out) {
    if (!n) return;
    if (auto* ctrl = dynamic_cast<const Control*>(n)) {
        if (ctrl->get_focus_mode() != FocusMode::NONE)
            out.push_back(const_cast<Control*>(ctrl));
    }
    for (Node* c : n->get_children()) collect_focusable(c, out);
}
}  // namespace

Control* Control::find_next_focus(bool reverse) const {
    const Node* root = this;
    while (root->get_parent()) root = root->get_parent();
    std::vector<Control*> chain;
    collect_focusable(root, chain);
    if (chain.empty()) return nullptr;
    auto it = std::find(chain.begin(), chain.end(), this);
    if (it == chain.end()) return chain.front();
    size_t idx = static_cast<size_t>(it - chain.begin());
    size_t n = chain.size();
    return chain[reverse ? (idx + n - 1) % n : (idx + 1) % n];
}

}  // namespace eda
```

> 说明：`set_size` 仅调整 `offsets[RIGHT/BOTTOM]`，保持左上位置（对齐 Godot 行为）；锚点预设 `BOTTOM_RIGHT` 先 `set_size` 再应用可保证右下角吸附——见 `RightAnchorStaysPinnedOnParentResize` 测试。`parent_size_()` 通过 `dynamic_cast<Control*>` 取父 Control 的 `get_size()`，否则退回 `viewport_size_`（顶层控件由 Window 注入）。

- [ ] **Step 5: 创建 scene/gui/CMakeLists.txt 并接入 scene**

`scene/gui/CMakeLists.txt`：

```cmake
add_library(eda_scene_gui STATIC
    control.cpp
    control_canvas.cpp)
target_include_directories(eda_scene_gui PUBLIC ${EDA_ROOT})
target_link_libraries(eda_scene_gui PUBLIC eda_scene eda_core)  # CanvasItem(P2)/math/color
```

`scene/CMakeLists.txt` 末尾追加：

```cmake
add_subdirectory(gui)
```

- [ ] **Step 6: 运行验证通过**

Run: `cmake --build build && ctest --test-dir build -R "ControlAnchorTest|ControlMinSizeTest|ControlFocusTest|ControlDrawTest|ControlA11yTest" --output-on-failure`

Expected: PASS（约 16 个测试：锚点 6 + 最小尺寸 2 + 焦点 5 + 自绘 2 + a11y 1）

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat(scene/gui): add Control base with anchor layout, focus, accessibility"
```

---

## Task 2: 布局与容器（Container / Box / Grid / Split / Scroll / Tab）

**范围：** 接管子控件几何的容器体系——`Container` 基类（`_notification(SORT_CHILDREN)` + `fit_child_in_rect`）、`BoxContainer`(HBox/VBox) 按 `size_flags` 一维排列、`GridContainer`、`SplitContainer`、`ScrollContainer`、`TabContainer`、`Margin/Center/AspectRatio/FlowContainer`；最小尺寸递归合并；`queue_sort` 一帧去重。

**Files:**
- Create: `scene/gui/containers/{container,box_container,grid_container,split_container,scroll_container,tab_container,margin_container,center_container,aspect_ratio_container,flow_container}.h` 与对应 `.cpp`
- Create: `scene/gui/widgets/{button,label,line_edit}.h/.cpp`（验证容器排列所需的最小控件集）
- Modify: `scene/gui/CMakeLists.txt`、`tests/CMakeLists.txt`
- Create: `tests/test_containers.cpp`

**Interfaces 概要:**
- `Container : Control`：`queue_sort()`、`fit_child_in_rect(Control*, Rect2)`、`_notification(SORT_CHILDREN)` 虚
- `BoxContainer : Container`：`add_child` 按 `size_flags`(FILL/EXPAND/SHRINK_CENTER) + `separation` 排列
- `GridContainer : Container`：`set_columns(int)`
- `SplitContainer`：可拖拽分隔条、`set_split_ratio(float)`
- `ScrollContainer`：超框滚动、`set_clip_contents(true)`、`follow_focus`
- `TabContainer`：`tab_changed` 信号、可关闭页签

- [ ] 待展开（进入实现时补 Step 1..N 的 TDD：Box 排列断言、最小尺寸冒泡断言、Grid columns 断言、Split 拖拽比例持久化断言）

---

## Task 3: 主题系统（Theme + StyleBox + 三套预设）

**范围：** 统一 `Theme` Resource（`(控件类型, 属性名) → Variant` 键值存储：colors/font_colors/font_sizes/constants/icons/styleboxes）、`StyleBox`（flat/line/texture/nine-patch）、明/暗/高对比度三套内置预设、`add_theme_*` 三级覆盖、SVG 图标按前景色染色、HarfBuzz/FreeType 字体管理、`.theme.toml` 导入导出、`ui_scale` DPI 适配。

**Files:**
- Create: `scene/gui/theme/{theme,style_box,default_theme,theme_loader}.h/.cpp`
- Create: `scene/gui/fonts/font_provider.h/.cpp`
- Modify: `scene/gui/CMakeLists.txt`、`tests/CMakeLists.txt`
- Create: `tests/test_theme.cpp`

**Interfaces 概要:**
- `Theme`：`set/get_color(name,type)`、`set/get_font_size`、`set/get_constant`、`set/get_stylebox`、`set/get_icon`
- `Control::get_theme_color(name,type)` 等（回退：控件局部 → 工程主题 → 默认主题）
- `StyleBox::draw(ControlCanvas&, Rect2)`
- `DefaultTheme::make_light() / make_dark() / make_high_contrast()`
- 切换主题：`ThemeServer::set_current(Theme*)` → 广播 `theme_changed` → 所有控件 `queue_redraw()`

- [ ] 待展开（TDD：查表回退断言、StyleBox 绘制断言、暗色模式颜色对比度 ≥ 4.5:1 断言、实时切换全控件失效缓存断言）

---

## Task 4: 可停靠 Dock 与面板（DockManager / DockContainer / 浮动窗口）

**范围：** 类 Godot/VS 的可拖拽停靠面板——`DockManager` 管理左/右/下/上/中心区域 + 拖拽预览高亮、`DockContainer` 多标签页、`DockPanel` 单面板基类、浮动窗口（拖出主窗口）、中心区独占编辑器画布、预设布局（原理图/PCB/库模式各一套）、`.layout.toml` 持久化与启动恢复、F2 切换显隐。

**Files:**
- Create: `editor/dock/{dock_manager,dock_container,dock_panel,floating_window,dock_layout}.h/.cpp`
- Create: `editor/CMakeLists.txt`
- Modify: 顶层 `CMakeLists.txt`（`add_subdirectory(editor)`）
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_dock.cpp`

**Interfaces 概要:**
- `DockManager::register_panel(id, DockPanel*)`、`dock_to(side, panel)`、`undock_to_floating(panel)`
- `DockContainer : TabContainer`（复用 Task 2）
- `DockPanel : Control`（编辑器面板基类，P7/P8 各业务面板继承）
- `DockLayout::save(path) / load(path)` → `.layout.toml`

- [ ] 待展开（TDD：停靠状态机断言、布局序列化往返一致断言、最小尺寸保护断言）

---

## Task 5: 属性 Inspector（反射驱动 + 插件式）

**范围：** `EditorInspector` 基于 ClassDB `get_property_list()` 自动生成属性编辑器（LineEdit/SpinBox/ColorPicker/EnumDropdown/Vector2Editor/CheckBox/ResourcePicker）、按 `category` 分组折叠、`EditorInspectorPlugin` 插件注入自定义编辑器、多选合并（不同值显示 "---"）、搜索过滤、只读模式（库锁定）、所有修改经 `UndoRedo` 入栈。

**Files:**
- Create: `editor/inspector/{inspector,inspector_plugin,property_editors,inspector_search}.h/.cpp`
- Modify: `editor/CMakeLists.txt`、`tests/CMakeLists.txt`
- Create: `tests/test_inspector.cpp`

**Interfaces 概要:**
- `Inspector::edit(Object* target)` / `edit(const std::vector<Object*>& multi)`
- `InspectorPlugin::can_handle(Object*) / parse_property(...)`
- 注册：`Inspector::add_plugin(InspectorPlugin*)`
- 属性变更 → `EditorUndoRedo::add_action(...)` → 入栈

- [ ] 待展开（TDD：反射生成编辑器断言、多选合并 "---" 断言、插件注入断言、撤销入栈断言；依赖 P2 ClassDB + P3 UndoRedo）

---

## Task 6: 命令面板（Command Registry + 模糊搜索）

**范围：** `Ctrl+Shift+P` 唤起的命令面板——`CommandRegistry` 单一真相源（菜单/工具栏/命令面板共用 `Command{id,name,shortcut,icon,category,callback}`）、子序列 + 拼音首字母 + 最近使用加权模糊匹配、多模式前缀（`>` 命令 / `@` 跳转器件 / `:` 跳图页 / `#` 设置）、上下键 + 回车 + Esc、按编辑器模式过滤、万级命令 ≤ 16ms。

**Files:**
- Create: `editor/command_palette/{command,command_registry,command_palette,fuzzy_match}.h/.cpp`
- Modify: `editor/CMakeLists.txt`、`tests/CMakeLists.txt`
- Create: `tests/test_command_palette.cpp`

**Interfaces 概要:**
- `CommandRegistry::register_command(Command)` / `find(id)` / `all(mode_filter)`
- `CommandPalette : Control`：`set_query(s)` → 增量匹配返回排序结果
- `fuzzy_match(query, text) → score`（子序列 + 连续匹配奖励 + 拼音首字母 + 频率加权）

- [ ] 待展开（TDD：`bsr` → "板框设计" 拼音首字母断言、子序列匹配断言、最近使用加权断言、性能基准 10000 命令 ≤ 16ms 断言）

---

## Task 7: 快捷键体系（InputMap + 三套预设）

**范围：** `InputMap`（物理按键 `KeyModifierMask + Key` → 抽象 `Action`）、KiCad / Altium / 立创EDA / 默认 四套预设、作用域（全局 vs 编辑器局部，按焦点切换）、冲突检测（高亮警告、禁覆盖关键动作）、`.keymap.toml` 导入导出、跨平台 Ctrl/Cmd 映射、菜单/tooltip/命令面板统一显示当前绑定。

**Files:**
- Create: `editor/shortcuts/{input_map,keymap_loader}.h/.cpp`
- Create: `editor/shortcuts/presets/{kicad,altium,jlceda,default}.toml`
- Modify: `editor/CMakeLists.txt`、`tests/CMakeLists.txt`
- Create: `tests/test_shortcuts.cpp`

**Interfaces 概要:**
- `InputMap::bind(action, KeyBinding)` / `get_bindings_for(action)` / `action_from_event(KeyEvent) -> std::string`
- `InputMap::load_preset(name)` / `save(path)` / `detect_conflicts() -> std::vector<Conflict>`
- 业务订阅：`Input::is_action_pressed("pcb_route")`（与 P1 InputState 衔接）

- [ ] 待展开（TDD：四套预设加载断言、Action 映射断言、冲突检测断言、作用域切换断言、Ctrl/Cmd 跨平台映射断言）

---

## Task 8: i18n + 无障碍 + 本地化（翻译系统 / a11y / l10n）

**范围：**
- **翻译系统（08-01）**：`tr() / tr_n / tr_ctxt`、`.po` 资源（`lang/<locale>/LC_MESSAGES/eda.po`）、CLDR 复数、msgctxt 上下文消歧、CI `xgettext` 抽取 `.pot` + 覆盖率门禁、术语一致性校验（对齐术语表）、ICU `MessageFormat`、运行时切换免重启（信号广播全控件刷新）。
- **无障碍（08-02）**：WCAG 2.1 AA 对齐（对比度阈值）、键盘全操作、焦点可视指示、`ui_scale` 0.8x~2.0x 缩放、平台 AT API 后端（Win UIAutomation / macOS NSAccessibility / Linux AT-SPI2）、减少动画、RTL 翻转。
- **本地化（08-03）**：ICU 数字/日期/货币格式、**单位本地化（mm/mil/in 输入解析归一化，EDA 特有）**、locale 三层回退（用户 > 工程 > 系统）、locale 与语言独立（英文 UI + 公制单位）。

**Files:**
- Create: `scene/gui/i18n/{translation,translation_server,po_loader,po_extractor}.h/.cpp`
- Create: `scene/gui/a11y/{accessibility,accessibility_tree,at_api}.h/.cpp`
- Create: `scene/gui/l10n/{locale,number_format,units}.h/.cpp`
- Create: `lang/en/LC_MESSAGES/eda.po`、`lang/zh-CN/LC_MESSAGES/eda.po`（首发中英）
- Modify: `scene/gui/CMakeLists.txt`（链接 ICU）、`tests/CMakeLists.txt`
- Create: `tests/test_i18n.cpp`

**Interfaces 概要:**
- `tr(source) / tr_n(singular, plural, n) / tr_ctxt(context, source)`
- `TranslationServer::set_locale(locale) -> broadcast locale_changed`
- `PoLoader::load(path) -> Translation`
- `Control` 增 `accessibility_role / accessibility_value / accessibility_state`
- `Locale::system_detect() / user_pref() / project_pref()`、`Locale::current()`
- `Units::parse("1.5mm") -> nano_meters`、`Units::format(nm, Unit::MIL)`

- [ ] 待展开（TDD：`tr()` 查表回退断言、CLDR 复数规则断言（中无/英 1-other/俄多类）、msgctxt 消歧断言、locale 切换信号广播断言、mm/mil 解析归一化断言、对比度 ≥ 4.5:1 断言）

---

## 接口契约（Contract）

### Control API —— 暴露给所有编辑器（P6/P7/P8/P9...）

| 契约 | 调用方 | 说明 |
|------|--------|------|
| `Control` 基类（锚点/几何/最小尺寸/焦点/a11y） | 所有编辑器面板 | Task 1 |
| 内置控件集 `Button/Label/LineEdit/TextEdit/Tree/ItemList/ComboBox/CheckBox/SpinBox/ColorPicker` | 所有编辑器 UI | Task 2 |
| 容器 `HBox/VBox/Grid/Split/Scroll/Tab/Margin/Center/Flow` | 所有编辑器布局 | Task 2 |
| `get_theme_color/font_size/constant/stylebox/icon(name,type)` | 所有控件查表外观 | Task 3 |
| `tr(source) / tr_n / tr_ctxt` | 所有 UI 文本 | Task 8 |

### Dock / Inspector API —— 暴露给编辑器面板

| 契约 | 调用方 | 说明 |
|------|--------|------|
| `DockManager::register_panel(id, DockPanel*)` / `dock_to(side, panel)` | P7 原理图 / P8 PCB / P6 库等业务面板注册 | Task 4 |
| `DockLayout::save / load(.layout.toml)` | 工程保存与启动恢复 | Task 4 |
| `Inspector::edit(Object*)` / `Inspector::add_plugin(InspectorPlugin*)` | 所有编辑器选中对象后属性编辑 | Task 5 |

### 命令面板 / 快捷键 API —— 暴露给工具模式与菜单

| 契约 | 调用方 | 说明 |
|------|--------|------|
| `CommandRegistry::register_command(Command{id,name,shortcut,callback})` | 所有编辑器动作（菜单/工具栏/命令面板单一真相源） | Task 6 |
| `CommandPalette::set_query(s)` | Ctrl+Shift+P 唤起 | Task 6 |
| `InputMap::bind(action, KeyBinding)` / `action_from_event(KeyEvent)` | 工具模式订阅（`Input::is_action_pressed("pcb_route")`） | Task 7 |
| `InputMap::load_preset("kicad"/"altium"/"jlceda"/"default")` | 用户切换键位预设 | Task 7 |

---

## 验收标准（Definition of Done）

- [ ] **Control 树能布局渲染**：嵌套 Control 的锚点布局在不同父尺寸下计算正确（Task 1 全部测试 PASS，含锚点/位置/尺寸/最小尺寸/焦点遍历/自绘/无障碍标签）
- [ ] **容器正确排列子控件**：HBox/VBox/Grid 按 `size_flags` 排列，最小尺寸递归合并无矛盾，`queue_sort` 一帧去重
- [ ] **主题可切换**：明/暗/高对比度三套预设运行时切换，全部控件 `queue_redraw()` 后视觉一致，业务代码零改动；高对比度满足 WCAG 文本对比度 ≥ 4.5:1
- [ ] **Dock 可停靠保存布局**：Dock 可拖拽吸附四边 / 浮出主窗口 / 多标签页；`.layout.toml` 序列化与启动恢复往返一致；窗口缩放时最小尺寸保护（不挤压到 0）
- [ ] **Inspector 反映选中对象属性**：基于反射自动生成属性编辑器；多选合并显示 "---"；插件可注入自定义编辑器；修改经 UndoRedo 可撤销
- [ ] **命令面板模糊搜索**：`Ctrl+Shift+P` 唤起，键入子序列/拼音首字母命中正确命令，万级命令下匹配 ≤ 16ms
- [ ] **快捷键三套预设切换**：KiCad/Altium/立创EDA/默认 四套预设加载切换；冲突检测高亮；业务代码只订阅 Action，换键位零改动
- [ ] **i18n 运行时切换免重启**：切换 locale 后所有 UI 文本重渲染（信号广播），`.po` 覆盖率 ≥ 95%，术语译法与术语表一致（CI 门禁）；缺失键退回英文，绝不显示 key
- [ ] **a11y 对比度达标**：WCAG 2.1 AA 文本对比度 ≥ 4.5:1、UI 组件边界 ≥ 3:1；焦点可视指示（2px 对比色边框）；键盘全操作（Tab/Shift+Tab/Esc）；`ui_scale` 0.8x~2.0x 缩放不破坏布局；控件暴露 `accessibility_label/role` 供平台 AT
- [ ] **单位本地化**：输入框接受 `"1.5mm"` / `"60mil"` / `"0.0625in"` 自动归一化；mm/mil 切换影响显示与解析

---

## 自检记录（2026-07-27）

- **Spec 覆盖**：Control 控件体系（Task 1）、布局与容器（Task 2）、主题系统（Task 3）、可停靠 Dock（Task 4）、属性 Inspector（Task 5）、命令面板（Task 6）、快捷键体系（Task 7）、i18n + 无障碍 + 本地化（Task 8）—— 对标 [[04-GUI与编辑器框架]] 八个子功能全覆盖
- **Task 1 完整性**：Control 类签名包含全部要求项——继承 `CanvasItem`、`_draw()` 虚函数、锚点锚定（`set_anchor/get_anchor/set_anchors_preset`）、`set_size/get_size/set_position/get_position`、焦点 `set_focus_mode/set_focus/has_focus/find_next_focus`、`accessibility_label`；GoogleTest 覆盖锚点布局计算（6）、最小尺寸（2）、焦点遍历（4，含伪渲染器验证焦点高亮）、自绘（2，伪渲染器）、无障碍（1）共约 16 个断言
- **类型一致性**：`Control/Container/Theme/DockManager/Inspector/CommandRegistry/InputMap/TranslationServer` 跨任务命名一致；`tr()` source 取自术语表"英文 source"列；`dock`=可停靠面板、`inspector`=属性检查器（对齐 [[00-总览/04-术语表]]）
- **上游契约**：`Control : CanvasItem`（P2）、`RenderingServer.canvas_item_add_*`（P1 扩展）、`ClassDB`（P2，Inspector 消费）、`UndoRedo`（P3，Inspector 消费）—— 本 plan 仅消费不重造
- **占位符**：仅 Task 2..8 标注 `- [ ] 待展开`（按要求）；Task 1 给完整测试与实现代码，无空 placeholder

---

## 后续（不在本 plan）

- **P5**：工程树 / 开放格式 / 快照 / 模板（`eda/project/`，`core/io/`）
- **P6**：元件库编辑器（符号/封装/3D/一体化绑定/IPC-7351 向导）
- **P7**：原理图编辑器（基于本框架的 Control + Dock + Inspector 搭建）
- **P8**：PCB 编辑器（同上，多模式工具切换基于 `editor/tools/tool_manager`）
- **后续扩展**：IME 深度集成、自定义控件派生的插件接口（与 P18 `modules/` 协同）
