// Signal / Callable —— 非模板部分实现。
//
// 模板代码（Signal<Args...>、Callable::Model、Callable 构造函数）全部在头文件内；
// 此处仅承载 Callable 的非模板成员（invoke / get_object / operator==）的 out-of-line
// 定义，便于 eda_core 静态库提供一个稳定的翻译单元（也方便日后追加 MessageQueue 等）。

#include "core/object/signal.h"

namespace eda {

void Callable::invoke(const Variant* args, std::size_t argc) const {
    if (impl_) {
        impl_->invoke(args, argc);
    }
}

Object* Callable::get_object() const {
    return impl_ ? impl_->get_object() : nullptr;
}

bool Callable::operator==(const Callable& other) const {
    // 共享同一 Concept 实例（含同为 nullptr）视为相等；不支持跨实例结构化相等。
    return impl_ == other.impl_;
}

}  // namespace eda
