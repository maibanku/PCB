// eda/project/attachment_manager.cpp
//
// P5 Task 4 · 附件清单管理（AttachmentManager）实现。
//
// 设计要点：
//   - add 时把 project_root_ 注入到 Attachment，使 PROJECT_RESOURCE 的 is_valid
//     能解析 res:// 路径（与 Attachment::project_root 协同）。
//   - 同名 add 走"就地替换"语义（保留位置，UUID / 字段全替换为新值）。
//   - remove 时级联清理 board_links_ / datasheet_links_ 中的悬空引用。
//   - 板 / 器件关联的 link 接口幂等：重复关联同一对返回 false，清单不变。

#include "eda/project/attachment_manager.h"

#include <algorithm>
#include <utility>

namespace eda {

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// 从 v 中移除首个等于 s 的元素；返回是否实际移除。
bool remove_one(std::vector<std::string>& v, const std::string& s) {
    auto it = std::find(v.begin(), v.end(), s);
    if (it == v.end()) return false;
    v.erase(it);
    return true;
}

}  // namespace

AttachmentManager::AttachmentManager() = default;

AttachmentManager::AttachmentManager(std::string project_root)
    : project_root_(std::move(project_root)) {}

Attachment& AttachmentManager::add(Attachment a) {
    // 注入 project_root，使 PROJECT_RESOURCE 的 is_valid() 可解析 res://
    a.set_project_root(project_root_);

    // 同名替换（保留位置）
    for (Attachment& existing : items_) {
        if (existing.name() == a.name()) {
            existing = std::move(a);
            return existing;
        }
    }
    items_.push_back(std::move(a));
    return items_.back();
}

bool AttachmentManager::remove(const std::string& name) {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&](const Attachment& a) { return a.name() == name; });
    if (it == items_.end()) return false;
    items_.erase(it);

    // 级联清理：删掉的附件不应继续挂在板 / 器件上
    for (auto& kv : board_links_) remove_one(kv.second, name);
    for (auto& kv : datasheet_links_) remove_one(kv.second, name);
    return true;
}

const Attachment* AttachmentManager::find(const std::string& name) const {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&](const Attachment& a) { return a.name() == name; });
    return it == items_.end() ? nullptr : &(*it);
}

Attachment* AttachmentManager::find(const std::string& name) {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&](const Attachment& a) { return a.name() == name; });
    return it == items_.end() ? nullptr : &(*it);
}

std::vector<Attachment> AttachmentManager::list_by_type(
    Attachment::Type t) const {
    std::vector<Attachment> out;
    for (const Attachment& a : items_) {
        if (a.type() == t) out.push_back(a);
    }
    return out;
}

std::vector<Attachment> AttachmentManager::scan_invalid() const {
    std::vector<Attachment> out;
    for (const Attachment& a : items_) {
        if (!a.is_valid()) out.push_back(a);
    }
    return out;
}

// ============================================================================
// 板关联（占位映射）
// ============================================================================

bool AttachmentManager::attach_to_board(const std::string& board_uuid,
                                        const std::string& attachment_name) {
    if (board_uuid.empty() || attachment_name.empty()) return false;
    if (find(attachment_name) == nullptr) return false;  // 附件必须先 add
    std::vector<std::string>& names = board_links_[board_uuid];
    if (contains(names, attachment_name)) return false;  // 幂等：已关联
    names.push_back(attachment_name);
    return true;
}

bool AttachmentManager::detach_from_board(const std::string& board_uuid,
                                          const std::string& attachment_name) {
    auto it = board_links_.find(board_uuid);
    if (it == board_links_.end()) return false;
    return remove_one(it->second, attachment_name);
}

std::vector<std::string> AttachmentManager::attachments_of_board(
    const std::string& board_uuid) const {
    auto it = board_links_.find(board_uuid);
    if (it == board_links_.end()) return {};
    return it->second;
}

// ============================================================================
// 器件库 datasheet 联动占位（P6 落地后接入）
// ============================================================================

bool AttachmentManager::link_datasheet(const std::string& component_uuid,
                                       const std::string& attachment_name) {
    if (component_uuid.empty() || attachment_name.empty()) return false;
    if (find(attachment_name) == nullptr) return false;  // 附件必须先 add
    std::vector<std::string>& names = datasheet_links_[component_uuid];
    if (contains(names, attachment_name)) return false;  // 幂等：已关联
    names.push_back(attachment_name);
    return true;
}

bool AttachmentManager::unlink_datasheet(const std::string& component_uuid,
                                         const std::string& attachment_name) {
    auto it = datasheet_links_.find(component_uuid);
    if (it == datasheet_links_.end()) return false;
    return remove_one(it->second, attachment_name);
}

std::vector<std::string> AttachmentManager::datasheets_of(
    const std::string& component_uuid) const {
    auto it = datasheet_links_.find(component_uuid);
    if (it == datasheet_links_.end()) return {};
    return it->second;
}

}  // namespace eda
