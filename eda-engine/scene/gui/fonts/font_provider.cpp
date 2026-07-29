// FontProvider 实现（stb_truetype 集成）。
//
// 所有 stb_truetype.h 调用集中在本文件：头文件 font_provider.h 仅前向声明 stbtt_fontinfo，
// 避免把 third_party 头扩散到业务 TU（编译单元）。

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"

#include "scene/gui/fonts/font_provider.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <vector>

namespace eda {

// stbtt_fontinfo 在 stb_truetype.h 中为 typedef，FontFace::info 用指针形式以避免头文件传染。
// 这里 operator new / delete 显式管理，使 FontFace 可拷贝/移动时不泄漏。

namespace {

// 读 TTF 文件为字节流。失败返回空 vector。
std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
}

}  // namespace

// ===========================================================================
// FontProvider —— 单例
// ===========================================================================

FontProvider::FontProvider() = default;

FontProvider::~FontProvider() {
    // 释放所有 stbtt_fontinfo（ttf_data 由 vector 自动析构）。
    for (auto& [_, face] : faces_) {
        if (face.info != nullptr) {
            delete face.info;
            face.info = nullptr;
        }
    }
}

FontProvider& FontProvider::get() {
    // C++11 函数内 static：线程安全初始化（Meyers singleton）。
    static FontProvider instance;
    return instance;
}

const GlyphInfo& FontProvider::empty_glyph() {
    static const GlyphInfo kEmpty{};
    return kEmpty;
}

// ---------------------------------------------------------------------------
// 加载
// ---------------------------------------------------------------------------

bool FontProvider::load_font(const std::string& family_name, const std::string& ttf_path) {
    std::vector<uint8_t> data = read_file_bytes(ttf_path);
    if (data.empty()) {
        return false;
    }

    // 释放旧 info（若有）后再覆盖。
    FontFace face;
    face.ttf_data = std::move(data);
    face.info = new (std::nothrow) stbtt_fontinfo;
    if (face.info == nullptr) {
        return false;
    }

    // stbtt_InitFont 不拷贝 ttf_data，仅缓存内部指针；ttf_data 必须与 info 同生命周期。
    int ok = stbtt_InitFont(face.info, face.ttf_data.data(), 0);
    if (!ok) {
        delete face.info;
        return false;
    }
    face.ready = true;

    // 覆盖旧 family（含其 info 析构）。先取引用再析构旧 info，避免 map 节点析构误删。
    auto& slot = faces_[family_name];
    if (slot.info != nullptr) {
        delete slot.info;
    }
    slot = std::move(face);
    // 注意：move 后 face.info 指针所有权转移到 slot；face.ttf_data 也已转移。

    // 换字形数据后族内缓存失效（不同 TTF 的度量不同）。
    for (auto it = glyph_cache_.begin(); it != glyph_cache_.end(); ) {
        if (it->first.family == family_name) {
            it = glyph_cache_.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool FontProvider::load_default_system_fonts() {
    // Windows 系统兜底：Inter / JetBrains Mono 在多数开发机缺失，退化为 arial / consola。
    bool any_ok = false;
    any_ok |= load_font("Inter",          "C:/Windows/Fonts/arial.ttf");
    any_ok |= load_font("JetBrains Mono", "C:/Windows/Fonts/consola.ttf");
    return any_ok;
}

bool FontProvider::ensure_loaded(const std::string& family_name) {
    if (has_family(family_name)) {
        return true;
    }
    // 兜底表：逻辑名 → 候选 TTF（按优先级）。
    std::vector<std::string> candidates;
    if (family_name == "Inter") {
        candidates = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
    } else if (family_name == "JetBrains Mono") {
        candidates = {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"};
    } else {
        // 未知名一律退到 arial。
        candidates = {"C:/Windows/Fonts/arial.ttf"};
    }
    for (const auto& path : candidates) {
        if (load_font(family_name, path)) {
            return true;
        }
    }
    return false;
}

bool FontProvider::has_family(const std::string& family_name) const {
    auto it = faces_.find(family_name);
    return it != faces_.end() && it->second.ready;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const GlyphInfo& FontProvider::get_glyph(const std::string& family_name,
                                         uint32_t codepoint,
                                         int pixel_size) {
    auto face_it = faces_.find(family_name);
    if (face_it == faces_.end() || !face_it->second.ready) {
        return empty_glyph();
    }
    const FontFace& face = face_it->second;

    GlyphCacheKey key{family_name, codepoint, pixel_size};
    auto cache_it = glyph_cache_.find(key);
    if (cache_it != glyph_cache_.end()) {
        return cache_it->second;
    }

    // 栅格化 alpha bitmap。
    const float scale = stbtt_ScaleForPixelHeight(face.info, static_cast<float>(pixel_size));
    int w = 0, h = 0;
    int ixoff = 0, iyoff = 0;
    unsigned char* bmp = stbtt_GetCodepointBitmapSubpixel(
        face.info, scale, scale, 0.0f, 0.0f,
        static_cast<int>(codepoint), &w, &h, &ixoff, &iyoff);

    GlyphInfo info;
    info.width = w;
    info.height = h;
    info.left_bearing = static_cast<float>(ixoff);
    info.top_bearing = static_cast<float>(iyoff);

    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(face.info, static_cast<int>(codepoint), &advance, &lsb);
    info.advance = static_cast<float>(advance) * scale;

    if (bmp != nullptr && w > 0 && h > 0) {
        info.bitmap.assign(bmp, bmp + (static_cast<size_t>(w) * h));
    }
    // stb 文档：调用方负责释放返回的 bitmap。
    if (bmp != nullptr) {
        stbtt_FreeBitmap(bmp, nullptr);
    }

    // 入缓存并返回引用（unordered_map 节点稳定，引用不失效直至 unload_all）。
    auto [inserted_it, _] = glyph_cache_.emplace(std::move(key), std::move(info));
    return inserted_it->second;
}

FontMetrics FontProvider::get_font_metrics(const std::string& family_name, int pixel_size) {
    auto face_it = faces_.find(family_name);
    if (face_it == faces_.end() || !face_it->second.ready) {
        return FontMetrics{};
    }
    const FontFace& face = face_it->second;
    const float scale = stbtt_ScaleForPixelHeight(face.info, static_cast<float>(pixel_size));

    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(face.info, &ascent, &descent, &line_gap);

    FontMetrics m;
    m.ascent   = static_cast<float>(ascent)   * scale;
    // stb 返回的 descent 通常为负数；统一存正数表示"基线到行底的像素距离"。
    m.descent  = -static_cast<float>(descent) * scale;
    m.line_gap = static_cast<float>(line_gap) * scale;
    return m;
}

void FontProvider::unload_all() {
    for (auto& [_, face] : faces_) {
        if (face.info != nullptr) {
            delete face.info;
            face.info = nullptr;
        }
    }
    faces_.clear();
    glyph_cache_.clear();
}

}  // namespace eda
