#pragma once

// Resource 资源系统（对齐 Godot core/resource.{h,cpp} + resource_loader / resource_cache）。
//
// P2 Task 5 范围：
//   * Resource : public RefCounted         —— 引用计数 + res:// 路径 + 序列化占位虚函数
//   * ResourceFormatLoader / Saver         —— 按扩展名分发的格式接口（派生类实现具体格式）
//   * ResourceLoader / ResourceSaver       —— 门面：注册格式、按扩展名分派、走 OS 解析 res://
//   * ResourceCache                        —— 按 path 全局去重，同一 res:// 多次 load 返回同实例
//
// 边界：首版只提供文本加载骨架（loader/saver 读取 / 写出 Resource::serialize() 输出）；
//       具体派生资源（Texture/Audio/Theme 等）由下游 Task 实现，并通过 register_format
//       注入。基类的 serialize/deserialize 为占位（空串 / false）。
//
// 依赖：core/object/ref_counted.h + ref.h（T1）、core/os/os.h（res:// 解析）。

#include "core/object/ref.h"
#include "core/object/ref_counted.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

// ============================================================================
// Resource —— 引用计数的可持久化资源基类。
// 派生自 RefCounted：跨对象共享同一实例（多个元件引用同一符号 / 多个节点引用同一主题）。
// path_：以 "res://" 表示的虚拟路径；ResourceCache 按 path_ 唯一缓存。
// ============================================================================
class Resource : public RefCounted {
public:
    static std::string get_class_static() { return "Resource"; }
    std::string get_class() const override { return "Resource"; }
    bool is_class(const std::string& name) const override {
        return name == "Resource" || RefCounted::is_class(name);
    }

    // res:// 虚拟路径；由 ResourceLoader::load 在加载成功后回填。
    void set_path(const std::string& path) { path_ = path; }
    const std::string& get_path() const { return path_; }

    // 序列化占位（虚）。派生资源 override 实现具体文本格式（.tres / .toml / .json）。
    // Task 3 Variant 落地 to_json/from_json 后由派生资源升级格式。
    virtual std::string serialize() const;
    virtual bool deserialize(const std::string& data);

    // 注：计划原稿将 ctor/dtor 置于 protected（强制走 Ref<T> 管理），与 ref_counted.h
    //     保持一致：实际公开默认构造与析构以使测试可直接 new Resource / 栈构造。
    //     生产代码仍建议通过 Ref<T> 管理生命周期（RefCounted::unref 归零自动销毁）。
    Resource() = default;
    ~Resource() override = default;

private:
    std::string path_;
};

// ============================================================================
// ResourceFormatLoader / Saver —— 按扩展名分发的格式接口。
// 派生类负责：1) 声明所处理的扩展名集合  2) 在磁盘路径上构造 / 写出 Resource。
// 注册方持有所有权（通常为 static 程序期对象）；registry 仅持有裸指针，clear_formats 不清堆。
// ============================================================================
class ResourceFormatLoader {
public:
    virtual ~ResourceFormatLoader() = default;
    virtual bool handles_extension(const std::string& ext) const = 0;
    // 入参 disk_path：经 OS::resolve_resource_path 解析后的磁盘绝对路径。
    // 出参：新构造并完成反序列化的 Resource（失败返回空 Ref）。
    virtual Ref<Resource> load(const std::string& disk_path) const = 0;
};

class ResourceFormatSaver {
public:
    virtual ~ResourceFormatSaver() = default;
    virtual bool handles_extension(const std::string& ext) const = 0;
    virtual bool save(const std::string& disk_path, Ref<Resource> res) const = 0;
};

// ============================================================================
// ResourceCache —— 按 path 全局去重缓存。
// 同一 res:// 路径多次 load 返回同一实例（指针相等），避免重复 IO 与闪烁。
// 缓存持有 Ref<Resource>，资源在所有外部 Ref 释放后仍由 cache 保活，直到 remove / clear。
// ============================================================================
class ResourceCache {
public:
    // 命中返回已缓存实例；未命中返回空 Ref。
    static Ref<Resource> get_ref(const std::string& path);

    // 写入 / 覆盖一条缓存。
    static void add(const std::string& path, Ref<Resource> res);

    // 移除单条；底层 Resource 是否销毁取决于是否还有其它 Ref 持有。
    static void remove(const std::string& path);

    static bool has(const std::string& path);

    // 测试 / main 退出时调用，释放所有缓存的 Ref。
    static void clear();

private:
    ResourceCache() = default;
    struct State;
    static State& state();
};

// ============================================================================
// ResourceLoader —— 加载门面。
// 流程：1) 查 ResourceCache，命中直接返回
//       2) 取 path 扩展名，遍历已注册 loader 找首个 handles_extension 命中者
//       3) 经 OS::resolve_resource_path 把 res:// 转为磁盘路径
//       4) loader->load(disk_path) 失败返回空 Ref；成功则 set_path(path)、写入 cache
// ============================================================================
class ResourceLoader {
public:
    static Ref<Resource> load(const std::string& path);

    static void register_format(ResourceFormatLoader* loader);
    static void clear_formats();

    // 工具：取路径最后一段扩展名（小写，无点）。如 "a/b.Txt" -> "txt"。
    static std::string get_extension(const std::string& path);
};

// ============================================================================
// ResourceSaver —— 保存门面。
// 流程：1) 取扩展名找 saver  2) OS 解析 res://  3) saver->save(disk_path, res)  4) set_path
// ============================================================================
class ResourceSaver {
public:
    static bool save(const std::string& path, Ref<Resource> res);

    static void register_format(ResourceFormatSaver* saver);
    static void clear_formats();
};

}  // namespace eda
