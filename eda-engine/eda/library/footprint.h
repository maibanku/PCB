#pragma once

// eda/library/footprint.h
//
// P6 元件库 · 封装数据模型（Task：Symbol + Footprint + Device 一体化绑定）。
//
// 对应 docs/06-元件库/02-封装库编辑器。
// 对齐 REQ-007 EDA Object Model：Footprint : Resource；几何/焊盘坐标统一使用
// P3 geometry 的 Point/Coord/Polygon（int64 nm）。
//
// 数据模型分层：
//   Footprint (Resource)
//     ├── pads_          : Pad[]（SMD/通孔/NPTH，number/position/size/shape/layer/drill）
//     ├── silkscreen_    : GraphicsElement[]（线/弧/多边形/文本，复用 symbol.h GraphicsElement）
//     ├── courtyard_     : geometry::Polygon（器件占用区，DFM 间距检查基石）
//     ├── ipc7351_       : IPC-7351 字段（密度等级 L/M/N、参考高度等）
//     └── model_3d_      : Model3DRef（STEP 模型引用 + 偏移/旋转/缩放）
//
// 序列化：手写最小 TOML 子集，输出 .fp.toml。
//
// 依赖：core/object/{ref,resource}.h、eda/geometry/types.h、eda/library/symbol.h
//       （复用 GraphicsElement / GraphicsKind）、eda/project/project.h（generate_uuid）。

#include <cstdint>
#include <string>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "eda/geometry/types.h"
#include "eda/library/symbol.h"  // GraphicsElement / GraphicsKind 复用

namespace eda {

// ============================================================================
// 枚举：焊盘类型（覆盖 SMD / 通孔 PTH / 非镀通孔 NPTH）
// ============================================================================
enum class PadType {
    SMD,           // 表贴焊盘
    THROUGH_HOLE,  // 通孔（镀覆，PTH）
    NON_PLATED,    // 非镀通孔（NPTH，安装孔/定位孔）
};

std::string_view to_string_view(PadType t);
PadType parse_pad_type(std::string_view s);

// ============================================================================
// 枚举：焊盘形状
// ============================================================================
enum class PadShape {
    CIRCLE,
    SQUARE,
    RECTANGLE,
    OVAL,
    ROUNDED_RECT,
    POLYGON,
};

std::string_view to_string_view(PadShape s);
PadShape parse_pad_shape(std::string_view s);

// ============================================================================
// 枚举：焊盘层分配
// ============================================================================
enum class PadLayer {
    TOP,     // SMD 顶层
    BOTTOM,  // SMD 底层
    ALL,     // 通孔贯穿所有信号层
};

std::string_view to_string_view(PadLayer l);
PadLayer parse_pad_layer(std::string_view s);

// ============================================================================
// Pad —— 焊盘（值类型）
// ============================================================================
class Pad {
public:
    Pad() = default;
    Pad(std::string number, geometry::Point position,
        PadType type = PadType::SMD,
        PadShape shape = PadShape::RECTANGLE,
        geometry::Coord width_nm = 0,
        geometry::Coord height_nm = 0,
        PadLayer layer = PadLayer::TOP);

    const std::string& number() const { return number_; }
    geometry::Point position() const { return position_; }
    PadType type() const { return type_; }
    PadShape shape() const { return shape_; }
    geometry::Coord width_nm() const { return width_nm_; }
    geometry::Coord height_nm() const { return height_nm_; }
    PadLayer layer() const { return layer_; }
    geometry::Coord drill_diameter_nm() const { return drill_diameter_nm_; }
    double paste_coverage() const { return paste_coverage_; }   // 钢网覆盖率 0~1
    const geometry::Polygon& custom_shape() const { return custom_shape_; }

    void set_number(std::string n) { number_ = std::move(n); }
    void set_position(geometry::Point p) { position_ = p; }
    void set_type(PadType t) { type_ = t; }
    void set_shape(PadShape s) { shape_ = s; }
    void set_width_nm(geometry::Coord w) { width_nm_ = w; }
    void set_height_nm(geometry::Coord h) { height_nm_ = h; }
    void set_layer(PadLayer l) { layer_ = l; }
    void set_drill_diameter_nm(geometry::Coord d) { drill_diameter_nm_ = d; }
    void set_paste_coverage(double p) { paste_coverage_ = p; }
    void set_custom_shape(geometry::Polygon s) { custom_shape_ = std::move(s); }

    bool operator==(const Pad& o) const {
        return number_ == o.number_ && position_ == o.position_ &&
               type_ == o.type_ && shape_ == o.shape_ &&
               width_nm_ == o.width_nm_ && height_nm_ == o.height_nm_ &&
               layer_ == o.layer_ && drill_diameter_nm_ == o.drill_diameter_nm_ &&
               paste_coverage_ == o.paste_coverage_;
    }
    bool operator!=(const Pad& o) const { return !(*this == o); }

private:
    std::string number_;                       // 焊盘号（与符号引脚号对应）
    geometry::Point position_;                 // 焊盘中心（nm）
    PadType type_ = PadType::SMD;
    PadShape shape_ = PadShape::RECTANGLE;
    geometry::Coord width_nm_ = 0;             // X 尺寸（nm）
    geometry::Coord height_nm_ = 0;            // Y 尺寸（nm）
    PadLayer layer_ = PadLayer::TOP;
    geometry::Coord drill_diameter_nm_ = 0;    // 通孔钻孔直径（nm）；SMD=0
    double paste_coverage_ = 1.0;              // 钢网覆盖率（默认 1.0 = 100%）
    geometry::Polygon custom_shape_;           // 仅 shape == POLYGON 使用
};

// ============================================================================
// Model3DRef —— 3D 模型引用（STEP/VRML 路径 + 偏移/旋转/缩放）
// ============================================================================
class Model3DRef {
public:
    Model3DRef() = default;

    const std::string& path() const { return path_; }       // res://3d/soic8.step
    geometry::Coord offset_x_nm() const { return offset_x_nm_; }
    geometry::Coord offset_y_nm() const { return offset_y_nm_; }
    geometry::Coord offset_z_nm() const { return offset_z_nm_; }  // 板面以上高度
    double rotation_x_deg() const { return rotation_x_deg_; }
    double rotation_y_deg() const { return rotation_y_deg_; }
    double rotation_z_deg() const { return rotation_z_deg_; }
    double scale() const { return scale_; }
    geometry::Coord body_height_nm() const { return body_height_nm_; }  // 器件最大高度（DRC 用）

    void set_path(std::string p) { path_ = std::move(p); }
    void set_offset_nm(geometry::Coord x, geometry::Coord y, geometry::Coord z) {
        offset_x_nm_ = x; offset_y_nm_ = y; offset_z_nm_ = z;
    }
    void set_rotation_deg(double x, double y, double z) {
        rotation_x_deg_ = x; rotation_y_deg_ = y; rotation_z_deg_ = z;
    }
    void set_scale(double s) { scale_ = s; }
    void set_body_height_nm(geometry::Coord h) { body_height_nm_ = h; }

    bool operator==(const Model3DRef& o) const {
        return path_ == o.path_ && offset_x_nm_ == o.offset_x_nm_ &&
               offset_y_nm_ == o.offset_y_nm_ && offset_z_nm_ == o.offset_z_nm_ &&
               rotation_x_deg_ == o.rotation_x_deg_ && rotation_y_deg_ == o.rotation_y_deg_ &&
               rotation_z_deg_ == o.rotation_z_deg_ && scale_ == o.scale_ &&
               body_height_nm_ == o.body_height_nm_;
    }

private:
    std::string path_;
    geometry::Coord offset_x_nm_ = 0;
    geometry::Coord offset_y_nm_ = 0;
    geometry::Coord offset_z_nm_ = 0;
    double rotation_x_deg_ = 0;
    double rotation_y_deg_ = 0;
    double rotation_z_deg_ = 0;
    double scale_ = 1.0;
    geometry::Coord body_height_nm_ = 0;
};

// ============================================================================
// Ipc7351Fields —— IPC-7351 标准字段（密度等级、参考高度、courtyard excess）
// ============================================================================
class Ipc7351Fields {
public:
    Ipc7351Fields() = default;

    const std::string& density_level() const { return density_level_; }  // "L"/"M"/"N"
    const std::string& footprint_name() const { return footprint_name_; }  // IPC 标准名
    geometry::Coord lead_span_nm() const { return lead_span_nm_; }     // 引脚跨距
    geometry::Coord body_width_nm() const { return body_width_nm_; }
    geometry::Coord body_length_nm() const { return body_length_nm_; }
    geometry::Coord courtyard_excess_nm() const { return courtyard_excess_nm_; }  // courtyard 余量
    bool operator==(const Ipc7351Fields& o) const {
        return density_level_ == o.density_level_ && footprint_name_ == o.footprint_name_ &&
               lead_span_nm_ == o.lead_span_nm_ && body_width_nm_ == o.body_width_nm_ &&
               body_length_nm_ == o.body_length_nm_ &&
               courtyard_excess_nm_ == o.courtyard_excess_nm_;
    }

    void set_density_level(std::string l) { density_level_ = std::move(l); }
    void set_footprint_name(std::string n) { footprint_name_ = std::move(n); }
    void set_lead_span_nm(geometry::Coord s) { lead_span_nm_ = s; }
    void set_body_width_nm(geometry::Coord w) { body_width_nm_ = w; }
    void set_body_length_nm(geometry::Coord l) { body_length_nm_ = l; }
    void set_courtyard_excess_nm(geometry::Coord e) { courtyard_excess_nm_ = e; }

private:
    std::string density_level_ = "M";   // 默认中等密度
    std::string footprint_name_;
    geometry::Coord lead_span_nm_ = 0;
    geometry::Coord body_width_nm_ = 0;
    geometry::Coord body_length_nm_ = 0;
    geometry::Coord courtyard_excess_nm_ = 250'000;  // 默认 0.25 mm 余量
};

// ============================================================================
// Footprint —— PCB 封装资源（继承 Resource）
// ============================================================================
class Footprint : public Resource {
public:
    static std::string get_class_static() { return "Footprint"; }
    std::string get_class() const override { return "Footprint"; }
    bool is_class(const std::string& name) const override {
        return name == "Footprint" || Resource::is_class(name);
    }

    Footprint();  // 自动生成 UUID

    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }          // 显示名（SOIC8_3.9x4.9mm_P1.27mm）
    const std::string& description() const { return description_; }
    const std::string& keywords() const { return keywords_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_description(std::string d) { description_ = std::move(d); }
    void set_keywords(std::string k) { keywords_ = std::move(k); }

    // ----- 焊盘管理 -----
    size_t pad_count() const { return pads_.size(); }
    const std::vector<Pad>& pads() const { return pads_; }
    const Pad& pad(size_t i) const { return pads_[i]; }
    Pad& pad(size_t i) { return pads_[i]; }

    Pad& add_pad(Pad p);
    bool remove_pad_by_number(const std::string& number);
    const Pad* find_pad_by_number(const std::string& number) const;
    Pad* find_pad_by_number(const std::string& number);

    // ----- 丝印图元（线/弧/多边形/文本）-----
    size_t silkscreen_count() const { return silkscreen_.size(); }
    const std::vector<GraphicsElement>& silkscreen() const { return silkscreen_; }
    GraphicsElement& add_silkscreen(GraphicsElement g);

    // ----- courtyard（器件占用区；DFM 间距检查基石）-----
    bool has_courtyard() const { return !courtyard_.outer.empty(); }
    const geometry::Polygon& courtyard() const { return courtyard_; }
    void set_courtyard(geometry::Polygon p) { courtyard_ = std::move(p); }
    void clear_courtyard() { courtyard_ = geometry::Polygon{}; }

    // ----- IPC-7351 字段 -----
    const Ipc7351Fields& ipc7351() const { return ipc7351_; }
    Ipc7351Fields& ipc7351() { return ipc7351_; }
    void set_ipc7351(Ipc7351Fields f) { ipc7351_ = std::move(f); }

    // ----- 3D 模型引用 -----
    bool has_model_3d() const { return !model_3d_.path().empty(); }
    const Model3DRef& model_3d() const { return model_3d_; }
    Model3DRef& model_3d() { return model_3d_; }
    void set_model_3d(Model3DRef m) { model_3d_ = std::move(m); }

    // ----- 测试专用 -----
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ----- Resource 序列化接口（.fp.toml）-----
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;
    static Ref<Footprint> parse(const std::string& text);

private:
    // 重置业务成员到空白状态（保留 uuid_）；用于 deserialize() 开头，
    // 因 Footprint 继承不可拷贝/移动的 Resource，无法用 *this = Footprint() 重置。
    void reset();

    std::string uuid_;
    std::string name_;
    std::string description_;
    std::string keywords_;

    std::vector<Pad> pads_;
    std::vector<GraphicsElement> silkscreen_;
    geometry::Polygon courtyard_;        // 默认空（无 outer）
    Ipc7351Fields ipc7351_;
    Model3DRef model_3d_;
};

}  // namespace eda
