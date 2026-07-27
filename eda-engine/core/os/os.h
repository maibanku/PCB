#pragma once

// OS —— 操作系统抽象最小集（对齐 Godot core/os/os.h）。
//
// P2 Task 5 仅暴露 Resource 系统所需的最小接口：
//   * get_singleton()              —— 进程级唯一实例（指针风格，与 Godot 一致）
//   * get_resource_root() /
//     set_resource_root(string)    —— res:// 虚拟路径映射的磁盘根
//   * resolve_resource_path(path)  —— "res://foo.toml" -> root + "foo.toml"
//   * get_ticks_msec()             —— 进程启动后经过的毫秒数（占位，基于 steady_clock）
//
// 集成契约：本头文件不依赖 core/object/*，避免与 Resource 相互引用。
// 默认 resource_root_ 为相对 "res/"；由 main.cpp 在启动期改为可执行目录同级 res/
// 绝对路径；测试在 SetUp 中 set_resource_root 到临时目录。

#include <chrono>
#include <cstdint>
#include <string>

namespace eda {

class OS {
public:
    // 进程级单例。首次调用惰性构造；永不销毁（与 Godot 一致，避免析构顺序问题）。
    static OS* get_singleton();

    // res:// 解析根（绝对路径或相对路径；尾部分隔符可选）
    const std::string& get_resource_root() const { return resource_root_; }
    void set_resource_root(std::string root) { resource_root_ = std::move(root); }

    // "res://foo.toml" -> resource_root_ + "foo.toml"（自动补足尾部分隔符）。
    // 不以 "res://" 开头的路径原样返回（视为已是磁盘路径）。
    std::string resolve_resource_path(const std::string& path) const;

    // 自单例构造以来经过的毫秒数；steady_clock 单调递增。
    uint64_t get_ticks_msec() const;

    OS(const OS&) = delete;
    OS& operator=(const OS&) = delete;

private:
    OS();
    static OS* singleton_;

    std::string resource_root_;
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace eda
