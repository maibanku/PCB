#pragma once

// eda/schematic/component.h
//
// P7 原理图核心数据模型 · 器件实例 Component（07-01 原理图图元）。
//
// 对应 docs/07-原理图编辑器/01-原理图图元 与 04-网络表生成。
// 对齐 REQ-007 EDA Object Model：Component : Resource（重用 P2 引用计数 + res:// 路径
// + 序列化虚函数）；坐标统一使用 P3 geometry 的 Point/Coord（int64 nm）。
//
// 数据模型分层：
//   Component (Resource)
//     ├── symbol_        : Ref<Symbol>（P6 符号引用，in-memory 解析；提供 Pin 列表）
//     ├── device_        : Ref<Device>（P6 器件一体化引用，可空；提供参数/封装绑定）
//     ├── symbol_ref_    : res:// 路径字符串（序列化用，解耦 in-memory 资源缓存）
//     ├── device_ref_    : res:// 路径字符串（序列化用）
//     ├── refdes_        : 位号（R1/C2/U3），位号编排 Annotate（07-06）写入
//     ├── position_      : 原理图世界坐标（nm）
//     ├── rotation_      : 0/90/180/270 度（CCW 正，四向枚举）
//     ├── attributes_    : 属性覆盖 map（实例级覆盖 Symbol/Device 默认值，如阻值/封装）
//     └── pins_          : 由 Symbol 推导的引脚连接点（动态计算，不持久化）
//
// Component 与 Symbol 的关系（vs KiCad 的符号实例分离）：
//   - KiCad 把每份"符号实例"深拷贝到原理图中（脱离库演化）；
//   - 本引擎采用引用模型：Component 仅持引用 + 几何变换 + 覆盖，符号几何仍由 Symbol 资源
//     单一来源——库修订可传播，ECO 双向同步（P14）基础由此奠定。
//
// 旋转约定：以 position_ 为锚点的 CCW（逆时针）旋转。Pin 世界坐标 =
//   position_ + rotate_ccw(symbol_pin.position, rotation_)。
//   0°→(x,y)；90°→(-y,x)；180°→(-x,-y)；270°→(y,-x)。
//
// 序列化：手写最小 TOML 子集，输出嵌入到 Schematic 的 .sch.toml（component 段）。
// 单独序列化时输出独立 .cmp.toml（与 .sym.toml/.dev.toml 风格一致）。
//
// 依赖：core/object/{ref,resource}.h（P2）、eda/geometry/types.h（P3）、
//       eda/library/{symbol,device}.h（P6）、eda/project/project.h（generate_uuid）。

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "eda/geometry/types.h"
#include "eda/library/device.h"
#include "eda/library/symbol.h"

namespace eda::sch {

// ============================================================================
// 枚举：器件旋转角度（CCW，四向）
//   仅允许 0/90/180/270；其它角度在原理图栅格上无意义（set_rotation 会校验）。
// ============================================================================
enum class Rotation {
    DEG_0 = 0,
    DEG_90 = 90,
    DEG_180 = 180,
    DEG_270 = 270,
};

// 数字角度（0/90/180/270）与枚举互转；非法值回落 DEG_0。
Rotation rotation_from_degrees(int degrees) noexcept;
int to_degrees(Rotation r) noexcept;
std::string_view to_string_view(Rotation r);
Rotation parse_rotation(std::string_view s);

// ============================================================================
// ConnectionPoint —— 引脚在原理图上的电气连接点（值类型）
//   由 Symbol.pin.position 经 Component 旋转/平移后的世界坐标。
//   网表生成（04）以连接点为节点；端点重合即同网络。
// ============================================================================
struct ConnectionPoint {
    std::string pin_number;       // 对应 Symbol.Pin.number（如 "1"）
    geometry::Point position{};   // 世界坐标（nm）
};

// ============================================================================
// Component —— 原理图器件实例（继承 Resource）
// ============================================================================
class Component : public Resource {
public:
    static std::string get_class_static() { return "Component"; }
    std::string get_class() const override { return "Component"; }
    bool is_class(const std::string& name) const override {
        return name == "Component" || Resource::is_class(name);
    }

    Component();  // 自动生成 UUID

    // ----- 身份 / UUID -----
    const std::string& uuid() const { return uuid_; }
    const std::string& refdes() const { return refdes_; }  // R1/C2/U3，可空（未 Annotate）
    void set_refdes(std::string r) { refdes_ = std::move(r); }

    // ----- 符号引用（in-memory + 路径双轨）-----
    // in-memory Ref 用于直接访问 Pin 列表（网表生成、连接点计算）。
    // 路径字符串用于序列化（解耦 ResourceCache，支持离线解析）。
    // set_symbol(Ref<Symbol>) 同时回填 symbol_ref_（若 Symbol 已有 res:// 路径）。
    bool has_symbol() const { return symbol_.valid(); }
    Ref<Symbol> symbol() const { return symbol_; }
    void set_symbol(Ref<Symbol> sym);

    bool has_device() const { return device_.valid(); }
    Ref<Device> device() const { return device_; }
    void set_device(Ref<Device> dev);

    // 路径字符串（序列化用；与 in-memory Ref 互不依赖）
    const std::string& symbol_ref() const { return symbol_ref_; }
    void set_symbol_ref(std::string ref) { symbol_ref_ = std::move(ref); }
    const std::string& device_ref() const { return device_ref_; }
    void set_device_ref(std::string ref) { device_ref_ = std::move(ref); }

    // ----- 放置 -----
    geometry::Point position() const { return position_; }
    void set_position(geometry::Point p) { position_ = p; }

    Rotation rotation() const { return rotation_; }
    int rotation_degrees() const { return to_degrees(rotation_); }
    void set_rotation(Rotation r) { rotation_ = r; }
    // 便捷：接受 0/90/180/270；非法值忽略并返回 false。
    bool set_rotation_degrees(int degrees);

    // ----- 属性覆盖（实例级，覆盖 Symbol/Device 默认）-----
    // 例：set_attribute("resistance", "10kΩ") 覆盖默认 10kΩ；set_attribute("value","LM358")
    const std::unordered_map<std::string, std::string>& attributes() const { return attributes_; }
    void set_attribute(std::string name, std::string value);
    bool remove_attribute(const std::string& name);
    const std::string* find_attribute(const std::string& name) const;

    // ----- 引脚连接点（由 Symbol + 放置变换动态计算）-----
    // 返回每个 Pin 在世界坐标系下的连接点；has_symbol()==false 时返回空。
    std::vector<ConnectionPoint> connection_points() const;
    // 单 Pin 的世界坐标；找不到（或无 Symbol）返回 nullopt。
    std::optional<geometry::Point> pin_position(const std::string& pin_number) const;
    // 静态工具：给定 symbol 局部点 + 旋转 + 平移，计算世界坐标（与 connection_points 同算法）
    static geometry::Point transform_local_to_world(geometry::Point local,
                                                    geometry::Point origin,
                                                    Rotation rotation);

    // ----- 测试专用：注入固定 UUID（生产代码勿用）-----
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ----- Resource 序列化接口（.cmp.toml，可嵌入 Schematic）-----
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;
    static Ref<Component> parse(const std::string& text);

private:
    std::string uuid_;
    std::string refdes_;

    Ref<Symbol> symbol_;
    Ref<Device> device_;
    std::string symbol_ref_;  // res://lib/...sym.toml
    std::string device_ref_;  // res://lib/...dev.toml

    geometry::Point position_{};
    Rotation rotation_ = Rotation::DEG_0;

    std::unordered_map<std::string, std::string> attributes_;
};

}  // namespace eda::sch
