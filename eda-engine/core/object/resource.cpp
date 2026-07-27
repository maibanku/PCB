#include "core/object/resource.h"

#include "core/os/os.h"

#include <algorithm>
#include <cctype>

namespace eda {

// ============================================================================
// Resource —— 序列化占位实现。
// ============================================================================

std::string Resource::serialize() const {
    // 基类无数据；派生类 override。
    return std::string();
}

bool Resource::deserialize(const std::string& /*data*/) {
    // 基类不识别任何输入；派生类 override。
    return false;
}

// ============================================================================
// ResourceCache
// ============================================================================

struct ResourceCache::State {
    std::unordered_map<std::string, Ref<Resource>> entries;
};

ResourceCache::State& ResourceCache::state() {
    // 函数局部 static：避免跨翻译单元初始化顺序问题，且天然单例。
    static State instance;
    return instance;
}

Ref<Resource> ResourceCache::get_ref(const std::string& path) {
    State& s = state();
    auto it = s.entries.find(path);
    if (it == s.entries.end()) {
        return Ref<Resource>();
    }
    return it->second;
}

void ResourceCache::add(const std::string& path, Ref<Resource> res) {
    state().entries[path] = std::move(res);
}

void ResourceCache::remove(const std::string& path) {
    state().entries.erase(path);
}

bool ResourceCache::has(const std::string& path) {
    return state().entries.find(path) != state().entries.end();
}

void ResourceCache::clear() {
    state().entries.clear();
}

// ============================================================================
// ResourceLoader
// ============================================================================

namespace {

struct LoaderRegistry {
    std::vector<ResourceFormatLoader*> loaders;
};

LoaderRegistry& loader_registry() {
    static LoaderRegistry r;
    return r;
}

}  // namespace

void ResourceLoader::register_format(ResourceFormatLoader* loader) {
    if (loader == nullptr) {
        return;
    }
    auto& r = loader_registry();
    if (std::find(r.loaders.begin(), r.loaders.end(), loader) == r.loaders.end()) {
        r.loaders.push_back(loader);
    }
}

void ResourceLoader::clear_formats() {
    loader_registry().loaders.clear();
}

std::string ResourceLoader::get_extension(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    std::string leaf = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = leaf.find_last_of('.');
    if (dot == std::string::npos) {
        return std::string();
    }
    std::string ext = leaf.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return ext;
}

Ref<Resource> ResourceLoader::load(const std::string& path) {
    // 1) cache 命中：直接返回（同一 path 同一实例）。
    if (ResourceCache::has(path)) {
        return ResourceCache::get_ref(path);
    }

    // 2) 按扩展名找首个支持的 loader。
    const std::string ext = get_extension(path);
    ResourceFormatLoader* matched = nullptr;
    for (ResourceFormatLoader* l : loader_registry().loaders) {
        if (l->handles_extension(ext)) {
            matched = l;
            break;
        }
    }
    if (matched == nullptr) {
        return Ref<Resource>();
    }

    // 3) 解析 res:// → 磁盘路径。
    const std::string disk_path = OS::get_singleton()->resolve_resource_path(path);

    // 4) 加载并回填 path；失败不写 cache。
    Ref<Resource> res = matched->load(disk_path);
    if (!res.valid()) {
        return Ref<Resource>();
    }
    res->set_path(path);
    ResourceCache::add(path, res);
    return res;
}

// ============================================================================
// ResourceSaver
// ============================================================================

namespace {

struct SaverRegistry {
    std::vector<ResourceFormatSaver*> savers;
};

SaverRegistry& saver_registry() {
    static SaverRegistry r;
    return r;
}

}  // namespace

void ResourceSaver::register_format(ResourceFormatSaver* saver) {
    if (saver == nullptr) {
        return;
    }
    auto& r = saver_registry();
    if (std::find(r.savers.begin(), r.savers.end(), saver) == r.savers.end()) {
        r.savers.push_back(saver);
    }
}

void ResourceSaver::clear_formats() {
    saver_registry().savers.clear();
}

bool ResourceSaver::save(const std::string& path, Ref<Resource> res) {
    if (!res.valid()) {
        return false;
    }
    const std::string ext = ResourceLoader::get_extension(path);
    ResourceFormatSaver* matched = nullptr;
    for (ResourceFormatSaver* s : saver_registry().savers) {
        if (s->handles_extension(ext)) {
            matched = s;
            break;
        }
    }
    if (matched == nullptr) {
        return false;
    }
    const std::string disk_path = OS::get_singleton()->resolve_resource_path(path);
    if (!matched->save(disk_path, res)) {
        return false;
    }
    res->set_path(path);
    return true;
}

}  // namespace eda
