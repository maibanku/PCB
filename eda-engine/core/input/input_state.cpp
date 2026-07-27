#include "core/input/input_state.h"

namespace eda {

void InputState::push(InputEvent e) {
    if (e.type == InputEvent::Type::KEY) {
        // PRESS / REPEAT 视为按下；RELEASE 从按下集合移除
        if (e.action == KeyAction::PRESS || e.action == KeyAction::REPEAT) {
            keys_down_.insert(e.key);
        } else if (e.action == KeyAction::RELEASE) {
            keys_down_.erase(e.key);
        }
    } else if (e.type == InputEvent::Type::MOUSE_MOVE) {
        mouse_pos_ = e.mouse_pos;
    }
    // MOUSE_BUTTON / MOUSE_SCROLL 仅入队（mouse_pos_ 由上层在构造事件时填好），
    // 这里不再额外更新状态——按下语义交给业务层判定。
    events_.push_back(e);
}

bool InputState::poll(InputEvent& out) {
    if (events_.empty()) {
        return false;
    }
    out = events_.front();
    events_.erase(events_.begin());
    return true;
}

}  // namespace eda
