// InputMap / ShortcutDef 单元测试（P4 Task 7）。
//
// 覆盖（约 28 个用例，分 8 组）：
//   1. 预设切换差异（4）：zoom_in 在三套预设各不同 / LCEDA redo 用 Ctrl+Shift+Z /
//      LCEDA delete 用 Backspace / zoom_fit 三套预设各不同。
//   2. 预设覆盖（2）：三套预设都覆盖通用命令（open/save/undo/copy/paste/...） /
//      三套预设都含 redo/delete/zoom 五项核心动作。
//   3. bind/lookup 往返（3）：基础往返 / display 自动生成 / 同 (key,mods,id) 仅刷 display。
//   4. unbind（3）：移除命中 / 未命中返回 0 / 不误伤不同 mods。
//   5. 冲突检测（4）：预设加载后无自冲突 / 用户 bind 制造冲突并定位 / unbind 解冲突 /
//      冲突下 lookup 取首条。
//   6. InputEvent → command 映射（5）：mods 命中 / mods 不匹配未中 / 非 KEY 事件 /
//      RELEASE 忽略 / 未知 key 未中。
//   7. preset_changed 信号（2）：切换触发并携带新 Preset / 构造期不触发。
//   8. GLFW 互转 + format（3）：key_from_glfw 往返 / format 修饰+键名 / preset_name。
//
// 所有权：所有 InputMap 栈对象，Signal 在析构时清理 tracked Object（本测试 connect
// 的是无 Object 归属的 lambda，tracked_objects_ 为空，析构无副作用）。

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/input/input_event.h"
#include "editor/shortcuts/input_map.h"
#include "editor/shortcuts/shortcut_def.h"

using namespace eda;

// 便捷：构造一个 KEY PRESS 事件（GLFW key code 由 InputMap::glfw_from_key 推导）。
static InputEvent key_press(Key k) {
    InputEvent e{};
    e.type   = InputEvent::Type::KEY;
    e.key    = InputMap::glfw_from_key(k);
    e.action = KeyAction::PRESS;
    return e;
}

// ===========================================================================
// 1. 预设切换差异
// ===========================================================================

TEST(ShortcutPresetTest, ZoomInDiffersAcrossAllThreePresets) {
    InputMap m(Preset::KICAD);
    EXPECT_EQ(m.lookup(Key::F1, ModifierFlags::NONE), "view.zoom_in");

    m.set_preset(Preset::ALTIUM);
    EXPECT_EQ(m.lookup(Key::PAGE_UP, ModifierFlags::NONE), "view.zoom_in");
    // KiCad 的 F1 缩放在 Altium 预设中已清空
    EXPECT_FALSE(m.lookup(Key::F1, ModifierFlags::NONE).has_value());

    m.set_preset(Preset::LCEDA);
    EXPECT_EQ(m.lookup(Key::EQUALS, ModifierFlags::CTRL), "view.zoom_in");
    EXPECT_FALSE(m.lookup(Key::F1, ModifierFlags::NONE).has_value());
    EXPECT_FALSE(m.lookup(Key::PAGE_UP, ModifierFlags::NONE).has_value());
}

TEST(ShortcutPresetTest, RedoInLcedaUsesCtrlShiftZ) {
    InputMap m(Preset::KICAD);
    EXPECT_EQ(m.lookup(Key::Y, ModifierFlags::CTRL), "edit.redo");
    EXPECT_FALSE(m.lookup(Key::Z, ModifierFlags::CTRL | ModifierFlags::SHIFT).has_value());

    m.set_preset(Preset::LCEDA);
    EXPECT_EQ(m.lookup(Key::Z, ModifierFlags::CTRL | ModifierFlags::SHIFT), "edit.redo");
    // KiCad/Altium 的 Ctrl+Y redo 在 LCEDA 中已清空
    EXPECT_FALSE(m.lookup(Key::Y, ModifierFlags::CTRL).has_value());
}

TEST(ShortcutPresetTest, DeleteInLcedaUsesBackspace) {
    InputMap m(Preset::LCEDA);
    EXPECT_EQ(m.lookup(Key::BACKSPACE, ModifierFlags::NONE), "edit.delete");
    // KiCad/Altium 的 Del 在 LCEDA 中已清空
    EXPECT_FALSE(m.lookup(Key::DELETE, ModifierFlags::NONE).has_value());

    m.set_preset(Preset::ALTIUM);
    EXPECT_EQ(m.lookup(Key::DELETE, ModifierFlags::NONE), "edit.delete");
    EXPECT_FALSE(m.lookup(Key::BACKSPACE, ModifierFlags::NONE).has_value());
}

TEST(ShortcutPresetTest, ZoomFitDiffersAcrossPresets) {
    InputMap m(Preset::KICAD);
    EXPECT_EQ(m.lookup(Key::HOME, ModifierFlags::NONE), "view.zoom_fit");

    m.set_preset(Preset::ALTIUM);
    EXPECT_EQ(m.lookup(Key::END, ModifierFlags::NONE), "view.zoom_fit");

    m.set_preset(Preset::LCEDA);
    EXPECT_EQ(m.lookup(Key::F, ModifierFlags::CTRL | ModifierFlags::SHIFT), "view.zoom_fit");
}

// ===========================================================================
// 2. 预设覆盖（通用命令 + 核心动作）
// ===========================================================================

TEST(ShortcutPresetCoverageTest, AllPresetsCoverCommonCommands) {
    const Preset presets[] = {Preset::KICAD, Preset::ALTIUM, Preset::LCEDA};
    for (Preset p : presets) {
        SCOPED_TRACE(std::string("preset=") + InputMap::preset_name(p));
        InputMap m(p);
        // 通用命令——三套预设完全一致（行业肌肉记忆）。
        EXPECT_EQ(m.lookup(Key::N, ModifierFlags::CTRL), "file.new");
        EXPECT_EQ(m.lookup(Key::O, ModifierFlags::CTRL), "file.open");
        EXPECT_EQ(m.lookup(Key::S, ModifierFlags::CTRL), "file.save");
        EXPECT_EQ(m.lookup(Key::S, ModifierFlags::CTRL | ModifierFlags::SHIFT), "file.save_as");
        EXPECT_EQ(m.lookup(Key::W, ModifierFlags::CTRL), "file.close");
        EXPECT_EQ(m.lookup(Key::Z, ModifierFlags::CTRL), "edit.undo");
        EXPECT_EQ(m.lookup(Key::C, ModifierFlags::CTRL), "edit.copy");
        EXPECT_EQ(m.lookup(Key::X, ModifierFlags::CTRL), "edit.cut");
        EXPECT_EQ(m.lookup(Key::V, ModifierFlags::CTRL), "edit.paste");
        EXPECT_EQ(m.lookup(Key::A, ModifierFlags::CTRL), "edit.select_all");
        EXPECT_EQ(m.lookup(Key::P, ModifierFlags::CTRL | ModifierFlags::SHIFT),
                  "command_palette.open");
    }
}

TEST(ShortcutPresetCoverageTest, AllPresetsHaveRedoDeleteZoom) {
    const Preset presets[] = {Preset::KICAD, Preset::ALTIUM, Preset::LCEDA};
    const std::string required[] = {
        "edit.redo", "edit.delete",
        "view.zoom_in", "view.zoom_out", "view.zoom_fit",
    };
    for (Preset p : presets) {
        SCOPED_TRACE(std::string("preset=") + InputMap::preset_name(p));
        InputMap m(p);
        std::vector<std::string> ids;
        for (const auto& b : m.all_bindings()) ids.push_back(b.command_id);
        for (const auto& need : required) {
            SCOPED_TRACE("need=" + need);
            EXPECT_NE(std::find(ids.begin(), ids.end(), need), ids.end());
        }
    }
}

// ===========================================================================
// 3. bind / lookup 往返
// ===========================================================================

TEST(ShortcutBindTest, BindAndLookupRoundtrip) {
    InputMap m;
    // KICAD 默认未绑定 Ctrl+K → 绑定一条独立命令验证往返。
    EXPECT_FALSE(m.lookup(Key::K, ModifierFlags::CTRL).has_value());
    m.bind(Key::K, ModifierFlags::CTRL, "test.kilo");

    auto hit = m.lookup(Key::K, ModifierFlags::CTRL);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, "test.kilo");
}

TEST(ShortcutBindTest, BindAutoGeneratesDisplay) {
    InputMap m;
    m.bind(Key::P, ModifierFlags::CTRL | ModifierFlags::SHIFT, "x.test");
    const ShortcutDef* b = m.binding(Key::P, ModifierFlags::CTRL | ModifierFlags::SHIFT);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->display, "Ctrl+Shift+P");
}

TEST(ShortcutBindTest, BindSameTripleOnlyUpdatesDisplay) {
    InputMap m;  // KICAD 默认含 Ctrl+S → "file.save"，display="Ctrl+S"
    const ShortcutDef* before = m.binding(Key::S, ModifierFlags::CTRL);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->display, "Ctrl+S");

    std::size_t count_before = m.binding_count();
    m.bind(Key::S, ModifierFlags::CTRL, "file.save", "MySave");
    // 同 (key, mods, command_id) → 仅刷新 display，不追加新条目
    EXPECT_EQ(m.binding_count(), count_before);
    const ShortcutDef* after = m.binding(Key::S, ModifierFlags::CTRL);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->display, "MySave");
}

// ===========================================================================
// 4. unbind
// ===========================================================================

TEST(ShortcutUnbindTest, UnbindRemovesBinding) {
    InputMap m;
    ASSERT_TRUE(m.lookup(Key::S, ModifierFlags::CTRL).has_value());

    std::size_t removed = m.unbind(Key::S, ModifierFlags::CTRL);
    EXPECT_GE(removed, 1u);
    EXPECT_FALSE(m.lookup(Key::S, ModifierFlags::CTRL).has_value());
}

TEST(ShortcutUnbindTest, UnbindMissingReturnsZero) {
    InputMap m;
    EXPECT_EQ(m.unbind(Key::F12, ModifierFlags::ALT), 0u);
}

TEST(ShortcutUnbindTest, UnbindDoesNotTouchDifferentMods) {
    InputMap m;  // KICAD 同时含 Ctrl+S (file.save) 与 Ctrl+Shift+S (file.save_as)
    m.unbind(Key::S, ModifierFlags::CTRL);
    EXPECT_FALSE(m.lookup(Key::S, ModifierFlags::CTRL).has_value());
    EXPECT_TRUE(m.lookup(Key::S, ModifierFlags::CTRL | ModifierFlags::SHIFT).has_value());
}

// ===========================================================================
// 5. 冲突检测
// ===========================================================================

TEST(ShortcutConflictTest, FreshPresetHasNoConflicts) {
    for (Preset p : {Preset::KICAD, Preset::ALTIUM, Preset::LCEDA}) {
        SCOPED_TRACE(std::string("preset=") + InputMap::preset_name(p));
        InputMap m(p);
        EXPECT_TRUE(m.conflicts().empty());
    }
}

TEST(ShortcutConflictTest, BindConflictIsDetected) {
    InputMap m;  // KICAD 已有 Ctrl+S → "file.save"
    m.bind(Key::S, ModifierFlags::CTRL, "file.export");

    auto conflicts = m.conflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].key, Key::S);
    EXPECT_EQ(conflicts[0].mods, ModifierFlags::CTRL);
    ASSERT_EQ(conflicts[0].command_ids.size(), 2u);
    // 插入序：预设的 "file.save" 在前，后绑的 "file.export" 在后
    EXPECT_EQ(conflicts[0].command_ids[0], "file.save");
    EXPECT_EQ(conflicts[0].command_ids[1], "file.export");
}

TEST(ShortcutConflictTest, UnbindResolvesConflict) {
    InputMap m;
    m.bind(Key::S, ModifierFlags::CTRL, "file.export");
    ASSERT_FALSE(m.conflicts().empty());

    // unbind 移除 Ctrl+S 上的全部条目（含冲突双方），再单独绑回 "file.export"
    m.unbind(Key::S, ModifierFlags::CTRL);
    m.bind(Key::S, ModifierFlags::CTRL, "file.export");

    EXPECT_TRUE(m.conflicts().empty());
    EXPECT_EQ(m.lookup(Key::S, ModifierFlags::CTRL), "file.export");
}

TEST(ShortcutConflictTest, LookupReturnsFirstOnConflict) {
    InputMap m;
    m.bind(Key::S, ModifierFlags::CTRL, "file.export");
    // 预设的 "file.save" 插入在前 → lookup 返回 "file.save"（不抛、不取最后）
    EXPECT_EQ(m.lookup(Key::S, ModifierFlags::CTRL), "file.save");
}

// ===========================================================================
// 6. InputEvent → command 映射
// ===========================================================================

TEST(ShortcutInputEventTest, LookupFromInputEventUsesCurrentMods) {
    InputMap m;
    m.set_current_mods(ModifierFlags::CTRL);

    // Ctrl+O → "file.open"
    auto hit = m.lookup(key_press(Key::O));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, "file.open");
}

TEST(ShortcutInputEventTest, ModsMismatchMisses) {
    InputMap m;
    m.set_current_mods(ModifierFlags::NONE);  // 未按 Ctrl

    // 单 'O' 未绑定 → 未中
    EXPECT_FALSE(m.lookup(key_press(Key::O)).has_value());
}

TEST(ShortcutInputEventTest, NonKeyEventReturnsNullopt) {
    InputMap m;
    InputEvent e{};
    e.type = InputEvent::Type::MOUSE_MOVE;
    EXPECT_FALSE(m.lookup(e).has_value());
}

TEST(ShortcutInputEventTest, ReleaseIgnored) {
    InputMap m;
    m.set_current_mods(ModifierFlags::CTRL);
    InputEvent e = key_press(Key::O);
    e.action = KeyAction::RELEASE;  // 释放事件不应触发快捷键
    EXPECT_FALSE(m.lookup(e).has_value());
}

TEST(ShortcutInputEventTest, UnknownKeyReturnsNullopt) {
    InputMap m;
    m.set_current_mods(ModifierFlags::CTRL);
    InputEvent e{};
    e.type   = InputEvent::Type::KEY;
    e.key    = 0xFFFF;  // 未映射的 GLFW key
    e.action = KeyAction::PRESS;
    EXPECT_FALSE(m.lookup(e).has_value());
}

// ===========================================================================
// 7. preset_changed 信号
// ===========================================================================

TEST(ShortcutSignalTest, PresetChangedFiresOnSwitch) {
    InputMap m(Preset::KICAD);
    int     count    = 0;
    Preset  captured = Preset::KICAD;
    m.preset_changed().connect([&](Preset p) { captured = p; ++count; });

    m.set_preset(Preset::ALTIUM);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(captured, Preset::ALTIUM);

    m.set_preset(Preset::LCEDA);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(captured, Preset::LCEDA);
}

TEST(ShortcutSignalTest, ConstructorDoesNotFireSignal) {
    int count = 0;
    InputMap m(Preset::KICAD);
    m.preset_changed().connect([&](Preset) { ++count; });
    // 构造期不 emit；connect 后未调 set_preset → 0 次
    EXPECT_EQ(count, 0);
}

// ===========================================================================
// 8. GLFW 互转 + format + preset_name
// ===========================================================================

TEST(ShortcutGlfwTest, KeyFromGlfwRoundtrips) {
    // 字母 / 数字 / 功能键用区间映射
    EXPECT_EQ(InputMap::key_from_glfw(65),  Key::A);       // GLFW_KEY_A
    EXPECT_EQ(InputMap::key_from_glfw(79),  Key::O);       // GLFW_KEY_O
    EXPECT_EQ(InputMap::key_from_glfw(90),  Key::Z);       // GLFW_KEY_Z
    EXPECT_EQ(InputMap::key_from_glfw(48),  Key::NUM_0);
    EXPECT_EQ(InputMap::key_from_glfw(57),  Key::NUM_9);
    EXPECT_EQ(InputMap::key_from_glfw(290), Key::F1);
    EXPECT_EQ(InputMap::key_from_glfw(301), Key::F12);

    // 特殊键
    EXPECT_EQ(InputMap::key_from_glfw(256), Key::ESCAPE);
    EXPECT_EQ(InputMap::key_from_glfw(261), Key::DELETE);
    EXPECT_EQ(InputMap::key_from_glfw(259), Key::BACKSPACE);
    EXPECT_EQ(InputMap::key_from_glfw(45),  Key::MINUS);
    EXPECT_EQ(InputMap::key_from_glfw(61),  Key::EQUALS);

    // 互转往返
    EXPECT_EQ(InputMap::glfw_from_key(InputMap::key_from_glfw(79)), 79);
    EXPECT_EQ(InputMap::key_from_glfw(InputMap::glfw_from_key(Key::F5)), Key::F5);

    // 未映射
    EXPECT_EQ(InputMap::key_from_glfw(0xFFFF), Key::UNKNOWN);
    EXPECT_EQ(InputMap::glfw_from_key(Key::UNKNOWN), 0);
}

TEST(ShortcutFormatTest, FormatProducesHumanReadable) {
    EXPECT_EQ(InputMap::format(Key::S, ModifierFlags::CTRL), "Ctrl+S");
    EXPECT_EQ(InputMap::format(Key::P, ModifierFlags::CTRL | ModifierFlags::SHIFT),
              "Ctrl+Shift+P");
    EXPECT_EQ(InputMap::format(Key::DELETE, ModifierFlags::NONE), "Del");
    EXPECT_EQ(InputMap::format(Key::F1, ModifierFlags::NONE), "F1");
    EXPECT_EQ(InputMap::format(Key::EQUALS, ModifierFlags::CTRL), "Ctrl+=");
    EXPECT_EQ(InputMap::format(Key::NUM_5, ModifierFlags::NONE), "5");
}

TEST(ShortcutPresetNameTest, NamesAreStable) {
    EXPECT_STREQ(InputMap::preset_name(Preset::KICAD),  "KiCad");
    EXPECT_STREQ(InputMap::preset_name(Preset::ALTIUM), "Altium");
    EXPECT_STREQ(InputMap::preset_name(Preset::LCEDA),  "LCEDA");
}
