#pragma once

#include "core/input/input_event.h"
#include "core/math/vector2.h"

#include <unordered_set>
#include <vector>

namespace eda {

// 输入状态机：维护当前帧的输入事件队列与持久输入状态（按下键集合 / 鼠标位置）。
//
//   - push()        : 上层把一个 InputEvent 投递进来；同步更新 keys_down_ / mouse_pos_
//   - poll()        : FIFO 取出队列前端事件；取空返回 false
//   - is_key_down() : 查询某键当前是否处于按下状态（跨帧持久，直到对应 RELEASE）
//   - mouse_position() : 查询最近一次鼠标位置（跨帧持久）
//   - clear()       : 帧边界调用——丢弃未消费的事件队列，但保留 keys_down_ / mouse_pos_
//                     （状态代表"当前输入"，事件代表"本帧增量"，二者语义分离）
class InputState {
public:
    void push(InputEvent e);
    bool poll(InputEvent& out);

    bool is_key_down(int key) const { return keys_down_.count(key) > 0; }
    Vector2 mouse_position() const { return mouse_pos_; }

    void clear() { events_.clear(); }

private:
    std::vector<InputEvent> events_;
    std::unordered_set<int> keys_down_;
    Vector2 mouse_pos_{};
};

}  // namespace eda
