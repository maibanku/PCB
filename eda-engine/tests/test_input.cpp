#include <gtest/gtest.h>

#include "core/input/input_state.h"

using namespace eda;

// Plan Task 3 Step 2 用例 1：push 后可 poll 出事件，且按键进入"按下"状态；poll 完队列空
TEST(InputTest, PushAndPollKey) {
    InputState s;
    s.push({InputEvent::Type::KEY, 65, KeyAction::PRESS});
    InputEvent e;
    ASSERT_TRUE(s.poll(e));
    EXPECT_EQ(e.key, 65);
    EXPECT_TRUE(s.is_key_down(65));
    EXPECT_FALSE(s.poll(e));
}

// Plan Task 3 Step 2 用例 2：同一键 PRESS 后再 RELEASE，按下状态被清除
TEST(InputTest, ReleaseRemovesKey) {
    InputState s;
    s.push({InputEvent::Type::KEY, 65, KeyAction::PRESS});
    s.push({InputEvent::Type::KEY, 65, KeyAction::RELEASE});
    EXPECT_FALSE(s.is_key_down(65));
}

// Plan Task 3 Step 2 用例 3：MOUSE_MOVE 事件更新 mouse_position()
TEST(InputTest, MouseMoveUpdatesPosition) {
    InputState s;
    s.push({InputEvent::Type::MOUSE_MOVE, 0, KeyAction::PRESS, Vector2{10, 20}});
    InputEvent e;
    s.poll(e);
    EXPECT_EQ(s.mouse_position(), (Vector2{10, 20}));
}

// 帧边界 clear()：丢弃未消费事件队列，但保留持久状态（keys_down_ / mouse_pos_）
TEST(InputTest, FrameClearDropsEventQueueButKeepsState) {
    InputState s;
    s.push({InputEvent::Type::KEY, 65, KeyAction::PRESS});
    s.push({InputEvent::Type::MOUSE_MOVE, 0, KeyAction::PRESS, Vector2{10, 20}});

    s.clear();

    InputEvent e;
    EXPECT_FALSE(s.poll(e));                              // 事件队列已清空
    EXPECT_TRUE(s.is_key_down(65));                       // 按键状态保留
    EXPECT_EQ(s.mouse_position(), (Vector2{10, 20}));     // 鼠标位置保留
}
