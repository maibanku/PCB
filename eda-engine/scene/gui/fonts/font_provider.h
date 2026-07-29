// FontProvider —— TTF 字体加载 + glyph 栅格化 + glyph cache（P4 Task 协同：文本渲染）。
//
// 在 Theme（scene/gui/theme.h，font token：default/small/heading/mono）与 Canvas2D
// （scene/2d/canvas_2d.h）之间补齐"字体 → 像素"这一段：Theme 只登记 family/size/weight
// token，本类负责把 TTF 字节流交给 stb_truetype 栅格化为 alpha bitmap，并缓存。
//
// 设计要点：
//   - 单例（Meyers singleton，与 ThemeManager 一致）——进程内字体数据共享，避免重复解析 TTF。
//   - family_name 为逻辑键（如 "Inter" / "JetBrains Mono"），load_font 时绑定一份 TTF。
//     Inter/JetBrains Mono 在 Windows 默认不存在，load_default_system_fonts() 用
//     C:/Windows/Fonts/arial.ttf / consola.ttf 作系统兜底。
//   - stbtt_fontinfo 内部指针指向 ttf_data 缓冲，故 FontFace 持有 ttf_data 直至卸载。
//   - get_glyph(family, codepoint, pixel_size) 走两段查表：
//       * 命中缓存（family + codepoint + size） → 直接返回引用；
//       * 未命中 → stbtt_GetCodepointBitmap 栅格化 → 存入缓存 → 返回。
//   - 缺失 family：返回静态空 GlyphInfo（不抛异常，对齐 Variant 缺键策略）。
//
// 与 text_renderer 协同：text_renderer 查 Theme 拿 token → 拆出 family/size → 调
// FontProvider::get_glyph 拿 GlyphInfo → 当前首版用 draw_rect_filled 画 glyph bounding
// rect 占位（可辨识文本长度/位置），后续升级为按 alpha bitmap 逐像素 quad。

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct stbtt_fontinfo;  // 前向声明，避免头文件传染 stb_truetype.h

namespace eda {

// 单个字符的栅格化结果。
struct GlyphInfo {
    int    width  = 0;       // bitmap 像素宽
    int    height = 0;       // bitmap 像素高
    float  advance = 0.0f;   // 笔触水平推进（像素）
    float  left_bearing = 0.0f;   // bitmap 左边缘相对笔触的偏移（像素）
    float  top_bearing  = 0.0f;   // bitmap 顶边缘相对基线的偏移（向下为正）
    std::vector<uint8_t> bitmap;  // alpha 灰度位图（width × height，0=透明 255=不透明）
};

// 字体行度量（基线相关）。
struct FontMetrics {
    float ascent   = 0.0f;   // 基线到行顶（正数）
    float descent  = 0.0f;   // 基线到行底（正数，stb 原值取负后存正）
    float line_gap = 0.0f;   // 行间隙
    float line_height() const { return ascent + descent + line_gap; }
};

// ---------------------------------------------------------------------------
// FontProvider —— 进程级单例。
// ---------------------------------------------------------------------------

class FontProvider {
public:
    static FontProvider& get();

    FontProvider(const FontProvider&) = delete;
    FontProvider& operator=(const FontProvider&) = delete;
    FontProvider(FontProvider&&) = delete;
    FontProvider& operator=(FontProvider&&) = delete;

    // —— 加载 ——
    //
    // 从 TTF 文件路径加载字体绑定到 family_name。读取失败 / 解析失败返回 false。
    // 同一 family 重复加载：后者覆盖前者（含 ttf_data 与 fontinfo）。
    bool load_font(const std::string& family_name, const std::string& ttf_path);

    // Windows 系统兜底：把常用逻辑名映射到系统 TTF。
    //   "Inter"          → C:/Windows/Fonts/arial.ttf
    //   "JetBrains Mono" → C:/Windows/Fonts/consola.ttf
    // 返回是否至少加载成功一个。
    bool load_default_system_fonts();

    // 若 family 未加载，则尝试按 fallback 表懒加载（render_text 用）。
    // 命中 fallback 成功返回 true；否则 false。
    bool ensure_loaded(const std::string& family_name);

    bool has_family(const std::string& family_name) const;

    // —— 查询 ——
    //
    // 取字符位图：命中缓存直接返回；否则栅格化后入缓存。缺失 family 返回静态空 GlyphInfo。
    // codepoint 取 uint32_t 以兼容多字节；ASCII 场景调用方把 char 转 uint8_t 再传入。
    const GlyphInfo& get_glyph(const std::string& family_name,
                               uint32_t codepoint,
                               int pixel_size);

    // 取字体行度量（ascent / descent / line_gap），按 pixel_size 缩放。
    // 缺失 family 返回零值 FontMetrics。
    FontMetrics get_font_metrics(const std::string& family_name, int pixel_size);

    // —— 测试 / 重置 ——
    void unload_all();  // 清空所有 family 与 glyph 缓存（测试隔离用）

private:
    FontProvider();
    ~FontProvider();
    FontProvider& self() { return *this; }

    // 字体面（TTF 数据 + stbtt_fontinfo）。ttf_data 必须先于 info 构造、晚于 info 析构。
    struct FontFace {
        std::vector<uint8_t> ttf_data;
        stbtt_fontinfo*      info = nullptr;  // 指向 stbtt_fontinfo（完整定义在 .cpp）
        bool                 ready = false;
    };

    // glyph cache 键：family + codepoint + size。
    struct GlyphCacheKey {
        std::string family;
        uint32_t    codepoint;
        int         pixel_size;
        bool operator==(const GlyphCacheKey& o) const {
            return family == o.family
                && codepoint == o.codepoint
                && pixel_size == o.pixel_size;
        }
    };

    struct GlyphCacheKeyHash {
        size_t operator()(const GlyphCacheKey& k) const {
            // 简易组合哈希；冲突率不影响正确性（== 兜底）。
            size_t h = std::hash<std::string>{}(k.family);
            h ^= static_cast<size_t>(k.codepoint) * 0x9E3779B97F4A7C15ULL;
            h ^= static_cast<size_t>(k.pixel_size) * 0xC2B2AE3D27D4EB4FULL;
            return h;
        }
    };

    std::unordered_map<std::string, FontFace>                       faces_;
    std::unordered_map<GlyphCacheKey, GlyphInfo, GlyphCacheKeyHash> glyph_cache_;

    // 空字形占位（缺失 family 时返回的静态实例）。
    static const GlyphInfo& empty_glyph();
};

}  // namespace eda
