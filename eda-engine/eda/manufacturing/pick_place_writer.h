#pragma once

// eda/manufacturing/pick_place_writer.h
//
// P13 制造输出 · Pick & Place 贴片坐标 CSV 写入器（骨架）。
// 13-制造输出/03-PickAndPlace坐标。
//
// 输入：eda::pcb::FootprintInstance 列表（Board.footprints()）。
// 输出：CSV 文本（std::string），直供 SMT 贴片机程序。
//
// CSV 列（按 README 约定）：
//   Designator, Footprint, X(mm), Y(mm), Rotation, Layer, Value
//
//   - Designator : FootprintInstance.refdes（位号 R1/C2/U3；空时占位 "?"）
//   - Footprint  : footprint_ref（lib URI 或短名）
//   - X / Y(mm)  : position 的 nm→mm
//   - Rotation   : 弧度→度（0/90/180/270 标准步进，骨架输出原值，不做量化）
//   - Layer      : "Top" / "Bottom"（flipped ? Bottom : Top）
//   - Value      : 当前 FootprintInstance 无 Value 字段（占位空串，待元件库接入）
//
// 暂不覆盖（后续 Task）：
//   - 原点配置（板框左下 / 中心 / fiducial）
//   - 底层 X 镜像（绕板框 Y 轴）
//   - 旋转角量化到 90° 步进
//   - 装配变体（DNF 排除）
//   - 厂商格式预设（富士/雅马哈/松下/Europlacer/Mydata）
//   - feeder slot / MPN
//
// 命名规范：namespace eda::mfg、PascalCase 类型、snake_case 方法、成员尾下划线 _、
//           include 从仓库根。

#include <string>
#include <vector>

#include "eda/geometry/types.h"

namespace eda::pcb {
class Board;
struct FootprintInstance;
}  // namespace eda::pcb

namespace eda::mfg {

// ============================================================================
// PickPlaceWriter —— 把 FootprintInstance 列表写出为贴片坐标 CSV。
// ============================================================================
class PickPlaceWriter {
public:
    PickPlaceWriter();
    ~PickPlaceWriter();

    // ---- 配置 ----
    // 小数位（X/Y mm 输出精度，默认 4 位）。
    void set_coord_decimals(int d) { coord_decimals_ = d; }
    int coord_decimals() const { return coord_decimals_; }

    // 旋转角输出小数位（默认 1 位，0/90/180/270 标准值无误差）。
    void set_rotation_decimals(int d) { rotation_decimals_ = d; }
    int rotation_decimals() const { return rotation_decimals_; }

    // ---- 主接口 ----
    // 写出 Board 全部 FootprintInstance。
    std::string write(const pcb::Board& board) const;

    // 写出指定 FootprintInstance 列表（可子集 / 跨板）。
    std::string write(const std::vector<pcb::FootprintInstance>& fps) const;

    // ---- 工具 ----
    // 把 mm 按当前精度格式化（如 1.5 → "1.5000"）。
    std::string format_mm(double mm) const;

    // 弧度 → 度（FootprintInstance.rotation 是弧度）。
    static double radians_to_degrees(double rad);

private:
    int coord_decimals_ = 4;
    int rotation_decimals_ = 1;
};

}  // namespace eda::mfg
