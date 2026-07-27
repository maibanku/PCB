// Signal / Callable 单元测试（P2 Task 4）。
//
// 覆盖：
//   - 三种 connect 形态：Callable 包装的成员函数、直接传入成员函数指针、lambda、自由函数
//   - emit 基本触发与多连接分发
//   - disconnect（按 Callable 身份）与 disconnect_all
//   - 析构解绑：Object 死后 Signal 自动清空其连接、emit 不悬空
//   - Signal 死前向 Object 注销，避免悬空 SignalBase*
//   - emit 重入安全（回调内 connect/disconnect 不破坏迭代器）
//
// 依赖集成契约（详见 signal.h 顶部说明）：Object 需提供
//   _signal_register_connection / _signal_unregister_connection，
// 并在 ~Object() 调用 _signal_disconnect_all()。

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/object/callable.h"
#include "core/object/object.h"
#include "core/object/signal.h"

using namespace eda;

namespace {

// Receiver —— 派生自 Object 的信号接收者，提供多个成员槽。
class Receiver : public Object {
public:
    std::string get_class() const override { return "Receiver"; }

    std::vector<int> received;
    int sum = 0;

    void on_int(int x) { received.push_back(x); }
    void on_two_int(int a, int b) { sum = a + b; }
};

// 既有签名：自由函数（通过 lambda 转发包装，避免对 Callable 自由函数构造的边界争议）。
void free_on_int(int x, int* sink) { *sink = x; }

// 全局槽：供"直接 connect 自由函数指针"用例验证。
namespace {
int g_free_fn_sink = 0;
void free_int_fn(int x) { g_free_fn_sink = x; }
}  // namespace

}  // namespace

// ---------------------------------------------------------------------------
// 基本连接与触发
// ---------------------------------------------------------------------------

TEST(SignalTest, ConnectCallableAndEmit) {
    Signal<int> sig;
    Receiver r;
    auto c = Callable(&Receiver::on_int, &r);
    sig.connect(c);
    sig.emit(42);
    ASSERT_EQ(r.received.size(), 1u);
    EXPECT_EQ(r.received[0], 42);
}

TEST(SignalTest, ConnectMethodDirectly) {
    Signal<int> sig;
    Receiver r;
    sig.connect(&Receiver::on_int, &r);
    sig.emit(7);
    ASSERT_EQ(r.received.size(), 1u);
    EXPECT_EQ(r.received[0], 7);
}

TEST(SignalTest, ConnectLambda) {
    Signal<int> sig;
    int sink = 0;
    sig.connect([&sink](int x) { sink = x; });
    sig.emit(99);
    EXPECT_EQ(sink, 99);
}

TEST(SignalTest, ConnectFreeFunctionViaLambda) {
    Signal<int> sig;
    int sink = 0;
    sig.connect([&sink](int x) { free_on_int(x, &sink); });
    sig.emit(33);
    EXPECT_EQ(sink, 33);
}

TEST(SignalTest, ConnectFreeFunctionPointer) {
    Signal<int> sig;
    g_free_fn_sink = 0;
    sig.connect(free_int_fn);  // 直接 connect 函数指针（走 Callable 转换构造）
    sig.emit(55);
    EXPECT_EQ(g_free_fn_sink, 55);
}

TEST(SignalTest, EmitWithoutConnectionsIsNoop) {
    Signal<int> sig;
    sig.emit(123);
    SUCCEED();
}

TEST(SignalTest, MultipleConnectionsAllFire) {
    Signal<int> sig;
    Receiver a, b;
    sig.connect(&Receiver::on_int, &a);
    sig.connect(&Receiver::on_int, &b);
    sig.emit(5);
    EXPECT_EQ(a.received.size(), 1u);
    EXPECT_EQ(b.received.size(), 1u);
    EXPECT_EQ(a.received[0], 5);
    EXPECT_EQ(b.received[0], 5);
}

TEST(SignalTest, TwoArgSignalEmitsBothArgs) {
    Signal<int, int> sig;
    Receiver r;
    sig.connect(&Receiver::on_two_int, &r);
    sig.emit(3, 4);
    EXPECT_EQ(r.sum, 7);
}

TEST(SignalTest, ConnectSameCallableIsIdempotent) {
    Signal<int> sig;
    Receiver r;
    auto c = Callable(&Receiver::on_int, &r);
    sig.connect(c);
    sig.connect(c);  // 重复：身份相同，应被忽略
    EXPECT_EQ(sig.connection_count(), 1u);
    sig.emit(1);
    EXPECT_EQ(r.received.size(), 1u);
}

// ---------------------------------------------------------------------------
// disconnect / disconnect_all
// ---------------------------------------------------------------------------

TEST(SignalTest, DisconnectByCallableStopsEmission) {
    Signal<int> sig;
    Receiver r;
    auto c = Callable(&Receiver::on_int, &r);
    sig.connect(c);
    sig.disconnect(c);
    EXPECT_EQ(sig.connection_count(), 0u);
    sig.emit(42);
    EXPECT_TRUE(r.received.empty());
}

TEST(SignalTest, DisconnectOneKeepsOthers) {
    Signal<int> sig;
    Receiver a, b;
    auto ca = Callable(&Receiver::on_int, &a);
    sig.connect(ca);
    sig.connect(&Receiver::on_int, &b);
    sig.disconnect(ca);
    sig.emit(1);
    EXPECT_TRUE(a.received.empty());
    ASSERT_EQ(b.received.size(), 1u);
    EXPECT_EQ(b.received[0], 1);
}

TEST(SignalTest, DisconnectAllClearsEverything) {
    Signal<int> sig;
    Receiver a, b;
    sig.connect(&Receiver::on_int, &a);
    sig.connect(&Receiver::on_int, &b);
    sig.disconnect_all();
    EXPECT_EQ(sig.connection_count(), 0u);
    sig.emit(1);
    EXPECT_TRUE(a.received.empty());
    EXPECT_TRUE(b.received.empty());
}

// ---------------------------------------------------------------------------
// 析构反向解绑（核心难点）
// ---------------------------------------------------------------------------

TEST(SignalTest, ObjectDestructionAutoDisconnects) {
    Signal<int> sig;
    {
        Receiver r;
        sig.connect(&Receiver::on_int, &r);
        sig.emit(10);
        ASSERT_EQ(r.received.size(), 1u);
        EXPECT_EQ(sig.connection_count(), 1u);
    }  // ~Object -> _signal_disconnect_all -> sig 清掉 target==&r 的连接
    EXPECT_EQ(sig.connection_count(), 0u);
    // 再次 emit 不悬空、不触发任何回调
    sig.emit(20);
    SUCCEED();
}

TEST(SignalTest, ObjectDestructionDoesNotAffectOtherConnections) {
    Signal<int> sig;
    Receiver keeper;
    sig.connect(&Receiver::on_int, &keeper);
    {
        Receiver tmp;
        sig.connect(&Receiver::on_int, &tmp);
        EXPECT_EQ(sig.connection_count(), 2u);
    }  // tmp 死后其连接被自动移除
    EXPECT_EQ(sig.connection_count(), 1u);
    sig.emit(77);
    ASSERT_EQ(keeper.received.size(), 1u);
    EXPECT_EQ(keeper.received[0], 77);
}

TEST(SignalTest, SignalDestructionUnregistersFromObject) {
    // Signal 先于 Object 死：Signal 析构时把自身从 Object 的 connected_signals_ 移除，
    // Object 后续析构时不应再触及本 Signal。
    Receiver r;
    {
        Signal<int> sig;
        sig.connect(&Receiver::on_int, &r);
    }  // ~Signal -> _signal_unregister_connection(&r, this)
    SUCCEED();  // 到这里无 UB 即通过
}

// ---------------------------------------------------------------------------
// 重入安全
// ---------------------------------------------------------------------------

TEST(SignalTest, EmitReentrySafeWithConnect) {
    Signal<int> sig;
    int outer = 0;
    int inner = 0;

    Callable inner_cb([&inner](int) { ++inner; });

    sig.connect([&sig, &inner_cb, &outer](int) {
        ++outer;
        sig.connect(inner_cb);  // 回调内再 connect：不影响本次迭代快照
    });

    sig.emit(1);
    // 第一次 emit 时快照仅含 outer 回调；inner_cb 尚未触发。
    EXPECT_EQ(outer, 1);
    EXPECT_EQ(inner, 0);
    EXPECT_EQ(sig.connection_count(), 2u);  // 但 connections_ 已扩展

    sig.emit(2);
    // 第二次 emit 快照含 outer + inner_cb；幂等 connect(inner_cb) 不再重复添加。
    EXPECT_EQ(outer, 2);
    EXPECT_EQ(inner, 1);
    EXPECT_EQ(sig.connection_count(), 2u);
}

TEST(SignalTest, EmitReentrySafeWithDisconnect) {
    Signal<int> sig;
    int a_count = 0;
    int b_count = 0;

    Callable b_cb([&b_count](int) { ++b_count; });

    sig.connect([&sig, &b_cb, &a_count](int) {
        ++a_count;
        sig.disconnect(b_cb);  // 回调内 disconnect：快照仍含 b_cb，本次照常触发
    });
    sig.connect(b_cb);

    sig.emit(1);
    // 快照含 a 与 b：a 先触发（同时把 b_cb 从 connections_ 移除），b_cb 仍在快照内被触发。
    EXPECT_EQ(a_count, 1);
    EXPECT_EQ(b_count, 1);
    EXPECT_EQ(sig.connection_count(), 1u);  // connections_ 中只剩 a 回调

    sig.emit(2);
    // 第二次快照仅含 a；b_cb 不再触发。
    EXPECT_EQ(a_count, 2);
    EXPECT_EQ(b_count, 1);
}
