#pragma once

// eda/library/symbol.h
//
// P6 元件库 · 符号数据模型（Task：Symbol + Footprint + Device 一体化绑定）。
//
// 对应 docs/06-元件库/01-符号库编辑器 与 04-器件一体化绑定。
// 对齐 REQ-007 EDA Object Model：Symbol : Resource（重用 P2 引用计数 + res:// 路径
// + 序列化占位虚函数）；图元坐标统一使用 P3 geometry 的 Point/Coord（int64 nm）。
//
// 数据模型分层：
//   Symbol (Resource)
//     ├── graphics_   : GraphicsElement[]（线/圆弧/多边形/文本，复用 P3 geometry 类型）
//     ├── pins_       : Pin[]（number/name/position/electrical_type/orientation/length）
//     ├── pin_map_    : symbol pin number ↔ footprint pad number（多 gate/重编号支持）
//     ├── gates_      : 多 gate 单元（运放/逻辑门；首版仅 Gate 元信息）
//     └── attrs_      : 符号级元数据（designation 前缀 / description / keywords / datasheet / mpn）
//
// 序列化：手写最小 TOML 子集（不引 toml++），输出 .sym.toml，对齐 eda/project 的写法。
//
// 依赖：core/object/{ref,resource}.h（P2）、eda/geometry/types.h（P3）、
//       eda/project/project.h（generate_uuid 复用）。

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "eda/geometry/types.h"

namespace eda {

// ============================================================================
// 枚举：引脚电气类型（IEEE 315 / IEC 60617，决定 ERC 检查冲突）
// ============================================================================
enum class ElectricalType {
    PASSIVE,      // 无源（电阻/电容/电感引脚）
    INPUT,        // 输入
    OUTPUT,       // 输出
    BIDIRECTIONAL,// 双向（I/O）
    POWER_INPUT,  // 电源入（VCC/VDD）
    POWER_OUTPUT, // 电源出（稳压器输出）
    OPEN_COLLECTOR, // 集电极开路
    OPEN_EMITTER,   // 发射极开路
    TRI_STATE,    // 三态
    UNSPECIFIED,  // 未指定
};

// 序列化字符串名：PASSIVE -> "passive" 等；未识别默认 PASSIVE（前向兼容）。
std::string_view to_string_view(ElectricalType t);
ElectricalType parse_electrical_type(std::string_view s);

// ============================================================================
// 枚举：引脚朝向（原理图四向）
// ============================================================================
enum class Orientation {
    RIGHT,  // 引脚向右延伸（电气连接点在右端）
    LEFT,
    UP,
    DOWN,
};

std::string_view to_string_view(Orientation o);
Orientation parse_orientation(std::string_view s);

// ============================================================================
// Pin —— 符号引脚（值类型）
// ============================================================================
class Pin {
public:
    Pin() = default;
    Pin(std::string number, std::string name, geometry::Point position,
        ElectricalType etype = ElectricalType::PASSIVE,
        Orientation orientation = Orientation::RIGHT,
        geometry::Coord length_nm = 0,
        int gate = 0);

    const std::string& number() const { return number_; }
    const std::string& name() const { return name_; }
    geometry::Point position() const { return position_; }
    ElectricalType electrical_type() const { return electrical_type_; }
    Orientation orientation() const { return orientation_; }
    geometry::Coord length_nm() const { return length_nm_; }
    int gate() const { return gate_; }

    void set_number(std::string n) { number_ = std::move(n); }
    void set_name(std::string n) { name_ = std::move(n); }
    void set_position(geometry::Point p) { position_ = p; }
    void set_electrical_type(ElectricalType t) { electrical_type_ = t; }
    void set_orientation(Orientation o) { orientation_ = o; }
    void set_length_nm(geometry::Coord l) { length_nm_ = l; }
    void set_gate(int g) { gate_ = g; }

    bool operator==(const Pin& o) const {
        return number_ == o.number_ && name_ == o.name_ &&
               position_ == o.position_ && electrical_type_ == o.electrical_type_ &&
               orientation_ == o.orientation_ && length_nm_ == o.length_nm_ &&
               gate_ == o.gate_;
    }
    bool operator!=(const Pin& o) const { return !(*this == o); }

private:
    std::string number_;                       // 引脚号（与封装焊盘号对应）
    std::string name_;                         // 引脚名（VCC/GND/IN 等）
    geometry::Point position_;                 // 电气连接点（nm）
    ElectricalType electrical_type_ = ElectricalType::PASSIVE;
    Orientation orientation_ = Orientation::RIGHT;
    geometry::Coord length_nm_ = 0;            // 引脚桩长度（nm）
    int gate_ = 0;                             // 所属 gate 索引（多 gate 器件，默认 0）
};

// ============================================================================
// GraphicsElement —— 符号图形元素（discriminated union，复用 P3 geometry 值类型）
//   线段（折线 Polyline）/ 圆弧（Arc）/ 多边形（Polygon，闭合填充）/ 文本（Text）
// ============================================================================
enum class GraphicsKind {
    POLYLINE,  // 开路径（线段链）
    ARC,       // 圆弧（参数化）
    POLYGON,   // 闭合多边形（可填充）
    TEXT,      // 文本注释
};

std::string_view to_string_view(GraphicsKind k);
GraphicsKind parse_graphics_kind(std::string_view s);

class GraphicsElement {
public:
    GraphicsElement() = default;

    GraphicsKind kind = GraphicsKind::POLYLINE;
    geometry::Polyline polyline;        // kind == POLYLINE
    geometry::Arc arc;                  // kind == ARC
    geometry::Polygon polygon;          // kind == POLYGON
    std::string text;                   // kind == TEXT
    geometry::Point text_anchor{};      // kind == TEXT（锚点 nm）
    geometry::Coord stroke_width_nm = 100'000;  // 默认 0.1 mm
    geometry::Coord text_size_nm = 1'500'000;   // 默认 1.5 mm
    bool filled = false;                // kind == POLYGON
};

// ============================================================================
// Gate —— 多 gate 单元（运放/逻辑门，一个 Symbol 含多 Gate）
// 首版仅记录 gate 元信息（索引 + 显示名），图形仍按 GraphicsElement 留待扩展。
// ============================================================================
class Gate {
public:
    Gate() = default;
    explicit Gate(int index, std::string name = std::string());

    int index() const { return index_; }
    const std::string& name() const { return name_; }
    void set_index(int i) { index_ = i; }
    void set_name(std::string n) { name_ = std::move(n); }

    bool operator==(const Gate& o) const {
        return index_ == o.index_ && name_ == o.name_;
    }

private:
    int index_ = 0;
    std::string name_;
};

// ============================================================================
// Symbol —— 原理图符号资源（继承 Resource）
// ============================================================================
class Symbol : public Resource {
public:
    static std::string get_class_static() { return "Symbol"; }
    std::string get_class() const override { return "Symbol"; }
    bool is_class(const std::string& name) const override {
        return name == "Symbol" || Resource::is_class(name);
    }

    Symbol();  // 自动生成 UUID

    // ----- 身份 / UUID（稳定主键，重命名不破坏引用）-----
    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }       // 显示名（如 "LM358"）
    const std::string& designation() const { return designation_; }  // 位号前缀 U?/R?/C?
    const std::string& description() const { return description_; }
    const std::string& keywords() const { return keywords_; }
    const std::string& datasheet_url() const { return datasheet_url_; }
    const std::string& default_mpn() const { return default_mpn_; }   // 关联默认 MPN

    void set_name(std::string n) { name_ = std::move(n); }
    void set_designation(std::string d) { designation_ = std::move(d); }
    void set_description(std::string d) { description_ = std::move(d); }
    void set_keywords(std::string k) { keywords_ = std::move(k); }
    void set_datasheet_url(std::string u) { datasheet_url_ = std::move(u); }
    void set_default_mpn(std::string m) { default_mpn_ = std::move(m); }

    // ----- 引脚管理 -----
    size_t pin_count() const { return pins_.size(); }
    const std::vector<Pin>& pins() const { return pins_; }
    const Pin& pin(size_t i) const { return pins_[i]; }
    Pin& pin(size_t i) { return pins_[i]; }

    // 新增引脚；若 number 已存在则忽略（避免重复）。返回新增引脚的引用，找不到时返回空。
    Pin& add_pin(Pin p);
    // 按 number 移除引脚；同时清理 pin_map 中以该 number 为键的项。返回是否实际移除。
    bool remove_pin_by_number(const std::string& number);
    // 按 number 查找；返回 nullptr 表示未命中。
    const Pin* find_pin_by_number(const std::string& number) const;
    Pin* find_pin_by_number(const std::string& number);
    // 按 name 查找（首个匹配）。
    const Pin* find_pin_by_name(const std::string& name) const;

    // ----- 图形元素 -----
    size_t graphics_count() const { return graphics_.size(); }
    const std::vector<GraphicsElement>& graphics() const { return graphics_; }
    GraphicsElement& add_graphics(GraphicsElement g);

    // ----- Pin 映射（符号引脚号 ↔ 封装焊盘号）-----
    // 与 Device 协作：器件绑定时由 Device 把 symbol.pin ↔ footprint.pad 串起来。
    // Symbol 自身的 pin_map_ 用于无器件上下文的纯符号库（如多 gate 重编号）。
    const std::unordered_map<std::string, std::string>& pin_map() const { return pin_map_; }
    void map_pin(std::string pin_number, std::string mapped_id);
    bool unmap_pin(const std::string& pin_number);
    const std::string* mapped_id(const std::string& pin_number) const;

    // ----- 多 Gate 单元 -----
    size_t gate_count() const { return gates_.size(); }
    const std::vector<Gate>& gates() const { return gates_; }
    Gate& add_gate(Gate g);

    // ----- 测试专用：注入固定 UUID 验证确定性输出（生产代码勿用）-----
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ----- Resource 序列化接口（.sym.toml）-----
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;

    // 便捷 IO 门面（不依赖 ResourceLoader/Saver，直接字符串往返）
    static Ref<Symbol> parse(const std::string& text);

private:
    // 重置业务成员到空白状态（保留 uuid_）；用于 deserialize() 开头，
    // 因 Symbol 继承不可拷贝/移动的 Resource，无法用 *this = Symbol() 重置。
    void reset();

    std::string uuid_;
    std::string name_;
    std::string designation_;     // U?/R?/C? 位号前缀
    std::string description_;
    std::string keywords_;
    std::string datasheet_url_;
    std::string default_mpn_;     // 关联默认 MPN（可被 Device 覆盖）

    std::vector<Pin> pins_;
    std::vector<GraphicsElement> graphics_;
    std::unordered_map<std::string, std::string> pin_map_;  // pin number → mapped id
    std::vector<Gate> gates_;
};

}  // namespace eda
