#pragma once

// eda/project/attachment.h
//
// P5 Task 4 · 附件管理（Attachment）——三类引用 + 失效检测。
//
// 范围（对齐 plan/工程与文件系统实现计划.md Task 4）：
//   - 三类附件节点：
//       LOCAL_FILE        本地磁盘文件（绝对 / 相对路径）
//       EXTERNAL_URL      外链 URL（http/https）
//       PROJECT_RESOURCE  工程内 res:// 引用（相对工程根，自包含可迁移）
//   - is_valid() 失效检测：
//       LOCAL_FILE        -> std::filesystem::exists(path)
//       EXTERNAL_URL      -> 占位（不发起 HTTP；仅校验 URL 语法 http/https，
//                            可由 set_external_url_check_enabled 切换语义，
//                            真实探活由 P6+ 网络层落地）
//       PROJECT_RESOURCE  -> res_to_native(project_root) 后查 exists；
//                            project_root 为空时仅校验 res:// 前缀
//
// 设计：
//   - 值类型（与 Page / TemplateDef 风格一致）：可拷贝、深比较、可放入 vector。
//   - 携带可选 project_root_：AttachmentManager::add 时从管理器注入，使
//     PROJECT_RESOURCE 的 is_valid() 能解析 res:// 路径；脱离管理器仍可做基本
//     语法校验（syntactically_valid）。
//   - UUID 字段（v4 字符串）与工程树其他节点一致——重命名 / 移动文件不破坏引用。
//
// 依赖：core/io/path.h（res:// 解析）、eda/project/project.h（generate_uuid）。
// 不改 CMakeLists.txt：eda/project/attachment.cpp 由主循环加入 eda_project target。

#include <cstdint>
#include <string>
#include <string_view>

#include "eda/project/project.h"  // generate_uuid / is_valid_uuid

namespace eda {

// ============================================================================
// Attachment —— 单个附件节点（值类型）
// ============================================================================

class Attachment {
public:
    // 附件类型（schema 字符串名见 to_string_view）。
    enum class Type {
        LOCAL_FILE,        // 本地磁盘文件
        EXTERNAL_URL,      // 外链 URL（http/https）
        PROJECT_RESOURCE,  // 工程内 res:// 引用
    };

    Attachment();  // 自动生成 UUID
    ~Attachment() = default;
    Attachment(const Attachment&) = default;
    Attachment& operator=(const Attachment&) = default;
    Attachment(Attachment&&) noexcept = default;
    Attachment& operator=(Attachment&&) noexcept = default;

    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }
    Type type() const { return type_; }
    // LOCAL_FILE / PROJECT_RESOURCE 使用 path；EXTERNAL_URL 留空。
    const std::string& path() const { return path_; }
    // EXTERNAL_URL 使用 url；其余类型留空。
    const std::string& url() const { return url_; }
    // 字节数；未知为 -1（附件未提供尺寸时）。
    int64_t size() const { return size_; }
    // Unix 秒（UTC），附件添加时间。
    int64_t added_time() const { return added_time_; }
    // 可选描述；空串表示未设置。
    const std::string& description() const { return description_; }
    // PROJECT_RESOURCE 的 res:// 解析基（由 AttachmentManager 注入）。
    const std::string& project_root() const { return project_root_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_type(Type t) { type_ = t; }
    void set_path(std::string p) { path_ = std::move(p); }
    void set_url(std::string u) { url_ = std::move(u); }
    void set_size(int64_t s) { size_ = s; }
    void set_added_time(int64_t t) { added_time_ = t; }
    void set_description(std::string d) { description_ = std::move(d); }
    void set_project_root(std::string r) { project_root_ = std::move(r); }

    // 失效检测（触磁盘 / 网络）：
    //   LOCAL_FILE        std::filesystem::exists(path)
    //   EXTERNAL_URL      占位（不联网；仅 URL 语法校验）
    //   PROJECT_RESOURCE  project_root 非空 -> res_to_native + exists
    //                     project_root 为空 -> 仅校验 res:// 前缀
    bool is_valid() const;

    // 与 project_root / 磁盘 / 网络无关的纯语法校验：
    //   LOCAL_FILE        path 非空
    //   EXTERNAL_URL      url 以 http:// 或 https:// 开头
    //   PROJECT_RESOURCE  path 以 res:// 开头
    // 供 UI 实时反馈与 scan_invalid 预筛。
    bool syntactically_valid() const;

    bool operator==(const Attachment& o) const {
        return uuid_ == o.uuid_ && name_ == o.name_ && type_ == o.type_ &&
               path_ == o.path_ && url_ == o.url_ && size_ == o.size_ &&
               added_time_ == o.added_time_ &&
               description_ == o.description_ &&
               project_root_ == o.project_root_;
    }
    bool operator!=(const Attachment& o) const { return !(*this == o); }

    // 测试专用：注入固定 UUID（与 Page / Board 风格一致）。
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ------------------------------------------------------------------
    // 全局开关：EXTERNAL_URL 是否做真实网络探活。
    //   默认 false（仅语法校验，不联网——本版占位实现）。
    //   设为 true 当前仍只做语法校验（占位），真实 HTTP 探活由后续网络层实现。
    // ------------------------------------------------------------------
    static void set_external_url_check_enabled(bool enabled);
    static bool external_url_check_enabled();

private:
    std::string uuid_;
    std::string name_;
    Type type_ = Type::LOCAL_FILE;
    std::string path_;
    std::string url_;
    int64_t size_ = -1;
    int64_t added_time_ = 0;
    std::string description_;
    std::string project_root_;  // PROJECT_RESOURCE 用：res:// 解析基
};

// ============================================================================
// 序列化字符串名（schema 用小写下划线；与 project.toml 风格一致）
// ============================================================================

// LOCAL_FILE -> "local_file"，EXTERNAL_URL -> "external_url"，
// PROJECT_RESOURCE -> "project_resource"。
std::string_view to_string_view(Attachment::Type t);
// 反序列化：未识别字符串默认 LOCAL_FILE（前向兼容——新枚举值在旧版本里降级）。
Attachment::Type parse_attachment_type(std::string_view s);

}  // namespace eda
