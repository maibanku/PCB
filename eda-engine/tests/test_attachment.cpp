// P5 Task 4 · 附件管理（Attachment）测试。
//
// 覆盖（对齐任务要求）：
//   - 三类 type 字符串映射（LOCAL_FILE / EXTERNAL_URL / PROJECT_RESOURCE）
//   - is_valid：
//       LOCAL_FILE        存在 / 不存在 / 空路径
//       EXTERNAL_URL      http/https 语法命中；ftp / 非法 URL 命中 false；
//                         开关切换语义不变（占位不联网）
//       PROJECT_RESOURCE  有 project_root 时查盘存在 / 缺失；
//                         无 project_root 仅认 res:// 前缀；
//                         非 res:// 路径 false
//   - AttachmentManager 增删查（add / remove / find / list / size）
//   - 同名 add 替换语义
//   - add 自动注入 project_root（PROJECT_RESOURCE 全检可解析）
//   - 按 type 过滤
//   - scan_invalid：失效清单（LOCAL_FILE 缺失 + EXTERNAL_URL 非法 + PROJECT_RESOURCE 缺失）
//   - attach_to_board：关联 / 查询 / 幂等 / 拒绝未知附件 / 解关联
//   - remove 级联清理 board_links_
//   - datasheet 联动占位：link / unlink / 查询 / 多器件共享
//
// 依赖：eda/project/attachment.h + eda/project/attachment_manager.h + core/io/path.h。
// 不改 CMakeLists.txt：test_attachment 由主循环加入 tests/CMakeLists（eda_project 链接）。

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "eda/project/attachment.h"
#include "eda/project/attachment_manager.h"

using namespace eda;

namespace {

// 每个用例独立临时目录的 fixture（与 test_snapshot / test_template 风格一致）。
class AttachmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        auto base = std::filesystem::temp_directory_path() /
                    ("eda_attachment_test_" + std::to_string(counter_++));
        std::filesystem::create_directories(base, ec);
        dir_ = base.string();
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    const std::string& dir() const { return dir_; }

    // 在临时目录下写一个文件（自动创建父目录），返回完整磁盘路径。
    std::string write_local_file(const std::string& rel,
                                 const std::string& content) {
        std::string p = dir() + "/" + rel;
        std::filesystem::path fp(p);
        std::error_code ec;
        std::filesystem::create_directories(fp.parent_path(), ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << content;
        return p;
    }

private:
    std::string dir_;
    static int counter_;
};
int AttachmentTest::counter_ = 0;

Attachment make_local(const std::string& name, const std::string& path) {
    Attachment a;
    a.set_name(name);
    a.set_type(Attachment::Type::LOCAL_FILE);
    a.set_path(path);
    a.set_added_time(1754496000);
    return a;
}

Attachment make_url(const std::string& name, const std::string& url) {
    Attachment a;
    a.set_name(name);
    a.set_type(Attachment::Type::EXTERNAL_URL);
    a.set_url(url);
    a.set_added_time(1754496000);
    return a;
}

Attachment make_res(const std::string& name, const std::string& res_path) {
    Attachment a;
    a.set_name(name);
    a.set_type(Attachment::Type::PROJECT_RESOURCE);
    a.set_path(res_path);
    a.set_added_time(1754496000);
    return a;
}

}  // namespace

// ============================================================================
// 三类 type 字符串映射
// ============================================================================

TEST(AttachmentTypeTest, StringRoundTrip) {
    EXPECT_EQ(to_string_view(Attachment::Type::LOCAL_FILE), "local_file");
    EXPECT_EQ(to_string_view(Attachment::Type::EXTERNAL_URL), "external_url");
    EXPECT_EQ(to_string_view(Attachment::Type::PROJECT_RESOURCE),
              "project_resource");

    EXPECT_EQ(parse_attachment_type("local_file"),
              Attachment::Type::LOCAL_FILE);
    EXPECT_EQ(parse_attachment_type("external_url"),
              Attachment::Type::EXTERNAL_URL);
    EXPECT_EQ(parse_attachment_type("project_resource"),
              Attachment::Type::PROJECT_RESOURCE);
    // 未识别 -> LOCAL_FILE（前向兼容）
    EXPECT_EQ(parse_attachment_type("unknown"),
              Attachment::Type::LOCAL_FILE);
}

TEST(AttachmentTypeTest, DefaultValueIsLocalFile) {
    Attachment a;
    EXPECT_EQ(a.type(), Attachment::Type::LOCAL_FILE);
    EXPECT_TRUE(a.uuid().size() == 36);  // 8-4-4-4-12
    EXPECT_EQ(a.size(), -1);             // 未知尺寸
}

// ============================================================================
// is_valid：LOCAL_FILE 存在 / 不存在 / 空路径
// ============================================================================

TEST_F(AttachmentTest, LocalFileValidWhenExists) {
    const std::string p = write_local_file("foo.pdf", "PDF-1.4 body");
    const auto a = make_local("foo", p);
    EXPECT_TRUE(a.is_valid());
    EXPECT_TRUE(a.syntactically_valid());
}

TEST_F(AttachmentTest, LocalFileInvalidWhenMissing) {
    const auto a = make_local("ghost", dir() + "/nope-not-here.pdf");
    EXPECT_FALSE(a.is_valid());
    EXPECT_TRUE(a.syntactically_valid());  // path 非空 -> 语法层面仍认
}

TEST_F(AttachmentTest, LocalFileInvalidWhenPathEmpty) {
    Attachment a;
    a.set_name("empty");
    a.set_type(Attachment::Type::LOCAL_FILE);
    EXPECT_FALSE(a.is_valid());
    EXPECT_FALSE(a.syntactically_valid());
}

// ============================================================================
// is_valid：EXTERNAL_URL 占位（不联网；仅语法）
// ============================================================================

TEST_F(AttachmentTest, ExternalUrlValidBySyntax) {
    EXPECT_TRUE(make_url("wiki", "https://example.com/wiki/datasheet.pdf").is_valid());
    EXPECT_TRUE(make_url("mirror", "http://mirror.example.org/x.pdf").is_valid());
}

TEST_F(AttachmentTest, ExternalUrlInvalidWhenNotHttp) {
    EXPECT_FALSE(make_url("ftp", "ftp://example.com/x.pdf").is_valid());
    EXPECT_FALSE(make_url("garbage", "not-a-url").is_valid());
    EXPECT_FALSE(make_url("empty", "").is_valid());
}

// 开关切换不应改变占位语义（仍仅语法校验）
TEST_F(AttachmentTest, ExternalUrlCheckToggleIsHarmless) {
    Attachment::set_external_url_check_enabled(true);
    EXPECT_TRUE(make_url("a", "https://example.com/y.pdf").is_valid());
    EXPECT_FALSE(make_url("b", "ftp://example.com/y.pdf").is_valid());
    Attachment::set_external_url_check_enabled(false);
    EXPECT_TRUE(make_url("a", "https://example.com/y.pdf").is_valid());
}

// ============================================================================
// is_valid：PROJECT_RESOURCE（res:// 解析）
// ============================================================================

TEST_F(AttachmentTest, ProjectResourceValidWithRoot) {
    write_local_file("attachments/datasheet.pdf", "PDF body");
    auto a = make_res("chip-ds", "res://attachments/datasheet.pdf");
    a.set_project_root(dir());
    EXPECT_TRUE(a.is_valid());
}

TEST_F(AttachmentTest, ProjectResourceInvalidWhenMissingOnDisk) {
    auto a = make_res("chip-ds", "res://attachments/missing.pdf");
    a.set_project_root(dir());
    EXPECT_FALSE(a.is_valid());
}

TEST_F(AttachmentTest, ProjectResourceNoRootOnlyChecksScheme) {
    auto a = make_res("chip-ds", "res://anything/here.pdf");
    EXPECT_TRUE(a.is_valid());  // project_root 缺失 -> 仅认 res:// 前缀
}

TEST_F(AttachmentTest, ProjectResourceInvalidWhenNotResScheme) {
    auto a = make_res("bad", "/abs/path/foo.pdf");
    a.set_project_root(dir());
    EXPECT_FALSE(a.is_valid());
    EXPECT_FALSE(a.syntactically_valid());
}

// ============================================================================
// AttachmentManager：增删查
// ============================================================================

TEST_F(AttachmentTest, AddFindListSize) {
    AttachmentManager mgr(dir());
    EXPECT_TRUE(mgr.empty());
    EXPECT_EQ(mgr.size(), 0u);
    EXPECT_TRUE(mgr.list().empty());

    mgr.add(make_local("foo", write_local_file("foo.pdf", "x")));
    mgr.add(make_url("wiki", "https://example.com/x.pdf"));
    EXPECT_EQ(mgr.size(), 2u);
    EXPECT_FALSE(mgr.empty());

    const auto* found = mgr.find("foo");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type(), Attachment::Type::LOCAL_FILE);
    EXPECT_EQ(found->path(), dir() + "/foo.pdf");

    EXPECT_EQ(mgr.find("wiki")->type(), Attachment::Type::EXTERNAL_URL);
    EXPECT_EQ(mgr.find("nonexistent"), nullptr);
}

TEST_F(AttachmentTest, AddSameNameReplacesInPlace) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("dup", "https://example.com/old.pdf"));
    mgr.add(make_url("dup", "https://example.com/new.pdf"));

    EXPECT_EQ(mgr.size(), 1u);  // 替换不增长
    const auto* found = mgr.find("dup");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->url(), "https://example.com/new.pdf");
}

TEST_F(AttachmentTest, RemoveReturnsTrueOnlyWhenPresent) {
    AttachmentManager mgr(dir());
    mgr.add(make_local("foo", write_local_file("foo.pdf", "x")));

    EXPECT_TRUE(mgr.remove("foo"));
    EXPECT_EQ(mgr.size(), 0u);
    EXPECT_EQ(mgr.find("foo"), nullptr);

    EXPECT_FALSE(mgr.remove("foo"));      // 已不存在
    EXPECT_FALSE(mgr.remove("never"));    // 从未存在
}

TEST_F(AttachmentTest, ManagerInjectsProjectRootForRes) {
    write_local_file("attachments/x.pdf", "PDF");
    AttachmentManager mgr(dir());

    auto a = make_res("ds", "res://attachments/x.pdf");
    mgr.add(a);  // add 时注入 project_root

    const auto* found = mgr.find("ds");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->project_root(), dir());
    EXPECT_TRUE(found->is_valid());  // res:// 解析命中磁盘文件
}

// ============================================================================
// 按 type 过滤
// ============================================================================

TEST_F(AttachmentTest, ListByType) {
    AttachmentManager mgr(dir());
    mgr.add(make_local("a", write_local_file("a.pdf", "x")));
    mgr.add(make_local("b", write_local_file("b.pdf", "x")));
    mgr.add(make_url("c", "https://example.com/c.pdf"));
    mgr.add(make_res("d", "res://d.pdf"));

    EXPECT_EQ(mgr.list_by_type(Attachment::Type::LOCAL_FILE).size(), 2u);
    EXPECT_EQ(mgr.list_by_type(Attachment::Type::EXTERNAL_URL).size(), 1u);
    EXPECT_EQ(mgr.list_by_type(Attachment::Type::PROJECT_RESOURCE).size(), 1u);

    // 过滤结果携带正确 name
    const auto urls = mgr.list_by_type(Attachment::Type::EXTERNAL_URL);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].name(), "c");
}

// ============================================================================
// scan_invalid：失效附件清单
// ============================================================================

TEST_F(AttachmentTest, ScanInvalidFlagsMissingAndBadUrls) {
    AttachmentManager mgr(dir());
    mgr.add(make_local("ok",    write_local_file("ok.pdf", "x")));
    mgr.add(make_local("ghost", dir() + "/missing.pdf"));
    mgr.add(make_url("good",    "https://example.com/x.pdf"));
    mgr.add(make_url("bad",     "ftp://example.com/x.pdf"));

    const auto invalid = mgr.scan_invalid();
    ASSERT_EQ(invalid.size(), 2u);
    EXPECT_EQ(invalid[0].name(), "ghost");  // 保留插入序
    EXPECT_EQ(invalid[1].name(), "bad");
}

TEST_F(AttachmentTest, ScanInvalidIncludesResMissingWithRoot) {
    AttachmentManager mgr(dir());
    mgr.add(make_res("res-ok",   "res://attachments/present.pdf"));
    mgr.add(make_res("res-miss", "res://attachments/absent.pdf"));
    // 注入 root 后再写文件：res-ok 命中，res-miss 缺失
    write_local_file("attachments/present.pdf", "PDF");

    const auto invalid = mgr.scan_invalid();
    ASSERT_EQ(invalid.size(), 1u);
    EXPECT_EQ(invalid[0].name(), "res-miss");
}

TEST_F(AttachmentTest, ScanInvalidEmptyWhenAllHealthy) {
    AttachmentManager mgr(dir());
    mgr.add(make_local("ok", write_local_file("ok.pdf", "x")));
    mgr.add(make_url("good", "https://example.com/x.pdf"));
    EXPECT_TRUE(mgr.scan_invalid().empty());
}

// ============================================================================
// attach_to_board：板关联
// ============================================================================

TEST_F(AttachmentTest, AttachToBoardAndQuery) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));

    EXPECT_TRUE(mgr.attach_to_board("board-uuid-1", "ds"));
    const auto links = mgr.attachments_of_board("board-uuid-1");
    ASSERT_EQ(links.size(), 1u);
    EXPECT_EQ(links[0], "ds");

    // 未关联的板返回空
    EXPECT_TRUE(mgr.attachments_of_board("other-board").empty());
}

TEST_F(AttachmentTest, AttachToBoardIdempotent) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));

    EXPECT_TRUE(mgr.attach_to_board("b1", "ds"));
    EXPECT_FALSE(mgr.attach_to_board("b1", "ds"));  // 重复 -> 幂等失败
    EXPECT_EQ(mgr.attachments_of_board("b1").size(), 1u);
}

TEST_F(AttachmentTest, AttachToBoardRejectsUnknownAttachment) {
    AttachmentManager mgr(dir());
    EXPECT_FALSE(mgr.attach_to_board("b1", "never-added"));
    EXPECT_TRUE(mgr.attachments_of_board("b1").empty());
}

TEST_F(AttachmentTest, AttachToBoardRejectsEmptyArgs) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));
    EXPECT_FALSE(mgr.attach_to_board("", "ds"));
    EXPECT_FALSE(mgr.attach_to_board("b1", ""));
}

TEST_F(AttachmentTest, MultipleBoardsShareAttachment) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));
    EXPECT_TRUE(mgr.attach_to_board("b1", "ds"));
    EXPECT_TRUE(mgr.attach_to_board("b2", "ds"));  // 不同板可关联同一附件
    EXPECT_EQ(mgr.attachments_of_board("b1").size(), 1u);
    EXPECT_EQ(mgr.attachments_of_board("b2").size(), 1u);
}

TEST_F(AttachmentTest, DetachFromBoard) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));
    mgr.attach_to_board("b1", "ds");

    EXPECT_TRUE(mgr.detach_from_board("b1", "ds"));
    EXPECT_TRUE(mgr.attachments_of_board("b1").empty());
    EXPECT_FALSE(mgr.detach_from_board("b1", "ds"));  // 已无关联
    EXPECT_FALSE(mgr.detach_from_board("no-board", "ds"));
}

TEST_F(AttachmentTest, RemoveAttachmentCleansBoardLink) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("ds", "https://example.com/ds.pdf"));
    mgr.attach_to_board("b1", "ds");

    EXPECT_TRUE(mgr.remove("ds"));
    EXPECT_TRUE(mgr.attachments_of_board("b1").empty());  // 悬空引用已清理
}

// ============================================================================
// datasheet 联动占位
// ============================================================================

TEST_F(AttachmentTest, DatasheetLinkRoundTrip) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("mcu-ds", "https://vendor.com/mcu.pdf"));

    EXPECT_TRUE(mgr.link_datasheet("comp-stm32-1", "mcu-ds"));
    EXPECT_FALSE(mgr.link_datasheet("comp-stm32-1", "mcu-ds"));  // 幂等

    const auto ds1 = mgr.datasheets_of("comp-stm32-1");
    ASSERT_EQ(ds1.size(), 1u);
    EXPECT_EQ(ds1[0], "mcu-ds");

    EXPECT_TRUE(mgr.unlink_datasheet("comp-stm32-1", "mcu-ds"));
    EXPECT_TRUE(mgr.datasheets_of("comp-stm32-1").empty());
    EXPECT_FALSE(mgr.unlink_datasheet("comp-stm32-1", "mcu-ds"));
}

// 多器件共享同一份 datasheet（工程内不重复存储）
TEST_F(AttachmentTest, DatasheetSharedAcrossComponents) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("mcu-ds", "https://vendor.com/mcu.pdf"));

    EXPECT_TRUE(mgr.link_datasheet("comp-stm32-1", "mcu-ds"));
    EXPECT_TRUE(mgr.link_datasheet("comp-stm32-2", "mcu-ds"));

    EXPECT_EQ(mgr.datasheets_of("comp-stm32-1").size(), 1u);
    EXPECT_EQ(mgr.datasheets_of("comp-stm32-2").size(), 1u);
}

TEST_F(AttachmentTest, DatasheetLinkRejectsUnknownAttachment) {
    AttachmentManager mgr(dir());
    EXPECT_FALSE(mgr.link_datasheet("c1", "never-added"));
    EXPECT_TRUE(mgr.datasheets_of("c1").empty());
}

TEST_F(AttachmentTest, RemoveAttachmentCleansDatasheetLink) {
    AttachmentManager mgr(dir());
    mgr.add(make_url("mcu-ds", "https://vendor.com/mcu.pdf"));
    mgr.link_datasheet("c1", "mcu-ds");

    EXPECT_TRUE(mgr.remove("mcu-ds"));
    EXPECT_TRUE(mgr.datasheets_of("c1").empty());
}
