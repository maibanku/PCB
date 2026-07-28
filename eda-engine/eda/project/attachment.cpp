// eda/project/attachment.cpp
//
// P5 Task 4 · 附件管理（Attachment）实现。
//
// 设计要点：
//   - 失效检测按 type 分发；LOCAL_FILE / PROJECT_RESOURCE 走 std::filesystem，
//     EXTERNAL_URL 占位（不联网，仅语法校验——开关预留语义，由后续网络层接入）。
//   - 全局进程级开关 g_external_url_check_enabled 默认 false：调用方默认不联网。
//   - 不引第三方依赖；与 project.cpp / template_def.cpp 风格一致。

#include "eda/project/attachment.h"

#include <filesystem>
#include <utility>

#include "core/io/path.h"

namespace eda {

namespace {

// EXTERNAL_URL 探活开关（进程级）。默认 false：不联网。
bool g_external_url_check_enabled = false;

// 基本 URL 语法：仅认 http:// / https:// 前缀，不解析主机 / 端口 / 路径。
// 足够区分"明显不是 URL"的输入；完整 URL 校验留给后续网络层。
bool looks_like_http_url(const std::string& s) {
    if (s.compare(0, 7, "http://") == 0 && s.size() > 7) return true;
    if (s.compare(0, 8, "https://") == 0 && s.size() > 8) return true;
    return false;
}

}  // namespace

Attachment::Attachment() : uuid_(generate_uuid()) {}

bool Attachment::syntactically_valid() const {
    switch (type_) {
        case Type::LOCAL_FILE:
            return !path_.empty();
        case Type::EXTERNAL_URL:
            return looks_like_http_url(url_);
        case Type::PROJECT_RESOURCE:
            return Path::is_res(path_);
    }
    return false;
}

bool Attachment::is_valid() const {
    switch (type_) {
        case Type::LOCAL_FILE:
            if (path_.empty()) return false;
            // 直接查磁盘存在性（含绝对路径 / 相对路径 / 盘符路径）。
            return std::filesystem::exists(path_);
        case Type::EXTERNAL_URL:
            // 占位：不发起 HTTP。开关仅控制未来是否联网；当前一律语法校验。
            (void)g_external_url_check_enabled;
            return looks_like_http_url(url_);
        case Type::PROJECT_RESOURCE: {
            if (!Path::is_res(path_)) return false;
            // project_root 缺失时无法解析为磁盘路径，仅认 res:// 前缀
            // （AttachmentManager::add 注入 project_root 后即可全检）。
            if (project_root_.empty()) return true;
            const std::string native = Path::res_to_native(path_, project_root_);
            return std::filesystem::exists(native);
        }
    }
    return false;
}

void Attachment::set_external_url_check_enabled(bool enabled) {
    g_external_url_check_enabled = enabled;
}

bool Attachment::external_url_check_enabled() {
    return g_external_url_check_enabled;
}

// ============================================================================
// 序列化字符串名
// ============================================================================

std::string_view to_string_view(Attachment::Type t) {
    switch (t) {
        case Attachment::Type::LOCAL_FILE:        return "local_file";
        case Attachment::Type::EXTERNAL_URL:      return "external_url";
        case Attachment::Type::PROJECT_RESOURCE:  return "project_resource";
    }
    return "local_file";
}

Attachment::Type parse_attachment_type(std::string_view s) {
    if (s == "external_url")      return Attachment::Type::EXTERNAL_URL;
    if (s == "project_resource")  return Attachment::Type::PROJECT_RESOURCE;
    return Attachment::Type::LOCAL_FILE;  // 含 "local_file" 与未识别值
}

}  // namespace eda
