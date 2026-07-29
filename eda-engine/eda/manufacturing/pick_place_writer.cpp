// eda/manufacturing/pick_place_writer.cpp
//
// P13 制造输出 · Pick & Place 贴片坐标 CSV 写入器实现。

#include "eda/manufacturing/pick_place_writer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "eda/geometry/units.h"   // nm_to_mm
#include "eda/pcb/board.h"        // FootprintInstance 定义于此

namespace eda::mfg {

namespace {

// 把 double 按指定小数位格式化（默认四舍五入）。
std::string fmt_fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

// CSV 字段转义：含 , " \n 时整体加双引号，内部双引号翻倍。
// footprint_ref 偶含特殊字符（如 URI）但通常无逗号；refdes/Value 同理。
std::string csv_escape(const std::string& s) {
    const bool need_quote =
        s.find(',') != std::string::npos ||
        s.find('"') != std::string::npos ||
        s.find('\n') != std::string::npos ||
        s.find('\r') != std::string::npos;
    if (!need_quote) return s;
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

constexpr double kPi = 3.14159265358979323846;

}  // namespace

// ============================================================================
// PickPlaceWriter
// ============================================================================

PickPlaceWriter::PickPlaceWriter() = default;
PickPlaceWriter::~PickPlaceWriter() = default;

std::string PickPlaceWriter::format_mm(double mm) const {
    return fmt_fixed(mm, coord_decimals_);
}

double PickPlaceWriter::radians_to_degrees(double rad) {
    return rad * 180.0 / kPi;
}

// ----------------------------------------------------------------------------
// write(fps)
// ----------------------------------------------------------------------------

std::string PickPlaceWriter::write(
    const std::vector<pcb::FootprintInstance>& fps) const {
    std::ostringstream out;

    // 表头（对齐 README 列定义）
    out << "Designator,Footprint,X(mm),Y(mm),Rotation,Layer,Value\n";

    for (const auto& fp : fps) {
        const std::string designator = fp.refdes.empty() ? std::string("?") : fp.refdes;
        const std::string layer = fp.flipped ? "Bottom" : "Top";
        const double x_mm = geometry::nm_to_mm(fp.position.x);
        const double y_mm = geometry::nm_to_mm(fp.position.y);
        const double rot_deg = radians_to_degrees(fp.rotation);

        out << csv_escape(designator) << ','
            << csv_escape(fp.footprint_ref) << ','
            << format_mm(x_mm) << ','
            << format_mm(y_mm) << ','
            << fmt_fixed(rot_deg, rotation_decimals_) << ','
            << layer << ','
            << "" /* Value 字段空，待元件库接入 */ << '\n';
    }
    return out.str();
}

// ----------------------------------------------------------------------------
// write(board)
// ----------------------------------------------------------------------------

std::string PickPlaceWriter::write(const pcb::Board& board) const {
    return write(board.footprints());
}

}  // namespace eda::mfg
