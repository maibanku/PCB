#pragma once

// eda/project/attachment_manager.h
//
// P5 Task 4 · 附件清单管理（AttachmentManager）。
//
// 范围（对齐 plan/工程与文件系统实现计划.md Task 4）：
//   - add(Attachment) / remove(name) / list() / find(name)
//   - 按 type 过滤
//   - scan_invalid()：失效附件清单（依赖 Attachment::is_valid）
//   - attach_to_board(board_uuid, attachment_name)：附件 ↔ 板 UUID 关联（占位映射）
//   - 与器件库 datasheet 联动占位（P6 落地后接入：同一 datasheet 工程内不重复存储）
//
// 设计：
//   - 持有 project_root_：add 时注入到 Attachment，使 PROJECT_RESOURCE 的 is_valid
//     能正确解析 res:// 路径。
//   - 业务主键为附件名（name）：用户可读、便于 UI 展示；UUID 在 Attachment 内部
//     保留稳定标识（重命名 / 移动文件不破坏引用）。
//   - 不做磁盘持久化（attachments.toml 读写留给后续任务 / 调用方）。
//   - board_links_ / datasheet_links_ 为 board_uuid / component_uuid 到附件名的
//     多对多映射，顺序保留插入序、去重。
//
// 依赖：eda/project/attachment.h。
// 不改 CMakeLists.txt：eda/project/attachment_manager.cpp 由主循环加入 eda_project。

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "eda/project/attachment.h"

namespace eda {

// ============================================================================
// AttachmentManager —— 附件清单 + 失效检测 + 板 / 器件关联
// ============================================================================

class AttachmentManager {
public:
    // 默认构造：project_root 为空（PROJECT_RESOURCE 仅做语法校验）。
    AttachmentManager();
    // 指定工程根：用于 res:// 解析。
    explicit AttachmentManager(std::string project_root);

    // 新增附件。若同名已存在则替换（保留位置；UUID 更新为新附件的）。
    // 自动注入 project_root_，使 PROJECT_RESOURCE 的 is_valid() 可解析。
    // 返回新增 / 被替换项的引用，便于调用方继续配置。
    Attachment& add(Attachment a);

    // 按名移除。返回是否实际移除；同时清理 board / datasheet 关联。
    bool remove(const std::string& name);

    // 按名查找（线性扫描；附件数量级小，无需 hash）。
    const Attachment* find(const std::string& name) const;
    Attachment* find(const std::string& name);

    // 全量清单（拷贝；调用方可任意修改）。
    const std::vector<Attachment>& items() const { return items_; }
    std::vector<Attachment> list() const { return items_; }

    // 按 type 过滤（拷贝）。
    std::vector<Attachment> list_by_type(Attachment::Type t) const;

    // 失效附件清单（is_valid() == false），保留插入序。
    std::vector<Attachment> scan_invalid() const;

    // ------------------------------------------------------------------
    // 附件 ↔ 板 UUID 关联（占位映射）
    //
    // board_uuid 由 ProjectTree 管理；此处仅记映射，不校验 board 是否存在
    // （板可能尚未加入工程树，关联先于板建立）。
    // ------------------------------------------------------------------
    bool attach_to_board(const std::string& board_uuid,
                         const std::string& attachment_name);
    bool detach_from_board(const std::string& board_uuid,
                           const std::string& attachment_name);
    std::vector<std::string> attachments_of_board(
        const std::string& board_uuid) const;

    // ------------------------------------------------------------------
    // 器件库 datasheet 联动占位（P6 落地后接入）
    //
    // 将某器件 UUID 关联到指定附件名（datasheet 引用）。同一 datasheet 工程
    // 内不重复存储——多器件可共享一份附件。
    // ------------------------------------------------------------------
    bool link_datasheet(const std::string& component_uuid,
                        const std::string& attachment_name);
    bool unlink_datasheet(const std::string& component_uuid,
                          const std::string& attachment_name);
    std::vector<std::string> datasheets_of(
        const std::string& component_uuid) const;

    const std::string& project_root() const { return project_root_; }
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

private:
    std::string project_root_;
    std::vector<Attachment> items_;

    // board_uuid -> [attachment_name]（保留插入序，去重）
    std::map<std::string, std::vector<std::string>> board_links_;
    // component_uuid -> [attachment_name]（datasheet 引用，保留插入序，去重）
    std::map<std::string, std::vector<std::string>> datasheet_links_;
};

}  // namespace eda
