#include "core/os/os.h"

namespace eda {

OS* OS::singleton_ = nullptr;

OS::OS()
    : resource_root_("res/"),
      start_time_(std::chrono::steady_clock::now()) {}

OS* OS::get_singleton() {
    if (singleton_ == nullptr) {
        singleton_ = new OS();
    }
    return singleton_;
}

std::string OS::resolve_resource_path(const std::string& path) const {
    static const std::string prefix = "res://";
    if (path.size() >= prefix.size() &&
        path.compare(0, prefix.size(), prefix) == 0) {
        std::string suffix = path.substr(prefix.size());
        std::string root = resource_root_;
        // 自动补足尾部分隔符，避免拼出 "resfoo.toml"；同时容忍正反斜杠。
        if (!root.empty() && root.back() != '/' && root.back() != '\\') {
            root += '/';
        }
        return root + suffix;
    }
    return path;
}

uint64_t OS::get_ticks_msec() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    return static_cast<uint64_t>(elapsed.count());
}

}  // namespace eda
