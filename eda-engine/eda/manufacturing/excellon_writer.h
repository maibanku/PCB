#pragma once

// eda/manufacturing/excellon_writer.h
//
// P13 制造输出 · Excellon 钻孔文件写入器（骨架）。
// 13-制造输出/02-Excellon钻孔。
//
// 输入：eda::pcb::Board 的孔数据（当前数据模型仅 Via 提供 drill_diameter；
//       Pad 暂无 drill 字段，故 NPTH/通孔 Pad 钻孔待 P6/P8 扩展后接入）。
// 输出：Excellon 格式钻孔文本（std::string），plated/non-plated 分文件。
//
// 骨架覆盖范围：
//   - 单位 mm，精度 3:3（默认，可配置）；坐标 X{mm*1e3}Y{mm*1e3}，前导零省略（LZ）。
//   - M48 头：声明 METRIC,LZ + 刀具列表（T1C0.30 / T2C0.40 ...）。
//   - 刀具按 drill_diameter 分组，相同直径共享一把刀；D-code 编号 T1..Tn。
//   - plated（PTH，沉铜——过孔）与 non-plated（NPTH，定位孔/螺丝孔）分文件输出。
//   - 钻孔命令：选刀 Tnn → X/Y 坐标行（每行一钻）。
//   - 结束 M30。
//
// 暂不覆盖（后续 Task）：
//   - 槽孔（G85）
//   - 盲/埋孔分文件 + Z 起止层标注
//   - feed/speed 参数（M48 扩展字段）
//   - inch 单位
//   - 钻孔图 PDF
//   - Pad.drill 字段接入（待数据模型扩展）
//
// 命名规范：namespace eda::mfg、PascalCase 类型、snake_case 方法、成员尾下划线 _、
//           include 从仓库根。

#include <cstdint>
#include <string>

#include "eda/geometry/types.h"

namespace eda::pcb {
class Board;
}  // namespace eda::pcb

namespace eda::mfg {

// ============================================================================
// ExcellonOutput —— plated / non-plated 两份钻孔文本。
// 工厂按 PTH / NPTH 分次钻孔（NPTH 在沉铜工艺后钻），故必须分文件。
// ============================================================================
struct ExcellonOutput {
    std::string pth;    // Plated Through Hole（沉铜孔，过孔）
    std::string npth;   // Non-Plated Through Hole（定位孔、螺丝孔）
};

// ============================================================================
// ExcellonWriter —— 把 pcb::Board 的孔数据写出为 Excellon 钻孔文件。
// ============================================================================
class ExcellonWriter {
public:
    ExcellonWriter();
    ~ExcellonWriter();

    // ---- 配置 ----
    void set_unit_mm(bool mm) { unit_mm_ = mm; }
    bool unit_mm() const { return unit_mm_; }

    // 精度：integer_digits:decimal_digits（默认 3:3，即 mm*1e3）。
    void set_precision(int integer_digits, int decimal_digits);
    int integer_digits() const { return integer_digits_; }
    int decimal_digits() const { return decimal_digits_; }

    // ---- 主接口 ----
    // 一次写出 PTH + NPTH 两份（PTH = 所有 Via；NPTH = 当前空，待 NPTH 孔模型接入）。
    ExcellonOutput write(const pcb::Board& board) const;

    // 单独写出 PTH（过孔）。
    std::string write_pth(const pcb::Board& board) const;

    // 单独写出 NPTH（定位孔，骨架阶段为空文件——仍含完整头尾）。
    std::string write_npth(const pcb::Board& board) const;

    // ---- 工具 ----
    // 把 mm 坐标按当前精度转为 Excellon 整数字符串（前导零省略）。
    std::string format_coord(double mm) const;

private:
    bool unit_mm_ = true;
    int integer_digits_ = 3;
    int decimal_digits_ = 3;

    int64_t scale_factor() const;
};

}  // namespace eda::mfg
