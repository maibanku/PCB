// editor/shortcuts/shortcut_def.h
//
// P4 Task 7 · 快捷键体系（ShortcutDef / Key / ModifierFlags）。
//
// 本头定义快捷键体系的"值类型"层：
//   - ModifierFlags  修饰键位掩码（CTRL / SHIFT / ALT / SUPER 可组合）
//   - Key            抽象物理按键枚举（与 GLFW 数值解耦；转换在 InputMap）
//   - ShortcutDef    一条绑定 { key, mods, command_id, display }
//
// 设计原则：
//   - 仅依赖 <cstdint> / <string>，零业务耦合；可被 InputMap / 命令面板显示 /
//     菜单 tooltip / .keymap.toml 序列化等任意模块包含。
//   - Key 是 scoped enum，覆盖 EDA 常用键面（A-Z / 0-9 / F1-F12 / 方向 / Esc /
//     Tab / Space / Enter / Delete / Backspace / Home / End / PageUp / PageDown /
//     Minus / Equals）；未覆盖键统一 UNKNOWN，业务如需扩展追加枚举值即可。
//   - ModifierFlags 是 scoped enum + 重载运算符（| / & / ~），保类型安全的同时
//     支持 mods | mods 组合（避免退化为 int 后混入普通整数算术）。
//
// 与上游契约：
//   - InputEvent（core/input/input_event.h）：key 字段是 GLFW key code（int）；
//     InputMap::key_from_glfw 负责把它映射到本头的 Key 枚举。
//   - CommandRegistry（editor/command_palette）：仅 command_id 字符串耦合——
//     本头不依赖 CommandRegistry 类型，command_id 取值对齐 builtin:: 常量
//     （"file.open" / "edit.undo" / "view.zoom_in" ...）。

#pragma once

#include <cstdint>
#include <string>

namespace eda {

// ===========================================================================
// ModifierFlags —— 修饰键位掩码。
//
// 对齐 GLFW / Win32 / macOS 主流修饰键约定：
//   - CTRL   : Win/Linux Control；macOS Command（" Cmd"）——跨平台 Ctrl/Cmd
//              统一在 InputMap 层做平台映射，业务只见 CTRL。
//   - SHIFT  : Shift
//   - ALT    : Alt / Option
//   - SUPER  : Win 键 / macOS Command——保留与 CTRL 区分以携带原始平台信息，
//              便于后续 ".keymap.toml" 在 macOS 上把 CTRL 重映射为 SUPER。
//
// 位掩码语义：NONE = 0；CTRL/SHIFT/ALT/SUPER 各占一位；组合用 a | b。
// 例：Ctrl+Shift+P → CTRL | SHIFT；Ctrl+S → CTRL；普通 Delete → NONE。
// ===========================================================================

enum class ModifierFlags : std::uint32_t {
    NONE  = 0u,
    CTRL  = 1u << 0,
    SHIFT = 1u << 1,
    ALT   = 1u << 2,
    SUPER = 1u << 3,
};

// 位组合（scoped enum，运算结果仍为 ModifierFlags，不退化为 int）。
inline ModifierFlags operator|(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline ModifierFlags operator&(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline ModifierFlags operator^(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(
        static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}
inline ModifierFlags operator~(ModifierFlags a) {
    // 仅保留定义的四位，避免高位污染（掩码后取反也只在四位内）。
    constexpr std::uint32_t MASK = 0u | 1u | 2u | 4u | 8u;
    return static_cast<ModifierFlags>(~static_cast<std::uint32_t>(a) & MASK);
}

// 是否有任意修饰位被置位（mods == NONE 时返回 false）。
inline bool any_modifier(ModifierFlags a) {
    return static_cast<std::uint32_t>(a) != 0u;
}

// ===========================================================================
// Key —— 抽象物理按键。
//
// 数值约定：UNKNOWN=0；其余按"字母 → 数字 → 功能键 → 方向 → 编辑控制 →
// 导航 → 符号"分组连续编号，便于 key_from_glfw 做区间映射。
// 字母 / 数字 / 功能键的枚举值各自组内连续，可用 static_cast 算术批量转换。
// ===========================================================================

enum class Key {
    UNKNOWN = 0,

    // 字母 A-Z（GLFW: 65..90）
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // 数字 0-9（主键盘上排；GLFW: 48..57）
    NUM_0, NUM_1, NUM_2, NUM_3, NUM_4,
    NUM_5, NUM_6, NUM_7, NUM_8, NUM_9,

    // 功能键 F1-F12（GLFW: 290..301）
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,

    // 方向键
    UP,
    DOWN,
    LEFT,
    RIGHT,

    // 编辑 / 控制键
    ESCAPE,
    TAB,
    SPACE,
    ENTER,
    BACKSPACE,
    DELETE,

    // 导航键
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,

    // 符号键（用于 Ctrl+= / Ctrl+- 等缩放快捷键）
    MINUS,    // '-'
    EQUALS,   // '=' / '+'
};

// ===========================================================================
// ShortcutDef —— 一条快捷键绑定。
//
// 字段语义：
//   - key         抽象物理键（Key 枚举；UNKNOWN 视为无效绑定）
//   - mods        修饰键组合（ModifierFlags 位掩码；可 NONE）
//   - command_id  目标命令 id（对齐 CommandRegistry builtin:: 常量；
//                 InputMap 仅以字符串耦合，不查 CommandRegistry）
//   - display     可读显示串（"Ctrl+Shift+P"）；空串表示由 InputMap 在 bind
//                 时调 format(key, mods) 自动生成
//
// 聚合体：可用 ShortcutDef{Key::P, CTRL|SHIFT, "command_palette.open"} 初始化。
// ===========================================================================

struct ShortcutDef {
    Key           key         = Key::UNKNOWN;
    ModifierFlags mods        = ModifierFlags::NONE;
    std::string   command_id;
    std::string   display;
};

}  // namespace eda
