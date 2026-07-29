// tests/test_pcb.cpp
//
// P8 PCB 编辑器 · 对象树骨架测试（REQ-008 六属性契约）。
//
// 覆盖：
//   - UUID 生成 / 校验（generate_uuid / is_valid_uuid）
//   - Layer / Stackup（图层增删查、copper_count、枚举字符串往返）
//   - Board 构造 + 默认状态 + UUID 稳定
//   - 添加 Track / Pad / Via / Net（引用稳定、计数、UUID 有效）
//   - PcbEntity 六属性契约：
//       1. UUID          —— 同一对象 UUID 跨调用稳定
//       2. Geometry      —— bounding_box 各图元类型正确
//       3. Properties    —— properties() 关键值正确
//       4. Constraints   —— 占位默认 nullptr
//       5. History       —— set_history/history 往返 + 向子对象传播
//       6. Serialization —— Track/Pad/Via/Net/Board 文本往返保留 UUID + 关键字段
//   - 板框 Polygon：set_outline / bounding_box = outline AABB
//   - find_by_uuid：Board 自身 + Track/Pad/Via/Net + 未命中
//
// 集成时由主构建在 tests/CMakeLists.txt 追加：
//   add_executable(test_pcb test_pcb.cpp)
//   target_link_libraries(test_pcb PRIVATE eda_pcb gtest_main)
//   gtest_discover_tests(test_pcb)

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "eda/command/undo_stack.h"
#include "eda/geometry/types.h"
#include "eda/pcb/board.h"
#include "eda/pcb/layer.h"
#include "eda/pcb/net.h"
#include "eda/pcb/pad.h"
#include "eda/pcb/pcb_entity.h"
#include "eda/pcb/track.h"
#include "eda/pcb/via.h"

using namespace eda;
using namespace eda::pcb;
using namespace eda::geometry;

namespace {

// 1mm = 1_000_000 nm（geometry::Coord 单位为 nm，见 eda/geometry/types.h）
constexpr Coord kMm = 1'000'000;

// 构造矩形板框 Polygon（4 顶点闭合）。
Polygon make_rect_outline(Coord x0, Coord y0, Coord x1, Coord y1) {
    Polygon p;
    p.outer = {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1}};
    return p;
}

}  // namespace

// ============================================================
// UUID 生成 / 校验
// ============================================================

TEST(PcbUuidTest, GenerateUuidHasValidV4Format) {
    const std::string u = generate_uuid();
    EXPECT_TRUE(is_valid_uuid(u));
    EXPECT_EQ(u.size(), 36u);
    EXPECT_EQ(u[8], '-');
    EXPECT_EQ(u[13], '-');
    EXPECT_EQ(u[14], '4');  // version 4
    EXPECT_EQ(u[18], '-');
    EXPECT_EQ(u[23], '-');
}

TEST(PcbUuidTest, GenerateUuidProducesUniqueValues) {
    std::vector<std::string> uuids;
    for (int i = 0; i < 1000; ++i) uuids.push_back(generate_uuid());
    std::sort(uuids.begin(), uuids.end());
    const auto it = std::unique(uuids.begin(), uuids.end());
    EXPECT_EQ(it, uuids.end());  // 1000 次无重复
}

TEST(PcbUuidTest, IsValidUuidRejectsBadInput) {
    EXPECT_FALSE(is_valid_uuid(""));
    EXPECT_FALSE(is_valid_uuid("abc"));
    EXPECT_FALSE(is_valid_uuid("12345678-1234-1234-1234-12345678901z"));  // 'z' 非法
    EXPECT_TRUE(is_valid_uuid("12345678-1234-4234-8234-1234567890ab"));
}

// ============================================================
// Layer / Stackup
// ============================================================

TEST(PcbLayerTest, AddLayerAndQuery) {
    Stackup s;
    EXPECT_EQ(s.layer_count(), 0u);
    EXPECT_EQ(s.copper_count(), 0u);

    s.add_layer(Layer("F.Cu", LayerType::COPPER, 0));
    s.add_layer(Layer("B.Cu", LayerType::COPPER, 1));
    s.add_layer(Layer("F.Mask", LayerType::SOLDER_MASK));

    EXPECT_EQ(s.layer_count(), 3u);
    EXPECT_EQ(s.copper_count(), 2u);

    ASSERT_NE(s.find("F.Cu"), nullptr);
    EXPECT_EQ(s.find("F.Cu")->type(), LayerType::COPPER);
    EXPECT_EQ(s.find("F.Cu")->copper_index(), 0);
    EXPECT_TRUE(s.find("F.Cu")->is_copper());
    EXPECT_FALSE(s.find("F.Mask")->is_copper());
    EXPECT_EQ(s.find("missing"), nullptr);
}

TEST(PcbLayerTest, EnumStringRoundTrip) {
    EXPECT_EQ(layer_type_string(LayerType::SOLDER_MASK), "solder_mask");
    EXPECT_EQ(parse_layer_type("silkscreen"), LayerType::SILKSCREEN);
    EXPECT_EQ(parse_layer_type("unknown"), LayerType::USER);  // 前向兼容

    EXPECT_EQ(pad_shape_string(PadShape::OCTAGON), "octagon");
    EXPECT_EQ(parse_pad_shape("circle"), PadShape::CIRCLE);

    EXPECT_EQ(via_type_string(ViaType::BLIND), "blind");
    EXPECT_EQ(parse_via_type("buried"), ViaType::BURIED);
}

// ============================================================
// Board 构造 + UUID 稳定
// ============================================================

TEST(PcbBoardTest, BoardDefaultsAndValidUuid) {
    Board b;
    EXPECT_TRUE(is_valid_uuid(b.uuid()));
    EXPECT_EQ(b.name(), "Board");
    EXPECT_EQ(b.track_count(), 0u);
    EXPECT_EQ(b.pad_count(), 0u);
    EXPECT_EQ(b.via_count(), 0u);
    EXPECT_EQ(b.net_count(), 0u);
    EXPECT_EQ(b.layer_count(), 0u);
    EXPECT_EQ(b.footprint_count(), 0u);
}

TEST(PcbBoardTest, BoardUuidStableAcrossCalls) {
    Board b;
    const std::string u1 = b.uuid();
    const std::string u2 = b.uuid();
    EXPECT_EQ(u1, u2);
}

TEST(PcbBoardTest, CustomNameInCtor) {
    Board b("MainBoard");
    EXPECT_EQ(b.name(), "MainBoard");
}

// ============================================================
// 添加 Track / Pad / Via / Net
// ============================================================

TEST(PcbBoardTest, AddTrackReturnsStableRefWithUuid) {
    Board b;
    Path p{{Point{0, 0}, Point{kMm, 0}}, 200 * 1000};  // 长 1mm，宽 0.2mm
    Track& t = b.add_track(p, "F.Cu");
    EXPECT_TRUE(is_valid_uuid(t.uuid()));
    EXPECT_EQ(t.layer_id(), "F.Cu");
    EXPECT_EQ(t.width(), 200 * 1000);
    EXPECT_EQ(b.track_count(), 1u);
    EXPECT_EQ(b.tracks()[0].get(), &t);  // 引用稳定
}

TEST(PcbBoardTest, AddPadReturnsStableRefWithNumber) {
    Board b;
    Pad& p = b.add_pad(Point{kMm, kMm}, 500 * 1000, 500 * 1000, PadShape::RECT, "F.Cu", "1");
    EXPECT_TRUE(is_valid_uuid(p.uuid()));
    EXPECT_EQ(p.number(), "1");
    EXPECT_EQ(p.shape(), PadShape::RECT);
    EXPECT_EQ(b.pad_count(), 1u);
}

TEST(PcbBoardTest, AddViaInfersTypeFromLayerPair) {
    Board b;
    Via& v = b.add_via(Point{2 * kMm, 2 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");
    EXPECT_TRUE(is_valid_uuid(v.uuid()));
    EXPECT_EQ(v.via_type(), ViaType::THROUGH);  // F.Cu → B.Cu 推断为通孔
    EXPECT_EQ(v.pad_diameter(), 600 * 1000);
    EXPECT_EQ(b.via_count(), 1u);
}

TEST(PcbBoardTest, AddNetAndMembers) {
    Board b;
    Net& n = b.add_net("VCC");
    EXPECT_TRUE(is_valid_uuid(n.uuid()));
    EXPECT_EQ(n.name(), "VCC");
    EXPECT_EQ(n.member_count(), 0u);

    n.add_member("track-uuid-1");
    n.add_member("pad-uuid-2");
    n.add_member("track-uuid-1");  // 重复添加不应增加
    EXPECT_EQ(n.member_count(), 2u);
    EXPECT_TRUE(n.has_member("track-uuid-1"));
    EXPECT_FALSE(n.has_member("nope"));

    EXPECT_TRUE(n.remove_member("pad-uuid-2"));
    EXPECT_FALSE(n.has_member("pad-uuid-2"));
    EXPECT_EQ(n.member_count(), 1u);
    EXPECT_FALSE(n.remove_member("pad-uuid-2"));  // 已不存在
}

// ============================================================
// 六属性契约 · Geometry（bounding_box）
// ============================================================

TEST(PcbBoundingBoxTest, TrackBoxIncludesWidthPadding) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}}, 200 * 1000}, "F.Cu");
    const Box b = t.bounding_box();
    // 中心线 0..1mm，width/2=0.1mm 各向外扩
    EXPECT_EQ(b.min.x, -100 * 1000);
    EXPECT_EQ(b.min.y, -100 * 1000);
    EXPECT_EQ(b.max.x, kMm + 100 * 1000);
    EXPECT_EQ(b.max.y, 100 * 1000);
}

TEST(PcbBoundingBoxTest, PadBoxCenteredOnPosition) {
    Pad p(Point{kMm, kMm}, 500 * 1000, 300 * 1000, PadShape::RECT, "F.Cu", "1");
    const Box b = p.bounding_box();
    EXPECT_EQ(b.min.x, kMm - 250 * 1000);
    EXPECT_EQ(b.min.y, kMm - 150 * 1000);
    EXPECT_EQ(b.max.x, kMm + 250 * 1000);
    EXPECT_EQ(b.max.y, kMm + 150 * 1000);
}

TEST(PcbBoundingBoxTest, ViaBoxFromPadDiameter) {
    Via v(Point{2 * kMm, 2 * kMm}, 300 * 1000, 600 * 1000, "F.Cu", "B.Cu");
    const Box b = v.bounding_box();
    EXPECT_EQ(b.min.x, 2 * kMm - 300 * 1000);
    EXPECT_EQ(b.min.y, 2 * kMm - 300 * 1000);
    EXPECT_EQ(b.max.x, 2 * kMm + 300 * 1000);
    EXPECT_EQ(b.max.y, 2 * kMm + 300 * 1000);
}

TEST(PcbBoundingBoxTest, BoardBoxIsOutlineAabb) {
    Board b;
    b.set_outline(make_rect_outline(0, 0, 50 * kMm, 30 * kMm));
    const Box bb = b.bounding_box();
    EXPECT_EQ(bb.min.x, 0);
    EXPECT_EQ(bb.min.y, 0);
    EXPECT_EQ(bb.max.x, 50 * kMm);
    EXPECT_EQ(bb.max.y, 30 * kMm);
}

TEST(PcbBoundingBoxTest, BoardBoxFallsBackToChildrenWhenNoOutline) {
    Board b;
    b.add_track(Path{{Point{0, 0}, Point{10 * kMm, 0}}, 100 * 1000}, "F.Cu");
    b.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000, PadShape::RECT, "F.Cu");
    const Box bb = b.bounding_box();
    // track BB: x∈[-50k, 10_050k], y∈[-50k, 50k]
    // pad BB:   x∈[4_750k, 5_250k], y∈[4_750k, 5_250k]
    EXPECT_EQ(bb.min.x, -50 * 1000);
    EXPECT_EQ(bb.min.y, -50 * 1000);
    EXPECT_EQ(bb.max.x, 10 * kMm + 50 * 1000);
    EXPECT_EQ(bb.max.y, 5 * kMm + 250 * 1000);
}

// ============================================================
// 六属性契约 · Properties
// ============================================================

TEST(PcbPropertiesTest, TrackPropertiesExposeKeyFields) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}, Point{kMm, kMm}}, 200 * 1000}, "F.Cu", "net-1");
    const PropertySet ps = t.properties();
    EXPECT_EQ(ps.at("layer").as_string(), "F.Cu");
    EXPECT_EQ(ps.at("net").as_string(), "net-1");
    EXPECT_EQ(ps.at("width").as_int(), 200 * 1000);
    EXPECT_EQ(ps.at("segment_count").as_int(), 2);  // 3 点 = 2 段
}

TEST(PcbPropertiesTest, PadPropertiesExposeShapeAndNumber) {
    Pad p(Point{0, 0}, 600 * 1000, 600 * 1000, PadShape::CIRCLE, "F.Cu", "A1");
    const PropertySet ps = p.properties();
    EXPECT_EQ(ps.at("shape").as_string(), "circle");
    EXPECT_EQ(ps.at("number").as_string(), "A1");
    EXPECT_EQ(ps.at("layer").as_string(), "F.Cu");
    EXPECT_EQ(ps.at("width").as_int(), 600 * 1000);
}

TEST(PcbPropertiesTest, ViaPropertiesExposeLayerPairAndType) {
    Via v(Point{0, 0}, 200 * 1000, 400 * 1000, "F.Cu", "B.Cu", "net-v");
    const PropertySet ps = v.properties();
    EXPECT_EQ(ps.at("from").as_string(), "F.Cu");
    EXPECT_EQ(ps.at("to").as_string(), "B.Cu");
    EXPECT_EQ(ps.at("type").as_string(), "through");
    EXPECT_EQ(ps.at("drill").as_int(), 200 * 1000);
    EXPECT_EQ(ps.at("pad").as_int(), 400 * 1000);
}

TEST(PcbPropertiesTest, NetPropertiesExposeNameAndMemberCount) {
    Net n("GND", "Power");
    n.add_member("a");
    n.add_member("b");
    const PropertySet ps = n.properties();
    EXPECT_EQ(ps.at("name").as_string(), "GND");
    EXPECT_EQ(ps.at("net_class").as_string(), "Power");
    EXPECT_EQ(ps.at("member_count").as_int(), 2);
}

TEST(PcbPropertiesTest, BoardPropertiesExposeCounts) {
    Board b("Main");
    b.add_layer(Layer("F.Cu", LayerType::COPPER, 0));
    b.add_track(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    b.add_net("VCC");
    const PropertySet ps = b.properties();
    EXPECT_EQ(ps.at("name").as_string(), "Main");
    EXPECT_EQ(ps.at("layer_count").as_int(), 1);
    EXPECT_EQ(ps.at("track_count").as_int(), 1);
    EXPECT_EQ(ps.at("net_count").as_int(), 1);
}

TEST(PcbPropertiesTest, CustomPropertyRoundTrip) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    t.set_property("impedance", Variant(int64_t(50)));  // 50Ω，用户自定义属性
    EXPECT_EQ(t.get_property("impedance").as_int(), 50);
    // properties() 按值返回快照（每调用一次都是新的 PropertySet），
    // 必须先存局部变量再比较迭代器，否则 find/end 来自不同容器（UB）。
    const PropertySet ps = t.properties();
    EXPECT_TRUE(ps.find("impedance") != ps.end());
}

// ============================================================
// 六属性契约 · Constraints（占位）
// ============================================================

TEST(PcbConstraintsTest, DefaultsToNullptr) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    EXPECT_EQ(t.constraints(), nullptr);
    EXPECT_EQ(const_cast<const Track&>(t).constraints(), nullptr);  // const 重载
}

// ============================================================
// 六属性契约 · History（UndoStack 挂点 + 子对象传播）
// ============================================================

TEST(PcbHistoryTest, SetHistoryRoundTripsOnBoard) {
    Board b;
    command::UndoStack stack;
    b.set_history(&stack);
    EXPECT_EQ(b.history(), &stack);
}

TEST(PcbHistoryTest, HistoryPropagatesToChildren) {
    Board b;
    command::UndoStack stack;
    b.set_history(&stack);

    Track& t = b.add_track(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    Pad& p = b.add_pad(Point{0, 0}, 100 * 1000, 100 * 1000, PadShape::RECT, "F.Cu");
    Via& v = b.add_via(Point{0, 0}, 100 * 1000, 200 * 1000, "F.Cu", "B.Cu");
    Net& n = b.add_net("VCC");

    EXPECT_EQ(t.history(), &stack);
    EXPECT_EQ(p.history(), &stack);
    EXPECT_EQ(v.history(), &stack);
    EXPECT_EQ(n.history(), &stack);
}

// ============================================================
// 六属性契约 · Serialization（单图元往返）
// ============================================================

TEST(PcbSerializeTest, TrackRoundTripPreservesFields) {
    Track orig(Path{{Point{1 * kMm, 2 * kMm}, Point{3 * kMm, 4 * kMm},
                    Point{5 * kMm, 6 * kMm}}, 250 * 1000}, "F.Cu", "net-abc");
    orig.set_property("impedance", Variant(int64_t(50)));
    const std::string json = orig.serialize();

    Track copy;
    ASSERT_TRUE(copy.deserialize(json));
    EXPECT_EQ(copy.uuid(), orig.uuid());
    EXPECT_EQ(copy.layer_id(), "F.Cu");
    EXPECT_EQ(copy.net_uuid(), "net-abc");
    EXPECT_EQ(copy.width(), 250 * 1000);
    ASSERT_EQ(copy.path().points.size(), 3u);
    EXPECT_EQ(copy.path().points[1].x, 3 * kMm);
    EXPECT_EQ(copy.path().points[2].y, 6 * kMm);
    EXPECT_EQ(copy.get_property("impedance").as_int(), 50);  // custom 属性往返
}

TEST(PcbSerializeTest, PadRoundTripPreservesShapeAndPosition) {
    Pad orig(Point{7 * kMm, 8 * kMm}, 600 * 1000, 400 * 1000, PadShape::OVAL, "B.Cu", "5");
    const std::string json = orig.serialize();

    Pad copy;
    ASSERT_TRUE(copy.deserialize(json));
    EXPECT_EQ(copy.uuid(), orig.uuid());
    EXPECT_EQ(copy.shape(), PadShape::OVAL);
    EXPECT_EQ(copy.number(), "5");
    EXPECT_EQ(copy.layer_id(), "B.Cu");
    EXPECT_EQ(copy.position().x, 7 * kMm);
    EXPECT_EQ(copy.position().y, 8 * kMm);
    EXPECT_EQ(copy.width(), 600 * 1000);
    EXPECT_EQ(copy.height(), 400 * 1000);
}

TEST(PcbSerializeTest, ViaRoundTripPreservesDrillAndLayers) {
    Via orig(Point{1 * kMm, 1 * kMm}, 200 * 1000, 400 * 1000, "F.Cu", "B.Cu", "net-v");
    const std::string json = orig.serialize();

    Via copy;
    ASSERT_TRUE(copy.deserialize(json));
    EXPECT_EQ(copy.uuid(), orig.uuid());
    EXPECT_EQ(copy.drill_diameter(), 200 * 1000);
    EXPECT_EQ(copy.pad_diameter(), 400 * 1000);
    EXPECT_EQ(copy.layer_from(), "F.Cu");
    EXPECT_EQ(copy.layer_to(), "B.Cu");
    EXPECT_EQ(copy.net_uuid(), "net-v");
    EXPECT_EQ(copy.via_type(), ViaType::THROUGH);
}

TEST(PcbSerializeTest, NetRoundTripPreservesMembers) {
    Net orig("SDA", "Digital");
    orig.add_member("track-1");
    orig.add_member("pad-7");
    const std::string json = orig.serialize();

    Net copy;
    ASSERT_TRUE(copy.deserialize(json));
    EXPECT_EQ(copy.uuid(), orig.uuid());
    EXPECT_EQ(copy.name(), "SDA");
    EXPECT_EQ(copy.net_class(), "Digital");
    EXPECT_EQ(copy.member_count(), 2u);
    EXPECT_TRUE(copy.has_member("track-1"));
    EXPECT_TRUE(copy.has_member("pad-7"));
}

TEST(PcbSerializeTest, EntitySerializeContainsTypeTag) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    const std::string json = t.serialize();
    EXPECT_NE(json.find("\"type\""), std::string::npos);
    EXPECT_NE(json.find("\"Track\""), std::string::npos);
    EXPECT_NE(json.find("\"uuid\""), std::string::npos);
}

// ============================================================
// Board 整树序列化往返
// ============================================================

TEST(PcbBoardSerializeTest, FullTreeRoundTrip) {
    Board orig("MainBoard");
    orig.set_outline(make_rect_outline(0, 0, 40 * kMm, 25 * kMm));
    orig.set_rules_ref("rules://jlcpcb-cap@1.0");
    orig.add_layer(Layer("F.Cu", LayerType::COPPER, 0));
    orig.add_layer(Layer("B.Cu", LayerType::COPPER, 1));
    orig.add_layer(Layer("F.Mask", LayerType::SOLDER_MASK));

    Track& t1 = orig.add_track(
        Path{{Point{1 * kMm, 1 * kMm}, Point{10 * kMm, 1 * kMm}}, 200 * 1000}, "F.Cu");
    Pad& p1 = orig.add_pad(Point{5 * kMm, 5 * kMm}, 600 * 1000, 600 * 1000,
                           PadShape::RECT, "F.Cu", "1");
    Via& v1 = orig.add_via(Point{3 * kMm, 3 * kMm}, 300 * 1000, 600 * 1000,
                           "F.Cu", "B.Cu");
    Net& n1 = orig.add_net("VCC");
    Net& n2 = orig.add_net("GND");
    n1.add_member(t1.uuid());
    n1.add_member(p1.uuid());
    n2.add_member(v1.uuid());

    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "lib://standard@1.2.0/SOIC-8";
    fp.refdes = "U1";
    fp.position = Point{20 * kMm, 12 * kMm};
    fp.rotation = 1.5707963;  // 90°
    fp.flipped = false;
    orig.add_footprint(fp);

    const std::string json = orig.serialize();

    Board copy;
    ASSERT_TRUE(copy.deserialize(json));

    // Board 顶层
    EXPECT_EQ(copy.uuid(), orig.uuid());
    EXPECT_EQ(copy.name(), "MainBoard");
    EXPECT_EQ(copy.rules_ref(), "rules://jlcpcb-cap@1.0");

    // 板框
    ASSERT_EQ(static_cast<int>(copy.outline().outer.size()), 4);
    EXPECT_EQ(copy.outline().outer[2].x, 40 * kMm);
    EXPECT_EQ(copy.outline().outer[2].y, 25 * kMm);
    EXPECT_EQ(copy.bounding_box().max.x, 40 * kMm);

    // 图层
    EXPECT_EQ(copy.layer_count(), 3u);
    EXPECT_EQ(copy.layer(0).layer_id(), "F.Cu");
    EXPECT_EQ(copy.layer(0).type(), LayerType::COPPER);
    EXPECT_EQ(copy.layer(2).layer_id(), "F.Mask");
    EXPECT_EQ(copy.layer(2).type(), LayerType::SOLDER_MASK);

    // 子对象：UUID 与关键字段往返
    ASSERT_EQ(copy.track_count(), 1u);
    EXPECT_EQ(copy.tracks()[0]->uuid(), t1.uuid());
    EXPECT_EQ(copy.tracks()[0]->layer_id(), "F.Cu");
    EXPECT_EQ(copy.tracks()[0]->width(), 200 * 1000);

    ASSERT_EQ(copy.pad_count(), 1u);
    EXPECT_EQ(copy.pads()[0]->uuid(), p1.uuid());
    EXPECT_EQ(copy.pads()[0]->number(), "1");
    EXPECT_EQ(copy.pads()[0]->shape(), PadShape::RECT);

    ASSERT_EQ(copy.via_count(), 1u);
    EXPECT_EQ(copy.vias()[0]->uuid(), v1.uuid());
    EXPECT_EQ(copy.vias()[0]->via_type(), ViaType::THROUGH);

    ASSERT_EQ(copy.net_count(), 2u);
    const Net* vcc = copy.find_net(n1.uuid());
    ASSERT_NE(vcc, nullptr);
    EXPECT_EQ(vcc->name(), "VCC");
    EXPECT_EQ(vcc->member_count(), 2u);
    EXPECT_TRUE(vcc->has_member(t1.uuid()));
    EXPECT_TRUE(vcc->has_member(p1.uuid()));

    // 器件实例
    ASSERT_EQ(copy.footprint_count(), 1u);
    const auto& fp2 = copy.footprint(0);
    EXPECT_EQ(fp2.uuid, fp.uuid);
    EXPECT_EQ(fp2.footprint_ref, "lib://standard@1.2.0/SOIC-8");
    EXPECT_EQ(fp2.refdes, "U1");
    EXPECT_NEAR(fp2.rotation, 1.5707963, 1e-6);
    EXPECT_EQ(fp2.position.x, 20 * kMm);
}

// ============================================================
// find_by_uuid
// ============================================================

TEST(PcbFindTest, FindBoardSelfAndChildren) {
    Board b;
    Track& t = b.add_track(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    Pad& p = b.add_pad(Point{0, 0}, 100 * 1000, 100 * 1000, PadShape::RECT, "F.Cu");
    Via& v = b.add_via(Point{0, 0}, 100 * 1000, 200 * 1000, "F.Cu", "B.Cu");
    Net& n = b.add_net("VCC");

    EXPECT_EQ(b.find_by_uuid(b.uuid()), &b);  // Board 自身
    EXPECT_EQ(b.find_by_uuid(t.uuid()), &t);
    EXPECT_EQ(b.find_by_uuid(p.uuid()), &p);
    EXPECT_EQ(b.find_by_uuid(v.uuid()), &v);
    EXPECT_EQ(b.find_by_uuid(n.uuid()), &n);
    EXPECT_EQ(b.find_by_uuid("non-existent-uuid"), nullptr);

    // 强类型查找
    EXPECT_EQ(b.find_track(t.uuid()), &t);
    EXPECT_EQ(b.find_pad(p.uuid()), &p);
    EXPECT_EQ(b.find_via(v.uuid()), &v);
    EXPECT_EQ(b.find_net(n.uuid()), &n);
    EXPECT_EQ(b.find_track(p.uuid()), nullptr);  // 类型不匹配
}

TEST(PcbFindTest, FindFootprintInstanceByUuid) {
    Board b;
    FootprintInstance fp;
    fp.uuid = generate_uuid();
    fp.footprint_ref = "lib://X";
    b.add_footprint(fp);
    ASSERT_NE(b.find_footprint_by_uuid(fp.uuid), nullptr);
    EXPECT_EQ(b.find_footprint_by_uuid(fp.uuid)->footprint_ref, "lib://X");
    EXPECT_EQ(b.find_footprint_by_uuid("nope"), nullptr);
}

// ============================================================
// recompute_net_boxes：Net 包围盒刷新
// ============================================================

TEST(PcbNetBoxTest, RecomputeMergesMemberBoxes) {
    Board b;
    Track& t = b.add_track(
        Path{{Point{0, 0}, Point{10 * kMm, 0}}, 100 * 1000}, "F.Cu");
    Pad& p = b.add_pad(Point{5 * kMm, 5 * kMm}, 500 * 1000, 500 * 1000,
                       PadShape::RECT, "F.Cu");
    Net& n = b.add_net("VCC");
    n.add_member(t.uuid());
    n.add_member(p.uuid());

    // 初始 Net 包围盒为默认（空）
    EXPECT_EQ(n.bounding_box().max.x, 0);

    b.recompute_net_boxes();
    const Box nb = n.bounding_box();
    // track BB: x∈[-50k, 10_050k], pad BB: x∈[4_750k, 5_250k]
    EXPECT_EQ(nb.min.x, -50 * 1000);
    EXPECT_EQ(nb.max.x, 10 * kMm + 50 * 1000);
    EXPECT_EQ(nb.min.y, -50 * 1000);
    EXPECT_EQ(nb.max.y, 5 * kMm + 250 * 1000);
}

// ============================================================
// ClassDB 元信息（get_class / is_class）
// ============================================================

TEST(PcbClassInfoTest, ClassNamesChainCorrectly) {
    Track t(Path{{Point{0, 0}, Point{kMm, 0}}, 100 * 1000}, "F.Cu");
    EXPECT_EQ(t.get_class(), "Track");
    EXPECT_TRUE(t.is_class("Track"));
    EXPECT_TRUE(t.is_class("PcbEntity"));
    EXPECT_TRUE(t.is_class("Object"));
    EXPECT_FALSE(t.is_class("Pad"));

    Board b;
    EXPECT_EQ(b.get_class_static(), "Board");
    EXPECT_EQ(b.get_class(), "Board");
    EXPECT_TRUE(b.is_class("PcbEntity"));
    EXPECT_TRUE(b.is_class("Object"));
}