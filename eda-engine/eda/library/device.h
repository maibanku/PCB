#pragma once

// eda/library/device.h
//
// P6 元件库 · 器件一体化绑定（Task：Symbol + Footprint + Device 一体化绑定）。
//
// 对应 docs/06-元件库/04-器件一体化绑定。
// 对齐 REQ-007 EDA Object Model + REQ-010 参数系统（Property schema 六字段）。
//
// 一体化数据模型（vs KiCad 符号/封装分离）：
//   Device (Resource)
//     ├── symbol_ref_      : res:// 引用到 Symbol（可能多 gate）
//     ├── footprint_ref_   : res:// 引用到 Footprint（可能多封装变体）
//     ├── pin_map_         : symbol pin number ↔ footprint pad number（ECO 双向同步基础）
//     ├── model_3d_ref_    : 3D 模型路径（封装变体级引用）
//     ├── properties_      : Property[]（参数元数据；六字段 schema，Variant 承载 value）
//     ├── spice_           : SPICE 模型占位（求解由 P9 仿真器承担）
//     └── supply_          : 供应链信息占位（MPN/manufacturer，下单由 P21 承担）
//
// 序列化：手写最小 TOML 子集，输出 .dev.toml。
//
// 依赖：core/object/{ref,resource,variant}.h、eda/library/{symbol,footprint}.h、
//       eda/project/project.h（generate_uuid）。

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "core/object/variant.h"

namespace eda {

// ============================================================================
// Property —— 参数元数据（REQ-010 六字段 schema）
//   name       : 参数名（"resistance"/"capacitance"/"tolerance"/...）
//   value      : Variant 承载的实际值（INT/FLOAT/STRING/...）
//   type_name  : 类型名（"resistance"/"capacitance"/"string"）；用于 UI 分组
//   unit       : 单位字符串（"Ω"/"F"/"V"/"%"）；纯文本，不参与运算
//   visible    : 是否在原理图/PCB 上显示
//   editable   : 是否允许用户编辑
// ============================================================================
class Property {
public:
    Property() = default;
    Property(std::string name, Variant value,
             std::string type_name = std::string(),
             std::string unit = std::string(),
             bool visible = false,
             bool editable = true);

    const std::string& name() const { return name_; }
    const Variant& value() const { return value_; }
    const std::string& type_name() const { return type_name_; }
    const std::string& unit() const { return unit_; }
    bool visible() const { return visible_; }
    bool editable() const { return editable_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_value(Variant v) { value_ = std::move(v); }
    void set_type_name(std::string t) { type_name_ = std::move(t); }
    void set_unit(std::string u) { unit_ = std::move(u); }
    void set_visible(bool v) { visible_ = v; }
    void set_editable(bool e) { editable_ = e; }

    bool operator==(const Property& o) const {
        return name_ == o.name_ && value_.serialize() == o.value_.serialize() &&
               type_name_ == o.type_name_ && unit_ == o.unit_ &&
               visible_ == o.visible_ && editable_ == o.editable_;
    }

private:
    std::string name_;
    Variant value_;
    std::string type_name_;
    std::string unit_;
    bool visible_ = false;
    bool editable_ = true;
};

// ============================================================================
// SpiceModel —— SPICE 模型占位（求解由 P9 承担）
// ============================================================================
class SpiceModel {
public:
    SpiceModel() = default;

    const std::string& model_ref() const { return model_ref_; }    // res://spice/lm358.sub
    const std::string& model_type() const { return model_type_; }  // "SUBCKT" / "MODEL"
    const std::string& subckt_name() const { return subckt_name_; }  // 模型内的子电路名
    bool enabled() const { return enabled_; }

    void set_model_ref(std::string r) { model_ref_ = std::move(r); }
    void set_model_type(std::string t) { model_type_ = std::move(t); }
    void set_subckt_name(std::string n) { subckt_name_ = std::move(n); }
    void set_enabled(bool e) { enabled_ = e; }

    bool operator==(const SpiceModel& o) const {
        return model_ref_ == o.model_ref_ && model_type_ == o.model_type_ &&
               subckt_name_ == o.subckt_name_ && enabled_ == o.enabled_;
    }

private:
    std::string model_ref_;
    std::string model_type_ = "SUBCKT";
    std::string subckt_name_;
    bool enabled_ = true;
};

// ============================================================================
// SupplyChainInfo —— 供应链信息占位（数据预置；接入由 P21 承担）
// ============================================================================
class SupplyChainInfo {
public:
    SupplyChainInfo() = default;

    const std::string& mpn() const { return mpn_; }                // 制造商零件号
    const std::string& manufacturer() const { return manufacturer_; }
    const std::string& supplier() const { return supplier_; }       // 如 LCSC
    const std::string& supplier_url() const { return supplier_url_; }
    const std::string& supplier_part_number() const { return supplier_part_number_; }
    double unit_price() const { return unit_price_; }               // 单价（CNY）
    int stock() const { return stock_; }
    const std::string& lifecycle() const { return lifecycle_; }     // active/nrnd/eol
    bool rohs() const { return rohs_; }

    void set_mpn(std::string m) { mpn_ = std::move(m); }
    void set_manufacturer(std::string m) { manufacturer_ = std::move(m); }
    void set_supplier(std::string s) { supplier_ = std::move(s); }
    void set_supplier_url(std::string u) { supplier_url_ = std::move(u); }
    void set_supplier_part_number(std::string p) { supplier_part_number_ = std::move(p); }
    void set_unit_price(double p) { unit_price_ = p; }
    void set_stock(int s) { stock_ = s; }
    void set_lifecycle(std::string l) { lifecycle_ = std::move(l); }
    void set_rohs(bool r) { rohs_ = r; }

    bool operator==(const SupplyChainInfo& o) const {
        return mpn_ == o.mpn_ && manufacturer_ == o.manufacturer_ &&
               supplier_ == o.supplier_ && supplier_url_ == o.supplier_url_ &&
               supplier_part_number_ == o.supplier_part_number_ &&
               unit_price_ == o.unit_price_ && stock_ == o.stock_ &&
               lifecycle_ == o.lifecycle_ && rohs_ == o.rohs_;
    }

private:
    std::string mpn_;
    std::string manufacturer_;
    std::string supplier_;
    std::string supplier_url_;
    std::string supplier_part_number_;
    double unit_price_ = 0.0;
    int stock_ = 0;
    std::string lifecycle_ = "active";
    bool rohs_ = true;
};

// ============================================================================
// Device —— 器件一体化绑定资源（继承 Resource）
//   聚合 Symbol + Footprint + 3D + Property + SPICE + SupplyChain 五位一体
// ============================================================================
class Device : public Resource {
public:
    static std::string get_class_static() { return "Device"; }
    std::string get_class() const override { return "Device"; }
    bool is_class(const std::string& name) const override {
        return name == "Device" || Resource::is_class(name);
    }

    Device();  // 自动生成 UUID

    // ----- 身份 -----
    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }       // 显示名（LM358）
    const std::string& description() const { return description_; }
    const std::string& category() const { return category_; }  // 分类（OpAmp/Resistor/...）
    const std::string& keywords() const { return keywords_; }
    const std::string& datasheet_url() const { return datasheet_url_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_description(std::string d) { description_ = std::move(d); }
    void set_category(std::string c) { category_ = std::move(c); }
    void set_keywords(std::string k) { keywords_ = std::move(k); }
    void set_datasheet_url(std::string u) { datasheet_url_ = std::move(u); }

    // ----- 一体化绑定：Symbol / Footprint / 3D -----
    bool has_symbol() const { return !symbol_ref_.empty(); }
    const std::string& symbol_ref() const { return symbol_ref_; }  // res://lib/opamp.sym.toml
    void bind_symbol(std::string ref) { symbol_ref_ = std::move(ref); }
    void unbind_symbol() { symbol_ref_.clear(); }

    bool has_footprint() const { return !footprint_ref_.empty(); }
    const std::string& footprint_ref() const { return footprint_ref_; }
    void bind_footprint(std::string ref) { footprint_ref_ = std::move(ref); }
    void unbind_footprint() { footprint_ref_.clear(); }

    bool has_model_3d() const { return !model_3d_ref_.empty(); }
    const std::string& model_3d_ref() const { return model_3d_ref_; }
    void bind_model_3d(std::string ref) { model_3d_ref_ = std::move(ref); }
    void unbind_model_3d() { model_3d_ref_.clear(); }

    // ----- Pin 映射（symbol pin number ↔ footprint pad number）-----
    // 三表对齐 ECO 基础：符号引脚号 ↔ 封装焊盘号。本字段维护 symbol→footprint 的字符串映射。
    const std::unordered_map<std::string, std::string>& pin_map() const { return pin_map_; }
    void map_pin(std::string symbol_pin_number, std::string footprint_pad_number);
    bool unmap_pin(const std::string& symbol_pin_number);
    const std::string* mapped_pad(const std::string& symbol_pin_number) const;
    void clear_pin_map() { pin_map_.clear(); }

    // ----- 参数（Property 列表，REQ-010 六字段）-----
    size_t property_count() const { return properties_.size(); }
    const std::vector<Property>& properties() const { return properties_; }
    const Property& property(size_t i) const { return properties_[i]; }
    Property& property(size_t i) { return properties_[i]; }

    Property& add_property(Property p);
    // 按名查找；找不到返回 nullptr。
    const Property* find_property(const std::string& name) const;
    Property* find_property(const std::string& name);
    // 按名移除；返回是否实际移除。
    bool remove_property(const std::string& name);

    // ----- SPICE 模型占位 -----
    bool has_spice() const { return !spice_.model_ref().empty(); }
    const SpiceModel& spice() const { return spice_; }
    SpiceModel& spice() { return spice_; }
    void set_spice(SpiceModel s) { spice_ = std::move(s); }

    // ----- 供应链信息占位 -----
    bool has_supply_chain() const { return !supply_.mpn().empty(); }
    const SupplyChainInfo& supply_chain() const { return supply_; }
    SupplyChainInfo& supply_chain() { return supply_; }
    void set_supply_chain(SupplyChainInfo s) { supply_ = std::move(s); }

    // ----- 替代料（Pin 兼容器件的 MPN/UUID 列表，P21 推荐用）-----
    const std::vector<std::string>& equivalents() const { return equivalents_; }
    void add_equivalent(std::string ref);
    bool remove_equivalent(const std::string& ref);

    // ----- 测试专用 -----
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ----- Resource 序列化接口（.dev.toml）-----
    std::string serialize() const override;
    bool deserialize(const std::string& text) override;
    static Ref<Device> parse(const std::string& text);

private:
    // 重置业务成员到空白状态（保留 uuid_）；用于 deserialize() 开头，
    // 因 Device 继承不可拷贝/移动的 Resource，无法用 *this = Device() 重置。
    void reset();

    std::string uuid_;
    std::string name_;
    std::string description_;
    std::string category_;
    std::string keywords_;
    std::string datasheet_url_;

    std::string symbol_ref_;     // res://lib/...sym.toml
    std::string footprint_ref_;  // res://lib/...fp.toml
    std::string model_3d_ref_;   // res://3d/...step
    std::unordered_map<std::string, std::string> pin_map_;  // symbol pin → footprint pad

    std::vector<Property> properties_;
    SpiceModel spice_;
    SupplyChainInfo supply_;
    std::vector<std::string> equivalents_;
};

}  // namespace eda
