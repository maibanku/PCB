---
title: eda/geometry 几何内核 · 设计笔记
lang: zh-CN
status: 权威
created: 2026-07-27
tags:
  - EDA
  - 几何
  - 设计笔记
  - P3
related:
  - "[[00-总览/03-项目目录结构]]"
  - "[[00-总览/02-实现路线图]]"
  - "[[00-总览/01-功能设计目录]]"
---

# eda/geometry 几何内核 · 设计笔记

> 模块路径：`eda/geometry/`。对应 Plan **P3**，功能章节 **B2**。这是 EDA 业务的几何地基：原理图/PCB/validation/manufacturing 的所有图元运算都依赖它。

## 1. 定位与边界

**做什么**：2D 几何图元的数据表示、布尔运算、偏移、三角剖分、空间索引、图元求交、测量、变换、网格吸附、圆弧离散化。

**不做什么**：
- 不做渲染（归 `servers/`+`drivers/`）
- 不做业务语义（走线/焊盘/网络等概念归 `eda/pcb`、`eda/schematic`）
- 不做设计规则判定（归 `eda/validation`，geometry 只提供几何事实）
- 不做 3D 几何（机械装配归 `eda/mechanical`）

**依赖**：`core/math`（Vector2/Transform2D）、第三方 Clipper2、earcut。
**被依赖**：`eda/schematic`、`eda/pcb`、`eda/validation`、`eda/manufacturing`。

---

## 2. 核心设计决策

| # | 决策点 | 推荐 | 理由 | 备选 |
|---|--------|------|------|------|
| **D1** | 坐标/数值基础 | **定点整数 `int64_t`，内部单位 nm（纳毫米）** | ① Clipper2 原生 int64，布尔运算零精度损失 ② EDA 行业惯例（KiCad 用 nm/整数）③ 浮点布尔会产生缝隙/重叠，万级焊盘 DRC 会累积误差 ④ nm 量程（±9.2×10⁹ nm ≈ ±9.2 m）覆盖任何板尺寸且分辨率够（0.001μm） | double+mm（直觉但布尔有精度坑） |
| **D2** | 图元抽象风格 | **值类型（少量 POD 结构）+ 自由函数**，不搞深继承层次 | ① 缓存友好、序列化简单 ② EDA 图元海量（万级），值类型+SOA 利于性能 ③ 算法以自由函数分派（类似 C++ 标准库风格） | OO 类层次（Point/Line/… 共同基类，多态开销） |
| **D3** | 圆弧表示 | **参数表示（圆心+半径+起止角+方向）+ 按需离散化** | 保留精确参数用于显示/编辑/测量；布尔/求交前离散化为 Polyline（Clipper2 不支持弧）。离散化角度容差可配 | 全程离散化（丢精度）、贝塞尔近似（过复杂） |
| **D4** | 第三方库 | **Clipper2（布尔/偏移）+ earcut（三角剖分）**，本模块用薄 C++ 接口包装，业务层只见 `eda::geometry::*` | 隔离第三方 API，未来可替换实现；业务层不直接 include Clipper2 | 自研布尔（不现实，鲁棒性极难） |
| **D5** | 空间索引 | **自研轻量 R-tree**（bulk-load、叶子存 AABB+图元 id） | 避免引入 Boost 重依赖；EDA DRC 邻域查询是主要场景，R-tree 足够；符合自研引擎精神 | Boost.Geometry rtree（成熟但重）、四叉树、网格 |

> D1/D5 是两个最可能引发讨论的点，见文末「待确认」。

---

## 3. 数据模型

### 3.1 基础标量与点

```cpp
namespace eda::geometry {

using Coord = int64_t;                 // 内部单位 nm（D1）

struct Point {                         // 整数点
    Coord x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

struct PointF {                        // 浮点点（显示/测量/离散化中间量，不参与布尔）
    double x = 0, y = 0;
};

}  // namespace eda::geometry
```

### 3.2 图元类型（值类型）

```cpp
// 线段
struct Segment { Point a, b; };

// 圆弧（D3）：中心+半径+起止角（弧度）+ 方向
struct Arc {
    Point center;
    Coord radius;
    double start_angle = 0.0;
    double end_angle = 0.0;
    bool ccw = true;                   // 逆时针为正
};

// 折线（开路径：导线、走线轮廓、未闭合板框段）
struct Polyline {
    std::vector<Point> points;         // 至少 2 点
};

// 多边形（可含孔：外环 + 内环列表）。环方向约定：外环 CCW，孔 CW
struct Polygon {
    std::vector<Point> outer;          // 外环
    std::vector<std::vector<Point>> holes;  // 孔（0..n）
};

// 带宽路径（走线本质：中心线 + 宽度，渲染/DRC 时偏移成 Polygon）
struct Path {
    std::vector<Point> points;         // 中心折线
    Coord width = 0;
};

// 轴对齐包围盒（空间索引用）
struct Box {
    Point min, max;
    static Box from_points(std::span<const Point>);
    bool intersects(const Box&) const;
};

}  // namespace eda::geometry
```

> **设计要点**：圆弧不塞进 Polyline，独立保留参数；但提供 `tessellate(Arc, tolerance) -> Polyline` 供布尔/求交前离散化。走线用 `Path`（中心线+宽）表达，避免每条走线都存完整多边形。

### 3.3 单位

```cpp
// units.h：用户层用 mil/mm/inch，边界处转为内部 nm
constexpr Coord mm_to_nm(double mm) { return (Coord)(mm * 1e6); }
constexpr Coord mil_to_nm(double mil) { return (Coord)(mil * 25400.0); }
constexpr Coord inch_to_nm(double in) { return (Coord)(in * 25.4e6); }
constexpr double nm_to_mm(Coord nm) { return nm / 1e6; }
```

---

## 4. 子模块与接口

| 子模块（头文件） | 核心接口 | 底层 | 用途 |
|---|---|---|---|
| `boolean.h` | `union(a,b)` `difference(a,b)` `intersection(a,b)` `xor(a,b)`，入参出参为 `Polygon`/`Polyline` 列表 | Clipper2 | 铺铜、板框、区域并差 |
| `offset.h` | `offset(Polygon, delta, join, miter)` `inflate` `shrink` | Clipper2 ClipperOffset | 铺铜收缩、间距、courtyard |
| `triangulate.h` | `triangulate(Polygon) -> 索引三角形列表` | earcut | 铺铜填充、GPU 渲染 |
| `index.h` | `RTree<T>`：`insert` `remove` `query(Box)` `nearest(Point)` | 自研（D5） | DRC 邻域、拾取、视图剔除 |
| `intersect.h` | `segment_segment` `segment_arc` `arc_arc` `polyline_self` | 参数方程/离散化 | 连通性、DRC、编辑推挤 |
| `measure.h` | `length(Polyline)` `area(Polygon)` `distance(a,b)` `point_to_segment` | 解析公式 | 线长、面积、间距检查 |
| `transform.h` | `translate` `rotate` `mirror` `scale`（对 Point/Polyline/Polygon） | core/math Transform2D | 布局变换、镜像 |
| `snap.h` | `snap_to_grid(Point, grid_nm)` `snap_point` `snap_angle` | 取整 | 栅格吸附 |
| `arc_tessellate.h` | `tessellate(Arc, tolerance_nm) -> Polyline` | 弧长采样 | 弧→折线（布尔前） |
| `units.h` | mm/mil/inch ↔ nm | constexpr | 单位边界转换 |

### 接口风格示例（布尔）

```cpp
namespace eda::geometry::boolean {

// 多个多边形并集，返回结果（可能多环、含孔）
std::vector<Polygon> unite(std::span<const Polygon> inputs);

// a 减 b
std::vector<Polygon> subtract(const Polygon& a, std::span<const Polygon> b);

// 交集（短路检测：结果非空即相交）
std::vector<Polygon> intersect(const Polygon& a, const Polygon& b);

}  // namespace eda::geometry::boolean
```

---

## 5. 文件结构

```
eda/geometry/
├── CMakeLists.txt          # eda_geometry 库；链接 Clipper2、earcut、eda_core
├── types.h                 # Coord/Point/Segment/Arc/Polyline/Polygon/Path/Box
├── units.h                 # 单位转换（header-only constexpr）
├── transform.h             # 2D 变换（header-only，复用 core/math）
├── snap.h                  # 网格吸附（header-only）
├── arc_tessellate.h / .cpp # 圆弧离散化
├── boolean.h / .cpp        # Clipper2 包装（unite/subtract/intersect/xor）
├── offset.h / .cpp         # ClipperOffset 包装
├── triangulate.h / .cpp    # earcut 包装
├── index.h / .cpp          # 自研 R-tree
├── intersect.h / .cpp      # 图元求交
└── measure.h / .cpp        # 长度/面积/距离
```

CMake target：`eda_geometry`（STATIC），`target_link_libraries(eda_geometry PUBLIC eda_core Clipper2 earcut)`。include 从仓库根：`#include "eda/geometry/boolean.h"`。

---

## 6. 与其他模块的关系（消费契约）

| 消费方 | 怎么用 geometry |
|--------|----------------|
| `eda/schematic` | 导线=Polyline；器件图形=Polygon/Polyline 集合；ERC 连通性=intersect |
| `eda/pcb` | 走线=Path；铺铜=Polygon（由 boolean+offset 生成）；板框=Polygon；器件 courtyard=offset |
| `eda/validation`（DRC） | 间距检查=index 邻域 + measure.distance；短路=boolean.intersect；铺铜连通=index |
| `eda/manufacturing` | Gerber/IPC-2581 输出=Polygon/Polyline 序列化；拼板=boolean.unite |

**关键约束**：业务层永远不直接调 Clipper2/earcut，只调 `eda::geometry::*`。这保证未来换底层实现不影响业务。

---

## 7. 关键算法选型与风险

| 算法 | 选型 | 风险/缓解 |
|------|------|----------|
| 布尔 | Clipper2（Vatti 扫描线） | 大型铺铜可能慢 → 分块 + 增量重建 |
| 偏移 | Clipper2 ClipperOffset | 相交处理 → 用 Clipper2 的清理选项 |
| 三角剖分 | earcut（耳切） | 不保证 Delaunay（铺铜够用）；若需更优网格再评估 |
| 空间索引 | 自研 R-tree（STR bulk-load） | 自研正确性风险 → 充分单测 + 与暴力查询交叉验证 |
| 圆弧离散化 | 等角采样 | 角度容差取 1°~5°（可配）；大圆弧用弦高容差控制 |

---

## 8. 测试策略（P3 plan 的 TDD 基础）

- **units**：mil↔mm↔nm 往返无损（边界值）
- **boolean**：经典用例——星+圆、同心环差、自交多边形、含孔并集；结果面积守恒
- **offset**：矩形膨胀、含孔内缩、锐角 miter 限幅
- **triangulate**：凹多边形、含孔、面积守恒（Σ三角形面积 == 多边形面积）
- **index(R-tree)**：插入/查询/删除一致性；邻域查询 vs 暴力 O(n²) 结果一致；万级图元查询时延目标
- **intersect**：线-线（平行/共线/相交）、线-弧、弧-弧
- **measure**：已知长度/面积反算
- **arc_tessellate**：弦高容差达标、端点闭合

性能基准（纳入 P22）：10k 图元 R-tree 邻域查询 < 目标时延；铺铜 boolean 时延目标。

---

## 9. P3 Plan 任务雏形（粗列，供 writing-plans 细化）

1. types.h + units.h + transform.h + snap.h（纯值类型，TDD 易）
2. arc_tessellate（弧→折线）
3. measure（长度/面积/距离）
4. intersect（线-线/线-弧/弧-弧）
5. boolean（Clipper2 包装，经典用例）
6. offset（ClipperOffset）
7. triangulate（earcut）
8. index（自研 R-tree，与暴力交叉验证）
9. 集成测试 + 性能基准骨架

---

## 10. 待确认

1. **D1 坐标基础**：内部用 int64/nm（推荐）还是 double/mm？影响全部图元类型签名。
2. **D5 空间索引**：自研 R-tree（推荐，无重依赖）还是引入 Boost.Geometry rtree（成熟省事但引入 Boost）？
3. **圆弧精度**：离散化默认弦高容差（建议 nm 级，可配）。
4. **范围**：geometry 只做 2D 吗？（推荐：是；3D 归 `eda/mechanical`。）
5. **是否需要曲线（贝塞尔样条）**？PCB 一般用圆弧，原理图用直线；暂不引入样条，YAGNI。

---

*审阅后：确认待确认项 → 即可进入 P3 `writing-plans` 产出可执行任务计划。*
