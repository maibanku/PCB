#include <gtest/gtest.h>

#include <string>

#include "core/object/object.h"
#include "core/object/ref.h"
#include "core/object/ref_counted.h"
#include "core/object/variant.h"

using namespace eda;

namespace {

class SpyObject : public Object {
public:
    std::string get_class() const override { return "SpyObject"; }
};

class SpyRefCounted : public RefCounted {
public:
    static inline int alive = 0;
    static inline int predelete_calls = 0;
    SpyRefCounted() { ++alive; }
    ~SpyRefCounted() override { --alive; }
    void notification(int what) override {
        if (what == NOTIFICATION_PREDELETE) ++predelete_calls;
    }
    static void reset_counters() { alive = 0; predelete_calls = 0; }
};

}  // namespace

// ===== Object 基类 =====

TEST(ObjectTest, GetClassReturnsObjectByDefault) {
    Object o;
    EXPECT_EQ(o.get_class(), "Object");
}

TEST(ObjectTest, GetClassStaticReturnsObject) {
    EXPECT_EQ(Object::get_class_static(), "Object");
}

TEST(ObjectTest, IsClassMatchesByString) {
    Object o;
    EXPECT_TRUE(o.is_class("Object"));
    EXPECT_FALSE(o.is_class("Node"));
    EXPECT_FALSE(o.is_class("RefCounted"));
}

TEST(ObjectTest, NotificationBaseDefaultIsNoop) {
    Object o;
    o.notification(Object::NOTIFICATION_PREDELETE);
    o.notification(Object::NOTIFICATION_POSTINITIALIZE);
    SUCCEED();
}

TEST(ObjectTest, DerivedOverridesGetClass) {
    SpyObject s;
    EXPECT_EQ(s.get_class(), "SpyObject");
    // 注：Task 1 的 is_class 仅做本类名匹配；父类链识别在 Task 2 ClassDB 落地后
}

TEST(ObjectTest, SetMetaAndGetMetaRoundtrip) {
    Object o;
    o.set_meta("score", Variant(int64_t(42)));
    EXPECT_EQ(o.get_meta("score").as_int(), 42);
    o.set_meta("name", Variant(std::string("pad")));
    EXPECT_EQ(o.get_meta("name").as_string(), "pad");
}

TEST(ObjectTest, GetMetaReturnsDefaultIfAbsent) {
    Object o;
    EXPECT_EQ(o.get_meta("missing", Variant(int64_t(7))).as_int(), 7);
    EXPECT_TRUE(o.get_meta("missing").is_nil());
}

TEST(ObjectTest, HasMetaReflectsSetMeta) {
    Object o;
    EXPECT_FALSE(o.has_meta("k"));
    o.set_meta("k", Variant(int64_t(1)));
    EXPECT_TRUE(o.has_meta("k"));
}

TEST(ObjectTest, SetGetWithoutClassDBReturnsFalseAndNil) {
    Object o;
    EXPECT_FALSE(o.set("no_such_prop", Variant(int64_t(1))));
    EXPECT_TRUE(o.get("no_such_prop").is_nil());
}

// ===== Variant 最小子集（Task 3 扩展） =====

TEST(VariantTest, DefaultIsNil) {
    Variant v;
    EXPECT_EQ(v.get_type(), Variant::NIL);
    EXPECT_TRUE(v.is_nil());
}

TEST(VariantTest, IntCrossConvertsToFloat) {
    Variant v(int64_t(42));
    EXPECT_EQ(v.get_type(), Variant::INT);
    EXPECT_EQ(v.as_int(), 42);
    EXPECT_DOUBLE_EQ(v.as_float(), 42.0);
}

TEST(VariantTest, StringToIntBestEffort) {
    Variant v(std::string("123"));
    EXPECT_EQ(v.as_int(), 123);
    EXPECT_EQ(v.as_string(), "123");
}

TEST(VariantTest, NilAsIntReturnsZero) {
    Variant v;
    EXPECT_EQ(v.as_int(), 0);
    EXPECT_EQ(v.as_string(), std::string());
}

// ===== RefCounted =====

TEST(RefCountedTest, IsClassRecognizesSelfAndObject) {
    RefCounted r;
    EXPECT_EQ(r.get_class(), "RefCounted");
    EXPECT_TRUE(r.is_class("RefCounted"));
    EXPECT_TRUE(r.is_class("Object"));  // 父类链（硬编码）
}

TEST(RefCountedTest, InitialReferenceCountIsZero) {
    auto* r = new RefCounted();
    EXPECT_EQ(r->get_reference_count(), 0);
    r->ref();
    EXPECT_EQ(r->get_reference_count(), 1);
    r->unref();  // 1->0 delete
}

TEST(RefCountedTest, RefAndUnrefManipulateCount) {
    auto* r = new RefCounted();
    r->ref();
    r->ref();
    EXPECT_EQ(r->get_reference_count(), 2);
    EXPECT_FALSE(r->unref());  // 2->1, 不删
    EXPECT_EQ(r->get_reference_count(), 1);
    r->unref();                // 1->0 delete（不再访问 r）
}

TEST(RefCountedTest, UnrefAtZeroEmitsPredeleteAndDeletes) {
    // unref 在 refcount 归零时：先发 NOTIFICATION_PREDELETE，再 delete this，
    // 返回 true 表示对象已被销毁（与"指针已不可用"等价）。
    SpyRefCounted::reset_counters();
    auto* r = new SpyRefCounted();
    r->ref();
    bool destroyed = r->unref();  // 1->0 → PREDELETE → delete
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(SpyRefCounted::predelete_calls, 1);
    EXPECT_EQ(SpyRefCounted::alive, 0);
}

TEST(RefCountedTest, UnrefAboveZeroKeepsAlive) {
    SpyRefCounted::reset_counters();
    auto* r = new SpyRefCounted();
    r->ref();
    r->ref();
    EXPECT_FALSE(r->unref());  // 2->1
    EXPECT_EQ(SpyRefCounted::predelete_calls, 0);
    EXPECT_EQ(SpyRefCounted::alive, 1);
    r->unref();                // 1->0 delete
    EXPECT_EQ(SpyRefCounted::predelete_calls, 1);
}

// ===== Ref<T> =====

TEST(RefTest, DefaultRefIsEmpty) {
    Ref<RefCounted> r;
    EXPECT_FALSE(r.valid());
    EXPECT_EQ(r.get(), nullptr);
}

TEST(RefTest, TakesOwnershipAndReleasesOnDtor) {
    SpyRefCounted::reset_counters();
    {
        Ref<SpyRefCounted> r(new SpyRefCounted());
        ASSERT_TRUE(r.valid());
        EXPECT_EQ(r->get_reference_count(), 1);
        EXPECT_EQ(SpyRefCounted::alive, 1);
    }
    EXPECT_EQ(SpyRefCounted::alive, 0);
    EXPECT_EQ(SpyRefCounted::predelete_calls, 1);
}

TEST(RefTest, CopyIncrementsCount) {
    SpyRefCounted::reset_counters();
    Ref<SpyRefCounted> a(new SpyRefCounted());
    Ref<SpyRefCounted> b = a;  // copy ctor
    EXPECT_EQ(a->get_reference_count(), 2);
    EXPECT_EQ(b->get_reference_count(), 2);
    a.reset();
    EXPECT_EQ(b->get_reference_count(), 1);
}

TEST(RefTest, MoveTransfersOwnership) {
    SpyRefCounted::reset_counters();
    Ref<SpyRefCounted> a(new SpyRefCounted());
    Ref<SpyRefCounted> b = std::move(a);
    EXPECT_FALSE(a.valid());
    ASSERT_TRUE(b.valid());
    EXPECT_EQ(b->get_reference_count(), 1);
}

TEST(RefTest, ResetReleasesAndBecomesEmpty) {
    SpyRefCounted::reset_counters();
    Ref<SpyRefCounted> r(new SpyRefCounted());
    r.reset();
    EXPECT_FALSE(r.valid());
    EXPECT_EQ(SpyRefCounted::alive, 0);
}

TEST(RefTest, AssignmentReleasesPrevious) {
    SpyRefCounted::reset_counters();
    Ref<SpyRefCounted> a(new SpyRefCounted());
    Ref<SpyRefCounted> b(new SpyRefCounted());
    EXPECT_EQ(SpyRefCounted::alive, 2);
    a = b;  // a 的旧对象 unref 归零销毁；a/b 共享 b 的对象
    EXPECT_EQ(SpyRefCounted::alive, 1);
    EXPECT_EQ(a->get_reference_count(), 2);
}
