// eda/formats/kicad_pcb_parser.cpp
//
// P16 格式兼容 · KiCad .kicad_pcb S-expression 解析器实现（骨架）。
//
// 流水线：
//   text  → SexprReader（手写递归下降：tokenize + parse）
//         → SexprNode 树（list / atom 二态）
//         → KiCadPcbMapper（遍历 kicad_pcb 子节点 → 填 pcb::Board）
//
// S-expression 文法（简化）：
//   node    := '(' node* ')' | string | atom
//   string  := '"' (escape | char)* '"'
//   atom    := (非空白、非括号字符)+
//   escape  := '\' ('n' | 't' | 'r' | '"' | '\')
//
// KiCad 字段子集（MVP）：
//   (net <code:int> <name:str>)
//   (segment (start x y) (end x y) (width w) (layer "id") (net code [name]))
//   (via (at x y) (size d) (drill d) (layers "from" "to") (net code [name]))
//   (footprint "ref" (layer "id") (at x y [r]) (property ...) (pad ...))
//   (pad "num" type shape (at x y [r]) (size w h) (layers ...) (net code [name]))
//   (gr_line (start x y) (end x y) (layer "Edge.Cuts"))
//
// 坐标：内部统一 nm；KiCad 坐标为 mm，由 mm_to_nm 完成 int64 边界转换。
//
// 命名规范：namespace eda::formats、PascalCase 类型、snake_case 方法、
//           成员尾下划线 _、include 从仓库根。

#include "eda/formats/kicad_pcb_parser.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eda/geometry/units.h"
#include "eda/pcb/board.h"
#include "eda/pcb/pad.h"

namespace eda::formats {

// ============================================================================
// SexprNode —— S-expression 树节点（list / atom 二态）+ 只读查询工具。
// 头原子（head）= 列表第一个原子，KiCad 约定 (head arg1 arg2 ...) 形态。
// ============================================================================
namespace {

struct SexprNode {
    bool is_list = false;
    std::string atom;                          // is_list == false 时有效
    std::vector<SexprNode> children;           // is_list == true 时有效

    bool is_atom() const { return !is_list; }

    // 列表的头原子名（如 "segment"）；非列表或空列表返回空串。
    std::string head_name() const {
        if (!is_list || children.empty()) return {};
        const SexprNode& h = children[0];
        return h.is_atom() ? h.atom : std::string{};
    }

    // 在直接子节点中查找首个 head==name 的列表。
    const SexprNode* find_child(const std::string& name) const {
        if (!is_list) return nullptr;
        for (const auto& c : children) {
            if (c.head_name() == name) return &c;
        }
        return nullptr;
    }

    // 在直接子节点中收集所有 head==name 的列表。
    std::vector<const SexprNode*> find_all(const std::string& name) const {
        std::vector<const SexprNode*> out;
        if (!is_list) return out;
        for (const auto& c : children) {
            if (c.head_name() == name) out.push_back(&c);
        }
        return out;
    }
};

// ============================================================================
// SexprReader —— 手写递归下降 S-expression 解析器。
// 仅向前扫描，无回溯；O(n) 时间复杂度。
// ============================================================================
class SexprReader {
public:
    explicit SexprReader(const std::string& src) : src_(src), pos_(0) {}

    // 解析整个输入为单个顶层节点；成功返回 true。
    bool parse(SexprNode& root, std::string& err) {
        skip_ws();
        if (pos_ >= src_.size()) {
            err = "empty input";
            return false;
        }
        SexprNode node;
        if (!parse_node(node, err)) return false;
        root = std::move(node);
        return true;  // 允许末尾空白/多余字符
    }

private:
    const std::string& src_;
    std::size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parse_node(SexprNode& out, std::string& err) {
        skip_ws();
        if (pos_ >= src_.size()) {
            err = "unexpected EOF";
            return false;
        }
        const char c = src_[pos_];
        if (c == '(') return parse_list(out, err);
        if (c == '"') return parse_string(out, err);
        return parse_atom(out, err);
    }

    bool parse_list(SexprNode& out, std::string& err) {
        ++pos_;  // 消费 '('
        out.is_list = true;
        out.children.clear();
        while (true) {
            skip_ws();
            if (pos_ >= src_.size()) {
                err = "unterminated list";
                return false;
            }
            if (src_[pos_] == ')') {
                ++pos_;
                return true;
            }
            SexprNode child;
            if (!parse_node(child, err)) return false;
            out.children.push_back(std::move(child));
        }
    }

    bool parse_string(SexprNode& out, std::string& err) {
        ++pos_;  // 消费开篇 '"'
        std::string s;
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') {
                out.is_list = false;
                out.atom = std::move(s);
                return true;
            }
            if (c == '\\' && pos_ < src_.size()) {
                const char e = src_[pos_++];
                switch (e) {
                    case 'n':  s.push_back('\n'); break;
                    case 't':  s.push_back('\t'); break;
                    case 'r':  s.push_back('\r'); break;
                    case '"':  s.push_back('"');  break;
                    case '\\': s.push_back('\\'); break;
                    default:   s.push_back(e);    break;
                }
            } else {
                s.push_back(c);
            }
        }
        err = "unterminated string";
        return false;
    }

    bool parse_atom(SexprNode& out, std::string& err) {
        std::string s;
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                c == '(' || c == ')') {
                break;
            }
            s.push_back(c);
            ++pos_;
        }
        if (s.empty()) {
            err = "empty atom";
            return false;
        }
        out.is_list = false;
        out.atom = std::move(s);
        return true;
    }
};

// ============================================================================
// 字段读取辅助：从 (head v1 v2 ...) 形态节点中取 mm 值并转 nm / 取字符串等。
// 失败时写 warning 到 stats 并返回安全默认值（不抛异常）。
// ============================================================================

// 解析失败时返回 0.0；调用方按需记 warning。
double parse_double_or(const std::string& atom_text, double fallback) {
    try {
        return std::stod(atom_text);
    } catch (...) {
        return fallback;
    }
}

int parse_int_or(const std::string& atom_text, int fallback) {
    try {
        return std::stoi(atom_text);
    } catch (...) {
        return fallback;
    }
}

// 从 (at X Y [R]) / (start X Y) / (end X Y) 读取 mm 坐标 → nm Point。
geometry::Point read_point_nm(const SexprNode& node, KiCadPcbParseStats* stats) {
    geometry::Point p{};
    if (!node.is_list || node.children.size() < 3) {
        if (stats) stats->warnings.push_back("point node malformed");
        return p;
    }
    if (!node.children[1].is_atom() || !node.children[2].is_atom()) {
        if (stats) stats->warnings.push_back("point coords not atom");
        return p;
    }
    const double x = parse_double_or(node.children[1].atom, 0.0);
    const double y = parse_double_or(node.children[2].atom, 0.0);
    p.x = geometry::mm_to_nm(x);
    p.y = geometry::mm_to_nm(y);
    return p;
}

// 从 (width W) / (size D) / (drill D) 读取 mm 标量 → nm Coord。
geometry::Coord read_scalar_nm(const SexprNode& node, KiCadPcbParseStats* stats) {
    if (!node.is_list || node.children.size() < 2 || !node.children[1].is_atom()) {
        if (stats) stats->warnings.push_back("scalar node malformed");
        return 0;
    }
    return geometry::mm_to_nm(parse_double_or(node.children[1].atom, 0.0));
}

// 从 (layer "id") 读取层 ID 字符串。
std::string read_layer_id(const SexprNode& node) {
    if (!node.is_list || node.children.size() < 2 || !node.children[1].is_atom()) {
        return {};
    }
    return node.children[1].atom;
}

// 从 (net N ["name"]) 读取网络 code。
int read_net_code(const SexprNode& node) {
    if (!node.is_list || node.children.size() < 2 || !node.children[1].is_atom()) {
        return -1;
    }
    return parse_int_or(node.children[1].atom, -1);
}

// KiCad pad shape 字符串 → pcb::PadShape；未识别形状（roundrect/custom）退化为 RECT。
pcb::PadShape parse_kicad_pad_shape(const std::string& s) {
    if (s == "circle")  return pcb::PadShape::CIRCLE;
    if (s == "oval")    return pcb::PadShape::OVAL;
    if (s == "octagon") return pcb::PadShape::OCTAGON;
    return pcb::PadShape::RECT;  // rect / roundrect / custom → RECT
}

// ============================================================================
// Edge.Cuts gr_line 链式拼接：把一组线段贪心拼成闭合 Polygon.outer。
// 同一端点（精确 Coord 等值）相连；任一端可接，必要时翻转段方向。
// 拼不闭合时仍返回已拼出的点序列（开放折线）。
// ============================================================================
geometry::Polygon chain_outline_segments(
    std::vector<std::pair<geometry::Point, geometry::Point>> segs) {
    geometry::Polygon poly;
    if (segs.empty()) return poly;

    std::vector<char> used(segs.size(), 0);
    geometry::Point cur_start = segs[0].first;
    geometry::Point cur_end = segs[0].second;
    used[0] = 1;
    poly.outer.push_back(cur_start);
    poly.outer.push_back(cur_end);

    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (used[i]) continue;
            const auto& s = segs[i];
            if (s.first == cur_end) {
                cur_end = s.second;
                poly.outer.push_back(cur_end);
                used[i] = 1;
                progressed = true;
                break;
            }
            if (s.second == cur_end) {
                cur_end = s.first;
                poly.outer.push_back(cur_end);
                used[i] = 1;
                progressed = true;
                break;
            }
        }
    }

    // 若首尾重合（闭合多边形），移除最后一个重复点。
    if (poly.outer.size() > 1 && poly.outer.front() == poly.outer.back()) {
        poly.outer.pop_back();
    }
    return poly;
}

}  // namespace

// ============================================================================
// KiCadPcbParser 公共接口实现
// ============================================================================

KiCadPcbParser::KiCadPcbParser() = default;
KiCadPcbParser::~KiCadPcbParser() = default;

bool KiCadPcbParser::parse(const std::string& file_path, pcb::Board& out,
                           KiCadPcbParseStats* stats) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        if (stats) stats->error_message = "cannot open file: " + file_path;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return parse_string(oss.str(), out, stats);
}

bool KiCadPcbParser::parse_string(const std::string& text, pcb::Board& out,
                                  KiCadPcbParseStats* stats) {
    KiCadPcbParseStats local_stats;
    KiCadPcbParseStats& s = stats ? *stats : local_stats;
    s = KiCadPcbParseStats{};

    // ---- 1. 解析 S-expression ----
    SexprReader reader(text);
    SexprNode root;
    std::string err;
    if (!reader.parse(root, err)) {
        s.error_message = "S-expression parse error: " + err;
        return false;
    }
    if (root.head_name() != "kicad_pcb") {
        s.error_message = "top-level head is not 'kicad_pcb'";
        return false;
    }

    // ---- 2. net 声明 → Board.add_net（建立 code → uuid 字典）----
    // (net 0 "") 表示未连接，跳过；只导入具名网络。
    std::unordered_map<int, std::string> net_code_to_uuid;
    {
        const auto nets = root.find_all("net");
        s.nets_total = nets.size();
        for (const SexprNode* n : nets) {
            if (!n->is_list || n->children.size() < 3) {
                s.warnings.push_back("net entry too short, skipped");
                continue;
            }
            const int code = parse_int_or(n->children[1].atom, -1);
            std::string name;
            if (n->children[2].is_atom()) name = n->children[2].atom;
            if (name.empty()) continue;  // net 0 "" 等未命名网络跳过
            pcb::Net& net = out.add_net(name);
            net_code_to_uuid[code] = net.uuid();
            ++s.nets_imported;
        }
    }

    // 网络码 → UUID 查找（缺省返回空串，表示未连网络）。
    auto lookup_net_uuid = [&net_code_to_uuid](const SexprNode* net_node) -> std::string {
        if (!net_node) return {};
        const int code = read_net_code(*net_node);
        auto it = net_code_to_uuid.find(code);
        return it == net_code_to_uuid.end() ? std::string{} : it->second;
    };

    // ---- 3. segment → Track ----
    {
        const auto segs = root.find_all("segment");
        s.segments_total = segs.size();
        for (const SexprNode* seg : segs) {
            const SexprNode* start = seg->find_child("start");
            const SexprNode* end = seg->find_child("end");
            const SexprNode* width = seg->find_child("width");
            const SexprNode* layer = seg->find_child("layer");
            const SexprNode* net = seg->find_child("net");

            if (!start || !end || !width || !layer) {
                s.warnings.push_back("segment missing required field, skipped");
                continue;
            }

            const geometry::Point a = read_point_nm(*start, nullptr);
            const geometry::Point b = read_point_nm(*end, nullptr);
            const geometry::Coord w = read_scalar_nm(*width, nullptr);
            const std::string layer_id = read_layer_id(*layer);
            const std::string net_uuid = lookup_net_uuid(net);

            geometry::Path path;
            path.points = {a, b};
            path.width = w;
            out.add_track(std::move(path), layer_id, net_uuid);
            ++s.segments_imported;
        }
    }

    // ---- 4. via → Via ----
    {
        const auto vias = root.find_all("via");
        s.vias_total = vias.size();
        for (const SexprNode* v : vias) {
            const SexprNode* at = v->find_child("at");
            const SexprNode* size = v->find_child("size");
            const SexprNode* drill = v->find_child("drill");
            const SexprNode* layers = v->find_child("layers");
            const SexprNode* net = v->find_child("net");

            if (!at || !size || !drill || !layers) {
                s.warnings.push_back("via missing required field, skipped");
                continue;
            }

            const geometry::Point p = read_point_nm(*at, nullptr);
            const geometry::Coord pad_d = read_scalar_nm(*size, nullptr);
            const geometry::Coord drill_d = read_scalar_nm(*drill, nullptr);

            // (layers "F.Cu" "B.Cu") — children[1]=from, [2]=to
            std::string layer_from;
            std::string layer_to;
            if (layers->children.size() >= 2 && layers->children[1].is_atom()) {
                layer_from = layers->children[1].atom;
            }
            if (layers->children.size() >= 3 && layers->children[2].is_atom()) {
                layer_to = layers->children[2].atom;
            }
            const std::string net_uuid = lookup_net_uuid(net);

            out.add_via(p, drill_d, pad_d, std::move(layer_from),
                        std::move(layer_to), net_uuid);
            ++s.vias_imported;
        }
    }

    // ---- 5. footprint → FootprintInstance + 内部 pad → Pad ----
    {
        const auto footprints = root.find_all("footprint");
        s.footprints_total = footprints.size();
        for (const SexprNode* fp : footprints) {
            // (footprint "lib:name" (layer ...) (at X Y [R]) ... (pad ...))
            // children[1] 为引用（可能为字符串原子，KiCad v6+ 带引号→原子存值）。
            std::string fp_ref;
            if (fp->children.size() >= 2 && fp->children[1].is_atom()) {
                fp_ref = fp->children[1].atom;
            }

            // footprint 层与位号
            const SexprNode* fp_layer = fp->find_child("layer");
            std::string fp_layer_id = read_layer_id(*fp_layer);
            const bool on_bottom = (fp_layer_id == "B.Cu");

            // footprint (at X Y [R])：单位 mm，旋转角度（度）
            const SexprNode* fp_at = fp->find_child("at");
            double fp_x = 0.0, fp_y = 0.0, fp_rot_deg = 0.0;
            if (fp_at && fp_at->children.size() >= 3 &&
                fp_at->children[1].is_atom() && fp_at->children[2].is_atom()) {
                fp_x = parse_double_or(fp_at->children[1].atom, 0.0);
                fp_y = parse_double_or(fp_at->children[2].atom, 0.0);
                if (fp_at->children.size() >= 4 && fp_at->children[3].is_atom()) {
                    fp_rot_deg = parse_double_or(fp_at->children[3].atom, 0.0);
                }
            }
            const double rad = fp_rot_deg * 3.14159265358979323846 / 180.0;
            const double cosr = std::cos(rad);
            const double sinr = std::sin(rad);

            // property "Reference" "R1"
            std::string refdes;
            for (const SexprNode* prop : fp->find_all("property")) {
                if (prop->children.size() >= 3 &&
                    prop->children[1].is_atom() && prop->children[1].atom == "Reference" &&
                    prop->children[2].is_atom()) {
                    refdes = prop->children[2].atom;
                    break;
                }
            }

            // pads
            const auto pads = fp->find_all("pad");
            s.pads_total += pads.size();
            for (const SexprNode* p : pads) {
                // (pad "num" type shape (at ...) (size W H) (layers ...) (net ...))
                if (p->children.size() < 4) {
                    s.warnings.push_back("pad entry too short, skipped");
                    continue;
                }
                std::string number;
                if (p->children[1].is_atom()) number = p->children[1].atom;
                std::string shape_str = "rect";
                if (p->children[3].is_atom()) shape_str = p->children[3].atom;
                const pcb::PadShape shape = parse_kicad_pad_shape(shape_str);

                const SexprNode* p_at = p->find_child("at");
                const SexprNode* p_size = p->find_child("size");
                const SexprNode* p_layers = p->find_child("layers");
                const SexprNode* p_net = p->find_child("net");

                // pad 局部坐标（相对 footprint 原点，mm）
                double pad_x = 0.0, pad_y = 0.0;
                if (p_at && p_at->children.size() >= 3 &&
                    p_at->children[1].is_atom() && p_at->children[2].is_atom()) {
                    pad_x = parse_double_or(p_at->children[1].atom, 0.0);
                    pad_y = parse_double_or(p_at->children[2].atom, 0.0);
                }
                // pad 尺寸（mm）
                geometry::Coord w = 0;
                geometry::Coord h = 0;
                if (p_size && p_size->children.size() >= 3 &&
                    p_size->children[1].is_atom() && p_size->children[2].is_atom()) {
                    w = geometry::mm_to_nm(parse_double_or(p_size->children[1].atom, 0.0));
                    h = geometry::mm_to_nm(parse_double_or(p_size->children[2].atom, 0.0));
                }
                if (shape == pcb::PadShape::CIRCLE) h = w;  // 圆形以 width 为直径

                // pad layer：取 (layers ...) 首个；缺省回落到 footprint 层。
                std::string layer_id;
                if (p_layers && p_layers->children.size() >= 2 &&
                    p_layers->children[1].is_atom()) {
                    layer_id = p_layers->children[1].atom;
                } else {
                    layer_id = fp_layer_id;
                }

                // pad 局部 → 世界：先绕 footprint 旋转，再平移
                const double wx = fp_x + (cosr * pad_x - sinr * pad_y);
                const double wy = fp_y + (sinr * pad_x + cosr * pad_y);

                const std::string net_uuid = lookup_net_uuid(p_net);

                pcb::Pad& pad = out.add_pad(
                    geometry::Point{geometry::mm_to_nm(wx), geometry::mm_to_nm(wy)},
                    w, h, shape, layer_id, number);
                pad.set_net_uuid(net_uuid);
                ++s.pads_imported;
            }

            // 登记器件实例（占位 footprint_ref + 位置 + 朝向 + 镜像 + 位号）
            pcb::FootprintInstance fp_inst;
            fp_inst.footprint_ref = fp_ref;
            fp_inst.position = geometry::Point{geometry::mm_to_nm(fp_x),
                                                geometry::mm_to_nm(fp_y)};
            fp_inst.rotation = rad;
            fp_inst.flipped = on_bottom;
            fp_inst.refdes = refdes;
            out.add_footprint(std::move(fp_inst));
            ++s.footprints_imported;
        }
    }

    // ---- 6. gr_line on Edge.Cuts → 板框 outline ----
    {
        std::vector<std::pair<geometry::Point, geometry::Point>> outline_segs;
        for (const SexprNode* g : root.find_all("gr_line")) {
            const SexprNode* layer = g->find_child("layer");
            const std::string layer_id = layer ? read_layer_id(*layer) : std::string{};
            if (layer_id != "Edge.Cuts") continue;  // 仅消费板框层线段

            const SexprNode* start = g->find_child("start");
            const SexprNode* end = g->find_child("end");
            if (!start || !end) continue;
            outline_segs.emplace_back(read_point_nm(*start, nullptr),
                                      read_point_nm(*end, nullptr));
        }
        s.outline_segments_total = outline_segs.size();
        s.outline_segments_imported = outline_segs.size();
        if (!outline_segs.empty()) {
            geometry::Polygon outline = chain_outline_segments(std::move(outline_segs));
            if (outline.outer.size() >= 3) {
                out.set_outline(std::move(outline));
            }
        }
    }

    // ---- 7. 识别但不导入的图元种类（列入 unsupported_kinds）----
    {
        static const char* kKnownUnsupported[] = {
            "gr_arc", "gr_circle", "gr_curve", "gr_poly", "gr_text",
            "dimension", "target", "zone", "rule", "filled_polygon"
        };
        for (const char* k : kKnownUnsupported) {
            const auto found = root.find_all(k);
            if (!found.empty()) {
                s.unsupported_kinds.push_back(std::string(k) + " x" +
                                              std::to_string(found.size()));
            }
        }
    }

    // 网络包围盒刷新（pads/vias/tracks 已落位）
    out.recompute_net_boxes();
    return true;
}

}  // namespace eda::formats
