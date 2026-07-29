// tests/test_library.cpp
//
// P6 元件库 · 核心数据模型测试（Symbol + Footprint + Device 一体化绑定）。
//
// 覆盖：
//   - Symbol：构造 + UUID + 引脚（add/find/remove）+ 图形 + Pin 映射 + 多 Gate + .sym.toml 往返
//   - Footprint：焊盘（SMD/通孔）+ 丝印 + courtyard + IPC-7351 + 3D 模型 + .fp.toml 往返
//   - Device：一体化绑定（Symbol+Footprint+3D）+ Pin 映射 + 参数（REQ-010 六字段 Property）
//             + SPICE 占位 + 供应链占位 + .dev.toml 往返
//   - UUID 稳定性：跨序列化往返保持
//
// 依赖：eda_library（eda_core + eda_geometry + eda_project 传递）
// 注意：本文件不改 CMakeLists；集成步骤由主循环完成（追加到 tests/CMakeLists.txt）：
//   add_executable(test_library test_library.cpp)
//   target_link_libraries(test_library PRIVATE eda_library gtest_main)
//   gtest_discover_tests(test_library)

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/object/ref.h"
#include "core/object/variant.h"
#include "eda/geometry/types.h"
#include "eda/geometry/units.h"
#include "eda/library/device.h"
#include "eda/library/footprint.h"
#include "eda/library/symbol.h"

using namespace eda;
using namespace eda::geometry;

// ============================================================================
// Symbol —— 构造 + UUID + 类层级
// ============================================================================

TEST(SymbolTest, ConstructorGeneratesValidUuid) {
    Symbol s;
    s.set_name("LM358");
    EXPECT_EQ(s.get_class(), "Symbol");
    EXPECT_EQ(Symbol::get_class_static(), "Symbol");
    EXPECT_TRUE(s.is_class("Symbol"));
    EXPECT_TRUE(s.is_class("Resource"));
    EXPECT_TRUE(s.is_class("RefCounted"));
    EXPECT_TRUE(s.is_class("Object"));
    EXPECT_FALSE(s.is_class("Footprint"));
    EXPECT_EQ(s.name(), "LM358");
    EXPECT_FALSE(s.uuid().empty());
    EXPECT_EQ(s.uuid().size(), 36u);  // UUID v4 字符串长度
}

TEST(SymbolTest, UniqueUuidsAcrossInstances) {
    Symbol a, b;
    EXPECT_NE(a.uuid(), b.uuid());
}

TEST(SymbolTest, ResourceInheritanceHoldsByRef) {
    Ref<Symbol> sym(new Symbol());
    sym->set_name("R");
    Ref<Resource> as_resource = sym;  // 上转
    EXPECT_EQ(as_resource->get_class(), "Symbol");
    EXPECT_TRUE(as_resource->is_class("Resource"));
    // path 由 Resource 基类提供
    EXPECT_TRUE(as_resource->get_path().empty());
    as_resource->set_path("res://lib/r.sym.toml");
    EXPECT_EQ(sym->get_path(), "res://lib/r.sym.toml");
}

// ============================================================================
// Symbol —— 引脚管理
// ============================================================================

TEST(SymbolTest, AddPinAppendsAndFindByNumberName) {
    Symbol s;
    Pin vcc("1", "VCC", Point{0, 0}, ElectricalType::POWER_INPUT);
    Pin in("2", "IN+", Point{1'000'000, 0}, ElectricalType::INPUT,
           Orientation::LEFT, 7'620'000);
    s.add_pin(vcc);
    s.add_pin(in);

    EXPECT_EQ(s.pin_count(), 2u);
    ASSERT_NE(s.find_pin_by_number("1"), nullptr);
    EXPECT_EQ(s.find_pin_by_number("1")->name(), "VCC");
    EXPECT_EQ(s.find_pin_by_number("1")->electrical_type(), ElectricalType::POWER_INPUT);
    EXPECT_EQ(s.find_pin_by_number("1")->orientation(), Orientation::RIGHT);

    ASSERT_NE(s.find_pin_by_name("IN+"), nullptr);
    EXPECT_EQ(s.find_pin_by_name("IN+")->number(), "2");
    EXPECT_EQ(s.find_pin_by_name("IN+")->orientation(), Orientation::LEFT);
    EXPECT_EQ(s.find_pin_by_name("IN+")->length_nm(), 7'620'000);

    EXPECT_EQ(s.find_pin_by_number("99"), nullptr);
    EXPECT_EQ(s.find_pin_by_name("nonexistent"), nullptr);
}

TEST(SymbolTest, RemovePinByNumberDropsAndReturnsStatus) {
    Symbol s;
    s.add_pin(Pin("1", "A", Point{}));
    s.add_pin(Pin("2", "B", Point{}));
    EXPECT_EQ(s.pin_count(), 2u);

    EXPECT_TRUE(s.remove_pin_by_number("1"));
    EXPECT_EQ(s.pin_count(), 1u);
    EXPECT_EQ(s.find_pin_by_number("1"), nullptr);

    EXPECT_FALSE(s.remove_pin_by_number("nonexistent"));
    EXPECT_EQ(s.pin_count(), 1u);
}

TEST(SymbolTest, PinMappingRoundtrip) {
    Symbol s;
    s.map_pin("1", "A1");
    s.map_pin("2", "A2");
    ASSERT_NE(s.mapped_id("1"), nullptr);
    EXPECT_EQ(*s.mapped_id("1"), "A1");
    EXPECT_EQ(s.mapped_id("3"), nullptr);
    EXPECT_EQ(s.pin_map().size(), 2u);

    EXPECT_TRUE(s.unmap_pin("1"));
    EXPECT_EQ(s.pin_map().size(), 1u);
    EXPECT_EQ(s.mapped_id("1"), nullptr);

    EXPECT_FALSE(s.unmap_pin("never"));
}

TEST(SymbolTest, AddGraphicsAndGates) {
    Symbol s;
    GraphicsElement line;
    line.kind = GraphicsKind::POLYLINE;
    line.polyline.points = {{0, 0}, {5'000'000, 0}};
    line.stroke_width_nm = 150'000;
    s.add_graphics(line);

    GraphicsElement text;
    text.kind = GraphicsKind::TEXT;
    text.text = "U?";
    text.text_anchor = Point{1'000'000, 1'000'000};
    text.text_size_nm = 2'000'000;
    s.add_graphics(text);

    EXPECT_EQ(s.graphics_count(), 2u);
    EXPECT_EQ(s.graphics()[0].kind, GraphicsKind::POLYLINE);
    EXPECT_EQ(s.graphics()[0].polyline.points.size(), 2u);
    EXPECT_EQ(s.graphics()[1].text, "U?");

    s.add_gate(Gate(0, "A"));
    s.add_gate(Gate(1, "B"));
    EXPECT_EQ(s.gate_count(), 2u);
    EXPECT_EQ(s.gates()[0].index(), 0);
    EXPECT_EQ(s.gates()[1].name(), "B");
}

// ============================================================================
// Symbol —— .sym.toml 序列化往返
// ============================================================================

TEST(SymbolTest, SerializeRoundtripPreservesFields) {
    Symbol original;
    original.assign_uuid_for_test("11111111-1111-4111-8111-111111111111");
    original.set_name("LM358");
    original.set_designation("U?");
    original.set_description("Dual Op-Amp");
    original.set_keywords("opamp,dual");
    original.set_datasheet_url("https://example.com/lm358.pdf");
    original.set_default_mpn("LM358DR");

    original.add_pin(Pin("1", "OUTA", Point{0, 0}, ElectricalType::OUTPUT,
                         Orientation::LEFT, 7'620'000, /*gate=*/0));
    original.add_pin(Pin("8", "VCC", Point{5'000'000, 5'000'000},
                         ElectricalType::POWER_INPUT, Orientation::UP, 5'080'000, /*gate=*/0));

    GraphicsElement box;
    box.kind = GraphicsKind::POLYGON;
    box.polygon.outer = {{0, 0}, {10'000'000, 0}, {10'000'000, 8'000'000}, {0, 8'000'000}};
    box.filled = false;
    original.add_graphics(box);

    original.map_pin("1", "A1");
    original.add_gate(Gate(0, "A"));

    const std::string text = original.serialize();
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("[[pins]]"), std::string::npos);
    EXPECT_NE(text.find("[[graphics]]"), std::string::npos);
    EXPECT_NE(text.find("[pin_map]"), std::string::npos);

    Ref<Symbol> restored = Symbol::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original.uuid());
    EXPECT_EQ(restored->name(), "LM358");
    EXPECT_EQ(restored->designation(), "U?");
    EXPECT_EQ(restored->description(), "Dual Op-Amp");
    EXPECT_EQ(restored->keywords(), "opamp,dual");
    EXPECT_EQ(restored->datasheet_url(), "https://example.com/lm358.pdf");
    EXPECT_EQ(restored->default_mpn(), "LM358DR");

    EXPECT_EQ(restored->pin_count(), 2u);
    // pins 按 number 字典序输出；"1" 应在前
    EXPECT_EQ(restored->pin(0).number(), "1");
    EXPECT_EQ(restored->pin(0).name(), "OUTA");
    EXPECT_EQ(restored->pin(0).electrical_type(), ElectricalType::OUTPUT);
    EXPECT_EQ(restored->pin(0).orientation(), Orientation::LEFT);
    EXPECT_EQ(restored->pin(0).length_nm(), 7'620'000);
    EXPECT_EQ(restored->pin(0).gate(), 0);

    EXPECT_EQ(restored->pin(1).number(), "8");
    EXPECT_EQ(restored->pin(1).name(), "VCC");

    EXPECT_EQ(restored->graphics_count(), 1u);
    EXPECT_EQ(restored->graphics()[0].kind, GraphicsKind::POLYGON);
    EXPECT_EQ(restored->graphics()[0].polygon.outer.size(), 4u);
    EXPECT_FALSE(restored->graphics()[0].filled);

    ASSERT_NE(restored->mapped_id("1"), nullptr);
    EXPECT_EQ(*restored->mapped_id("1"), "A1");

    EXPECT_EQ(restored->gate_count(), 1u);
    EXPECT_EQ(restored->gates()[0].index(), 0);
    EXPECT_EQ(restored->gates()[0].name(), "A");
}

TEST(SymbolTest, SerializeIsDeterministic) {
    Symbol a, b;
    a.assign_uuid_for_test("22222222-2222-4222-8222-222222222222");
    a.set_name("X");
    a.add_pin(Pin("2", "B", Point{0, 0}));
    a.add_pin(Pin("1", "A", Point{0, 0}));

    b.assign_uuid_for_test("22222222-2222-4222-8222-222222222222");
    b.set_name("X");
    b.add_pin(Pin("1", "A", Point{0, 0}));
    b.add_pin(Pin("2", "B", Point{0, 0}));

    // 不同插入顺序 → 相同字节级输出（pins 按 number 字典序）
    EXPECT_EQ(a.serialize(), b.serialize());
}

TEST(SymbolTest, DeserializeHandlesUnknownKeysGracefully) {
    // 前向兼容：未知键与字段被忽略
    std::string text =
        "schema_version = 1\n"
        "uuid = \"33333333-3333-4333-8333-333333333333\"\n"
        "name = \"X\"\n"
        "future_field = \"ignore me\"\n"
        "designation = \"R?\"\n";
    Ref<Symbol> s = Symbol::parse(text);
    ASSERT_TRUE(s.valid());
    EXPECT_EQ(s->name(), "X");
    EXPECT_EQ(s->designation(), "R?");
    EXPECT_EQ(s->uuid(), "33333333-3333-4333-8333-333333333333");
    EXPECT_EQ(s->pin_count(), 0u);
}

// ============================================================================
// Footprint —— 构造 + 焊盘
// ============================================================================

TEST(FootprintTest, ConstructorAndClassHierarchy) {
    Footprint fp;
    EXPECT_EQ(fp.get_class(), "Footprint");
    EXPECT_EQ(Footprint::get_class_static(), "Footprint");
    EXPECT_TRUE(fp.is_class("Footprint"));
    EXPECT_TRUE(fp.is_class("Resource"));
    EXPECT_TRUE(fp.is_class("Object"));
    EXPECT_FALSE(fp.is_class("Symbol"));
    EXPECT_FALSE(fp.uuid().empty());
}

TEST(FootprintTest, AddSmdPadAndThroughHolePad) {
    Footprint fp;
    Pad smd("1", Point{0, 0}, PadType::SMD, PadShape::RECTANGLE,
            /*w=*/600'000, /*h=*/600'000, PadLayer::TOP);
    smd.set_paste_coverage(0.95);
    fp.add_pad(smd);

    Pad th("2", Point{2'540'000, 0}, PadType::THROUGH_HOLE, PadShape::CIRCLE,
           /*w=*/1'600'000, /*h=*/1'600'000, PadLayer::ALL);
    th.set_drill_diameter_nm(800'000);
    fp.add_pad(th);

    EXPECT_EQ(fp.pad_count(), 2u);
    EXPECT_EQ(fp.pad(0).type(), PadType::SMD);
    EXPECT_EQ(fp.pad(0).shape(), PadShape::RECTANGLE);
    EXPECT_EQ(fp.pad(0).layer(), PadLayer::TOP);
    EXPECT_EQ(fp.pad(0).width_nm(), 600'000);
    EXPECT_EQ(fp.pad(0).paste_coverage(), 0.95);

    EXPECT_EQ(fp.pad(1).type(), PadType::THROUGH_HOLE);
    EXPECT_EQ(fp.pad(1).layer(), PadLayer::ALL);
    EXPECT_EQ(fp.pad(1).drill_diameter_nm(), 800'000);

    ASSERT_NE(fp.find_pad_by_number("1"), nullptr);
    EXPECT_EQ(fp.find_pad_by_number("1")->shape(), PadShape::RECTANGLE);
    EXPECT_EQ(fp.find_pad_by_number("99"), nullptr);

    EXPECT_TRUE(fp.remove_pad_by_number("1"));
    EXPECT_EQ(fp.pad_count(), 1u);
    EXPECT_FALSE(fp.remove_pad_by_number("ghost"));
}

TEST(FootprintTest, CourtyardSilkscreenIpc7351Model3D) {
    Footprint fp;
    fp.set_name("SOIC8_3.9x4.9mm_P1.27mm");

    Polygon courtyard;
    courtyard.outer = {{-2'000'000, -2'500'000}, {5'900'000, -2'500'000},
                       {5'900'000, 4'900'000}, {-2'000'000, 4'900'000}};
    fp.set_courtyard(courtyard);
    EXPECT_TRUE(fp.has_courtyard());
    EXPECT_EQ(fp.courtyard().outer.size(), 4u);

    GraphicsElement silk;
    silk.kind = GraphicsKind::POLYLINE;
    silk.polyline.points = {{0, 0}, {5'000'000, 0}};
    fp.add_silkscreen(silk);
    EXPECT_EQ(fp.silkscreen_count(), 1u);

    fp.ipc7351().set_density_level("N");
    fp.ipc7351().set_footprint_name("SOIC8P127_5X6_X1P75");
    fp.ipc7351().set_body_width_nm(3'900'000);
    fp.ipc7351().set_body_length_nm(4'900'000);
    fp.ipc7351().set_courtyard_excess_nm(500'000);
    EXPECT_EQ(fp.ipc7351().density_level(), "N");
    EXPECT_EQ(fp.ipc7351().body_width_nm(), 3'900'000);

    Model3DRef m;
    m.set_path("res://3d/soic8.step");
    m.set_offset_nm(1'000'000, 2'000'000, 1'750'000);
    m.set_rotation_deg(0.0, 0.0, 90.0);
    m.set_scale(1.0);
    m.set_body_height_nm(1'750'000);
    fp.set_model_3d(m);
    EXPECT_TRUE(fp.has_model_3d());
    EXPECT_EQ(fp.model_3d().path(), "res://3d/soic8.step");
    EXPECT_EQ(fp.model_3d().offset_z_nm(), 1'750'000);
    EXPECT_EQ(fp.model_3d().rotation_z_deg(), 90.0);
    EXPECT_EQ(fp.model_3d().body_height_nm(), 1'750'000);
}

// ============================================================================
// Footprint —— .fp.toml 序列化往返
// ============================================================================

TEST(FootprintTest, SerializeRoundtripPreservesPadsAndCourtyard) {
    Footprint original;
    original.assign_uuid_for_test("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    original.set_name("0603");
    original.set_description("0603 resistor footprint");
    original.set_keywords("0603,resistor");

    Pad p1("1", Point{-900'000, 0}, PadType::SMD, PadShape::RECTANGLE,
           900'000, 800'000, PadLayer::TOP);
    p1.set_paste_coverage(0.9);
    original.add_pad(p1);
    Pad p2("2", Point{900'000, 0}, PadType::SMD, PadShape::RECTANGLE,
           900'000, 800'000, PadLayer::TOP);
    original.add_pad(p2);

    Polygon cy;
    cy.outer = {{-1'800'000, -1'000'000}, {1'800'000, -1'000'000},
                {1'800'000, 1'000'000}, {-1'800'000, 1'000'000}};
    original.set_courtyard(cy);

    original.ipc7351().set_density_level("M");
    original.ipc7351().set_body_width_nm(800'000);
    original.ipc7351().set_body_length_nm(1'600'000);
    original.ipc7351().set_lead_span_nm(1'800'000);
    original.ipc7351().set_courtyard_excess_nm(250'000);

    Model3DRef m;
    m.set_path("res://3d/0603.step");
    m.set_offset_nm(0, 0, 500'000);
    m.set_body_height_nm(500'000);
    original.set_model_3d(m);

    const std::string text = original.serialize();
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("[[pads]]"), std::string::npos);
    EXPECT_NE(text.find("[courtyard]"), std::string::npos);
    EXPECT_NE(text.find("[ipc7351]"), std::string::npos);
    EXPECT_NE(text.find("[model_3d]"), std::string::npos);

    Ref<Footprint> restored = Footprint::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original.uuid());
    EXPECT_EQ(restored->name(), "0603");
    EXPECT_EQ(restored->description(), "0603 resistor footprint");
    EXPECT_EQ(restored->keywords(), "0603,resistor");

    EXPECT_EQ(restored->pad_count(), 2u);
    EXPECT_EQ(restored->pad(0).number(), "1");
    EXPECT_EQ(restored->pad(0).width_nm(), 900'000);
    EXPECT_EQ(restored->pad(0).height_nm(), 800'000);
    EXPECT_NEAR(restored->pad(0).paste_coverage(), 0.9, 1e-9);
    EXPECT_EQ(restored->pad(1).number(), "2");
    EXPECT_EQ(restored->pad(1).position().x, 900'000);

    EXPECT_TRUE(restored->has_courtyard());
    EXPECT_EQ(restored->courtyard().outer.size(), 4u);
    EXPECT_EQ(restored->courtyard().outer[0].x, -1'800'000);

    EXPECT_EQ(restored->ipc7351().density_level(), "M");
    EXPECT_EQ(restored->ipc7351().body_width_nm(), 800'000);
    EXPECT_EQ(restored->ipc7351().body_length_nm(), 1'600'000);
    EXPECT_EQ(restored->ipc7351().lead_span_nm(), 1'800'000);
    EXPECT_EQ(restored->ipc7351().courtyard_excess_nm(), 250'000);

    EXPECT_TRUE(restored->has_model_3d());
    EXPECT_EQ(restored->model_3d().path(), "res://3d/0603.step");
    EXPECT_EQ(restored->model_3d().offset_z_nm(), 500'000);
    EXPECT_EQ(restored->model_3d().body_height_nm(), 500'000);
}

TEST(FootprintTest, SerializeWithoutOptionalSections) {
    // 最小封装：无 courtyard、无 3D、无 silkscreen —— schema 顶层与 ipc7351 仍输出
    Footprint fp;
    fp.assign_uuid_for_test("00000000-0000-4000-8000-000000000000");
    fp.set_name("Empty");

    const std::string text = fp.serialize();
    EXPECT_EQ(text.find("[courtyard]"), std::string::npos);
    EXPECT_EQ(text.find("[model_3d]"), std::string::npos);
    EXPECT_NE(text.find("[ipc7351]"), std::string::npos);  // 始终输出

    Ref<Footprint> restored = Footprint::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_FALSE(restored->has_courtyard());
    EXPECT_FALSE(restored->has_model_3d());
    EXPECT_EQ(restored->pad_count(), 0u);
}

// ============================================================================
// Device —— 一体化绑定
// ============================================================================

TEST(DeviceTest, ConstructorAndClassHierarchy) {
    Device d;
    EXPECT_EQ(d.get_class(), "Device");
    EXPECT_EQ(Device::get_class_static(), "Device");
    EXPECT_TRUE(d.is_class("Device"));
    EXPECT_TRUE(d.is_class("Resource"));
    EXPECT_TRUE(d.is_class("Object"));
    EXPECT_FALSE(d.is_class("Symbol"));
    EXPECT_FALSE(d.has_symbol());
    EXPECT_FALSE(d.has_footprint());
    EXPECT_FALSE(d.has_model_3d());
    EXPECT_FALSE(d.uuid().empty());
}

TEST(DeviceTest, BindSymbolFootprintModel3D) {
    Device d;
    d.bind_symbol("res://lib/opamp.sym.toml");
    d.bind_footprint("res://lib/soic8.fp.toml");
    d.bind_model_3d("res://3d/soic8.step");

    EXPECT_TRUE(d.has_symbol());
    EXPECT_EQ(d.symbol_ref(), "res://lib/opamp.sym.toml");
    EXPECT_TRUE(d.has_footprint());
    EXPECT_EQ(d.footprint_ref(), "res://lib/soic8.fp.toml");
    EXPECT_TRUE(d.has_model_3d());
    EXPECT_EQ(d.model_3d_ref(), "res://3d/soic8.step");

    d.unbind_symbol();
    EXPECT_FALSE(d.has_symbol());
    d.unbind_footprint();
    EXPECT_FALSE(d.has_footprint());
    d.unbind_model_3d();
    EXPECT_FALSE(d.has_model_3d());
}

TEST(DeviceTest, PinMapSymbolToFootprint) {
    Device d;
    d.map_pin("1", "1");   // symbol pin "1" → footprint pad "1"
    d.map_pin("2", "2");
    d.map_pin("NC", "8");  // 多 gate 重编号

    ASSERT_NE(d.mapped_pad("1"), nullptr);
    EXPECT_EQ(*d.mapped_pad("1"), "1");
    ASSERT_NE(d.mapped_pad("NC"), nullptr);
    EXPECT_EQ(*d.mapped_pad("NC"), "8");
    EXPECT_EQ(d.mapped_pad("99"), nullptr);

    EXPECT_TRUE(d.unmap_pin("1"));
    EXPECT_EQ(d.mapped_pad("1"), nullptr);
    EXPECT_FALSE(d.unmap_pin("never"));
}

// ============================================================================
// Device —— 参数（REQ-010 六字段 Property schema）
// ============================================================================

TEST(DeviceTest, PropertySixFieldSchema) {
    Device d;
    Property r("resistance", Variant(10000.0), "resistance", "Ω", true, true);
    Property tol("tolerance", Variant(0.01), "ratio", "%", false, true);
    Property pkg("package", Variant(std::string("0603")), "string", "", true, false);
    d.add_property(r);
    d.add_property(tol);
    d.add_property(pkg);

    EXPECT_EQ(d.property_count(), 3u);

    ASSERT_NE(d.find_property("resistance"), nullptr);
    EXPECT_EQ(d.find_property("resistance")->unit(), "Ω");
    EXPECT_TRUE(d.find_property("resistance")->visible());
    EXPECT_TRUE(d.find_property("resistance")->editable());
    EXPECT_EQ(d.find_property("resistance")->value().get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(d.find_property("resistance")->value().as_float(), 10000.0);

    ASSERT_NE(d.find_property("package"), nullptr);
    EXPECT_FALSE(d.find_property("package")->editable());
    EXPECT_EQ(d.find_property("package")->value().as_string(), "0603");

    EXPECT_EQ(d.find_property("nonexistent"), nullptr);
    EXPECT_TRUE(d.remove_property("tolerance"));
    EXPECT_FALSE(d.remove_property("nonexistent"));
    EXPECT_EQ(d.property_count(), 2u);
}

TEST(DeviceTest, SpiceAndSupplyChainPlaceholders) {
    Device d;
    SpiceModel s;
    s.set_model_ref("res://spice/lm358.sub");
    s.set_model_type("SUBCKT");
    s.set_subckt_name("LM358");
    s.set_enabled(true);
    d.set_spice(s);

    EXPECT_TRUE(d.has_spice());
    EXPECT_EQ(d.spice().model_ref(), "res://spice/lm358.sub");
    EXPECT_EQ(d.spice().subckt_name(), "LM358");

    SupplyChainInfo sc;
    sc.set_mpn("LM358DR");
    sc.set_manufacturer("Texas Instruments");
    sc.set_supplier("LCSC");
    sc.set_supplier_part_number("C6551");
    sc.set_unit_price(0.42);
    sc.set_stock(20000);
    sc.set_lifecycle("active");
    sc.set_rohs(true);
    d.set_supply_chain(sc);

    EXPECT_TRUE(d.has_supply_chain());
    EXPECT_EQ(d.supply_chain().mpn(), "LM358DR");
    EXPECT_EQ(d.supply_chain().manufacturer(), "Texas Instruments");
    EXPECT_EQ(d.supply_chain().supplier(), "LCSC");
    EXPECT_DOUBLE_EQ(d.supply_chain().unit_price(), 0.42);
    EXPECT_EQ(d.supply_chain().stock(), 20000);
    EXPECT_TRUE(d.supply_chain().rohs());

    // 无 MPN：has_supply_chain 为 false
    Device empty;
    EXPECT_FALSE(empty.has_supply_chain());
}

TEST(DeviceTest, Equivalents) {
    Device d;
    d.add_equivalent("C6551");
    d.add_equivalent("C1234");
    EXPECT_EQ(d.equivalents().size(), 2u);
    // 去重
    d.add_equivalent("C6551");
    EXPECT_EQ(d.equivalents().size(), 2u);
    EXPECT_TRUE(d.remove_equivalent("C1234"));
    EXPECT_EQ(d.equivalents().size(), 1u);
    EXPECT_FALSE(d.remove_equivalent("never"));
}

// ============================================================================
// Device —— .dev.toml 序列化往返
// ============================================================================

TEST(DeviceTest, SerializeRoundtripPreservesBindingsAndProperties) {
    Device original;
    original.assign_uuid_for_test("deadbeef-1234-4abc-8def-567890abcdef");
    original.set_name("LM358");
    original.set_description("Dual Op-Amp");
    original.set_category("OpAmp");
    original.set_keywords("opamp,dual,rail-to-rail");
    original.set_datasheet_url("https://example.com/lm358.pdf");

    original.bind_symbol("res://lib/opamp.sym.toml");
    original.bind_footprint("res://lib/soic8.fp.toml");
    original.bind_model_3d("res://3d/soic8.step");

    original.map_pin("1", "1");
    original.map_pin("8", "8");

    original.add_property(Property("resistance", Variant(std::string("N/A")),
                                   "resistance", "Ω", true, false));
    original.add_property(Property("supply_voltage", Variant(5.0),
                                   "voltage", "V", true, true));

    SpiceModel sm;
    sm.set_model_ref("res://spice/lm358.sub");
    sm.set_subckt_name("LM358");
    original.set_spice(sm);

    SupplyChainInfo sc;
    sc.set_mpn("LM358DR");
    sc.set_manufacturer("TI");
    sc.set_supplier("LCSC");
    sc.set_unit_price(0.42);
    sc.set_stock(20000);
    sc.set_lifecycle("active");
    sc.set_rohs(true);
    original.set_supply_chain(sc);

    original.add_equivalent("LM358P");

    const std::string text = original.serialize();
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("[binding]"), std::string::npos);
    EXPECT_NE(text.find("[pin_map]"), std::string::npos);
    EXPECT_NE(text.find("[[properties]]"), std::string::npos);
    EXPECT_NE(text.find("[spice]"), std::string::npos);
    EXPECT_NE(text.find("[supply_chain]"), std::string::npos);
    EXPECT_NE(text.find("equivalents"), std::string::npos);

    Ref<Device> restored = Device::parse(text);
    ASSERT_TRUE(restored.valid());

    EXPECT_EQ(restored->uuid(), original.uuid());
    EXPECT_EQ(restored->name(), "LM358");
    EXPECT_EQ(restored->description(), "Dual Op-Amp");
    EXPECT_EQ(restored->category(), "OpAmp");
    EXPECT_EQ(restored->keywords(), "opamp,dual,rail-to-rail");
    EXPECT_EQ(restored->datasheet_url(), "https://example.com/lm358.pdf");

    EXPECT_EQ(restored->symbol_ref(), "res://lib/opamp.sym.toml");
    EXPECT_EQ(restored->footprint_ref(), "res://lib/soic8.fp.toml");
    EXPECT_EQ(restored->model_3d_ref(), "res://3d/soic8.step");

    ASSERT_NE(restored->mapped_pad("1"), nullptr);
    EXPECT_EQ(*restored->mapped_pad("1"), "1");
    ASSERT_NE(restored->mapped_pad("8"), nullptr);
    EXPECT_EQ(*restored->mapped_pad("8"), "8");

    EXPECT_EQ(restored->property_count(), 2u);
    // 按 name 字典序："resistance" < "supply_voltage"
    EXPECT_EQ(restored->property(0).name(), "resistance");
    EXPECT_EQ(restored->property(0).unit(), "Ω");
    EXPECT_FALSE(restored->property(0).editable());
    EXPECT_EQ(restored->property(0).value().as_string(), "N/A");
    EXPECT_EQ(restored->property(1).name(), "supply_voltage");
    EXPECT_DOUBLE_EQ(restored->property(1).value().as_float(), 5.0);

    EXPECT_EQ(restored->spice().model_ref(), "res://spice/lm358.sub");
    EXPECT_EQ(restored->spice().subckt_name(), "LM358");

    EXPECT_EQ(restored->supply_chain().mpn(), "LM358DR");
    EXPECT_EQ(restored->supply_chain().manufacturer(), "TI");
    EXPECT_EQ(restored->supply_chain().supplier(), "LCSC");
    EXPECT_DOUBLE_EQ(restored->supply_chain().unit_price(), 0.42);
    EXPECT_EQ(restored->supply_chain().stock(), 20000);
    EXPECT_TRUE(restored->supply_chain().rohs());

    ASSERT_EQ(restored->equivalents().size(), 1u);
    EXPECT_EQ(restored->equivalents()[0], "LM358P");
}

TEST(DeviceTest, SerializeMinimalDeviceWithoutOptionalSections) {
    Device d;
    d.assign_uuid_for_test("cafecafe-cafe-4caf-8caf-cafecafecafe");
    d.set_name("Bare");
    // 无 symbol/footprint/3D、无 pin_map、无 properties、无 spice、无 supply_chain、无 equivalents

    const std::string text = d.serialize();
    // [binding] 始终输出（schema 完整性），其余可选段全部省略
    EXPECT_NE(text.find("[binding]"), std::string::npos);
    EXPECT_EQ(text.find("[pin_map]"), std::string::npos);
    EXPECT_EQ(text.find("[[properties]]"), std::string::npos);
    EXPECT_EQ(text.find("[spice]"), std::string::npos);
    EXPECT_EQ(text.find("[supply_chain]"), std::string::npos);
    EXPECT_EQ(text.find("equivalents"), std::string::npos);

    Ref<Device> restored = Device::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->name(), "Bare");
    EXPECT_FALSE(restored->has_symbol());
    EXPECT_FALSE(restored->has_footprint());
    EXPECT_FALSE(restored->has_spice());
    EXPECT_FALSE(restored->has_supply_chain());
}

// ============================================================================
// UUID 稳定性 —— 跨序列化往返保持
// ============================================================================

TEST(UuidStabilityTest, SymbolUuidSurvivesMultipleRoundtrips) {
    Symbol s;
    s.set_name("X");
    const std::string original_uuid = s.uuid();
    ASSERT_FALSE(original_uuid.empty());

    std::string text = s.serialize();
    for (int i = 0; i < 3; ++i) {
        Ref<Symbol> restored = Symbol::parse(text);
        ASSERT_TRUE(restored.valid());
        EXPECT_EQ(restored->uuid(), original_uuid)
            << "UUID drifted after roundtrip " << i;
        text = restored->serialize();
    }
}

TEST(UuidStabilityTest, FootprintUuidSurvivesRoundtrip) {
    Footprint fp;
    fp.set_name("X");
    const std::string original_uuid = fp.uuid();

    Ref<Footprint> restored = Footprint::parse(fp.serialize());
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original_uuid);
}

TEST(UuidStabilityTest, DeviceUuidSurvivesRoundtrip) {
    Device d;
    d.set_name("X");
    const std::string original_uuid = d.uuid();

    Ref<Device> restored = Device::parse(d.serialize());
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original_uuid);
}

TEST(UuidStabilityTest, InjectedUuidPersistsAcrossSerializeDeserialize) {
    Symbol s;
    s.assign_uuid_for_test("12345678-1234-4abc-8def-aaaaaaaaaaaa");
    EXPECT_EQ(s.uuid(), "12345678-1234-4abc-8def-aaaaaaaaaaaa");

    Ref<Symbol> restored = Symbol::parse(s.serialize());
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), "12345678-1234-4abc-8def-aaaaaaaaaaaa");
}

// ============================================================================
// 集成：Device + Symbol + Footprint 协作（res:// 引用）
// ============================================================================

TEST(IntegrationTest, DeviceBindsSymbolAndFootprintByReference) {
    // 创建 Symbol、Footprint 并设置 path（res://）
    Ref<Symbol> sym(new Symbol());
    sym->set_name("LM358");
    sym->set_path("res://lib/opamp.sym.toml");
    sym->add_pin(Pin("1", "OUTA", Point{0, 0}, ElectricalType::OUTPUT));

    Ref<Footprint> fp(new Footprint());
    fp->set_name("SOIC8");
    fp->set_path("res://lib/soic8.fp.toml");
    fp->add_pad(Pad("1", Point{0, 0}, PadType::SMD, PadShape::RECTANGLE,
                    600'000, 600'000, PadLayer::TOP));

    // Device 通过 res:// 引用绑定
    Ref<Device> d(new Device());
    d->set_name("LM358");
    d->bind_symbol(sym->get_path());
    d->bind_footprint(fp->get_path());

    // 三表对齐：symbol pin "1" ↔ footprint pad "1"
    d->map_pin("1", "1");

    EXPECT_EQ(d->symbol_ref(), sym->get_path());
    EXPECT_EQ(d->footprint_ref(), fp->get_path());
    ASSERT_NE(d->mapped_pad("1"), nullptr);
    EXPECT_EQ(*d->mapped_pad("1"), "1");

    // 序列化往返保持绑定
    Ref<Device> restored = Device::parse(d->serialize());
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->symbol_ref(), "res://lib/opamp.sym.toml");
    EXPECT_EQ(restored->footprint_ref(), "res://lib/soic8.fp.toml");
    EXPECT_EQ(*restored->mapped_pad("1"), "1");
}

TEST(IntegrationTest, ResourcePolymorphismViaRefBase) {
    // Symbol/Footprint/Device 都能放进 Ref<Resource> 容器
    Ref<Resource> resources[] = {
        Ref<Resource>(new Symbol()),
        Ref<Resource>(new Footprint()),
        Ref<Resource>(new Device()),
    };
    EXPECT_EQ(resources[0]->get_class(), "Symbol");
    EXPECT_EQ(resources[1]->get_class(), "Footprint");
    EXPECT_EQ(resources[2]->get_class(), "Device");
    for (Ref<Resource>& r : resources) {
        EXPECT_TRUE(r->is_class("Resource"));
        EXPECT_TRUE(r->is_class("RefCounted"));
        EXPECT_TRUE(r->is_class("Object"));
    }
}
