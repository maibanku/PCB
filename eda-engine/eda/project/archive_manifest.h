#pragma once

// eda/project/archive_manifest.h
//
// P5 Task 5 · 工程打包归档（.edapkg）—— 清单元信息（ArchiveManifest）。
//
// 范围：
//   - ArchiveManifest：归档包元信息（引擎版本 / schema 版本 / 创建时间 /
//     工程 UUID / 依赖库版本快照 / 资源清单 / 签名占位）。
//   - manifest.toml 序列化：手写最小 TOML 子集（与 project.cpp / snapshot.cpp
//     风格一致——不引 toml++），字段顺序固定、列表按字典序输出（Git-diff 友好）。
//
// manifest.toml schema（首版 schema_version = 1）：
//   schema_version = 1
//   eda_version    = "0.1.0"
//   created_time   = 1754496000
//   project_uuid   = "550e8400-e29b-41d4-a716-446655440000"
//   signature      = ""
//
//   [[libraries]]              # 按 name 字典序
//   name    = "standard"
//   version = "1.2.0"
//   ref     = "lib://standard@1.2.0"
//
//   [[resources]]              # 按 path 字典序
//   path = "res://project.toml"
//   size = 412
//
// 设计：
//   - LibraryVersion / ArchiveResource 全部值类型，可拷贝、深比较（operator== default）。
//   - libraries 从 Project::dependencies()（形如 "lib://standard@1.2.0"）解析得到
//     {name, version, ref}；未带 "@version" 时 version 留空、name 取整段后缀。
//   - resources 的 path 统一用 res:// 工程内路径，size 为原始字节数；解包后据此
//     校验文件齐全（verify）。
//   - signature 字段为占位：首版固定空串，预留给后续 SHA-256 / Ed25519 数字签名
//     （P17 协作 / 防篡改场景落地）。
//
// 依赖：无（仅 STL）。复用手写 TOML 子集，与 ProjectLoader/ProjectSaver 对齐。
// 不改 CMakeLists.txt：eda/project/archive_manifest.cpp 由主循环加入 eda_project target。

#include <cstdint>
#include <string>
#include <vector>

namespace eda {

// ============================================================================
// LibraryVersion —— 依赖库版本快照条目
// ============================================================================

struct LibraryVersion {
    std::string name;     // 库名（如 "standard"）
    std::string version;  // 版本字符串（如 "1.2.0"）；未指定时为空
    std::string ref;      // 原始引用（如 "lib://standard@1.2.0"）

    bool operator==(const LibraryVersion&) const = default;
};

// ============================================================================
// ArchiveResource —— 归档内单个资源的描述（path + size）
// ============================================================================

struct ArchiveResource {
    std::string path;     // res:// 工程内路径（如 "res://sheets/sheet1.sch.toml"）
    int64_t size = 0;     // 文件字节数

    bool operator==(const ArchiveResource&) const = default;
};

// ============================================================================
// ArchiveManifest —— .edapkg 清单根对象
// ============================================================================

class ArchiveManifest {
public:
    ArchiveManifest();
    ~ArchiveManifest() = default;
    ArchiveManifest(const ArchiveManifest&) = default;
    ArchiveManifest& operator=(const ArchiveManifest&) = default;
    ArchiveManifest(ArchiveManifest&&) noexcept = default;
    ArchiveManifest& operator=(ArchiveManifest&&) noexcept = default;

    // 当前归档 schema 版本（破坏性变更走主版本号 + 迁移器，首版固定 1）。
    static constexpr int kCurrentSchemaVersion = 1;

    int schema_version() const { return schema_version_; }
    const std::string& eda_version() const { return eda_version_; }
    int64_t created_time() const { return created_time_; }
    const std::string& project_uuid() const { return project_uuid_; }
    const std::string& signature() const { return signature_; }  // 占位
    const std::vector<LibraryVersion>& libraries() const { return libraries_; }
    const std::vector<ArchiveResource>& resources() const { return resources_; }

    void set_eda_version(std::string v) { eda_version_ = std::move(v); }
    void set_created_time(int64_t t) { created_time_ = t; }
    void set_project_uuid(std::string u) { project_uuid_ = std::move(u); }
    void set_signature(std::string s) { signature_ = std::move(s); }

    // 解析单条 lib:// 引用并追加到 libraries_。形如 "lib://name@version" 或
    // "lib://name"；name 取协议后首个 '@' 之前（无 '@' 则取整段后缀），
    // version 取 '@' 之后（到行尾，去除尾随空白）。
    void add_library_from_ref(const std::string& ref);

    // 直接追加一条已组装好的库版本记录。
    void add_library(LibraryVersion lib) { libraries_.push_back(std::move(lib)); }

    // 追加一条资源（path/size）。调用方负责 path 形态（统一 res://）。
    void add_resource(ArchiveResource res) { resources_.push_back(std::move(res)); }
    // 便捷重载：等价于 add_resource({path, size})。
    void add_resource(const std::string& path, int64_t size);

    // 浅比较（schema_version / eda_version / created_time / project_uuid /
    // signature / libraries 顺序敏感 / resources 顺序敏感）。
    // 注意：libraries / resources 比较是顺序敏感的；serialize() 会先排序，
    // 故 round-trip 后比较稳定。
    bool operator==(const ArchiveManifest& o) const {
        return schema_version_ == o.schema_version_ &&
               eda_version_ == o.eda_version_ &&
               created_time_ == o.created_time_ &&
               project_uuid_ == o.project_uuid_ &&
               signature_ == o.signature_ &&
               libraries_ == o.libraries_ &&
               resources_ == o.resources_;
    }
    bool operator!=(const ArchiveManifest& o) const { return !(*this == o); }

    // 序列化为 manifest.toml 文本（确定性输出：字段顺序固定；
    // libraries 按 name 字典序；resources 按 path 字典序）。
    std::string serialize() const;
    // 从 manifest.toml 文本反序列化（解析前重置为空白状态）。
    // 失败容忍：未识别字段忽略（前向兼容）；严重语法错仍返回 true 并尽量填充。
    bool deserialize(const std::string& text);

private:
    int schema_version_ = kCurrentSchemaVersion;
    std::string eda_version_;
    int64_t created_time_ = 0;
    std::string project_uuid_;
    std::string signature_;  // 占位：默认空串
    std::vector<LibraryVersion> libraries_;
    std::vector<ArchiveResource> resources_;
};

// ============================================================================
// ArchiveManifestLoader —— manifest.toml 读写门面
// ============================================================================

class ArchiveManifestLoader {
public:
    // 解析 manifest.toml 文本 -> 新 ArchiveManifest。失败返回空 optional。
    static bool parse_into(ArchiveManifest& m, const std::string& text);
    // 加载 manifest.toml 文件 -> 新 ArchiveManifest。文件不可读返回 false。
    static bool load_into(ArchiveManifest& m, const std::string& path);
    // 序列化 manifest -> 文本。
    static std::string serialize(const ArchiveManifest& m) { return m.serialize(); }
    // 写 manifest -> 磁盘（目录不存在则失败）。
    static bool save(const ArchiveManifest& m, const std::string& path);
};

}  // namespace eda
