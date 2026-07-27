#include "core/object/ref_counted.h"

namespace eda {

bool RefCounted::unref() {
    // acq_rel：保证减操作与之前所有对对象数据的写入可见于即将销毁的析构链。
    int prev = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 1) {
        // refcount 归零：在析构前给对象一次"收到 PREDELETE"的机会，
        // 使派生类可在析构前释放信号连接 / 资源句柄。
        notification(NOTIFICATION_PREDELETE);
        delete this;
        return true;
    }
    return false;
}

}  // namespace eda
