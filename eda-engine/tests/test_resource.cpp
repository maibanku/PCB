// P2 Task 5 —— Resource 资源系统 + OS 抽象测试。
//
// 覆盖：
//   - Resource 基类（继承 RefCounted / 类名字符串 / path 读写 / serialize 占位）
//   - Ref<Resource> 引用计数（拷贝增计数、reset 减计数、归零销毁）
//   - OS 单例（get_singleton / resource_root 往返 / res:// 解析 / ticks 非递减）
//   - ResourceSaver + ResourceLoader 文本往返（save 后 load 还原 data）
//   - ResourceCache 同一 path 多次 load 返回同一实例（指针相等）+ cache 保活
//
// 依赖：core/object/{ref_counted, ref, resource}.h + core/os/os.h。
// 注意：本文件不改 CMakeLists；集成步骤由主循环完成（追加 resource.cpp + os/os.cpp 到 eda_core）。

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/object/ref.h"
#include "core/object/resource.h"
#include "core/os/os.h"

using namespace eda;

namespace {

// TextResource —— 测试用的具体 Resource：单字段 data，序列化为 "data=<value>"。
class TextResource : public Resource {
public:
    static std::string get_class_static() { return "TextResource"; }
    std::string get_class() const override { return "TextResource"; }
    bool is_class(const std::string& name) const override {
        return name == "TextResource" || Resource::is_class(name);
    }

    void set_data(std::string d) { data_ = std::move(d); }
    const std::string& get_data() const { return data_; }

    std::string serialize() const override {
        return std::string("data=") + data_;
    }
    bool deserialize(const std::string& s) override {
        static const std::string prefix = "data=";
        if (s.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        data_ = s.substr(prefix.size());
        return true;
    }

private:
    std::string data_;
};

// TextResourceFormatLoader/Saver —— 走纯文本 IO，构造 TextResource。
class TextResourceFormatLoader : public ResourceFormatLoader {
public:
    bool handles_extension(const std::string& ext) const override {
        return ext == "txt";
    }
    Ref<Resource> load(const std::string& disk_path) const override {
        std::ifstream f(disk_path, std::ios::binary);
        if (!f) {
            return Ref<Resource>();
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        Ref<TextResource> r(new TextResource());
        if (!r->deserialize(content)) {
            return Ref<Resource>();
        }
        return r;
    }
};

class TextResourceFormatSaver : public ResourceFormatSaver {
public:
    bool handles_extension(const std::string& ext) const override {
        return ext == "txt";
    }
    bool save(const std::string& disk_path, Ref<Resource> res) const override {
        std::ofstream f(disk_path, std::ios::binary | std::ios::trunc);
        if (!f) {
            return false;
        }
        f << res->serialize();
        return static_cast<bool>(f);
    }
};

// 程序期静态实例：register_format 仅持裸指针，避免堆泄漏跨用例累积。
TextResourceFormatLoader& text_loader() {
    static TextResourceFormatLoader inst;
    return inst;
}
TextResourceFormatSaver& text_saver() {
    static TextResourceFormatSaver inst;
    return inst;
}

// SpyResource —— 统计析构，验证 Ref<T> 归零销毁。
class SpyResource : public Resource {
public:
    static inline int alive = 0;
    SpyResource() { ++alive; }
    ~SpyResource() override { --alive; }

    static std::string get_class_static() { return "SpyResource"; }
    std::string get_class() const override { return "SpyResource"; }
    bool is_class(const std::string& name) const override {
        return name == "SpyResource" || Resource::is_class(name);
    }
};

std::string read_file(const std::string& disk_path) {
    std::ifstream f(disk_path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// ============================================================================
// Resource 基类（无 IO，无需 fixture）
// ============================================================================

TEST(ResourceTest, IsClassRecognizesSelfAndAncestors) {
    Resource r;
    EXPECT_EQ(r.get_class(), "Resource");
    EXPECT_EQ(Resource::get_class_static(), "Resource");
    EXPECT_TRUE(r.is_class("Resource"));
    EXPECT_TRUE(r.is_class("RefCounted"));
    EXPECT_TRUE(r.is_class("Object"));
    EXPECT_FALSE(r.is_class("Node"));
}

TEST(ResourceTest, PathDefaultsEmptyAndRoundtrips) {
    Resource r;
    EXPECT_TRUE(r.get_path().empty());
    r.set_path("res://foo.txt");
    EXPECT_EQ(r.get_path(), "res://foo.txt");
}

TEST(ResourceTest, DefaultSerializeAndDeserializeArePlaceholders) {
    Resource r;
    EXPECT_TRUE(r.serialize().empty());
    EXPECT_FALSE(r.deserialize("anything"));
}

// Ref<Resource> 引用计数

TEST(ResourceTest, RefCopyIncrementsCount) {
    Ref<Resource> a(new Resource());
    EXPECT_EQ(a->get_reference_count(), 1);
    Ref<Resource> b = a;
    EXPECT_EQ(a->get_reference_count(), 2);
    EXPECT_EQ(b->get_reference_count(), 2);
    EXPECT_EQ(a.get(), b.get());
}

TEST(ResourceTest, RefResetDropsOneRefKeepsObjectAlive) {
    Ref<Resource> a(new Resource());
    Ref<Resource> b = a;
    EXPECT_EQ(a->get_reference_count(), 2);
    a.reset();
    EXPECT_FALSE(a.valid());
    EXPECT_EQ(b->get_reference_count(), 1);
}

TEST(ResourceTest, RefLastReleaseDeletesObject) {
    SpyResource::alive = 0;
    EXPECT_EQ(SpyResource::alive, 0);
    {
        Ref<SpyResource> r(new SpyResource());
        EXPECT_EQ(SpyResource::alive, 1);
        EXPECT_EQ(r->get_reference_count(), 1);
    }
    EXPECT_EQ(SpyResource::alive, 0);
}

// ============================================================================
// OS 单例（每个用例独立设 resource_root，互不影响）
// ============================================================================

TEST(OSTest, GetSingletonReturnsSameInstance) {
    OS* a = OS::get_singleton();
    OS* b = OS::get_singleton();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
}

TEST(OSTest, SetGetResourceRootRoundtrip) {
    OS::get_singleton()->set_resource_root("/some/abs/path");
    EXPECT_EQ(OS::get_singleton()->get_resource_root(), "/some/abs/path");
}

TEST(OSTest, ResolveResPathWithoutTrailingSlash) {
    OS::get_singleton()->set_resource_root("/abs/res");
    EXPECT_EQ(OS::get_singleton()->resolve_resource_path("res://foo.txt"),
              "/abs/res/foo.txt");
}

TEST(OSTest, ResolveResPathWithTrailingSlash) {
    OS::get_singleton()->set_resource_root("/abs/res/");
    EXPECT_EQ(OS::get_singleton()->resolve_resource_path("res://foo.toml"),
              "/abs/res/foo.toml");
}

TEST(OSTest, ResolveResPathNestedSubdirs) {
    OS::get_singleton()->set_resource_root("/abs/res");
    EXPECT_EQ(OS::get_singleton()->resolve_resource_path("res://sub/dir/a.json"),
              "/abs/res/sub/dir/a.json");
}

TEST(OSTest, ResolveNonResPathPassesThrough) {
    OS::get_singleton()->set_resource_root("/abs/res");
    EXPECT_EQ(OS::get_singleton()->resolve_resource_path("/etc/passwd"),
              "/etc/passwd");
    EXPECT_EQ(OS::get_singleton()->resolve_resource_path("relative.txt"),
              "relative.txt");
}

TEST(OSTest, GetTicksMsecIsMonotonicNonDecreasing) {
    uint64_t t0 = OS::get_singleton()->get_ticks_msec();
    uint64_t t1 = OS::get_singleton()->get_ticks_msec();
    EXPECT_GE(t1, t0);
}

// ============================================================================
// IO 相关用例：fixture 提供临时目录 + 干净的 cache / loader / saver 注册表
// ============================================================================

class ResourceIOFixture : public ::testing::Test {
protected:
    static inline std::filesystem::path tmp_dir;

    static void SetUpTestSuite() {
        tmp_dir = std::filesystem::temp_directory_path() / "eda_resource_test";
        ResourceLoader::register_format(&text_loader());
        ResourceSaver::register_format(&text_saver());
    }

    static void TearDownTestSuite() {
        ResourceLoader::clear_formats();
        ResourceSaver::clear_formats();
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }

    void SetUp() override {
        ResourceCache::clear();
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
        std::filesystem::create_directories(tmp_dir, ec);
        OS::get_singleton()->set_resource_root(tmp_dir.string());
    }

    void TearDown() override {
        ResourceCache::clear();
    }
};

TEST_F(ResourceIOFixture, SaveWritesFileToResolvedDiskPath) {
    Ref<TextResource> r(new TextResource());
    r->set_data("hello");
    ASSERT_TRUE(ResourceSaver::save("res://save_test.txt", r));

    const std::string disk = (tmp_dir / "save_test.txt").string();
    EXPECT_TRUE(std::filesystem::exists(disk));
    EXPECT_EQ(read_file(disk), "data=hello");
}

TEST_F(ResourceIOFixture, SaveSetsResourcePathAttribute) {
    Ref<TextResource> r(new TextResource());
    r->set_data("p");
    EXPECT_TRUE(r->get_path().empty());
    ASSERT_TRUE(ResourceSaver::save("res://path_check.txt", r));
    EXPECT_EQ(r->get_path(), "res://path_check.txt");
}

TEST_F(ResourceIOFixture, SaveRejectsEmptyRef) {
    Ref<Resource> empty;
    EXPECT_FALSE(ResourceSaver::save("res://empty.txt", empty));
    EXPECT_FALSE(std::filesystem::exists(tmp_dir / "empty.txt"));
}

TEST_F(ResourceIOFixture, SaveRejectsUnknownExtension) {
    Ref<TextResource> r(new TextResource());
    r->set_data("x");
    EXPECT_FALSE(ResourceSaver::save("res://unknown.bin", r));
}

TEST_F(ResourceIOFixture, LoadReturnsEmptyForUnknownExtension) {
    // 写一个 .bin 文件然后尝试 load —— 无匹配 loader
    const std::string disk = (tmp_dir / "unknown.bin").string();
    std::ofstream f(disk, std::ios::trunc);
    f << "garbage";
    f.close();
    Ref<Resource> r = ResourceLoader::load("res://unknown.bin");
    EXPECT_FALSE(r.valid());
}

TEST_F(ResourceIOFixture, LoadReturnsEmptyForMissingFile) {
    Ref<Resource> r = ResourceLoader::load("res://missing.txt");
    EXPECT_FALSE(r.valid());
}

TEST_F(ResourceIOFixture, LoadReturnsEmptyWhenDeserializeFails) {
    // 写入一个格式错误的 .txt（不以 "data=" 开头），loader 应放弃
    const std::string disk = (tmp_dir / "broken.txt").string();
    std::ofstream f(disk, std::ios::trunc);
    f << "not-a-valid-format";
    f.close();
    Ref<Resource> r = ResourceLoader::load("res://broken.txt");
    EXPECT_FALSE(r.valid());
    // 加载失败不应污染 cache
    EXPECT_FALSE(ResourceCache::has("res://broken.txt"));
}

TEST_F(ResourceIOFixture, SaveThenLoadRoundtripPreservesDataAndPath) {
    Ref<TextResource> original(new TextResource());
    original->set_data("eda-engine-p2");
    ASSERT_TRUE(ResourceSaver::save("res://roundtrip.txt", original));

    Ref<Resource> loaded = ResourceLoader::load("res://roundtrip.txt");
    ASSERT_TRUE(loaded.valid());
    EXPECT_EQ(loaded->get_class(), "TextResource");
    EXPECT_TRUE(loaded->is_class("Resource"));
    EXPECT_EQ(loaded->get_path(), "res://roundtrip.txt");

    auto* typed = static_cast<TextResource*>(loaded.get());
    EXPECT_EQ(typed->get_data(), "eda-engine-p2");
}

TEST_F(ResourceIOFixture, RoundtripOverwritesExistingFileContent) {
    {
        Ref<TextResource> a(new TextResource());
        a->set_data("first");
        ASSERT_TRUE(ResourceSaver::save("res://overwrite.txt", a));
    }
    {
        Ref<TextResource> b(new TextResource());
        b->set_data("second");
        ASSERT_TRUE(ResourceSaver::save("res://overwrite.txt", b));
    }
    EXPECT_EQ(read_file((tmp_dir / "overwrite.txt").string()), "data=second");
}

// ============================================================================
// ResourceCache —— 单实例共享 + 保活语义
// ============================================================================

TEST_F(ResourceIOFixture, LoadTwiceReturnsSameInstance) {
    Ref<TextResource> seed(new TextResource());
    seed->set_data("cached");
    ASSERT_TRUE(ResourceSaver::save("res://cache_check.txt", seed));

    Ref<Resource> first = ResourceLoader::load("res://cache_check.txt");
    ASSERT_TRUE(first.valid());
    Ref<Resource> second = ResourceLoader::load("res://cache_check.txt");
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(first.get(), second.get());  // 同一指针
}

TEST_F(ResourceIOFixture, LoadPopulatesCacheHasEntry) {
    Ref<TextResource> seed(new TextResource());
    seed->set_data("x");
    ASSERT_TRUE(ResourceSaver::save("res://has_check.txt", seed));

    EXPECT_FALSE(ResourceCache::has("res://has_check.txt"));
    Ref<Resource> r = ResourceLoader::load("res://has_check.txt");
    ASSERT_TRUE(r.valid());
    EXPECT_TRUE(ResourceCache::has("res://has_check.txt"));
}

TEST_F(ResourceIOFixture, CacheAddAndGetReturnsSameRef) {
    Ref<TextResource> manual(new TextResource());
    manual->set_data("manual");
    ResourceCache::add("res://manual.txt", manual);

    EXPECT_TRUE(ResourceCache::has("res://manual.txt"));
    Ref<Resource> got = ResourceCache::get_ref("res://manual.txt");
    ASSERT_TRUE(got.valid());
    EXPECT_EQ(got.get(), manual.get());
}

TEST_F(ResourceIOFixture, CacheRemoveDropsSpecificEntry) {
    Ref<TextResource> seed(new TextResource());
    seed->set_data("removable");
    ASSERT_TRUE(ResourceSaver::save("res://removable.txt", seed));

    ResourceLoader::load("res://removable.txt");
    EXPECT_TRUE(ResourceCache::has("res://removable.txt"));
    ResourceCache::remove("res://removable.txt");
    EXPECT_FALSE(ResourceCache::has("res://removable.txt"));
}

TEST_F(ResourceIOFixture, CacheClearDropsAllEntries) {
    Ref<TextResource> seed(new TextResource());
    seed->set_data("clearable");
    ASSERT_TRUE(ResourceSaver::save("res://clearable.txt", seed));

    ResourceLoader::load("res://clearable.txt");
    EXPECT_TRUE(ResourceCache::has("res://clearable.txt"));
    ResourceCache::clear();
    EXPECT_FALSE(ResourceCache::has("res://clearable.txt"));
}

TEST_F(ResourceIOFixture, CacheGetRefReturnsEmptyForUnknown) {
    EXPECT_FALSE(ResourceCache::has("res://never.txt"));
    Ref<Resource> r = ResourceCache::get_ref("res://never.txt");
    EXPECT_FALSE(r.valid());
}

TEST_F(ResourceIOFixture, CacheKeepsResourceAliveAfterExternalRefsReleased) {
    Ref<TextResource> seed(new TextResource());
    seed->set_data("scoped");
    ASSERT_TRUE(ResourceSaver::save("res://scoped.txt", seed));

    Resource* raw = nullptr;
    {
        Ref<Resource> r = ResourceLoader::load("res://scoped.txt");
        raw = r.get();
        // local Ref + cache Ref = 2
        EXPECT_EQ(raw->get_reference_count(), 2);
    }
    // 外部 Ref 释放后，cache 仍持有；对象存活，refcount 降为 1
    EXPECT_EQ(raw->get_reference_count(), 1);
}

TEST_F(ResourceIOFixture, CacheSurvivesLoaderRegistryResetBecauseItOnlyHoldsRefs) {
    // 即便 loader 注册表清空，已 cache 的 Ref 仍可正常 get_ref（不重新走 IO）
    Ref<TextResource> seed(new TextResource());
    seed->set_data("survive");
    ASSERT_TRUE(ResourceSaver::save("res://survive.txt", seed));
    Ref<Resource> first = ResourceLoader::load("res://survive.txt");
    ASSERT_TRUE(first.valid());

    ResourceLoader::clear_formats();  // 临时清空（setUpTestSuite 的静态实例未注销）
    ResourceLoader::register_format(&text_loader());  // 还原

    Ref<Resource> second = ResourceLoader::load("res://survive.txt");
    EXPECT_EQ(first.get(), second.get());  // 仍命中 cache
}
