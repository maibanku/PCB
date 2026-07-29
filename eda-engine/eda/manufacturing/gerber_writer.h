#pragma once

// eda/manufacturing/gerber_writer.h
//
// P13 制造输出 · Gerber RS-274X 写入器（骨架）。
// 13-制造输出/01-Gerber输出。
//
// 输入：eda::pcb::Board + 目标 layer_id（如 "F.Cu" / "B.Cu" / "F.Mask"）。
// 输出：符合 RS-274X 的 Gerber 文本（std::string）。
//
// 骨架覆盖范围：
//   - 单位 mm，精度 3:4（默认，可配置）；坐标格式 X{mm*1e4}Y{mm*1e4}，前导零省略（L）。
//   - 光圈（Aperture / D-code）自动分配：
//       Track  → Circle, 直径 = 走线宽度
//       Pad    → Circle（CIRCLE 形状，直径 = width）或 Rect（RECT 形状，X×Y）
//                 OVAL/OCTAGON 退化到 Rect（RS-274X 基础形状不含椭圆/八边形，简化）
//       Via    → Circle, 直径 = pad_diameter
//   - 命令：
//       D01 = draw（线段绘制，配合 D02 move-to）
//       D02 = move with light off
//       D03 = flash（焊盘/过孔瞬间曝光）
//   - 板框 outline：用默认细光圈（Circle 0.10mm）走闭合多边形。
//   - 极性：正片（%LPD*%）。
//
// 暂不覆盖（后续 Task）：
//   - Region 填充（G36/G37，用于铺铜负片）
//   - 旋转/镜像光圈（AM 宏）
//   - 自定义命名模板（gerber_naming.template）
//   - 多边形/AM 自定义光圈
//   - inch 单位（仅 mm）
//
// 命名规范：namespace eda::mfg、PascalCase 类型、snake_case 方法、成员尾下划线 _、
//           include 从仓库根。

#include <cstdint>
#include <map>
#include <string>

#include "eda/geometry/types.h"

namespace eda::pcb {
class Board;
}  // namespace eda::pcb

namespace eda::mfg {

// ============================================================================
// 光圈形状（RS-274X 内置基础形状）。
// ============================================================================
enum class GerberApertureShape {
    CIRCLE,  // %ADDnnC,dia*%
    RECT,    // %ADDnnR,XxY*%
};

// ============================================================================
// GerberAperture —— D-code 描述（编号 + 形状 + 尺寸 mm）。
// 同形状+同尺寸的光圈被合并为同一 D-code（最小化 D-code 数量）。
// ============================================================================
struct GerberAperture {
    int d_code = 10;                              // D10..D999（10 起步，<10 保留给命令）
    GerberApertureShape shape = GerberApertureShape::CIRCLE;
    double width_mm = 0.0;                        // CIRCLE: 直径；RECT: X 尺寸
    double height_mm = 0.0;                       // CIRCLE: 0；RECT: Y 尺寸

    bool operator==(const GerberAperture& o) const {
        return shape == o.shape &&
               width_mm == o.width_mm &&
               height_mm == o.height_mm;
    }
};

// ============================================================================
// GerberWriter —— 将 pcb::Board 单层图元写出为 RS-274X Gerber 文本。
// ============================================================================
class GerberWriter {
public:
    GerberWriter();
    ~GerberWriter();

    // ---- 配置 ----
    // 单位：true=mm（默认），false=inch（骨架暂不实现 inch，仅保留接口）。
    void set_unit_mm(bool mm) { unit_mm_ = mm; }
    bool unit_mm() const { return unit_mm_; }

    // 精度：integer_digits:decimal_digits（默认 3:4，即 mm*1e4）。
    void set_precision(int integer_digits, int decimal_digits);
    int integer_digits() const { return integer_digits_; }
    int decimal_digits() const { return decimal_digits_; }

    // ---- 主接口 ----
    // 写出指定铜层/非铜层（按 layer_id 过滤 Board 内的 Track/Pad/Via）。
    std::string write_layer(const pcb::Board& board, const std::string& layer_id) const;

    // 写出板框 outline（独立 Gerber 文件，用细光圈走闭合多边形）。
    // 若板框为空，返回仅含文件头尾的空 Gerber。
    std::string write_outline(const pcb::Board& board) const;

    // 写出所有层：遍历 Board.stackup() 各 layer_id 一份 Gerber，
    // 并追加一个 "Outline" 键存放板框。返回 map 便于工厂按文件名落盘。
    std::map<std::string, std::string> write_all(const pcb::Board& board) const;

    // ---- 工具（暴露给测试 / 其他写入器复用）----
    // 把 mm 坐标按当前精度转为 Gerber 整数字符串（前导零省略）。
    std::string format_coord(double mm) const;

    // 把光圈格式化为 AD 行（不带前后分隔符），例如：
    //   Circle 0.2mm  → "ADD10C,0.200"
    //   Rect 0.5×0.5  → "ADD11R,0.500X0.500"
    static std::string format_aperture(const GerberAperture& ap);

private:
    bool unit_mm_ = true;
    int integer_digits_ = 3;
    int decimal_digits_ = 4;

    // 10^decimal_digits_，缓存避免反复 pow。
    int64_t scale_factor() const;
};

}  // namespace eda::mfg
