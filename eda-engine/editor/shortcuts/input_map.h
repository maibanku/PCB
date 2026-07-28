// editor/shortcuts/input_map.h
//
// P4 Task 7 · 快捷键体系（InputMap + 三套预设）。
//
// InputMap 是"物理按键 → 命令 id"的映射中心（对齐 Godot InputMap）：
//
//   - 三套内置预设（KICAD / ALTIUM / LCEDA）覆盖 KiCad / Altium / 立创EDA 用户的
//     常用键位习惯。set_preset 清空 bindings_ 再灌入对应预设，并 emit preset_changed。
//   - bind/unbind 维护 std::vector<ShortcutDef>：同一 (key, mods) 允许多个 command_id
//     共存——冲突由 conflicts() 检测并报告，lookup 取插入序首条命中（稳定可测）。
//   - lookup(InputEvent) 把 InputEvent.key（GLFW key code）经 key_from_glfw 映射为
//     Key 枚举，再叠加 InputMap 持有的 current_mods_（由平台层经 set_current_mods
//     维护，对应 GLFW 的 mods 回调）。这避免修改 InputEvent 结构，保持上游稳定。
//   - 仅字符串耦合 CommandRegistry：command_id 取值与 builtin:: 常量对齐
//     （"file.open" / "edit.undo" / ...），但 InputMap 不依赖 CommandRegistry 类型。
//
// 设计取舍：
//   - bindings_ 用 vector 而非 unordered_map：保留插入序（lookup 首条命中可预测），
//     并原生支持"同 (key, mods) 多 command_id"以暴露冲突（map 强制唯一会吞掉冲突）。
//   - preset_changed 用 eda::Signal<Preset>（codebase 标准 Signal 系统）。Preset 是
//     scoped enum，无法经 Variant 默认装箱——本头在类定义后特化 to_variant /
//     unpack_arg 桥接（特化须在使用点之前可见，故置于本头尾）。

#pragma once

#include "core/input/input_event.h"      // InputEvent（上游 GLFW 风格事件）
#include "core/object/signal.h"          // Signal<Preset>
#include "editor/shortcuts/shortcut_def.h"  // Key / ModifierFlags / ShortcutDef

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace eda {

class InputMap {
public:
    // =========================================================================
    // Preset —— 内置键位预设。
    //
    // KICAD  : KiCad 风格（F1/F2 缩放、Home 适配、Ctrl+Y 重做、Del 删除）
    // ALTIUM : Altium Designer 风格（PageUp/PageDown 缩放、End 适配、Ctrl+Y 重做）
    // LCEDA  : 立创EDA 风格（Ctrl+= / Ctrl+- 缩放、Ctrl+Shift+Z 重做、Backspace 删除）
    //
    // 三套预设对 file.open/save/undo/copy/paste 等通用命令使用相同绑定（行业惯例），
    // 在 redo / delete / zoom 等存在平台习惯差异的命令上区分——既保留肌肉记忆兼容，
    // 又让测试能在这些"差异点"上清晰断言预设切换生效。
    // =========================================================================
    enum class Preset { KICAD, ALTIUM, LCEDA };

    // 冲突记录：同一 (key, mods) 被多个 command_id 占用（command_ids.size() >= 2）。
    struct Conflict {
        Key                       key;
        ModifierFlags             mods;
        std::vector<std::string>  command_ids;
    };

    // 默认构造：加载 KICAD 预设（不 emit 信号——构造期无监听器）。
    InputMap();
    // 指定起始预设构造（测试 / 多实例编辑器各自独立预设）。
    explicit InputMap(Preset initial);
    ~InputMap();

    InputMap(const InputMap&) = delete;
    InputMap& operator=(const InputMap&) = delete;
    // 不可移动：preset_changed_ 是 Signal（按地址持有，移动后旧地址悬空）。
    InputMap(InputMap&&) = delete;
    InputMap& operator=(InputMap&&) = delete;

    // -------------------------------------------------------------------------
    // 预设管理
    // -------------------------------------------------------------------------

    // 切换预设：清空 bindings_ → 灌入对应预设 → emit preset_changed(p)。
    // 总是重灌（即便 p == get_preset()），用于"重置到预设默认值"。返回是否实际切换。
    bool set_preset(Preset p);
    Preset get_preset() const { return preset_; }

    // 预设可读名（"KiCad" / "Altium" / "LCEDA"）；UI 展示 / 调试用。
    static const char* preset_name(Preset p);

    // -------------------------------------------------------------------------
    // 绑定 CRUD
    // -------------------------------------------------------------------------

    // 绑定 (key, mods) → command_id。
    //   - display 留空：bind 时自动调 format(key, mods) 生成（"Ctrl+S" 风格）。
    //   - 同一 (key, mods, command_id) 已存在：仅刷新 display，不重复追加。
    //   - 同一 (key, mods) 但不同 command_id：仍追加（视为冲突，由 conflicts() 报告）。
    void bind(Key key, ModifierFlags mods,
              std::string command_id, std::string display = "");

    // 移除所有命中 (key, mods) 的绑定（含冲突方）；返回移除条数。
    std::size_t unbind(Key key, ModifierFlags mods);

    // -------------------------------------------------------------------------
    // 查询
    // -------------------------------------------------------------------------

    // 命中插入序首条（含冲突场景的首条）；未命中返回 nullopt。
    std::optional<std::string> lookup(Key key, ModifierFlags mods) const;

    // 从 InputEvent 查询：非 KEY 事件返回 nullopt；KEY 事件取
    // key_from_glfw(e.key) + current_mods_。current_mods_ 由平台层经
    // set_current_mods 维护（GLFW key callback 的 mods 位）。
    std::optional<std::string> lookup(const InputEvent& e) const;

    // 取首条命中的完整 ShortcutDef（含 display）；未命中 nullptr。
    const ShortcutDef* binding(Key key, ModifierFlags mods) const;

    // 全部绑定（拷贝；按插入序）。
    std::vector<ShortcutDef> all_bindings() const;

    // 已绑定条数（含冲突方——每条 ShortcutDef 计 1）。
    std::size_t binding_count() const { return bindings_.size(); }

    // -------------------------------------------------------------------------
    // 冲突检测
    // -------------------------------------------------------------------------

    // 返回所有 (key, mods) 上 command_id 数 >= 2 的冲突组（按首次出现序）。
    // 预设加载后应为空（每预设内部无自冲突）；冲突来自用户 bind 覆盖。
    std::vector<Conflict> conflicts() const;

    // -------------------------------------------------------------------------
    // 当前修饰键状态（平台层维护）
    // -------------------------------------------------------------------------

    // 平台层在分发 InputEvent 前调，反映当前按下的修饰键（GLFW mods 位）。
    // lookup(InputEvent) 读此值——InputEvent 本身不携带 mods（保持上游结构稳定）。
    void          set_current_mods(ModifierFlags m) { current_mods_ = m; }
    ModifierFlags get_current_mods() const { return current_mods_; }

    // -------------------------------------------------------------------------
    // 信号
    // -------------------------------------------------------------------------

    // 切换预设时 emit 新 Preset（set_preset 内触发；构造期不触发）。
    Signal<Preset>& preset_changed() { return preset_changed_; }

    // -------------------------------------------------------------------------
    // GLFW 互转 + 显示格式化
    // -------------------------------------------------------------------------

    // GLFW key code → Key 枚举；未覆盖返回 Key::UNKNOWN。
    static Key         key_from_glfw(int glfw_key);
    // Key 枚举 → GLFW key code；UNKNOWN 返回 0。
    static int         glfw_from_key(Key key);

    // 生成可读显示串："Ctrl+Shift+P"、"Del"、"F1"、"Ctrl++"。
    // 修饰序固定 Ctrl → Shift → Alt → Super，键名见 key_name_ 表。
    static std::string format(Key key, ModifierFlags mods);

private:
    // 清空 bindings_ 并灌入预设 p 的内置绑定。
    void load_preset_(Preset p);

    Preset                   preset_;
    std::vector<ShortcutDef> bindings_;
    ModifierFlags            current_mods_ = ModifierFlags::NONE;
    Signal<Preset>           preset_changed_;
};

}  // namespace eda

// ===========================================================================
// Signal<Preset> 与 Variant 的桥接特化。
//
// eda::Signal<Args...> 经 Variant 装箱传递参数；scoped enum Preset 默认无法装箱
// （to_variant / unpack_arg 主模板被 delete）。下面两个特化把 Preset 经 int64_t
// 中转，使 Signal<Preset>::emit / connect(lambda) 可正常工作。
//
// 可见性要求：Signal<Preset> 模板成员在调用点（emit / connect）实例化，本特化须
// 先于那次实例化可见——所以放在本头尾、namespace eda 内，调用方 include 本头即满足。
// ===========================================================================

namespace eda {

inline Variant to_variant(InputMap::Preset p) {
    return Variant(static_cast<std::int64_t>(static_cast<int>(p)));
}

template <>
inline InputMap::Preset unpack_arg<InputMap::Preset>(const Variant& v) {
    return static_cast<InputMap::Preset>(static_cast<int>(v.as_int()));
}

}  // namespace eda
