---
title: Godot 开源架构参考（目录 + 接口）
lang: zh-CN
status: 权威
created: 2026-07-27
tags:
  - Godot
  - 参考
  - 架构
  - 引擎
related:
  - "[[00-总览/03-项目目录结构]]"
  - "[[00-总览/01-功能设计目录]]"
---

# Godot 开源架构参考（目录 + 接口）

> **用途**：自研 EDA 引擎借鉴 Godot 时的**对照参考**。实现 P1（RenderingServer）、P2（Object/ClassDB）、P4（Control/GUI）等时直接对照原版，避免重新发明。
> **立场**：研究 Godot 开源实现，借鉴其架构思想；**不直接依赖 Godot 运行时**，不复制其源码。
> **版本基准**：Godot 4.x（MIT 许可，源码 github.com/godotengine/godot）。

---

## 1. 顶层目录结构（Godot 实际）

| 目录 | 职责 | 我们对应（见 [[00-总览/03-项目目录结构]]） |
|------|------|-------------------------------|
| `core/` | 基础类型、对象系统（Object/ClassDB）、Variant、Signal、Resource、容器、数学、String、内存 | `core/` |
| `servers/` | **后端无关**的低层服务抽象（RenderingServer、PhysicsServer、AudioServer、TextServer、NavigationServer） | `servers/` |
| `drivers/` | 各服务的**后端实现**（vulkan/opengl3 渲染驱动、alsa/wasapi/coreaudio 音频、物理驱动） | `drivers/` |
| `scene/` | 节点系统（Node、SceneTree）、内置节点（CanvasItem、Control、Node2D、Node3D） | `scene/` |
| `editor/` | Godot 编辑器自身（插件式、Dock、Inspector） | `editor/` |
| `platform/` | 平台层（OS、DisplayServer，各平台子目录 windows/linuxbsd/macos/android/ios/web） | `platform/` |
| `modules/` | **可插拔模块**（每个含 SCsub/config.py/register_types） | `modules/`（F2 插件） |
| `main/` | 程序入口（main.cpp、os_main） | `main/` |
| `thirdparty/` | 第三方库 | `third_party/` |

> **核心架构智慧**：`servers/`（抽象）与 `drivers/`（实现）严格分离——业务层只认 server 接口，换后端不动业务。这已直接用于我们的 P1（RenderingServer + drivers/opengl|vulkan）。

---

## 2. 核心子系统与关键接口

> 下列方法名为 Godot 真实 API（事实性引用），完整签名以 Godot 4.x 源码/文档为准。

### 2.1 `core/` — 基础与对象系统（P2 借鉴重点）

**Object**（一切对象的根基类，core/object/object.h）
- 生命周期：`notification(int p_what)`（虚通知，如 NOTIFICATION_ENTER_TREE）
- 属性：`set(name, value)` / `get(name)` / `get_property_list()` / `set_meta/get_meta`（任意附加数据）
- 信号：`add_user_signal`、`emit_signal(name, args)`、`connect(signal, callable)`、`disconnect`、`is_connected`
- 派生：`RefCounted`（引用计数，自动 GC）、非引用对象需手动管理

**ClassDB**（反射注册表，core/object/class_db.h）
- 注册宏：`GDREGISTER_CLASS(T)` / `GDREGISTER_VIRTUAL_CLASS`
- 绑定：`ClassDB::bind_method(name, &T::method)`、`bind_signal`、`bind_property`、`add_property`
- 运行时：`ClassDB::instantiate(class_name)`、`ClassDB::get_property_list`
- **价值**：编辑器 Inspector、序列化、脚本绑定全靠它——不写反射则无统一对象模型

**Variant**（动态类型，core/variant/variant.h）
- 承载 nil/bool/int/float/String/Vector2/3/4/Color/Rect2/Transform2D/Array/Dictionary/Object 等
- 内置运算与 `Variant::call()`；脚本/序列化的通用值类型

**Signal**（信号槽，core/object/undo_redo 等）
- `Signal(object, "name")`；`connect(Callable)`、`emit(args)`、`disconnect`
- Godot 的观察者解耦基石

**Resource**（共享资源，core/io/resource.h）
- 继承 `RefCounted`，引用计数共享；`resource_path`（`res://`）、`resource_name`
- 序列化到 `.tres`（文本）/`.res`（二进制）；`ResourceLoader::load` / `ResourceSaver::save`
- **价值**：器件库、符号、封装等天然是 Resource

**数学**（core/math/）：`Vector2/2i/3/3i/4`、`Color`、`Rect2/2i`、`Transform2D`、`Transform3D`、`Basis`、`AABB`、`Plane`、`Quaternion`——**我们的 `core/math/` 已对齐**（Vector2/Color/Rect2/Transform2D）。

**容器**（core/containers/）：`Vector<T>`、`LocalVector<T>`（无异常安全的高性能版）、`HashMap`、`HashSet`、`RBSet`。

**其他**：`String`、`StringName`（全局唯一字符串，标识用）、`NodePath`、`Callable`、`RID`（资源 ID，见下）。

### 2.2 `servers/` — 后端无关服务（P1/P3 借鉴重点）

**RenderingServer**（servers/rendering_server.h）—— 渲染服务器，**本 plan P1 的精简原型**
- **RID 句柄管理**：所有资源用 RID（轻量整数 ID），避免 Object 开销
  - `canvas_create() → RID`、`viewport_create() → RID`
  - `canvas_item_create() → RID`（2D 可绘制对象）
  - `texture_2d_create() → RID`、`material_create() → RID`、`shader_create() → RID`
- **canvas_item 绘制命令**（批渲染入口，对应我们的 `draw_triangles`）
  - `canvas_item_add_rect(rid, rect, color)`
  - `canvas_item_add_line(rid, from, to, color, width)`
  - `canvas_item_add_multiline(rid, lines, colors)`
  - `canvas_item_add_polygon(rid, points, colors, uvs=, texture=)`
  - `canvas_item_add_texture_rect(...)`、`canvas_item_add_circle` 等
- **canvas_item 状态**：`canvas_item_set_parent(item, parent)`、`canvas_item_set_transform`、`canvas_item_set_modulate`、`canvas_item_set_visible`、`canvas_item_set_z_index`
- **价值**：万级图元经 RID + canvas_item 命令批量提交，是 Godot 2D 高性能的核心——我们 EDA 的万焊盘渲染照此模式（当前 P1 简化为单 `draw_triangles`，后续扩展为 canvas_item RID 模型）。

> **RenderingServer vs RenderingDevice**：RenderingServer 是高层服务器（场景/材质/光照语义）；`RenderingDevice`（servers/rendering/rendering_device.h）是更底层的 GPU 抽象（buffer/texture/shader/pipeline/compute/draw_list），直接映射 Vulkan 概念。drivers 里的 Rasterizer 用 RenderingDevice 实现 RenderingServer。

**其他 server**（后续按需参考）：
- `PhysicsServer2D/3D`：space/body/shape/joint，全 RID 管理
- `AudioServer`、`TextServer`（字体/排版）、`NavigationServer`、`XRServer`

### 2.3 `drivers/` — 后端实现（P1 借鉴重点）

- **渲染驱动**：
  - `drivers/vulkan/`：`RasterizerRDVK` 等，用 RenderingDevice(Vulkan) 实现 RenderingServer
  - `drivers/opengl3/`：`RasterizerGLES3`，OpenGL 后端
  - 启发：我们的 `drivers/opengl/rasterizer_gl` + `drivers/vulkan/rasterizer_vk` 命名即源于此
- **音频驱动**：`drivers/alsa/`、`drivers/wasapi/`、`drivers/coreaudio/`、`drivers/pulseaudio/`
- **物理/其他**：bullet、bvh 等

### 2.4 `scene/` — 节点系统（P2/P4 借鉴重点）

**Node**（scene/main/node.h）—— 场景树节点基类
- 树结构：`add_child` / `remove_child` / `get_parent()` / `get_children()`
- 生命周期：`_enter_tree()` / `_exit_tree()` / `_ready()` / `_process(delta)` / `_physics_process(delta)`（虚函数，引擎回调）
- `owner` 概念（场景归属）、分组 `add_to_group` / `call_group`
- `name`、`scene_file_path`

**SceneTree**（scene/main/scene_tree.h）：管理 root 节点 + 主循环（process/input/draw 回调驱动）

**CanvasItem**（scene/2d/canvas_item.h）—— 2D 可绘制节点基类
- `Node2D`（有 Transform2D）和 `Control`（UI）都继承自它
- 持有一个 server 中的 canvas_item RID（`get_canvas_item()`）
- `_draw()` 虚函数：节点在此调 `draw_rect/draw_line/draw_texture` 等，实际压入 server canvas_item

**CanvasLayer**：独立的 2D 渲染层（多 UI 层/视差）

**Control**（scene/gui/control.h）—— UI 控件基类，**P4 自研 GUI 的范本**
- 布局：锚点（anchor_left/right/top/bottom）+ 偏移（offset_*）+ grow 方向 + `size_flags`（fill/expand）
- 尺寸：`set_size` / `set_position` / `get_rect()` / `get_minimum_size()`（虚）
- 主题：`add_theme_*`（覆盖全局 Theme Resource）
- 焦点：`set_focus_mode`、focus_enter/exit 信号
- 输入：`_gui_input(event)` 虚函数、`gui_input` 信号、鼠标过滤 mouse_filter
- 自绘：`queue_redraw()` + `_draw()`
- 内置控件（scene/gui/）：Button、Label、LineEdit、TextEdit、Tree、ItemList、ScrollContainer、TabContainer、SplitContainer、PopupMenu 等——可停靠 Dock/Inspector/命令面板的基础积木

### 2.5 `platform/` — 平台层（P1 借鉴，后续替换）

- `OS`（单例，platform/os）：系统信息、命令行、环境变量、线程、延迟
- `DisplayServer`（platform/）：窗口、事件循环、鼠标/键盘事件、剪贴板、光标、IME、显示器信息——**每个平台子目录实现一份**（windows/linuxbsd/macos/...）
- 我们当前用 GLFW 引导（`platform/glfw/`），后续替换为自研 DisplayServer 即对齐 Godot 此层

### 2.6 `editor/` — 编辑器框架（P4+ 借鉴）

- **插件式**：`EditorPlugin`（自定义 dock/inspector/工具/底栏）
- `EditorInspectorPlugin`（属性面板扩展）、`EditorNode`、`EditorInterface`
- **撤销栈**：`UndoRedo`（命令模式，add_do_method/add_undo_method/commit）
- 启发：**Godot 编辑器 = 用自己引擎做的专业编辑器**——这正是我们 EDA 编辑器（原理图/PCB）的模式：用自研引擎搭专业编辑器，而非通用 UI 套壳

### 2.7 `modules/` — 可插拔模块（P18 借鉴）

- 每个模块目录含：
  - `register_types.h/.cpp`：`initialize_module()` / `uninitialize_module()`，在此 `GDREGISTER_CLASS` 注册模块提供的类
  - `SCsub`（SCons 构建脚本）、`config.py`（模块元信息与依赖）
- 模块可注册新 Object / Resource / Server / Node，与引擎核心解耦
- 启发：我们的 `modules/`（F2 插件）沿用 register_types 思路；`eda/` 业务子目录也按"自包含 + 自带构建脚本"组织

### 2.8 `main/` — 入口

- `main/main.cpp`：启动流程（注册模块 → 初始化 OS/DisplayServer → 创建 SceneTree → 跑主循环）

---

## 3. 关键设计模式（借鉴清单）

| # | 模式 | Godot 实现 | 我们的借鉴点 | 对应 Plan |
|---|------|-----------|-------------|----------|
| 1 | **servers + drivers 分离** | RenderingServer(抽象) + drivers/(实现) | 已用：`servers/` + `drivers/` | P1 ✅ |
| 2 | **RID 句柄管理** | 所有渲染资源走 RID（轻量、批量） | draw_triangles → 未来 canvas_item RID 模型 | P1→扩展 |
| 3 | **Object + ClassDB 反射** | 统一对象模型、属性反射、序列化 | `core/object/`（Object/ClassDB） | P2 |
| 4 | **Node + SceneTree** | 节点树、生命周期、信号、自动 process | `scene/main/`（节点树） | P2 |
| 5 | **CanvasItem 批渲染** | 2D 图元经 canvas_item 命令批提交 | Canvas2D → CanvasItem 节点化 | P1→P3 |
| 6 | **Resource 共享** | RefCounted、`res://`、序列化 | `core/object/resource` | P2/P5 |
| 7 | **Signal 信号槽** | connect/emit 观察者解耦 | `core/object/signal` | P2 |
| 8 | **Variant 动态类型** | 脚本/序列化通用值 | `core/variant`（按需） | P2 |
| 9 | **Control 自绘 GUI** | 不依赖系统控件、锚点/容器/主题 | `scene/gui/`（Control 体系） | P4 |
| 10 | **modules 注册机制** | register_types 插件注册 | `modules/`（F2） | P18 |
| 11 | **UndoRedo 命令栈** | do/undo 方法对 + commit | `eda/command/`（命令栈/ECO） | P3/P14 |
| 12 | **编辑器插件式** | EditorPlugin/Dock/Inspector | `editor/` + EDA 编辑器 | P4 |

---

## 4. 与本项目对照映射

| Godot 概念 | Godot 位置 | 我们位置 | 状态 |
|-----------|-----------|---------|------|
| 数学类型 | core/math/ | core/math/ | ✅ 已对齐 |
| Object/ClassDB/Signal/Resource | core/object/ | core/object/ | ⏳ P2 |
| Variant | core/variant/ | core/variant/ | ⏳ P2 |
| RenderingServer | servers/rendering_server.h | servers/rendering_server.h | ✅ P1（精简） |
| RenderingDevice | servers/rendering/rendering_device.h | servers/（未来） | ⏳ |
| RasterizerGL/VK | drivers/opengl3/、drivers/vulkan/ | drivers/opengl/、drivers/vulkan/ | ✅ P1 |
| OS/DisplayServer | platform/os、platform/ | platform/glfw（后替换） | ✅ P1 引导 |
| Node/SceneTree/CanvasItem | scene/main/、scene/2d/ | scene/main/、scene/2d/ | ✅/⏳ P2 |
| Control + 控件 | scene/gui/ | scene/gui/ | ⏳ P4 |
| 编辑器框架 | editor/ | editor/ | ⏳ P4 |
| modules | modules/ | modules/ | ⏳ P18 |
| （EDA 专有，无对应） | — | eda/* | ⏳ 自研 |

---

## 5. 研读建议（怎么读 Godot 源码）

**按我们的 Plan 进度，分阶段研读**：

| 阶段 | 重点研读 Godot 文件 | 关联我们的产出 |
|------|---------------------|---------------|
| P1（渲染） | `servers/rendering_server.h`（canvas_item_* 方法族）、`servers/rendering/rendering_device.h`、`drivers/vulkan/*` `drivers/opengl3/*`、`scene/2d/canvas_item.h` | RenderingServer 抽象 + 双后端 |
| P2（对象） | `core/object/object.h`、`core/object/class_db.h`、`core/variant/variant.h`、`core/object/ref_counted.h`、`core/io/resource.h`、`core/object/undo_redo.h` | Object/ClassDB/Variant/Resource |
| P2（节点） | `scene/main/node.h`、`scene/main/scene_tree.h` | Node/SceneTree |
| P4（GUI） | `scene/gui/control.h`、`scene/gui/{button,label,line_edit,scroll_container,tab_container}.h`、`editor/editor_node.h`、`editor/editor_plugin.h`、`editor/editor_inspector*` | Control 体系 + 编辑器框架 |

**研读技巧**：
- 从 `.h` 看接口，`.cpp` 看实现要点，不逐行读（Godot 单文件常数千行）
- 关注"方法族分组"（canvas_item_*、texture_*、material_*），理解 RID 资源生命周期（create → set → free）
- 对照 `core/object/method_bind.h` 理解 ClassDB 如何把方法注册为可反射
- 注意差异：**我们的 EDA 不是游戏**——万级焊盘图元的稳定性优先级高于游戏动态对象；不需要动画/物理（音频可选）；但**编辑器框架几乎 1:1 可借鉴**

---

## 6. 参考来源

- [Godot 架构总览（4.6）](https://docs.godotengine.org/en/4.6/engine_details/architecture/godot_architecture_diagram.html) — 目录与 servers/drivers 关系（官方）
- [Engine Core and Modules（4.4）](https://docs.godotengine.org/en/4.4/contributing/development/core_and_modules/index.html) — core/modules 组织（官方）
- [Engine Architecture（stable）](https://docs.godotengine.org/en/stable/engine_details/architecture/index.html) — 源码组织（官方）
- [RenderingServer 类参考](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html) — canvas_item/RID 接口（官方）
- [Optimization using Servers](https://docs.godotengine.org/en/4.5/tutorials/performance/using_servers.html) — CanvasItem RID 与 server 性能（官方）
- 源码：[github.com/godotengine/godot](https://github.com/godotengine/godot)（MIT）

> 注：本文档方法名为 Godot 事实性 API，描述为对架构的理解性总结；逐方法精确签名以 Godot 4.x 源码与官方文档为准。WebFetch 受企业网络限制，部分细节经搜索核对 + 源码常识整理。
