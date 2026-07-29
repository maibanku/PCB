// eda/manufacturing/gerber_writer.cpp
//
// P13 制造输出 · Gerber RS-274X 写入器实现。

#include "eda/manufacturing/gerber_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "eda/geometry/units.h"      // nm_to_mm
#include "eda/pcb/board.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"

namespace eda::mfg {

namespace {

// 4 舍 5 入到整数（避免银行家舍入：std::round 是半数向上）。
int64_t round_to_int64(double v) {
    return static_cast<int64_t>(std::round(v));
}

// 把 double 按 %.Nf 格式化（N = 小数位），返回字符串（无尾随零省略）。
// 例：fmt_fixed(0.20, 3) → "0.200"。
std::string fmt_fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

}  // namespace

// ============================================================================
// GerberWriter
// ============================================================================

GerberWriter::GerberWriter() = default;
GerberWriter::~GerberWriter() = default;

void GerberWriter::set_precision(int integer_digits, int decimal_digits) {
    integer_digits_ = integer_digits;
    decimal_digits_ = decimal_digits;
}

int64_t GerberWriter::scale_factor() const {
    int64_t s = 1;
    for (int i = 0; i < decimal_digits_; ++i) s *= 10;
    return s;
}

std::string GerberWriter::format_coord(double mm) const {
    const int64_t scaled = round_to_int64(mm * static_cast<double>(scale_factor()));
    return std::to_string(scaled);
}

std::string GerberWriter::format_aperture(const GerberAperture& ap) {
    char buf[96];
    if (ap.shape == GerberApertureShape::CIRCLE) {
        // %ADDnnC,dia*%  → ADDnnC,dia（不含 % 和 *）
        std::snprintf(buf, sizeof(buf), "ADD%dC,%s",
                      ap.d_code, fmt_fixed(ap.width_mm, 3).c_str());
    } else {
        // %ADDnnR,XxY*%  → ADDnnR,XxY
        std::snprintf(buf, sizeof(buf), "ADD%dR,%sX%s",
                      ap.d_code,
                      fmt_fixed(ap.width_mm, 3).c_str(),
                      fmt_fixed(ap.height_mm, 3).c_str());
    }
    return std::string(buf);
}

// ----------------------------------------------------------------------------
// 内部辅助：为单层收集光圈字典 + 写命令体。
// ----------------------------------------------------------------------------

namespace {

// 光圈字典键（按形状 + 尺寸去重），值 = D-code。
// 使用有序 map 保证输出稳定（按插入顺序，便于测试可重现）。
struct ApertureKey {
    GerberApertureShape shape;
    double width_mm;
    double height_mm;
    bool operator<(const ApertureKey& o) const {
        if (shape != o.shape) return shape < o.shape;
        if (width_mm != o.width_mm) return width_mm < o.width_mm;
        return height_mm < o.height_mm;
    }
};

class ApertureDict {
public:
    // 查/插：相同 key 复用 D-code；首次插入分配新 D-code（10 起步）。
    int intern(GerberApertureShape shape, double width_mm, double height_mm) {
        ApertureKey k{shape, width_mm, height_mm};
        auto it = map_.find(k);
        if (it != map_.end()) return it->second;
        const int d = next_d_code_++;
        map_[k] = d;
        order_.push_back({d, shape, width_mm, height_mm});
        return d;
    }

    const std::vector<GerberAperture>& ordered() const { return order_; }

private:
    std::map<ApertureKey, int> map_;
    std::vector<GerberAperture> order_;
    int next_d_code_ = 10;
};

// Pad → 光圈描述：CIRCLE 用 width 作直径；RECT/OVAL/OCTAGON 退化为 Rect(X×Y)。
GerberApertureShape pad_aperture_shape(pcb::PadShape s) {
    return s == pcb::PadShape::CIRCLE ? GerberApertureShape::CIRCLE
                                      : GerberApertureShape::RECT;
}

// 判断 via 是否"穿越/触及"该层（出现在该层的焊盘上）。
bool via_touches_layer(const pcb::Via& v, const std::string& layer_id) {
    return v.layer_from() == layer_id || v.layer_to() == layer_id;
}

}  // namespace

// ----------------------------------------------------------------------------
// write_layer
// ----------------------------------------------------------------------------

std::string GerberWriter::write_layer(const pcb::Board& board,
                                      const std::string& layer_id) const {
    const int dec = std::max(0, decimal_digits_);
    const int prec_int = std::max(1, integer_digits_);

    // ---- Pass 1：扫描本层所有图元，建立光圈字典 ----
    ApertureDict dict;

    // 结构体记录每个图元对应的 D-code + 关键坐标，避免二次查找。
    struct TrackRec {
        int d_code;
        const pcb::Track* track;
    };
    struct FlashRec {
        int d_code;
        geometry::Point pos;
    };

    std::vector<TrackRec> track_recs;
    std::vector<FlashRec> flash_recs;

    // Tracks
    for (const auto& t : board.tracks()) {
        if (!t || t->layer_id() != layer_id) continue;
        const double w_mm = geometry::nm_to_mm(t->width());
        const int d = dict.intern(GerberApertureShape::CIRCLE, w_mm, 0.0);
        track_recs.push_back({d, t.get()});
    }

    // Pads（自由焊盘）
    for (const auto& p : board.pads()) {
        if (!p || p->layer_id() != layer_id) continue;
        const double w_mm = geometry::nm_to_mm(p->width());
        const double h_mm = geometry::nm_to_mm(p->height());
        const auto sh = pad_aperture_shape(p->shape());
        const int d = dict.intern(sh, w_mm, h_mm);
        flash_recs.push_back({d, p->position()});
    }

    // Vias（触及该层即出现焊盘）
    for (const auto& v : board.vias()) {
        if (!v || !via_touches_layer(*v, layer_id)) continue;
        const double dia_mm = geometry::nm_to_mm(v->pad_diameter());
        const int d = dict.intern(GerberApertureShape::CIRCLE, dia_mm, 0.0);
        flash_recs.push_back({d, v->position()});
    }

    // ---- Pass 2：组装 Gerber 文本 ----
    std::ostringstream out;

    // 文件头
    out << "G04 eda-engine P13 GerberWriter - layer=" << layer_id << "*\n";
    // FS: 格式说明（前导零省略 L、绝对坐标 A、整数位:小数位）
    out << "%FSLAX" << prec_int << dec << "Y" << prec_int << dec << "*%\n";
    out << "%MOMM*%\n";   // 骨架固定 mm（unit_mm_ 暂只支持 mm）
    out << "%LPD*%\n";    // 正片（Dark）

    // 光圈定义（AD）
    for (const auto& ap : dict.ordered()) {
        out << "%" << format_aperture(ap) << "*%\n";
    }

    // 走线（D01 draw）
    for (const auto& rec : track_recs) {
        const auto& pts = rec.track->path().points;
        if (pts.size() < 2) continue;
        out << "D" << rec.d_code << "*\n";  // 选择光圈
        // 第一段：D02 move 到起点（灭灯）
        out << "X" << format_coord(geometry::nm_to_mm(pts[0].x))
            << "Y" << format_coord(geometry::nm_to_mm(pts[0].y))
            << "D02*\n";
        // 后续点：D01 draw（亮灯）
        for (std::size_t i = 1; i < pts.size(); ++i) {
            out << "X" << format_coord(geometry::nm_to_mm(pts[i].x))
                << "Y" << format_coord(geometry::nm_to_mm(pts[i].y))
                << "D01*\n";
        }
    }

    // 焊盘/过孔（D03 flash）
    for (const auto& rec : flash_recs) {
        out << "D" << rec.d_code << "*\n";  // 选择光圈
        out << "X" << format_coord(geometry::nm_to_mm(rec.pos.x))
            << "Y" << format_coord(geometry::nm_to_mm(rec.pos.y))
            << "D03*\n";
    }

    // 结束
    out << "M02*\n";
    return out.str();
}

// ----------------------------------------------------------------------------
// write_outline
// ----------------------------------------------------------------------------

std::string GerberWriter::write_outline(const pcb::Board& board) const {
    const int dec = std::max(0, decimal_digits_);
    const int prec_int = std::max(1, integer_digits_);

    std::ostringstream out;
    out << "G04 eda-engine P13 GerberWriter - Board Outline*\n";
    out << "%FSLAX" << prec_int << dec << "Y" << prec_int << dec << "*%\n";
    out << "%MOMM*%\n";
    out << "%LPD*%\n";

    // 板框用细光圈（Circle 0.10mm，D10），保证可见但不抢眼
    constexpr double kOutlineWidth = 0.10;
    const GerberAperture outline_ap{10, GerberApertureShape::CIRCLE, kOutlineWidth, 0.0};
    out << "%" << format_aperture(outline_ap) << "*%\n";

    const auto& outer = board.outline().outer;
    if (outer.size() >= 2) {
        out << "D10*\n";
        out << "X" << format_coord(geometry::nm_to_mm(outer[0].x))
            << "Y" << format_coord(geometry::nm_to_mm(outer[0].y))
            << "D02*\n";
        for (std::size_t i = 1; i < outer.size(); ++i) {
            out << "X" << format_coord(geometry::nm_to_mm(outer[i].x))
                << "Y" << format_coord(geometry::nm_to_mm(outer[i].y))
                << "D01*\n";
        }
        // 闭合：末点 → 首点
        if (outer.size() >= 3) {
            out << "X" << format_coord(geometry::nm_to_mm(outer[0].x))
                << "Y" << format_coord(geometry::nm_to_mm(outer[0].y))
                << "D01*\n";
        }
    }

    out << "M02*\n";
    return out.str();
}

// ----------------------------------------------------------------------------
// write_all
// ----------------------------------------------------------------------------

std::map<std::string, std::string> GerberWriter::write_all(const pcb::Board& board) const {
    std::map<std::string, std::string> result;
    for (std::size_t i = 0; i < board.layer_count(); ++i) {
        const auto& layer_id = board.layer(i).layer_id();
        result.emplace(layer_id, write_layer(board, layer_id));
    }
    result.emplace("Outline", write_outline(board));
    return result;
}

}  // namespace eda::mfg
