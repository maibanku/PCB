// tests/test_schematic.cpp
//
// P7 原理图核心数据模型测试（Schematic + Component + Wire + NetLabel + PowerPort + Netlist）。
//
// 覆盖：
//   - Component：构造 + UUID + refdes + 旋转（0/90/180/270）+ 属性覆盖 + Symbol 引用
//                + connection_points 旋转变换 + 序列化往返
//   - Wire：构造 + 端点 + 线宽 + degenerate/has_endpoint + net_ref 可选
//   - NetLabel：构造 + 方向枚举映射
//   - PowerPort：构造 + kind 枚举映射
//   - Schematic：增删 Component（按 UUID/refdes 查找）+ Wire/NetLabel/PowerPort 管理 + notes
//   - generate_netlist：基本连通性（两器件+导线→同网络）/ NetLabel 命名 / PowerPort 命名 /
//                       自动命名 / 浮空引脚独立网络 / 多 Pin 多网
//   - 序列化往返：UUID 稳定、字段保真
//
// 依赖：eda_schematic（eda_core + eda_geometry + eda_library 传递）
// 注意：本文件不改 CMakeLists；集成步骤由主循环完成（追加到 tests/CMakeLists.txt）：
//   add_executable(test_schematic test_schematic.cpp)
//   target_link_libraries(test_schematic PRIVATE eda_schematic gtest_main)
//   gtest_discover_tests(test_schematic)

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/object/ref.h"
#include "eda/geometry/types.h"
#include "eda/library/symbol.h"
#include "eda/schematic/component.h"
#include "eda/schematic/net_label.h"
#include "eda/schematic/power_port.h"
#include "eda/schematic/schematic.h"
#include "eda/schematic/wire.h"

using namespace eda;
using namespace eda::geometry;
using namespace eda::sch;

namespace {

// 构造一个简单电阻符号：N 个引脚，每个引脚水平排列在 local 坐标。
// 引脚号从 1 开始，引脚位置 = (pin_index - 1) * pitch，沿 +x 排开。
Ref<Symbol> make_resistor_symbol(int pin_count, Coord pitch_nm = 2'000'000,
                                  const std::string& designation = "R?") {
    Ref<Symbol> sym(new Symbol());
    sym->set_name("Resistor");
    sym->set_designation(designation);
    for (int i = 0; i < pin_count; ++i) {
        std::string num = std::to_string(i + 1);
        Point local{(i - (pin_count - 1) / 2.0) * pitch_nm, 0};
        sym->add_pin(Pin(num, ("P" + num), local, ElectricalType::PASSIVE,
                          Orientation::RIGHT, 0));
    }
    return sym;
}

}  // namespace

// ============================================================================
// Component —— 构造 / UUID / refdes / 属性覆盖
// ============================================================================

TEST(ComponentTest, ConstructorGeneratesValidUuid) {
    Component c;
    EXPECT_EQ(c.get_class(), "Component");
    EXPECT_EQ(Component::get_class_static(), "Component");
    EXPECT_TRUE(c.is_class("Component"));
    EXPECT_TRUE(c.is_class("Resource"));
    EXPECT_TRUE(c.is_class("RefCounted"));
    EXPECT_TRUE(c.is_class("Object"));
    EXPECT_FALSE(c.is_class("Schematic"));
    EXPECT_FALSE(c.uuid().empty());
    EXPECT_EQ(c.uuid().size(), 36u);  // UUID v4 字符串长度
}

TEST(ComponentTest, UniqueUuidsAcrossInstances) {
    Component a, b;
    EXPECT_NE(a.uuid(), b.uuid());
}

TEST(ComponentTest, RefdesAndAttributeManagement) {
    Component c;
    c.set_refdes("R1");
    EXPECT_EQ(c.refdes(), "R1");

    c.set_attribute("value", "10kΩ");
    c.set_attribute("tolerance", "1%");
    EXPECT_EQ(c.attributes().size(), 2u);
    ASSERT_NE(c.find_attribute("value"), nullptr);
    EXPECT_EQ(*c.find_attribute("value"), "10kΩ");

    // 覆盖已有属性
    c.set_attribute("value", "4.7kΩ");
    EXPECT_EQ(*c.find_attribute("value"), "4.7kΩ");
    EXPECT_EQ(c.attributes().size(), 2u);

    EXPECT_TRUE(c.remove_attribute("value"));
    EXPECT_EQ(c.find_attribute("value"), nullptr);
    EXPECT_EQ(c.attributes().size(), 1u);
    EXPECT_FALSE(c.remove_attribute("nonexistent"));
}

TEST(ComponentTest, ResourceInheritanceHoldsByRef) {
    Ref<Component> comp(new Component());
    comp->set_refdes("U1");
    Ref<Resource> as_resource = comp;
    EXPECT_EQ(as_resource->get_class(), "Component");
    EXPECT_TRUE(as_resource->is_class("Resource"));
    EXPECT_TRUE(as_resource->get_path().empty());
    as_resource->set_path("res://sheets/main.sch.toml");
    EXPECT_EQ(comp->get_path(), "res://sheets/main.sch.toml");
}

// ============================================================================
// Component —— 旋转 / 坐标变换
// ============================================================================

TEST(ComponentTest, RotationValidatesOnlyFourAngles) {
    Component c;
    EXPECT_TRUE(c.set_rotation_degrees(0));
    EXPECT_EQ(c.rotation_degrees(), 0);
    EXPECT_TRUE(c.set_rotation_degrees(90));
    EXPECT_EQ(c.rotation_degrees(), 90);
    EXPECT_TRUE(c.set_rotation_degrees(180));
    EXPECT_EQ(c.rotation_degrees(), 180);
    EXPECT_TRUE(c.set_rotation_degrees(270));
    EXPECT_EQ(c.rotation_degrees(), 270);

    // 非法角度不应改变
    EXPECT_FALSE(c.set_rotation_degrees(45));
    EXPECT_EQ(c.rotation_degrees(), 270);
    EXPECT_FALSE(c.set_rotation_degrees(-90));
    EXPECT_EQ(c.rotation_degrees(), 270);
}

TEST(ComponentTest, RotationEnumMappingRoundtrip) {
    EXPECT_EQ(to_degrees(Rotation::DEG_0), 0);
    EXPECT_EQ(to_degrees(Rotation::DEG_90), 90);
    EXPECT_EQ(to_degrees(Rotation::DEG_180), 180);
    EXPECT_EQ(to_degrees(Rotation::DEG_270), 270);

    EXPECT_EQ(rotation_from_degrees(0), Rotation::DEG_0);
    EXPECT_EQ(rotation_from_degrees(90), Rotation::DEG_90);
    EXPECT_EQ(rotation_from_degrees(180), Rotation::DEG_180);
    EXPECT_EQ(rotation_from_degrees(270), Rotation::DEG_270);
    // 非法值回落 DEG_0
    EXPECT_EQ(rotation_from_degrees(45), Rotation::DEG_0);

    EXPECT_EQ(to_string_view(Rotation::DEG_90), "90");
    EXPECT_EQ(parse_rotation("90"), Rotation::DEG_90);
    EXPECT_EQ(parse_rotation("invalid"), Rotation::DEG_0);
}

TEST(ComponentTest, TransformLocalToWorldAllRotations) {
    const Point origin{10'000'000, 20'000'000};
    const Point local{1'000'000, 0};  // 局部 +x 方向

    // 0°: 不变 + 平移
    EXPECT_EQ(Component::transform_local_to_world(local, origin, Rotation::DEG_0),
              (Point{11'000'000, 20'000'000}));
    // 90° CCW: (x,y) -> (-y, x) = (0, 1_000_000)
    EXPECT_EQ(Component::transform_local_to_world(local, origin, Rotation::DEG_90),
              (Point{10'000'000, 21'000'000}));
    // 180°: (x,y) -> (-x, -y)
    EXPECT_EQ(Component::transform_local_to_world(local, origin, Rotation::DEG_180),
              (Point{9'000'000, 20'000'000}));
    // 270° CCW: (x,y) -> (y, -x)
    EXPECT_EQ(Component::transform_local_to_world(local, origin, Rotation::DEG_270),
              (Point{10'000'000, 19'000'000}));
}

// ============================================================================
// Component —— Symbol 引用 + 引脚连接点
// ============================================================================

TEST(ComponentTest, SetSymbolFromResourceBackfillsPath) {
    Ref<Symbol> sym = make_resistor_symbol(2);
    sym->set_path("res://lib/resistor.sym.toml");

    Component c;
    c.set_symbol(sym);
    EXPECT_TRUE(c.has_symbol());
    EXPECT_TRUE(c.symbol().valid());
    EXPECT_EQ(c.symbol_ref(), "res://lib/resistor.sym.toml");
}

TEST(ComponentTest, SetSymbolRefAloneDoesNotPopulateInMemory) {
    Component c;
    c.set_symbol_ref("res://lib/resistor.sym.toml");
    EXPECT_FALSE(c.has_symbol());       // in-memory Ref 仍空
    EXPECT_EQ(c.symbol_ref(), "res://lib/resistor.sym.toml");
    EXPECT_TRUE(c.connection_points().empty());  // 无 Symbol 无法计算
}

TEST(ComponentTest, ConnectionPointsUnrotatedComponent) {
    Ref<Symbol> sym = make_resistor_symbol(2, 2'000'000);
    // pins: pin "1" at (-1_000_000, 0), pin "2" at (1_000_000, 0)

    Component c;
    c.set_symbol(sym);
    c.set_position(Point{10'000'000, 10'000'000});
    c.set_refdes("R1");

    auto cps = c.connection_points();
    ASSERT_EQ(cps.size(), 2u);
    // 不假设 vector 顺序与 Symbol pin 顺序一致——按 pin_number 查找
    const ConnectionPoint* cp1 = nullptr;
    const ConnectionPoint* cp2 = nullptr;
    for (const auto& cp : cps) {
        if (cp.pin_number == "1") cp1 = &cp;
        if (cp.pin_number == "2") cp2 = &cp;
    }
    ASSERT_NE(cp1, nullptr);
    ASSERT_NE(cp2, nullptr);
    EXPECT_EQ(cp1->position, (Point{9'000'000, 10'000'000}));
    EXPECT_EQ(cp2->position, (Point{11'000'000, 10'000'000}));

    // pin_position 单点查询
    auto p1 = c.pin_position("1");
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(*p1, (Point{9'000'000, 10'000'000}));
    EXPECT_FALSE(c.pin_position("999").has_value());
}

TEST(ComponentTest, ConnectionPointsRotated90Component) {
    Ref<Symbol> sym = make_resistor_symbol(2, 2'000'000);

    Component c;
    c.set_symbol(sym);
    c.set_position(Point{10'000'000, 10'000'000});
    c.set_rotation(Rotation::DEG_90);

    auto cps = c.connection_points();
    ASSERT_EQ(cps.size(), 2u);
    // 90° CCW: pin "1" local(-1e6, 0) -> world (10e6, 9e6)；pin "2" local(1e6, 0) -> world (10e6, 11e6)
    for (const auto& cp : cps) {
        if (cp.pin_number == "1") {
            EXPECT_EQ(cp.position, (Point{10'000'000, 9'000'000}));
        } else if (cp.pin_number == "2") {
            EXPECT_EQ(cp.position, (Point{10'000'000, 11'000'000}));
        }
    }
}

TEST(ComponentTest, ConnectionPointsEmptyWithoutSymbol) {
    Component c;
    EXPECT_TRUE(c.connection_points().empty());
}

// ============================================================================
// Component —— 序列化往返
// ============================================================================

TEST(ComponentTest, SerializeRoundtripPreservesFields) {
    Component original;
    original.assign_uuid_for_test("22222222-2222-4222-8222-222222222222");
    original.set_refdes("U3");
    original.set_symbol_ref("res://lib/opamp.sym.toml");
    original.set_device_ref("res://lib/lm358.dev.toml");
    original.set_position(Point{-5'000'000, 7'000'000});
    original.set_rotation(Rotation::DEG_270);
    original.set_attribute("value", "LM358");
    original.set_attribute("tolerance", "1%");

    std::string text = original.serialize();
    ASSERT_FALSE(text.empty());
    // 注意：Component::serialize 自身输出是 standalone（schema_version + 字段），
    // 不含 [[components]] 段头（段头仅在 Schematic::serialize 中包裹）。
    EXPECT_NE(text.find("schema_version = 1"), std::string::npos);
    EXPECT_NE(text.find("refdes = \"U3\""), std::string::npos);
    EXPECT_NE(text.find("res://lib/opamp.sym.toml"), std::string::npos);
    EXPECT_NE(text.find("[attributes]"), std::string::npos);  // 属性子表

    Ref<Component> restored = Component::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original.uuid());
    EXPECT_EQ(restored->refdes(), "U3");
    EXPECT_EQ(restored->symbol_ref(), "res://lib/opamp.sym.toml");
    EXPECT_EQ(restored->device_ref(), "res://lib/lm358.dev.toml");
    EXPECT_EQ(restored->position(), (Point{-5'000'000, 7'000'000}));
    EXPECT_EQ(restored->rotation(), Rotation::DEG_270);
    EXPECT_EQ(restored->attributes().size(), 2u);
    ASSERT_NE(restored->find_attribute("value"), nullptr);
    EXPECT_EQ(*restored->find_attribute("value"), "LM358");
    ASSERT_NE(restored->find_attribute("tolerance"), nullptr);
    EXPECT_EQ(*restored->find_attribute("tolerance"), "1%");
}

TEST(ComponentTest, UuidStableAcrossRoundtrip) {
    Component original;
    original.assign_uuid_for_test("deadbeef-dead-4dead-beef-deadbeefdead");
    original.set_refdes("R5");

    std::string text = original.serialize();
    Ref<Component> restored = Component::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), "deadbeef-dead-4dead-beef-deadbeefdead");
}

// ============================================================================
// Wire —— 构造 + 端点 + 线宽 + degenerate
// ============================================================================

TEST(WireTest, ConstructorAndAccessors) {
    Wire w(Point{1'000'000, 2'000'000}, Point{3'000'000, 4'000'000});
    EXPECT_EQ(w.a(), (Point{1'000'000, 2'000'000}));
    EXPECT_EQ(w.b(), (Point{3'000'000, 4'000'000}));
    EXPECT_GT(w.width_nm(), 0);  // 默认 152'400

    w.set_width_nm(200'000);
    EXPECT_EQ(w.width_nm(), 200'000);

    w.set_a(Point{0, 0});
    EXPECT_EQ(w.a(), (Point{0, 0}));
}

TEST(WireTest, DegenerateAndEndpointChecks) {
    Wire w(Point{5'000'000, 5'000'000}, Point{5'000'000, 5'000'000});
    EXPECT_TRUE(w.is_degenerate());

    Wire w2(Point{0, 0}, Point{1'000'000, 0});
    EXPECT_FALSE(w2.is_degenerate());
    EXPECT_TRUE(w2.has_endpoint(Point{0, 0}));
    EXPECT_TRUE(w2.has_endpoint(Point{1'000'000, 0}));
    EXPECT_FALSE(w2.has_endpoint(Point{500'000, 0}));  // 中点不算端点（首版仅端点连通）
}

TEST(WireTest, NetRefOptional) {
    Wire w(Point{0, 0}, Point{1'000'000, 0});
    EXPECT_FALSE(w.has_net_ref());

    w.set_net_ref("SDA");
    EXPECT_TRUE(w.has_net_ref());
    EXPECT_EQ(w.net_ref(), "SDA");
}

TEST(WireTest, ValueEquality) {
    Wire a(Point{0, 0}, Point{1'000'000, 0});
    Wire b(Point{0, 0}, Point{1'000'000, 0});
    Wire c(Point{0, 0}, Point{2'000'000, 0});
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// NetLabel —— 构造 + 方向枚举
// ============================================================================

TEST(NetLabelTest, ConstructorAndAccessors) {
    NetLabel lbl("SDA", Point{1'000'000, 2'000'000});
    EXPECT_EQ(lbl.net_name(), "SDA");
    EXPECT_EQ(lbl.position(), (Point{1'000'000, 2'000'000}));
    EXPECT_EQ(lbl.direction(), LabelDirection::HORIZONTAL);

    lbl.set_direction(LabelDirection::VERTICAL);
    EXPECT_EQ(lbl.direction(), LabelDirection::VERTICAL);
    lbl.set_net_name("SCL");
    EXPECT_EQ(lbl.net_name(), "SCL");
}

TEST(NetLabelTest, DirectionEnumMapping) {
    EXPECT_EQ(to_string_view(LabelDirection::HORIZONTAL), "horizontal");
    EXPECT_EQ(to_string_view(LabelDirection::VERTICAL), "vertical");
    EXPECT_EQ(parse_label_direction("horizontal"), LabelDirection::HORIZONTAL);
    EXPECT_EQ(parse_label_direction("vertical"), LabelDirection::VERTICAL);
    EXPECT_EQ(parse_label_direction("garbage"), LabelDirection::HORIZONTAL);  // 默认
}

// ============================================================================
// PowerPort —— 构造 + kind 枚举
// ============================================================================

TEST(PowerPortTest, ConstructorAndAccessors) {
    PowerPort pp("VCC", Point{0, 0});
    EXPECT_EQ(pp.net_name(), "VCC");
    EXPECT_EQ(pp.kind(), PowerPortKind::POWER);

    pp.set_kind(PowerPortKind::GROUND);
    EXPECT_EQ(pp.kind(), PowerPortKind::GROUND);
}

TEST(PowerPortTest, KindEnumMapping) {
    EXPECT_EQ(to_string_view(PowerPortKind::POWER), "power");
    EXPECT_EQ(to_string_view(PowerPortKind::GROUND), "ground");
    EXPECT_EQ(to_string_view(PowerPortKind::OTHER), "other");
    EXPECT_EQ(parse_power_port_kind("power"), PowerPortKind::POWER);
    EXPECT_EQ(parse_power_port_kind("ground"), PowerPortKind::GROUND);
    EXPECT_EQ(parse_power_port_kind("other"), PowerPortKind::OTHER);
    EXPECT_EQ(parse_power_port_kind("???"), PowerPortKind::POWER);  // 默认
}

// ============================================================================
// Schematic —— 容器管理
// ============================================================================

TEST(SchematicTest, ConstructorAndIdentity) {
    Schematic s;
    EXPECT_EQ(s.get_class(), "Schematic");
    EXPECT_TRUE(s.is_class("Schematic"));
    EXPECT_TRUE(s.is_class("Resource"));
    EXPECT_FALSE(s.uuid().empty());
    EXPECT_EQ(s.uuid().size(), 36u);

    s.set_name("Main Sheet");
    EXPECT_EQ(s.name(), "Main Sheet");
}

TEST(SchematicTest, AddRemoveComponents) {
    Schematic s;
    Component c1;
    c1.assign_uuid_for_test("aaaa1111-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    c1.set_refdes("R1");
    Component c2;
    c2.assign_uuid_for_test("bbbb2222-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    c2.set_refdes("R2");

    s.add_component(std::move(c1));
    s.add_component(std::move(c2));
    EXPECT_EQ(s.component_count(), 2u);

    // 按 UUID 查找
    ASSERT_NE(s.find_component_by_uuid("aaaa1111-aaaa-4aaa-8aaa-aaaaaaaaaaaa"), nullptr);
    EXPECT_EQ(s.find_component_by_uuid("aaaa1111-aaaa-4aaa-8aaa-aaaaaaaaaaaa")->refdes(), "R1");
    EXPECT_EQ(s.find_component_by_uuid("nonexistent"), nullptr);

    // 按 refdes 查找
    ASSERT_NE(s.find_component_by_refdes("R2"), nullptr);
    EXPECT_EQ(s.find_component_by_refdes("R2")->uuid(),
              "bbbb2222-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    EXPECT_EQ(s.find_component_by_refdes("R99"), nullptr);

    // 通用 find_by_uuid / remove_by_uuid
    ASSERT_NE(s.find_by_uuid("aaaa1111-aaaa-4aaa-8aaa-aaaaaaaaaaaa"), nullptr);
    EXPECT_TRUE(s.remove_by_uuid("aaaa1111-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    EXPECT_EQ(s.component_count(), 1u);
    EXPECT_FALSE(s.remove_by_uuid("nonexistent"));
}

TEST(SchematicTest, AddWiresLabelsPortsNotes) {
    Schematic s;
    s.add_wire(Wire(Point{0, 0}, Point{1'000'000, 0}));
    s.add_wire(Wire(Point{2'000'000, 0}, Point{3'000'000, 0}));
    s.add_net_label(NetLabel("SDA", Point{0, 0}));
    s.add_power_port(PowerPort("VCC", Point{1'000'000, 1'000'000}));
    s.add_note("Test note");
    s.add_note("Another note");

    EXPECT_EQ(s.wire_count(), 2u);
    EXPECT_EQ(s.net_label_count(), 1u);
    EXPECT_EQ(s.power_port_count(), 1u);
    EXPECT_EQ(s.notes().size(), 2u);

    EXPECT_EQ(s.wire(0).a(), (Point{0, 0}));
    EXPECT_EQ(s.net_label(0).net_name(), "SDA");
    EXPECT_EQ(s.power_port(0).net_name(), "VCC");

    // 索引移除
    EXPECT_TRUE(s.remove_wire(0));
    EXPECT_EQ(s.wire_count(), 1u);
    EXPECT_FALSE(s.remove_wire(99));  // 越界
    EXPECT_TRUE(s.remove_note(0));
    EXPECT_EQ(s.notes().size(), 1u);
    EXPECT_EQ(s.notes()[0], "Another note");
}

// ============================================================================
// generate_netlist —— 连通性
// ============================================================================

TEST(NetlistTest, TwoComponentsWireBecomesSameNet) {
    // 经典用例：两个器件 + 一根导线 → 同一网络
    Ref<Symbol> r_sym = make_resistor_symbol(1);  // 1 pin at local (0,0)
    r_sym->set_path("res://lib/r.sym.toml");

    Schematic s;
    Component r1;
    r1.set_symbol(r_sym);
    r1.set_refdes("R1");
    r1.set_position(Point{10'000'000, 10'000'000});

    Component r2;
    r2.set_symbol(r_sym);
    r2.set_refdes("R2");
    r2.set_position(Point{20'000'000, 10'000'000});

    s.add_component(std::move(r1));
    s.add_component(std::move(r2));
    // 连接 R1.pin1 ↔ R2.pin1
    s.add_wire(Wire(Point{10'000'000, 10'000'000}, Point{20'000'000, 10'000'000}));

    Netlist nl = s.generate_netlist();
    ASSERT_EQ(nl.net_count(), 1u);  // 唯一网络
    const Net& n = nl.nets()[0];
    EXPECT_EQ(n.pins.size(), 2u);
    EXPECT_FALSE(n.is_power);

    // 引脚集合应包含 (R1,1) 与 (R2,1)
    bool has_r1 = false, has_r2 = false;
    for (const auto& pr : n.pins) {
        if (pr.refdes == "R1" && pr.pin_number == "1") has_r1 = true;
        if (pr.refdes == "R2" && pr.pin_number == "1") has_r2 = true;
    }
    EXPECT_TRUE(has_r1);
    EXPECT_TRUE(has_r2);
}

TEST(NetlistTest, FloatingPinsFormSeparateNets) {
    // 两器件无导线 → 两个独立网络（每引脚一网）
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component r1;
    r1.set_symbol(r_sym);
    r1.set_refdes("R1");
    r1.set_position(Point{10'000'000, 10'000'000});

    Component r2;
    r2.set_symbol(r_sym);
    r2.set_refdes("R2");
    r2.set_position(Point{20'000'000, 10'000'000});

    s.add_component(std::move(r1));
    s.add_component(std::move(r2));

    Netlist nl = s.generate_netlist();
    EXPECT_EQ(nl.net_count(), 2u);  // 两浮空引脚 → 两网
    EXPECT_EQ(nl.pin_count(), 2u);
}

TEST(NetlistTest, NetLabelNamesNet) {
    // NetLabel "SDA" 落在导线一端，整个连通分量命名为 SDA
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component r1;
    r1.set_symbol(r_sym);
    r1.set_refdes("U1");
    r1.set_position(Point{10'000'000, 10'000'000});

    s.add_component(std::move(r1));
    s.add_wire(Wire(Point{10'000'000, 10'000'000}, Point{15'000'000, 10'000'000}));
    s.add_net_label(NetLabel("SDA", Point{15'000'000, 10'000'000}));

    Netlist nl = s.generate_netlist();
    ASSERT_EQ(nl.net_count(), 1u);
    const Net& n = nl.nets()[0];
    EXPECT_EQ(n.name, "SDA");
    EXPECT_FALSE(n.is_power);
    ASSERT_EQ(n.pins.size(), 1u);
    EXPECT_EQ(n.pins[0].refdes, "U1");
    EXPECT_EQ(n.pins[0].pin_number, "1");
}

TEST(NetlistTest, PowerPortNamesAndMarksPower) {
    // PowerPort "VCC" 命名 + is_power=true
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component u1;
    u1.set_symbol(r_sym);
    u1.set_refdes("U1");
    u1.set_position(Point{10'000'000, 10'000'000});

    s.add_component(std::move(u1));
    s.add_wire(Wire(Point{10'000'000, 10'000'000}, Point{10'000'000, 15'000'000}));
    s.add_power_port(PowerPort("VCC", Point{10'000'000, 15'000'000}));

    Netlist nl = s.generate_netlist();
    ASSERT_EQ(nl.net_count(), 1u);
    const Net& n = nl.nets()[0];
    EXPECT_EQ(n.name, "VCC");
    EXPECT_TRUE(n.is_power);  // PowerPort 触发电源标记
}

TEST(NetlistTest, PowerPortOverridesNetLabelName) {
    // 同一连通分量上 NetLabel + PowerPort：PowerPort 胜出
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component u1;
    u1.set_symbol(r_sym);
    u1.set_refdes("U1");
    u1.set_position(Point{10'000'000, 10'000'000});

    s.add_component(std::move(u1));
    s.add_wire(Wire(Point{10'000'000, 10'000'000}, Point{20'000'000, 10'000'000}));
    // 末端同时贴 NetLabel（误占电源名）+ PowerPort（VCC）
    // PowerPort 优先级最高 → 命名为 VCC 且 is_power=true
    s.add_net_label(NetLabel("WRONGNAME", Point{20'000'000, 10'000'000}));
    s.add_power_port(PowerPort("VCC", Point{20'000'000, 10'000'000}));

    Netlist nl = s.generate_netlist();
    ASSERT_EQ(nl.net_count(), 1u);
    EXPECT_EQ(nl.nets()[0].name, "VCC");
    EXPECT_TRUE(nl.nets()[0].is_power);
}

TEST(NetlistTest, AutoNamesUnnamedNets) {
    // 浮空引脚 → 自动 N0001 等
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component r1;
    r1.set_symbol(r_sym);
    r1.set_refdes("R1");
    r1.set_position(Point{0, 0});

    Component r2;
    r2.set_symbol(r_sym);
    r2.set_refdes("R2");
    r2.set_position(Point{1'000'000, 0});

    s.add_component(std::move(r1));
    s.add_component(std::move(r2));

    Netlist nl = s.generate_netlist();
    EXPECT_EQ(nl.net_count(), 2u);
    // 自动名前缀 N + 4 位数字
    for (const Net& n : nl.nets()) {
        EXPECT_EQ(n.name.size(), 5u);
        EXPECT_EQ(n.name[0], 'N');
        EXPECT_FALSE(n.is_power);
    }
    // 两个网络名应互不相同
    EXPECT_NE(nl.nets()[0].name, nl.nets()[1].name);
}

TEST(NetlistTest, MultiPinComponentMultiNetTopology) {
    // 用两引脚电阻符号：R1.1↔R2.1，R1.2↔R2.2 → 两个独立网络
    Ref<Symbol> r_sym = make_resistor_symbol(2, 2'000'000);
    // pins: "1" at (-1_000_000, 0)；"2" at (1_000_000, 0)

    Schematic s;
    Component r1;
    r1.set_symbol(r_sym);
    r1.set_refdes("R1");
    r1.set_position(Point{10'000'000, 10'000'000});
    // R1.pin1 world = (9_000_000, 10_000_000)；R1.pin2 world = (11_000_000, 10_000_000)

    Component r2;
    r2.set_symbol(r_sym);
    r2.set_refdes("R2");
    r2.set_position(Point{20'000'000, 10'000'000});
    // R2.pin1 world = (19_000_000, 10_000_000)；R2.pin2 world = (21_000_000, 10_000_000)

    s.add_component(std::move(r1));
    s.add_component(std::move(r2));
    // pin1↔pin1
    s.add_wire(Wire(Point{9'000'000, 10'000'000}, Point{19'000'000, 10'000'000}));
    // pin2↔pin2
    s.add_wire(Wire(Point{11'000'000, 10'000'000}, Point{21'000'000, 10'000'000}));

    Netlist nl = s.generate_netlist();
    EXPECT_EQ(nl.net_count(), 2u);
    for (const Net& n : nl.nets()) {
        EXPECT_EQ(n.pins.size(), 2u);
        // 每个网络的两个 pin 应来自不同器件
        std::set<std::string> refdes_set;
        for (const auto& pr : n.pins) refdes_set.insert(pr.refdes);
        EXPECT_EQ(refdes_set.size(), 2u);
    }
}

TEST(NetlistTest, SkipsComponentsWithoutSymbol) {
    // has_symbol()==false 的 Component 不参与网表
    Schematic s;
    Component c1;  // 无 Symbol
    c1.set_refdes("X1");
    s.add_component(std::move(c1));

    Netlist nl = s.generate_netlist();
    EXPECT_EQ(nl.net_count(), 0u);
    EXPECT_EQ(nl.pin_count(), 0u);
}

TEST(NetlistTest, FindNetByName) {
    Ref<Symbol> r_sym = make_resistor_symbol(1);

    Schematic s;
    Component u1;
    u1.set_symbol(r_sym);
    u1.set_refdes("U1");
    u1.set_position(Point{0, 0});
    s.add_component(std::move(u1));
    s.add_net_label(NetLabel("SDA", Point{0, 0}));

    Netlist nl = s.generate_netlist();
    ASSERT_NE(nl.find_net("SDA"), nullptr);
    EXPECT_EQ(nl.find_net("nonexistent"), nullptr);
}

TEST(NetlistTest, EmptySchematicProducesEmptyNetlist) {
    Schematic s;
    Netlist nl = s.generate_netlist();
    EXPECT_EQ(nl.net_count(), 0u);
    EXPECT_EQ(nl.pin_count(), 0u);
}

TEST(NetlistTest, OutputIsDeterministicAcrossRuns) {
    // 同一 Schematic 两次生成应得完全相同的 Netlist
    Ref<Symbol> r_sym = make_resistor_symbol(2, 2'000'000);

    auto build = [&]() {
        Schematic s;
        Component r1;
        r1.set_symbol(r_sym);
        r1.set_refdes("R1");
        r1.set_position(Point{10'000'000, 10'000'000});
        Component r2;
        r2.set_symbol(r_sym);
        r2.set_refdes("R2");
        r2.set_position(Point{20'000'000, 10'000'000});
        s.add_component(std::move(r1));
        s.add_component(std::move(r2));
        s.add_wire(Wire(Point{9'000'000, 10'000'000}, Point{19'000'000, 10'000'000}));
        s.add_net_label(NetLabel("NET_A", Point{11'000'000, 10'000'000}));
        return s.generate_netlist();
    };

    Netlist a = build();
    Netlist b = build();
    ASSERT_EQ(a.net_count(), b.net_count());
    for (std::size_t i = 0; i < a.net_count(); ++i) {
        EXPECT_EQ(a.nets()[i].name, b.nets()[i].name);
        EXPECT_EQ(a.nets()[i].pins.size(), b.nets()[i].pins.size());
    }
}

// ============================================================================
// Schematic —— 序列化往返
// ============================================================================

TEST(SchematicTest, SerializeRoundtripPreservesFields) {
    Schematic original;
    original.assign_uuid_for_test("cccc3333-cccc-4ccc-8ccc-cccccccccccc");
    original.set_name("Main");

    Component c;
    c.assign_uuid_for_test("dddd4444-dddd-4ddd-8ddd-dddddddddddd");
    c.set_refdes("R1");
    c.set_symbol_ref("res://lib/r.sym.toml");
    c.set_position(Point{1'000'000, -2'000'000});
    c.set_rotation(Rotation::DEG_90);
    c.set_attribute("value", "10k");
    original.add_component(std::move(c));

    original.add_wire(Wire(Point{0, 0}, Point{5'000'000, 0}, 200'000));
    original.add_net_label(NetLabel("SDA", Point{0, 0}, LabelDirection::VERTICAL));
    original.add_power_port(PowerPort("GND", Point{5'000'000, 0}, PowerPortKind::GROUND));
    original.add_note("first");
    original.add_note("second");

    std::string text = original.serialize();
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("schema_version = 1"), std::string::npos);
    EXPECT_NE(text.find("[[components]]"), std::string::npos);
    EXPECT_NE(text.find("[[wires]]"), std::string::npos);
    EXPECT_NE(text.find("[[net_labels]]"), std::string::npos);
    EXPECT_NE(text.find("[[power_ports]]"), std::string::npos);
    EXPECT_NE(text.find("[components.attributes]"), std::string::npos);
    EXPECT_NE(text.find("notes = "), std::string::npos);

    Ref<Schematic> restored = Schematic::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), original.uuid());
    EXPECT_EQ(restored->name(), "Main");

    // Component 往返
    ASSERT_EQ(restored->component_count(), 1u);
    const Component& rc = restored->component(0);
    EXPECT_EQ(rc.uuid(), "dddd4444-dddd-4ddd-8ddd-dddddddddddd");
    EXPECT_EQ(rc.refdes(), "R1");
    EXPECT_EQ(rc.symbol_ref(), "res://lib/r.sym.toml");
    EXPECT_EQ(rc.position(), (Point{1'000'000, -2'000'000}));
    EXPECT_EQ(rc.rotation(), Rotation::DEG_90);
    ASSERT_EQ(rc.attributes().size(), 1u);
    ASSERT_NE(rc.find_attribute("value"), nullptr);
    EXPECT_EQ(*rc.find_attribute("value"), "10k");

    // Wire 往返
    ASSERT_EQ(restored->wire_count(), 1u);
    EXPECT_EQ(restored->wire(0).a(), (Point{0, 0}));
    EXPECT_EQ(restored->wire(0).b(), (Point{5'000'000, 0}));
    EXPECT_EQ(restored->wire(0).width_nm(), 200'000);

    // NetLabel 往返
    ASSERT_EQ(restored->net_label_count(), 1u);
    EXPECT_EQ(restored->net_label(0).net_name(), "SDA");
    EXPECT_EQ(restored->net_label(0).direction(), LabelDirection::VERTICAL);

    // PowerPort 往返
    ASSERT_EQ(restored->power_port_count(), 1u);
    EXPECT_EQ(restored->power_port(0).net_name(), "GND");
    EXPECT_EQ(restored->power_port(0).kind(), PowerPortKind::GROUND);

    // Notes 往返
    ASSERT_EQ(restored->notes().size(), 2u);
    EXPECT_EQ(restored->notes()[0], "first");
    EXPECT_EQ(restored->notes()[1], "second");
}

TEST(SchematicTest, UuidStableAcrossRoundtrip) {
    Schematic original;
    original.assign_uuid_for_test("eeee5555-eeee-4eee-8eee-eeeeeeeeeeee");
    original.set_name("StableUUID");

    std::string text = original.serialize();
    Ref<Schematic> restored = Schematic::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->uuid(), "eeee5555-eeee-4eee-8eee-eeeeeeeeeeee");
}

TEST(SchematicTest, SerializeRoundtripMultipleComponentsWithAttributes) {
    // 两个 Component 都带 attributes，确保 flush 逻辑正确
    Schematic original;
    original.set_name("Multi");

    Component c1;
    c1.assign_uuid_for_test("11111111-1111-4111-8111-111111111111");
    c1.set_refdes("R1");
    c1.set_attribute("value", "1k");

    Component c2;
    c2.assign_uuid_for_test("22222222-2222-4222-8222-222222222222");
    c2.set_refdes("R2");
    c2.set_attribute("value", "2k");
    c2.set_attribute("tol", "5%");

    original.add_component(std::move(c1));
    original.add_component(std::move(c2));

    std::string text = original.serialize();
    Ref<Schematic> restored = Schematic::parse(text);
    ASSERT_TRUE(restored.valid());
    ASSERT_EQ(restored->component_count(), 2u);

    EXPECT_EQ(restored->component(0).refdes(), "R1");
    EXPECT_EQ(restored->component(0).attributes().size(), 1u);
    ASSERT_NE(restored->component(0).find_attribute("value"), nullptr);
    EXPECT_EQ(*restored->component(0).find_attribute("value"), "1k");

    EXPECT_EQ(restored->component(1).refdes(), "R2");
    EXPECT_EQ(restored->component(1).attributes().size(), 2u);
    ASSERT_NE(restored->component(1).find_attribute("value"), nullptr);
    EXPECT_EQ(*restored->component(1).find_attribute("value"), "2k");
    ASSERT_NE(restored->component(1).find_attribute("tol"), nullptr);
    EXPECT_EQ(*restored->component(1).find_attribute("tol"), "5%");
}

TEST(SchematicTest, EmptySchematicSerializesAndParses) {
    Schematic original;
    original.set_name("Empty");
    std::string text = original.serialize();
    EXPECT_FALSE(text.empty());

    Ref<Schematic> restored = Schematic::parse(text);
    ASSERT_TRUE(restored.valid());
    EXPECT_EQ(restored->name(), "Empty");
    EXPECT_EQ(restored->component_count(), 0u);
    EXPECT_EQ(restored->wire_count(), 0u);
    EXPECT_EQ(restored->net_label_count(), 0u);
    EXPECT_EQ(restored->power_port_count(), 0u);
}
