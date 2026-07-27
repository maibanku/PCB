// Variant 单元测试（P2 Task 3）。
//
// 覆盖：
//   - 扩展类型构造与 get_type 识别（ARRAY / DICTIONARY / VECTOR2 / COLOR）
//   - as_* 取值、类型不匹配返回零值/默认视图（无异常）
//   - ARRAY 编辑：array_size / array_at / set_array_element / append（含越界）
//   - DICTIONARY 编辑：dict_size / dict_at / dict_has / set_dict_element / dict_erase
//   - COW 隔离：拷贝不波及原对象
//   - 嵌套 ARRAY / DICTIONARY
//   - JSON 往返（serialize / deserialize）：标量、集合、VECTOR2/COLOR、嵌套、无损
//   - 循环引用防护：append/set_array_element 传入 *this 不构造共享自身的循环

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "core/math/color.h"
#include "core/math/vector2.h"
#include "core/object/variant.h"

using namespace eda;

// ===== 扩展类型：构造与 get_type =====

TEST(VariantExtendedTypeTest, DefaultIsNil) {
    Variant v;
    EXPECT_EQ(v.get_type(), Variant::NIL);
    EXPECT_TRUE(v.is_nil());
}

TEST(VariantExtendedTypeTest, Vector2Constructor) {
    Variant v(Vector2(1.5f, 2.5f));
    EXPECT_EQ(v.get_type(), Variant::VECTOR2);
    EXPECT_FALSE(v.is_nil());
}

TEST(VariantExtendedTypeTest, ColorConstructor) {
    Variant c(Color{1.0f, 0.5f, 0.25f, 1.0f});
    EXPECT_EQ(c.get_type(), Variant::COLOR);
}

TEST(VariantExtendedTypeTest, ArrayConstructor) {
    std::vector<Variant> items{Variant(int64_t(1)), Variant(int64_t(2))};
    Variant v(items);
    EXPECT_EQ(v.get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_size(), 2u);
}

TEST(VariantExtendedTypeTest, DictConstructor) {
    std::unordered_map<std::string, Variant> items{
        {"name", Variant(std::string("pad"))},
        {"size", Variant(int64_t(42))}};
    Variant v(items);
    EXPECT_EQ(v.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(v.dict_size(), 2u);
}

// ===== as_vector2 / as_color =====

TEST(VariantExtendedAsTest, AsVector2ReturnsValue) {
    Variant v(Vector2(3.0f, 4.0f));
    Vector2 p = v.as_vector2();
    EXPECT_FLOAT_EQ(p.x, 3.0f);
    EXPECT_FLOAT_EQ(p.y, 4.0f);
}

TEST(VariantExtendedAsTest, AsColorReturnsValue) {
    Variant v(Color{0.1f, 0.2f, 0.3f, 0.4f});
    Color c = v.as_color();
    EXPECT_FLOAT_EQ(c.r, 0.1f);
    EXPECT_FLOAT_EQ(c.g, 0.2f);
    EXPECT_FLOAT_EQ(c.b, 0.3f);
    EXPECT_FLOAT_EQ(c.a, 0.4f);
}

TEST(VariantExtendedAsTest, AsVector2OnNonVectorReturnsZero) {
    Variant v(int64_t(42));
    Vector2 p = v.as_vector2();
    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
}

TEST(VariantExtendedAsTest, AsColorOnNonColorReturnsDefault) {
    Variant v(std::string("red"));
    Color c = v.as_color();
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);  // 默认不透明黑
}

// ===== ARRAY 只读视图与编辑 =====

TEST(VariantArrayTest, AsArrayReturnsItemsView) {
    std::vector<Variant> items{Variant(int64_t(10)), Variant(int64_t(20))};
    Variant v(items);
    const auto& view = v.as_array();
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0].as_int(), 10);
    EXPECT_EQ(view[1].as_int(), 20);
}

TEST(VariantArrayTest, AsArrayOnNonArrayReturnsEmpty) {
    Variant v(int64_t(5));
    EXPECT_TRUE(v.as_array().empty());
}

TEST(VariantArrayTest, ArrayAtReturnsElement) {
    std::vector<Variant> items{Variant(int64_t(1)), Variant(int64_t(2)), Variant(int64_t(3))};
    Variant v(items);
    EXPECT_EQ(v.array_at(0).as_int(), 1);
    EXPECT_EQ(v.array_at(2).as_int(), 3);
}

TEST(VariantArrayTest, ArrayAtOutOfBoundsReturnsNil) {
    std::vector<Variant> items{Variant(int64_t(1))};
    Variant v(items);
    EXPECT_TRUE(v.array_at(5).is_nil());
    EXPECT_TRUE(v.array_at(0).as_bool());  // 元素 1 truthy
}

TEST(VariantArrayTest, AppendGrowsArray) {
    Variant v(std::vector<Variant>{Variant(int64_t(1))});
    v.append(Variant(int64_t(2)));
    v.append(Variant(std::string("three")));
    EXPECT_EQ(v.array_size(), 3u);
    EXPECT_EQ(v.array_at(0).as_int(), 1);
    EXPECT_EQ(v.array_at(1).as_int(), 2);
    EXPECT_EQ(v.array_at(2).as_string(), "three");
}

TEST(VariantArrayTest, AppendOnNonArrayIsNoop) {
    Variant v(int64_t(42));
    v.append(Variant(int64_t(1)));
    EXPECT_EQ(v.get_type(), Variant::INT);  // 类型不变
    EXPECT_EQ(v.as_int(), 42);
}

TEST(VariantArrayTest, SetArrayElementUpdatesInPlace) {
    Variant v(std::vector<Variant>{Variant(int64_t(1)), Variant(int64_t(2))});
    v.set_array_element(0, Variant(int64_t(99)));
    EXPECT_EQ(v.array_at(0).as_int(), 99);
    EXPECT_EQ(v.array_at(1).as_int(), 2);  // 未触及
}

TEST(VariantArrayTest, SetArrayElementOutOfBoundsIgnored) {
    Variant v(std::vector<Variant>{Variant(int64_t(1))});
    v.set_array_element(5, Variant(int64_t(99)));  // 越界：忽略
    EXPECT_EQ(v.array_size(), 1u);
    EXPECT_EQ(v.array_at(0).as_int(), 1);
}

TEST(VariantArrayTest, EmptyArraySizeZero) {
    Variant v(std::vector<Variant>{});
    EXPECT_EQ(v.get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_size(), 0u);
    EXPECT_EQ(v.serialize(), "[]");
}

// ===== ARRAY COW 隔离 =====

TEST(VariantArrayCowTest, CopySharesAndMutationIsolates) {
    Variant a(std::vector<Variant>{Variant(int64_t(1)), Variant(int64_t(2))});
    Variant b = a;  // 共享存储
    EXPECT_EQ(a.array_size(), 2u);
    EXPECT_EQ(b.array_size(), 2u);

    b.append(Variant(int64_t(3)));            // COW：b 分离
    b.set_array_element(0, Variant(int64_t(99)));

    EXPECT_EQ(a.array_size(), 2u);            // 原对象未受影响
    EXPECT_EQ(a.array_at(0).as_int(), 1);
    EXPECT_EQ(b.array_size(), 3u);
    EXPECT_EQ(b.array_at(0).as_int(), 99);
}

TEST(VariantArrayCowTest, CopyAssignSharesAndMutationIsolates) {
    Variant a(std::vector<Variant>{Variant(int64_t(7))});
    Variant b(std::vector<Variant>{Variant(int64_t(0))});
    b = a;  // 拷贝赋值：共享
    b.set_array_element(0, Variant(int64_t(100)));
    EXPECT_EQ(a.array_at(0).as_int(), 7);
    EXPECT_EQ(b.array_at(0).as_int(), 100);
}

// ===== DICTIONARY 只读视图与编辑 =====

TEST(VariantDictTest, AsDictionaryReturnsItemsView) {
    std::unordered_map<std::string, Variant> items{{"k", Variant(int64_t(42))}};
    Variant v(items);
    const auto& view = v.as_dictionary();
    ASSERT_EQ(view.size(), 1u);
    ASSERT_TRUE(view.find("k") != view.end());
    EXPECT_EQ(view.at("k").as_int(), 42);
}

TEST(VariantDictTest, DictAtReturnsValue) {
    std::unordered_map<std::string, Variant> items{{"name", Variant(std::string("pad"))}};
    Variant v(items);
    EXPECT_EQ(v.dict_at("name").as_string(), "pad");
}

TEST(VariantDictTest, DictAtMissingKeyReturnsNil) {
    Variant v(std::unordered_map<std::string, Variant>{{"a", Variant(int64_t(1))}});
    EXPECT_TRUE(v.dict_at("missing").is_nil());
}

TEST(VariantDictTest, DictHasReflectsMembership) {
    Variant v(std::unordered_map<std::string, Variant>{{"a", Variant(int64_t(1))}});
    EXPECT_TRUE(v.dict_has("a"));
    EXPECT_FALSE(v.dict_has("b"));
}

TEST(VariantDictTest, SetDictElementInsertsAndUpdates) {
    Variant v(std::unordered_map<std::string, Variant>{});
    v.set_dict_element("x", Variant(int64_t(1)));  // 插入
    v.set_dict_element("y", Variant(int64_t(2)));  // 插入
    EXPECT_EQ(v.dict_size(), 2u);
    v.set_dict_element("x", Variant(int64_t(99)));  // 更新
    EXPECT_EQ(v.dict_size(), 2u);
    EXPECT_EQ(v.dict_at("x").as_int(), 99);
    EXPECT_EQ(v.dict_at("y").as_int(), 2);
}

TEST(VariantDictTest, DictEraseRemovesKey) {
    Variant v(std::unordered_map<std::string, Variant>{
        {"a", Variant(int64_t(1))}, {"b", Variant(int64_t(2))}});
    EXPECT_EQ(v.dict_erase("a"), 1u);
    EXPECT_FALSE(v.dict_has("a"));
    EXPECT_TRUE(v.dict_has("b"));
    EXPECT_EQ(v.dict_size(), 1u);
    EXPECT_EQ(v.dict_erase("missing"), 0u);
}

TEST(VariantDictTest, SetDictElementOnNonDictIsNoop) {
    Variant v(int64_t(42));
    v.set_dict_element("k", Variant(int64_t(1)));
    EXPECT_EQ(v.get_type(), Variant::INT);
}

// ===== DICTIONARY COW 隔离 =====

TEST(VariantDictCowTest, CopySharesAndMutationIsolates) {
    Variant a(std::unordered_map<std::string, Variant>{{"x", Variant(int64_t(1))}});
    Variant b = a;
    b.set_dict_element("x", Variant(int64_t(99)));
    b.set_dict_element("y", Variant(int64_t(2)));

    EXPECT_EQ(a.dict_at("x").as_int(), 1);
    EXPECT_FALSE(a.dict_has("y"));
    EXPECT_EQ(a.dict_size(), 1u);
    EXPECT_EQ(b.dict_at("x").as_int(), 99);
    EXPECT_EQ(b.dict_size(), 2u);
}

// ===== 嵌套 ARRAY / DICTIONARY =====

TEST(VariantNestedTest, ArrayOfArrays) {
    std::vector<Variant> inner1{Variant(int64_t(1)), Variant(int64_t(2))};
    std::vector<Variant> inner2{Variant(int64_t(3))};
    std::vector<Variant> outer{Variant(inner1), Variant(inner2)};
    Variant v(outer);

    EXPECT_EQ(v.array_size(), 2u);
    EXPECT_EQ(v.array_at(0).get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_at(0).array_size(), 2u);
    EXPECT_EQ(v.array_at(0).array_at(1).as_int(), 2);
    EXPECT_EQ(v.array_at(1).array_at(0).as_int(), 3);
}

TEST(VariantNestedTest, ArrayOfDicts) {
    std::unordered_map<std::string, Variant> pad{
        {"name", Variant(std::string("P1"))},
        {"size", Variant(int64_t(42))}};
    std::vector<Variant> arr{Variant(pad), Variant(int64_t(7))};
    Variant v(arr);

    ASSERT_EQ(v.array_at(0).get_type(), Variant::DICTIONARY);
    EXPECT_EQ(v.array_at(0).dict_at("name").as_string(), "P1");
    EXPECT_EQ(v.array_at(0).dict_at("size").as_int(), 42);
    EXPECT_EQ(v.array_at(1).as_int(), 7);
}

TEST(VariantNestedTest, DictOfArrays) {
    std::vector<Variant> points{Variant(Vector2(1.0f, 2.0f)), Variant(Vector2(3.0f, 4.0f))};
    std::unordered_map<std::string, Variant> root{{"points", Variant(points)}, {"count", Variant(int64_t(2))}};
    Variant v(root);

    ASSERT_EQ(v.dict_at("points").get_type(), Variant::ARRAY);
    EXPECT_EQ(v.dict_at("points").array_size(), 2u);
    EXPECT_FLOAT_EQ(v.dict_at("points").array_at(0).as_vector2().x, 1.0f);
    EXPECT_FLOAT_EQ(v.dict_at("points").array_at(1).as_vector2().y, 4.0f);
    EXPECT_EQ(v.dict_at("count").as_int(), 2);
}

// ===== JSON 序列化：基本类型 =====

TEST(VariantJsonSerializeTest, Nil) {
    Variant v;
    EXPECT_EQ(v.serialize(), "null");
}

TEST(VariantJsonSerializeTest, Bool) {
    EXPECT_EQ(Variant(true).serialize(), "true");
    EXPECT_EQ(Variant(false).serialize(), "false");
}

TEST(VariantJsonSerializeTest, Int) {
    EXPECT_EQ(Variant(int64_t(42)).serialize(), "42");
    EXPECT_EQ(Variant(int64_t(-5)).serialize(), "-5");
    EXPECT_EQ(Variant(int64_t(0)).serialize(), "0");
}

TEST(VariantJsonSerializeTest, FloatPreservesDecimalShape) {
    // FLOAT 必须保留浮点字面量形态，确保反序列化识别为 FLOAT 而非 INT
    EXPECT_EQ(Variant(42.0).serialize(), "42.0");
    EXPECT_EQ(Variant(3.5).serialize(), "3.5");
    EXPECT_EQ(Variant(-1.25).serialize(), "-1.25");
}

TEST(VariantJsonSerializeTest, String) {
    EXPECT_EQ(Variant(std::string("hello")).serialize(), "\"hello\"");
    EXPECT_EQ(Variant(std::string("")).serialize(), "\"\"");
}

TEST(VariantJsonSerializeTest, StringEscaping) {
    EXPECT_EQ(Variant(std::string("a\"b")).serialize(), "\"a\\\"b\"");
    EXPECT_EQ(Variant(std::string("a\\b")).serialize(), "\"a\\\\b\"");
    EXPECT_EQ(Variant(std::string("a\nb")).serialize(), "\"a\\nb\"");
    EXPECT_EQ(Variant(std::string("a\tb")).serialize(), "\"a\\tb\"");
}

TEST(VariantJsonSerializeTest, ArrayBasic) {
    std::vector<Variant> items{Variant(int64_t(1)), Variant(int64_t(2)), Variant(int64_t(3))};
    EXPECT_EQ(Variant(items).serialize(), "[1,2,3]");
}

TEST(VariantJsonSerializeTest, EmptyArrayAndDict) {
    EXPECT_EQ(Variant(std::vector<Variant>{}).serialize(), "[]");
    EXPECT_EQ(Variant(std::unordered_map<std::string, Variant>{}).serialize(), "{}");
}

TEST(VariantJsonSerializeTest, Vector2Shape) {
    Variant v(Vector2(1.5f, 2.5f));
    EXPECT_EQ(v.serialize(), "{\"x\":1.5,\"y\":2.5}");
}

TEST(VariantJsonSerializeTest, ColorShape) {
    Variant c(Color{1.0f, 0.5f, 0.25f, 1.0f});
    EXPECT_EQ(c.serialize(), "{\"r\":1,\"g\":0.5,\"b\":0.25,\"a\":1}");
}

// ===== JSON 反序列化：基本类型 =====

TEST(VariantJsonDeserializeTest, ParsesNull) {
    Variant v = Variant::deserialize("null");
    EXPECT_EQ(v.get_type(), Variant::NIL);
}

TEST(VariantJsonDeserializeTest, ParsesBool) {
    EXPECT_EQ(Variant::deserialize("true").as_bool(), true);
    EXPECT_EQ(Variant::deserialize("false").as_bool(), false);
}

TEST(VariantJsonDeserializeTest, ParsesInt) {
    Variant v = Variant::deserialize("123");
    EXPECT_EQ(v.get_type(), Variant::INT);
    EXPECT_EQ(v.as_int(), 123);
}

TEST(VariantJsonDeserializeTest, ParsesNegativeInt) {
    Variant v = Variant::deserialize("-456");
    EXPECT_EQ(v.get_type(), Variant::INT);
    EXPECT_EQ(v.as_int(), -456);
}

TEST(VariantJsonDeserializeTest, ParsesFloat) {
    Variant v = Variant::deserialize("3.14");
    EXPECT_EQ(v.get_type(), Variant::FLOAT);
    EXPECT_NEAR(v.as_float(), 3.14, 1e-9);
}

TEST(VariantJsonDeserializeTest, ParsesSciFloat) {
    Variant v = Variant::deserialize("1.5e3");
    EXPECT_EQ(v.get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(v.as_float(), 1500.0);
}

TEST(VariantJsonDeserializeTest, ParsesString) {
    Variant v = Variant::deserialize("\"hello\"");
    EXPECT_EQ(v.get_type(), Variant::STRING);
    EXPECT_EQ(v.as_string(), "hello");
}

TEST(VariantJsonDeserializeTest, ParsesStringWithEscapes) {
    Variant v = Variant::deserialize("\"a\\\"b\\nc\"");
    EXPECT_EQ(v.as_string(), "a\"b\nc");
}

TEST(VariantJsonDeserializeTest, ParsesArray) {
    Variant v = Variant::deserialize("[1,2,3]");
    EXPECT_EQ(v.get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_size(), 3u);
    EXPECT_EQ(v.array_at(0).as_int(), 1);
    EXPECT_EQ(v.array_at(2).as_int(), 3);
}

TEST(VariantJsonDeserializeTest, ParsesEmptyArray) {
    Variant v = Variant::deserialize("[]");
    EXPECT_EQ(v.get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_size(), 0u);
}

TEST(VariantJsonDeserializeTest, ParsesObjectAsDict) {
    Variant v = Variant::deserialize("{\"name\":\"pad\",\"size\":42}");
    EXPECT_EQ(v.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(v.dict_size(), 2u);
    EXPECT_EQ(v.dict_at("name").as_string(), "pad");
    EXPECT_EQ(v.dict_at("size").as_int(), 42);
}

TEST(VariantJsonDeserializeTest, ParsesObjectAsVector2) {
    Variant v = Variant::deserialize("{\"x\":1.5,\"y\":2.5}");
    EXPECT_EQ(v.get_type(), Variant::VECTOR2);
    EXPECT_FLOAT_EQ(v.as_vector2().x, 1.5f);
    EXPECT_FLOAT_EQ(v.as_vector2().y, 2.5f);
}

TEST(VariantJsonDeserializeTest, ParsesObjectAsColor) {
    Variant v = Variant::deserialize("{\"r\":1,\"g\":0.5,\"b\":0.25,\"a\":1}");
    EXPECT_EQ(v.get_type(), Variant::COLOR);
    EXPECT_FLOAT_EQ(v.as_color().r, 1.0f);
    EXPECT_FLOAT_EQ(v.as_color().g, 0.5f);
    EXPECT_FLOAT_EQ(v.as_color().b, 0.25f);
    EXPECT_FLOAT_EQ(v.as_color().a, 1.0f);
}

TEST(VariantJsonDeserializeTest, IgnoresWhitespace) {
    Variant v = Variant::deserialize("  [ 1 , 2 ]  ");
    EXPECT_EQ(v.array_size(), 2u);
    EXPECT_EQ(v.array_at(0).as_int(), 1);
}

// ===== JSON 往返无损 =====

TEST(VariantJsonRoundTripTest, NilRoundtrip) {
    Variant v;
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::NIL);
}

TEST(VariantJsonRoundTripTest, BoolRoundtrip) {
    Variant v(true);
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::BOOL);
    EXPECT_EQ(parsed.as_bool(), true);
}

TEST(VariantJsonRoundTripTest, IntRoundtrip) {
    Variant v(int64_t(-9876543210LL));
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::INT);
    EXPECT_EQ(parsed.as_int(), -9876543210LL);
}

TEST(VariantJsonRoundTripTest, FloatRoundtripLossless) {
    // %.17g 保证 double 可逆
    Variant v(3.141592653589793);
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(parsed.as_float(), 3.141592653589793);
}

TEST(VariantJsonRoundTripTest, FloatIntegerValuedStaysFloat) {
    Variant v(42.0);
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::FLOAT);  // 不可降级为 INT
    EXPECT_DOUBLE_EQ(parsed.as_float(), 42.0);
}

TEST(VariantJsonRoundTripTest, MixedFloatsInArrayPreserveTypes) {
    // 回归：append_double 曾误查整个输出缓冲——前一个含 '.' 的浮点会让后续的
    // 整数值浮点（如 42.0）退化为 INT。这里确保 [3.5, 42.0] 双双保持 FLOAT。
    std::vector<Variant> items{Variant(3.5), Variant(42.0), Variant(100.0)};
    Variant v(items);
    Variant parsed = Variant::deserialize(v.serialize());
    ASSERT_EQ(parsed.array_size(), 3u);
    EXPECT_EQ(parsed.array_at(0).get_type(), Variant::FLOAT);
    EXPECT_EQ(parsed.array_at(1).get_type(), Variant::FLOAT);
    EXPECT_EQ(parsed.array_at(2).get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(parsed.array_at(1).as_float(), 42.0);
    EXPECT_DOUBLE_EQ(parsed.array_at(2).as_float(), 100.0);
}

TEST(VariantJsonRoundTripTest, StringRoundtrip) {
    Variant v(std::string("hello world"));
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::STRING);
    EXPECT_EQ(parsed.as_string(), "hello world");
}

TEST(VariantJsonRoundTripTest, StringWithUnicodeAndEscapesRoundtrip) {
    std::string original = "quote:\" backslash:\\ newline:\n tab:\t emoji:😀";
    Variant v(original);
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.as_string(), original);
}

TEST(VariantJsonRoundTripTest, Vector2Roundtrip) {
    Variant v(Vector2(12.5f, -7.25f));
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::VECTOR2);
    EXPECT_FLOAT_EQ(parsed.as_vector2().x, 12.5f);
    EXPECT_FLOAT_EQ(parsed.as_vector2().y, -7.25f);
}

TEST(VariantJsonRoundTripTest, ColorRoundtrip) {
    Variant v(Color{0.1f, 0.2f, 0.3f, 0.4f});
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::COLOR);
    EXPECT_FLOAT_EQ(parsed.as_color().r, 0.1f);
    EXPECT_FLOAT_EQ(parsed.as_color().g, 0.2f);
    EXPECT_FLOAT_EQ(parsed.as_color().b, 0.3f);
    EXPECT_FLOAT_EQ(parsed.as_color().a, 0.4f);
}

TEST(VariantJsonRoundTripTest, EmptyArrayRoundtrip) {
    Variant v(std::vector<Variant>{});
    Variant parsed = Variant::deserialize(v.serialize());
    EXPECT_EQ(parsed.get_type(), Variant::ARRAY);
    EXPECT_EQ(parsed.array_size(), 0u);
}

TEST(VariantJsonRoundTripTest, HeterogeneousArrayRoundtrip) {
    std::vector<Variant> items{
        Variant(int64_t(1)),
        Variant(2.5),
        Variant(std::string("three")),
        Variant(true),
        Variant()};
    Variant v(items);
    Variant parsed = Variant::deserialize(v.serialize());
    ASSERT_EQ(parsed.get_type(), Variant::ARRAY);
    ASSERT_EQ(parsed.array_size(), 5u);
    EXPECT_EQ(parsed.array_at(0).as_int(), 1);
    EXPECT_EQ(parsed.array_at(1).get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(parsed.array_at(1).as_float(), 2.5);
    EXPECT_EQ(parsed.array_at(2).as_string(), "three");
    EXPECT_EQ(parsed.array_at(3).as_bool(), true);
    EXPECT_EQ(parsed.array_at(4).get_type(), Variant::NIL);
}

TEST(VariantJsonRoundTripTest, DictionaryRoundtrip) {
    std::unordered_map<std::string, Variant> items{
        {"name", Variant(std::string("pad"))},
        {"count", Variant(int64_t(3))},
        {"ratio", Variant(0.5)}};
    Variant v(items);
    Variant parsed = Variant::deserialize(v.serialize());
    ASSERT_EQ(parsed.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(parsed.dict_size(), 3u);
    EXPECT_EQ(parsed.dict_at("name").as_string(), "pad");
    EXPECT_EQ(parsed.dict_at("count").as_int(), 3);
    EXPECT_DOUBLE_EQ(parsed.dict_at("ratio").as_float(), 0.5);
}

TEST(VariantJsonRoundTripTest, NestedStructureRoundtrip) {
    // 模拟一个 PCB 焊盘描述：{pos: {x,y}, layers: ["top","bottom"], size: 1.5}
    std::unordered_map<std::string, Variant> pad{
        {"pos", Variant(Vector2(1.25f, 3.75f))},
        {"layers", Variant(std::vector<Variant>{Variant(std::string("top")), Variant(std::string("bottom"))})},
        {"size", Variant(1.5)}};
    Variant v(pad);

    Variant parsed = Variant::deserialize(v.serialize());
    ASSERT_EQ(parsed.get_type(), Variant::DICTIONARY);
    EXPECT_EQ(parsed.dict_at("pos").get_type(), Variant::VECTOR2);
    EXPECT_FLOAT_EQ(parsed.dict_at("pos").as_vector2().x, 1.25f);
    EXPECT_FLOAT_EQ(parsed.dict_at("pos").as_vector2().y, 3.75f);
    ASSERT_EQ(parsed.dict_at("layers").get_type(), Variant::ARRAY);
    EXPECT_EQ(parsed.dict_at("layers").array_size(), 2u);
    EXPECT_EQ(parsed.dict_at("layers").array_at(0).as_string(), "top");
    EXPECT_EQ(parsed.dict_at("layers").array_at(1).as_string(), "bottom");
    EXPECT_EQ(parsed.dict_at("size").get_type(), Variant::FLOAT);
    EXPECT_DOUBLE_EQ(parsed.dict_at("size").as_float(), 1.5);
}

// ===== 循环引用防护 =====

TEST(VariantCycleProtectionTest, SelfAppendBreaksAlias) {
    // v.append(v) 不做防护时会令 v 末元素共享 v 自身存储，serialize 会无限递归。
    Variant v(std::vector<Variant>{Variant(int64_t(1)), Variant(int64_t(2))});
    v.append(v);

    // 应得到 [1, 2, [1, 2]] —— 末元素是 append 时刻的快照（2 个元素），而非共享自身
    ASSERT_EQ(v.array_size(), 3u);
    ASSERT_EQ(v.array_at(2).get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_at(2).array_size(), 2u);
    EXPECT_EQ(v.array_at(2).array_at(0).as_int(), 1);
    EXPECT_EQ(v.array_at(2).array_at(1).as_int(), 2);

    // serialize 必须正常终止（深度上限 + 快照隔离双重保护）
    std::string s = v.serialize();
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s, "[1,2,[1,2]]");
}

TEST(VariantCycleProtectionTest, SelfSetArrayElementBreaksAlias) {
    Variant v(std::vector<Variant>{Variant(int64_t(1)), Variant(int64_t(2))});
    v.set_array_element(0, v);  // 用 v 自身的快照替换 v[0]

    ASSERT_EQ(v.array_size(), 2u);
    ASSERT_EQ(v.array_at(0).get_type(), Variant::ARRAY);
    EXPECT_EQ(v.array_at(0).array_size(), 2u);  // 快照：[1, 2]
    EXPECT_EQ(v.array_at(0).array_at(0).as_int(), 1);
    EXPECT_EQ(v.array_at(1).as_int(), 2);       // v[1] 未被替换

    EXPECT_NO_FATAL_FAILURE(v.serialize());
}

TEST(VariantCycleProtectionTest, SerializeHasDepthLimit) {
    // 构造深度嵌套（远超上限）的数组，serialize 必须由深度上限终止而不崩溃
    Variant inner(int64_t(0));
    constexpr int kDepth = 200;
    for (int i = 0; i < kDepth; ++i) {
        Variant wrapper(std::vector<Variant>{inner});
        inner = wrapper;
    }
    EXPECT_NO_FATAL_FAILURE(inner.serialize());
}

TEST(VariantCycleProtectionTest, SelfSetDictElementBreaksAlias) {
    Variant v(std::unordered_map<std::string, Variant>{{"k", Variant(int64_t(1))}});
    v.set_dict_element("self", v);  // 自引用：用快照填入
    ASSERT_EQ(v.dict_size(), 2u);
    ASSERT_EQ(v.dict_at("self").get_type(), Variant::DICTIONARY);
    EXPECT_EQ(v.dict_at("self").dict_size(), 1u);  // 快照：原始 1 键
    EXPECT_EQ(v.dict_at("self").dict_at("k").as_int(), 1);
    EXPECT_NO_FATAL_FAILURE(v.serialize());
}
