// editor/shortcuts/input_map.cpp
//
// InputMap 实现：预设加载 / bind-unbind-lookup / 冲突检测 / GLFW 互转 / 显示格式化。
//
// 关键约定：
//   - GLFW key code 数值直接作 int 字面量（对齐 GLFW 3.3+ 常量值；不引 GLFW 头，
//     保持 editor 模块零第三方依赖）。字母 / 数字 / 功能键用区间算术批量映射。
//   - load_preset_ 灌入的三套预设，command_id 字符串与 CommandRegistry builtin::
//     常量对齐（"file.open" / "edit.undo" / "view.zoom_in" ...）；不查 CommandRegistry，
//     仅字符串耦合。
//   - 冲突检测：bindings_ 用 vector 容许同 (key, mods) 多 command_id；conflicts()
//     按 (key, mods) 分组，返回 command_ids.size() >= 2 的组。

#include "editor/shortcuts/input_map.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace eda {

// ---------------------------------------------------------------------------
// GLFW key code 常量（仅用到的子集；数值取自 GLFW 3.3 glfw3.h）。
//   字母 65..90、数字 48..57、F1..F12 290..301 用区间映射，不在此枚举。
// ---------------------------------------------------------------------------

namespace {

// NOLINTBEGIN(readability-magic-numbers)  -- GLFW key codes are stable public contract.

constexpr int GLFW_KEY_SPACE      = 32;
constexpr int GLFW_KEY_APOSTROPHE = 39;  // 未使用，留作后续
constexpr int GLFW_KEY_MINUS      = 45;  // '-'
constexpr int GLFW_KEY_EQUAL      = 61;  // '=' / '+'
constexpr int GLFW_KEY_0          = 48;
constexpr int GLFW_KEY_9          = 57;
constexpr int GLFW_KEY_A          = 65;
constexpr int GLFW_KEY_Z          = 90;
constexpr int GLFW_KEY_F1         = 290;
constexpr int GLFW_KEY_F12        = 301;
constexpr int GLFW_KEY_ESCAPE     = 256;
constexpr int GLFW_KEY_ENTER      = 257;
constexpr int GLFW_KEY_TAB        = 258;
constexpr int GLFW_KEY_BACKSPACE  = 259;
constexpr int GLFW_KEY_DELETE     = 261;
constexpr int GLFW_KEY_HOME       = 268;
constexpr int GLFW_KEY_END        = 269;
constexpr int GLFW_KEY_PAGE_UP    = 266;
constexpr int GLFW_KEY_PAGE_DOWN  = 267;
constexpr int GLFW_KEY_UP         = 265;
constexpr int GLFW_KEY_DOWN       = 264;
constexpr int GLFW_KEY_LEFT       = 263;
constexpr int GLFW_KEY_RIGHT      = 262;

// NOLINTEND(readability-magic-numbers)

// 把 ModifierFlags 显示为 "Ctrl+Shift+Alt+Super" 子串（按固定位序）。
std::string mods_prefix_(ModifierFlags m) {
    std::string s;
    if (any_modifier(m & ModifierFlags::CTRL))  s += "Ctrl+";
    if (any_modifier(m & ModifierFlags::SHIFT)) s += "Shift+";
    if (any_modifier(m & ModifierFlags::ALT))   s += "Alt+";
    if (any_modifier(m & ModifierFlags::SUPER)) s += "Super+";
    return s;
}

// Key → 可读名（无修饰前缀）。字母显示为大写单字符；符号键显示其符号。
std::string key_name_(Key k) {
    auto base_A   = static_cast<int>(Key::A);
    auto base_0   = static_cast<int>(Key::NUM_0);
    auto base_F1  = static_cast<int>(Key::F1);
    auto ki = static_cast<int>(k);

    if (ki >= base_A && ki < base_A + 26) {
        return std::string(1, static_cast<char>('A' + (ki - base_A)));
    }
    if (ki >= base_0 && ki < base_0 + 10) {
        return std::string(1, static_cast<char>('0' + (ki - base_0)));
    }
    if (ki >= base_F1 && ki < base_F1 + 12) {
        return "F" + std::to_string(ki - base_F1 + 1);
    }
    switch (k) {
        case Key::ESCAPE:    return "Esc";
        case Key::TAB:       return "Tab";
        case Key::SPACE:     return "Space";
        case Key::ENTER:     return "Enter";
        case Key::BACKSPACE: return "Backspace";
        case Key::DELETE:    return "Del";
        case Key::HOME:      return "Home";
        case Key::END:       return "End";
        case Key::PAGE_UP:   return "PageUp";
        case Key::PAGE_DOWN: return "PageDown";
        case Key::UP:        return "Up";
        case Key::DOWN:      return "Down";
        case Key::LEFT:      return "Left";
        case Key::RIGHT:     return "Right";
        case Key::MINUS:     return "-";
        case Key::EQUALS:    return "=";
        case Key::UNKNOWN:   return "?";
    }
    return "?";
}

}  // namespace

// ===========================================================================
// 构造 / 析构
// ===========================================================================

InputMap::InputMap() : InputMap(Preset::KICAD) {}

InputMap::InputMap(Preset initial) : preset_(initial) {
    load_preset_(initial);
    // 构造期不 emit preset_changed：监听器尚未 connect，发也无意义；且 Signal 在
    // 对象构造未完成时 emit 存在迭代风险。
}

InputMap::~InputMap() = default;

// ===========================================================================
// 预设管理
// ===========================================================================

bool InputMap::set_preset(Preset p) {
    bool changed = (p != preset_);
    preset_ = p;
    load_preset_(p);
    preset_changed_.emit(p);
    return changed;
}

const char* InputMap::preset_name(Preset p) {
    switch (p) {
        case Preset::KICAD:  return "KiCad";
        case Preset::ALTIUM: return "Altium";
        case Preset::LCEDA:  return "LCEDA";
    }
    return "?";
}

// ===========================================================================
// 绑定 CRUD
// ===========================================================================

void InputMap::bind(Key key, ModifierFlags mods,
                    std::string command_id, std::string display) {
    // 同 (key, mods, command_id) 已存在 → 仅刷新 display，不重复追加。
    for (auto& b : bindings_) {
        if (b.key == key && b.mods == mods && b.command_id == command_id) {
            b.display = display.empty() ? format(key, mods) : std::move(display);
            return;
        }
    }
    ShortcutDef def;
    def.key        = key;
    def.mods       = mods;
    def.command_id = std::move(command_id);
    def.display    = display.empty() ? format(key, mods) : std::move(display);
    bindings_.push_back(std::move(def));
}

std::size_t InputMap::unbind(Key key, ModifierFlags mods) {
    auto before = bindings_.size();
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [&](const ShortcutDef& b) {
                           return b.key == key && b.mods == mods;
                       }),
        bindings_.end());
    return before - bindings_.size();
}

// ===========================================================================
// 查询
// ===========================================================================

std::optional<std::string> InputMap::lookup(Key key, ModifierFlags mods) const {
    const ShortcutDef* hit = binding(key, mods);
    if (!hit) return std::nullopt;
    return hit->command_id;
}

std::optional<std::string> InputMap::lookup(const InputEvent& e) const {
    if (e.type != InputEvent::Type::KEY) {
        return std::nullopt;
    }
    // 仅 PRESS / REPEAT 触发快捷键；RELEASE 忽略（避免释放瞬间再次派发同一命令）。
    if (e.action != KeyAction::PRESS && e.action != KeyAction::REPEAT) {
        return std::nullopt;
    }
    Key k = key_from_glfw(e.key);
    if (k == Key::UNKNOWN) return std::nullopt;
    return lookup(k, current_mods_);
}

const ShortcutDef* InputMap::binding(Key key, ModifierFlags mods) const {
    for (const auto& b : bindings_) {
        if (b.key == key && b.mods == mods) return &b;
    }
    return nullptr;
}

std::vector<ShortcutDef> InputMap::all_bindings() const {
    return bindings_;  // 拷贝
}

// ===========================================================================
// 冲突检测
// ===========================================================================

std::vector<InputMap::Conflict> InputMap::conflicts() const {
    // 按 (key, mods) 分组，收集首现序 + 命令 id 列表（去重）。
    struct Group {
        Key                       key;
        ModifierFlags             mods;
        std::vector<std::string>  ids;
        bool                      first = true;
    };

    std::vector<Group> groups;
    for (const auto& b : bindings_) {
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const Group& g) {
                                   return g.key == b.key && g.mods == b.mods;
                               });
        if (it == groups.end()) {
            Group g;
            g.key  = b.key;
            g.mods = b.mods;
            g.ids.push_back(b.command_id);
            groups.push_back(std::move(g));
        } else {
            // 同 command_id 不重复入列（重复 bind 已在 bind() 内合并，这里兜底）。
            if (std::find(it->ids.begin(), it->ids.end(), b.command_id) ==
                it->ids.end()) {
                it->ids.push_back(b.command_id);
            }
        }
    }

    std::vector<Conflict> out;
    for (auto& g : groups) {
        if (g.ids.size() >= 2) {
            Conflict c;
            c.key         = g.key;
            c.mods        = g.mods;
            c.command_ids = std::move(g.ids);
            out.push_back(std::move(c));
        }
    }
    return out;
}

// ===========================================================================
// GLFW 互转
// ===========================================================================

Key InputMap::key_from_glfw(int glfw_key) {
    auto base_A  = static_cast<int>(Key::A);
    auto base_0  = static_cast<int>(Key::NUM_0);
    auto base_F1 = static_cast<int>(Key::F1);

    if (glfw_key >= GLFW_KEY_A && glfw_key <= GLFW_KEY_Z) {
        return static_cast<Key>(base_A + (glfw_key - GLFW_KEY_A));
    }
    if (glfw_key >= GLFW_KEY_0 && glfw_key <= GLFW_KEY_9) {
        return static_cast<Key>(base_0 + (glfw_key - GLFW_KEY_0));
    }
    if (glfw_key >= GLFW_KEY_F1 && glfw_key <= GLFW_KEY_F12) {
        return static_cast<Key>(base_F1 + (glfw_key - GLFW_KEY_F1));
    }
    switch (glfw_key) {
        case GLFW_KEY_SPACE:     return Key::SPACE;
        case GLFW_KEY_ESCAPE:    return Key::ESCAPE;
        case GLFW_KEY_ENTER:     return Key::ENTER;
        case GLFW_KEY_TAB:       return Key::TAB;
        case GLFW_KEY_BACKSPACE: return Key::BACKSPACE;
        case GLFW_KEY_DELETE:    return Key::DELETE;
        case GLFW_KEY_HOME:      return Key::HOME;
        case GLFW_KEY_END:       return Key::END;
        case GLFW_KEY_PAGE_UP:   return Key::PAGE_UP;
        case GLFW_KEY_PAGE_DOWN: return Key::PAGE_DOWN;
        case GLFW_KEY_UP:        return Key::UP;
        case GLFW_KEY_DOWN:      return Key::DOWN;
        case GLFW_KEY_LEFT:      return Key::LEFT;
        case GLFW_KEY_RIGHT:     return Key::RIGHT;
        case GLFW_KEY_MINUS:     return Key::MINUS;
        case GLFW_KEY_EQUAL:     return Key::EQUALS;
        default:                 return Key::UNKNOWN;
    }
}

int InputMap::glfw_from_key(Key key) {
    if (key == Key::UNKNOWN) return 0;
    auto base_A  = static_cast<int>(Key::A);
    auto base_0  = static_cast<int>(Key::NUM_0);
    auto base_F1 = static_cast<int>(Key::F1);
    auto ki = static_cast<int>(key);

    if (ki >= base_A && ki < base_A + 26)  return GLFW_KEY_A + (ki - base_A);
    if (ki >= base_0 && ki < base_0 + 10)  return GLFW_KEY_0 + (ki - base_0);
    if (ki >= base_F1 && ki < base_F1 + 12) return GLFW_KEY_F1 + (ki - base_F1);

    switch (key) {
        case Key::SPACE:     return GLFW_KEY_SPACE;
        case Key::ESCAPE:    return GLFW_KEY_ESCAPE;
        case Key::ENTER:     return GLFW_KEY_ENTER;
        case Key::TAB:       return GLFW_KEY_TAB;
        case Key::BACKSPACE: return GLFW_KEY_BACKSPACE;
        case Key::DELETE:    return GLFW_KEY_DELETE;
        case Key::HOME:      return GLFW_KEY_HOME;
        case Key::END:       return GLFW_KEY_END;
        case Key::PAGE_UP:   return GLFW_KEY_PAGE_UP;
        case Key::PAGE_DOWN: return GLFW_KEY_PAGE_DOWN;
        case Key::UP:        return GLFW_KEY_UP;
        case Key::DOWN:      return GLFW_KEY_DOWN;
        case Key::LEFT:      return GLFW_KEY_LEFT;
        case Key::RIGHT:     return GLFW_KEY_RIGHT;
        case Key::MINUS:     return GLFW_KEY_MINUS;
        case Key::EQUALS:    return GLFW_KEY_EQUAL;
        case Key::UNKNOWN:   return 0;
    }
    return 0;
}

// ===========================================================================
// 显示格式化
// ===========================================================================

std::string InputMap::format(Key key, ModifierFlags mods) {
    return mods_prefix_(mods) + key_name_(key);
}

// ===========================================================================
// 三套预设加载（私有）
// ===========================================================================

void InputMap::load_preset_(Preset p) {
    bindings_.clear();

    // 通用绑定（三套预设一致——行业肌肉记忆）：
    //   file.new/open/save/save_as, edit.undo/copy/cut/paste/select_all,
    //   command_palette.open。
    auto add_common = [this](Key k, ModifierFlags m, const char* id) {
        bind(k, m, id);
    };
    add_common(Key::N, ModifierFlags::CTRL,                       "file.new");
    add_common(Key::O, ModifierFlags::CTRL,                       "file.open");
    add_common(Key::S, ModifierFlags::CTRL,                       "file.save");
    add_common(Key::S, ModifierFlags::CTRL | ModifierFlags::SHIFT, "file.save_as");
    add_common(Key::W, ModifierFlags::CTRL,                       "file.close");
    add_common(Key::Z, ModifierFlags::CTRL,                       "edit.undo");
    add_common(Key::C, ModifierFlags::CTRL,                       "edit.copy");
    add_common(Key::X, ModifierFlags::CTRL,                       "edit.cut");
    add_common(Key::V, ModifierFlags::CTRL,                       "edit.paste");
    add_common(Key::A, ModifierFlags::CTRL,                       "edit.select_all");
    add_common(Key::P, ModifierFlags::CTRL | ModifierFlags::SHIFT, "command_palette.open");

    // 预设差异点：redo / delete / zoom_in / zoom_out / zoom_fit。
    // 三个预设在这五项上的键位选择对齐各自 EDA 工具的官方默认键位。
    switch (p) {
        case Preset::KICAD:
            // KiCad：Ctrl+Y 重做；Del 删除；F1/F2 缩放；Home 适配屏幕。
            add_common(Key::Y,          ModifierFlags::CTRL, "edit.redo");
            add_common(Key::DELETE,     ModifierFlags::NONE, "edit.delete");
            add_common(Key::F1,         ModifierFlags::NONE, "view.zoom_in");
            add_common(Key::F2,         ModifierFlags::NONE, "view.zoom_out");
            add_common(Key::HOME,       ModifierFlags::NONE, "view.zoom_fit");
            break;
        case Preset::ALTIUM:
            // Altium Designer：Ctrl+Y 重做；Del 删除；PageUp/Down 缩放；End 适配。
            add_common(Key::Y,          ModifierFlags::CTRL, "edit.redo");
            add_common(Key::DELETE,     ModifierFlags::NONE, "edit.delete");
            add_common(Key::PAGE_UP,    ModifierFlags::NONE, "view.zoom_in");
            add_common(Key::PAGE_DOWN,  ModifierFlags::NONE, "view.zoom_out");
            add_common(Key::END,        ModifierFlags::NONE, "view.zoom_fit");
            break;
        case Preset::LCEDA:
            // 立创EDA（Web 风格）：Ctrl+Shift+Z 重做；Backspace 删除；
            // Ctrl+'='/Ctrl+'-' 缩放；Ctrl+Shift+F 适配。
            add_common(Key::Z,          ModifierFlags::CTRL | ModifierFlags::SHIFT, "edit.redo");
            add_common(Key::BACKSPACE,  ModifierFlags::NONE,                         "edit.delete");
            add_common(Key::EQUALS,     ModifierFlags::CTRL,                         "view.zoom_in");
            add_common(Key::MINUS,      ModifierFlags::CTRL,                         "view.zoom_out");
            add_common(Key::F,          ModifierFlags::CTRL | ModifierFlags::SHIFT, "view.zoom_fit");
            break;
    }
}

}  // namespace eda
