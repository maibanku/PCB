#pragma once

// eda/project/project.h
//
// P5 Task 1 · 工程树数据模型 + 开放文本格式（project.toml）序列化。
//
// 数据模型层级：
//   Project (根)
//     └── Board[]            板（主板 / 子板）
//           └── Page[]       图页（原理图 / PCB）
//
//   每个对象在构造时生成稳定 UUID（v4 字符串）作为主键——
//   重命名 / 移动文件不破坏引用（ECO 协作基础）。
//
// 序列化：手写最小 TOML 子集（不引 toml++），Git-diff 友好：
//   - 字段顺序在 schema 中固定，不随实现漂移
//   - boards / pages 数组按 uuid 字典序输出
//   - dependencies 按字典序输出
//   - 时间戳为 Unix 秒（UTC）
//   - 枚举用小写连字符（type = "main" / "schematic"）
//
// 集成（对齐 P2 Resource）：
//   - ProjectTree 继承 Resource : RefCounted，可由 Ref<ProjectTree> 共享。
//   - override serialize()/deserialize()，对接 ResourceLoader/Saver 分发链（P2 预留）。
//   - Project / Board / Page 当前为值类型（首版简化），P2 完整落地后可升级为 Resource 子类。
//
// 依赖：core/object/{ref,resource,ref_counted}.h（P2）。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/object/ref.h"
#include "core/object/resource.h"

namespace eda {

// ============================================================================
// 枚举：图页类型 / 板类型（schema 字符串名见 to_string_view）
// ============================================================================

enum class PageType {
    SCHEMATIC,  // 原理图
    PCB,        // PCB 版图
};

// 序列化用字符串名：SCHEMATIC -> "schematic"，PCB -> "pcb"
std::string_view to_string_view(PageType t);
// 反序列化：未识别字符串默认 SCHEMATIC（前向兼容——新枚举值在旧版本里降级）
PageType parse_page_type(std::string_view s);

enum class BoardType {
    MAIN,      // 主板
    DAUGHTER,  // 子板 / 扩展板
};

std::string_view to_string_view(BoardType t);
BoardType parse_board_type(std::string_view s);

// ============================================================================
// UUID v4 字符串生成 / 校验
// ============================================================================

// 生成 UUID v4 字符串（8-4-4-4-12 小写连字符）。
// 使用 std::random_device 播种进程级 mt19937（足够工程树节点主键用）。
std::string generate_uuid();

// 校验 UUID 字符串格式（仅长度与连字符位置；版本位不强制）。
bool is_valid_uuid(const std::string& s);

// ============================================================================
// Page —— 工程树叶子节点（图页）
// ============================================================================

class Page {
public:
    Page();  // 自动生成 UUID
    ~Page() = default;
    Page(const Page&) = default;
    Page& operator=(const Page&) = default;
    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) noexcept = default;

    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }
    PageType type() const { return type_; }
    const std::string& path() const { return path_; }  // res://sheets/sheet1.sch.toml
    int64_t modified_time() const { return modified_time_; }  // Unix 秒（UTC）

    void set_name(std::string n) { name_ = std::move(n); }
    void set_type(PageType t) { type_ = t; }
    void set_path(std::string p) { path_ = std::move(p); }
    void set_modified_time(int64_t t) { modified_time_ = t; }

    bool operator==(const Page& o) const {
        return uuid_ == o.uuid_ && name_ == o.name_ &&
               type_ == o.type_ && path_ == o.path_ &&
               modified_time_ == o.modified_time_;
    }
    bool operator!=(const Page& o) const { return !(*this == o); }

    // 测试专用：注入固定 UUID 验证确定性输出（生产代码勿用）
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

private:
    std::string uuid_;
    std::string name_;
    PageType type_ = PageType::SCHEMATIC;
    std::string path_;
    int64_t modified_time_ = 0;
};

// ============================================================================
// Board —— 工程 > 板 > 图页 的中间层
// ============================================================================

class Board {
public:
    Board();  // 自动生成 UUID
    ~Board() = default;
    Board(const Board&) = default;
    Board& operator=(const Board&) = default;
    Board(Board&&) noexcept = default;
    Board& operator=(Board&&) noexcept = default;

    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }
    BoardType type() const { return type_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_type(BoardType t) { type_ = t; }

    size_t page_count() const { return pages_.size(); }
    const Page& page(size_t i) const { return pages_[i]; }
    Page& page(size_t i) { return pages_[i]; }
    const std::vector<Page>& pages() const { return pages_; }

    // 新增图页（自动生成 UUID）。返回新增页的引用，便于调用方继续配置。
    Page& add_page(std::string name, PageType type, std::string path);
    // 按 UUID 移除图页；返回是否实际移除。
    bool remove_page_by_uuid(const std::string& uuid);

    const Page* find_page_by_uuid(const std::string& uuid) const;
    Page* find_page_by_uuid(const std::string& uuid);

    bool operator==(const Board& o) const {
        return uuid_ == o.uuid_ && name_ == o.name_ &&
               type_ == o.type_ && pages_ == o.pages_;
    }
    bool operator!=(const Board& o) const { return !(*this == o); }

    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

private:
    std::string uuid_;
    std::string name_;
    BoardType type_ = BoardType::MAIN;
    std::vector<Page> pages_;
};

// ============================================================================
// Project —— 工程根
// ============================================================================

class Project {
public:
    Project();  // 自动生成 UUID，schema_version = 1
    ~Project() = default;
    Project(const Project&) = default;
    Project& operator=(const Project&) = default;
    Project(Project&&) noexcept = default;
    Project& operator=(Project&&) noexcept = default;

    int schema_version() const { return schema_version_; }
    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }
    int64_t modified_time() const { return modified_time_; }

    void set_name(std::string n) { name_ = std::move(n); }
    void set_modified_time(int64_t t) { modified_time_ = t; }
    void touch();  // 更新 modified_time 为当前 Unix 秒（UTC）

    size_t board_count() const { return boards_.size(); }
    const Board& board(size_t i) const { return boards_[i]; }
    Board& board(size_t i) { return boards_[i]; }
    const std::vector<Board>& boards() const { return boards_; }

    // 新增板（自动生成 UUID）。返回新增板的引用。
    Board& add_board(std::string name, BoardType type);
    // 按 UUID 移除板；返回是否实际移除（同时级联移除其下所有图页）。
    bool remove_board_by_uuid(const std::string& uuid);

    const Board* find_board_by_uuid(const std::string& uuid) const;
    Board* find_board_by_uuid(const std::string& uuid);

    // 依赖库引用列表（lib://standard@1.2.0）。
    const std::vector<std::string>& dependencies() const { return dependencies_; }
    void add_dependency(std::string lib_ref);
    bool remove_dependency(const std::string& lib_ref);

    bool operator==(const Project& o) const {
        return schema_version_ == o.schema_version_ &&
               uuid_ == o.uuid_ && name_ == o.name_ &&
               modified_time_ == o.modified_time_ &&
               boards_ == o.boards_ &&
               dependencies_ == o.dependencies_;
    }
    bool operator!=(const Project& o) const { return !(*this == o); }

    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

private:
    int schema_version_ = 1;
    std::string uuid_;
    std::string name_;
    int64_t modified_time_ = 0;
    std::vector<Board> boards_;
    std::vector<std::string> dependencies_;
};

// ============================================================================
// ProjectTree —— 持有 Project 根，提供 load / save / find_by_uuid。
// 继承 Resource：对齐 P2 资源系统，可由 Ref<ProjectTree> 跨对象共享，
// 未来接入 ResourceLoader/Saver 分发链。
// ============================================================================

class ProjectTree : public Resource {
public:
    static std::string get_class_static() { return "ProjectTree"; }
    std::string get_class() const override { return "ProjectTree"; }
    bool is_class(const std::string& name) const override {
        return name == "ProjectTree" || Resource::is_class(name);
    }

    ProjectTree();  // 生成新 UUID 的 Project root

    Project& root() { return root_; }
    const Project& root() const { return root_; }

    // 按 UUID 递归查找（boards → pages）
    const Board* find_board_by_uuid(const std::string& uuid) const;
    const Page* find_page_by_uuid(const std::string& uuid) const;

    // 便捷 IO（实例方法）—— project.toml 读写
    bool load(const std::string& project_toml_path);
    bool save(const std::string& project_toml_path) const;
    std::string serialize_to_string() const;
    bool parse_from_string(const std::string& text);

    // Resource 接口（接入 P2 ResourceLoader/Saver 分发链）
    std::string serialize() const override { return serialize_to_string(); }
    bool deserialize(const std::string& text) override { return parse_from_string(text); }

private:
    Project root_;
};

// ============================================================================
// ProjectLoader / ProjectSaver —— project.toml 读写门面
// ============================================================================

class ProjectLoader {
public:
    // 加载磁盘文件 → 新构造 ProjectTree（失败返回空 Ref）。
    static Ref<ProjectTree> load(const std::string& project_toml_path);
    // 解析文本 → 新构造 ProjectTree（失败返回空 Ref）。
    static Ref<ProjectTree> parse(const std::string& text);

    // 填充已存在的 tree（解析前会重置 root 为空白状态）。
    // 文件不存在或读不出字节时返回 false；语法不识别的行被忽略（前向兼容）。
    static bool load_into(ProjectTree& tree, const std::string& project_toml_path);
    static bool parse_into(ProjectTree& tree, const std::string& text);
};

class ProjectSaver {
public:
    // 序列化 tree -> TOML 文本（确定性输出：字段顺序固定，boards/pages 按 uuid 字典序）。
    static std::string serialize(const ProjectTree* tree);
    // 仅序列化 Project 视图（不依赖 ProjectTree 包装）。
    static std::string serialize(const Project& root);
    // 写入磁盘（目录不存在则失败；调用方负责确保父目录可写）。
    static bool save(const std::string& project_toml_path, const ProjectTree* tree);
};

}  // namespace eda
