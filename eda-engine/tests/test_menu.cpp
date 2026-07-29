// 菜单系统单元测试（P4 菜单系统：MenuItem / Menu / MenuBar）。
//
// 覆盖（约 14 个用例）：
//   1. MenuBar 默认结构（2）：构造后含 7 个 Menu（File/Edit/View/Place/Design/Tools/Help）；
//      File 菜单含 5 命令项 + 1 separator，Edit/View 各含 2 / 3 命令项。
//   2. Menu open/close（2）：open/close 切换 is_open；重复 open / close 幂等。
//   3. MenuItem activated → command handler（2）：点击 item 触发 registry 中 handler；
//      command_id 缺失时安全跳过（仍 close）。
//   4. disabled 跳过（1）：disabled item.activate() 不 emit、不触发 handler、不 close。
//   5. separator 渲染（2）：separator 模式 _draw 仅画 1px 横线（无文本）；
//      separator 不响应 activate。
//   6. 同一时间只一个 Popup open（2）：open_menu 切换时旧 menu 自动 close；
//      click_menu_title 同义。
//   7. Esc / Alt 助记符（2）：handle_escape 关闭全部；Alt+F 打开 File。
//   8. MenuItem _draw hover 态（1）：hover 时画 highlight 填充背景。
//
// 所有权约定：MenuBar 默认构造建 heap Menu + heap MenuItem（Node 树级联 delete）。
// 自建 stack Menu / MenuItem 的用例遵循"Menu 先声明、MenuItem 后声明"约定——
// 子先析构自删于父的 children_，避免 ~Node 级联 delete 栈对象双释放。
//
// registry 注入：MenuBar / Menu 默认绑进程单例；测试经 set_registry 注入本地
// CommandRegistry，互不污染单例状态。

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "editor/command_palette/command_registry.h"
#include "scene/gui/canvas_item.h"  // RecordingControlCanvas / ControlCanvasScope
#include "scene/gui/widgets/menu.h"
#include "scene/gui/widgets/menu_bar.h"
#include "scene/gui/widgets/menu_item.h"

using namespace eda;

// ===========================================================================
// 1. MenuBar 默认结构
// ===========================================================================

TEST(MenuBarDefaultTest, ContainsSevenStandardMenus) {
    MenuBar bar;
    EXPECT_EQ(bar.get_menu_count(), 7);

    std::vector<std::string> titles;
    for (int i = 0; i < bar.get_menu_count(); ++i) {
        titles.push_back(bar.get_menu(i)->get_title());
    }
    EXPECT_EQ(titles, (std::vector<std::string>{
                          "File", "Edit", "View", "Place", "Design", "Tools", "Help"}));
}

TEST(MenuBarDefaultTest, FileMenuBindsFiveCommandsAndSeparator) {
    MenuBar bar;
    Menu* file_menu = bar.get_menu(0);
    ASSERT_NE(file_menu, nullptr);
    EXPECT_EQ(file_menu->get_title(), "File");

    auto items = file_menu->get_items();
    // 5 命令项 + 1 separator = 6 children
    ASSERT_EQ(items.size(), 6u);
    EXPECT_EQ(items[0]->get_command_id(), builtin::FILE_NEW);
    EXPECT_EQ(items[1]->get_command_id(), builtin::FILE_OPEN);
    EXPECT_EQ(items[2]->get_command_id(), builtin::FILE_SAVE);
    EXPECT_EQ(items[3]->get_command_id(), builtin::FILE_SAVE_AS);
    EXPECT_TRUE(items[4]->is_separator());
    EXPECT_EQ(items[5]->get_command_id(), builtin::FILE_CLOSE);
}

TEST(MenuBarDefaultTest, EditAndViewMenusBindBuiltinCommands) {
    MenuBar bar;
    Menu* edit_menu = bar.get_menu(1);
    ASSERT_NE(edit_menu, nullptr);
    auto edit_items = edit_menu->get_items();
    ASSERT_EQ(edit_items.size(), 2u);
    EXPECT_EQ(edit_items[0]->get_command_id(), builtin::EDIT_UNDO);
    EXPECT_EQ(edit_items[1]->get_command_id(), builtin::EDIT_REDO);

    Menu* view_menu = bar.get_menu(2);
    ASSERT_NE(view_menu, nullptr);
    auto view_items = view_menu->get_items();
    ASSERT_EQ(view_items.size(), 3u);
    EXPECT_EQ(view_items[0]->get_command_id(), builtin::VIEW_ZOOM_IN);
    EXPECT_EQ(view_items[1]->get_command_id(), builtin::VIEW_ZOOM_OUT);
    EXPECT_EQ(view_items[2]->get_command_id(), builtin::VIEW_ZOOM_FIT);
}

// ===========================================================================
// 2. Menu open / close
// ===========================================================================

TEST(MenuOpenCloseTest, OpenThenCloseTogglesIsOpen) {
    Menu menu;
    EXPECT_FALSE(menu.is_open());

    menu.open();
    EXPECT_TRUE(menu.is_open());

    menu.close();
    EXPECT_FALSE(menu.is_open());
}

TEST(MenuOpenCloseTest, OpenAndCloseAreIdempotent) {
    Menu menu;
    menu.open();
    menu.open();  // 重复 open：仍为 open，无副作用
    EXPECT_TRUE(menu.is_open());

    menu.close();
    menu.close();  // 重复 close：仍为 closed
    EXPECT_FALSE(menu.is_open());
}

// ===========================================================================
// 3. MenuItem activated → command handler
// ===========================================================================

TEST(MenuItemActivatedTest, ClickItemInvokesBoundCommandHandler) {
    MenuBar bar;
    // 本地 registry：先 register_builtins 占位，再覆盖 file.open 的 handler。
    CommandRegistry reg;
    reg.register_builtins();
    int fired = 0;
    Command override_cmd;
    override_cmd.id      = builtin::FILE_OPEN;
    override_cmd.name    = "Open File";
    override_cmd.handler = [&fired] { ++fired; };
    reg.register_command(override_cmd);  // 同 id 覆盖占位
    bar.set_registry(&reg);

    Menu* file_menu = bar.get_menu(0);
    file_menu->open();
    ASSERT_TRUE(file_menu->is_open());

    // Open 是 File 菜单第 2 项（index 1）。
    MenuItem* open_item = file_menu->get_items()[1];
    ASSERT_EQ(open_item->get_command_id(), builtin::FILE_OPEN);

    open_item->activate();  // → activated 信号 → Menu 槽查 registry → handler() → close
    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(file_menu->is_open());  // activate 后自动 close
}

TEST(MenuItemActivatedTest, MissingCommandStillClosesSafely) {
    // command_id 在 registry 中缺失：handler 不触发，但仍 close（无崩溃）。
    Menu menu;       // 先声明
    MenuItem item;   // 后声明（约定）
    item.set_command_id("nonexistent.command");
    menu.add_item(&item);
    menu.set_registry(&CommandRegistry::get());  // 单例（未注册该 id）

    menu.open();
    ASSERT_TRUE(menu.is_open());
    item.activate();  // 不触发任何 handler，但 close
    EXPECT_FALSE(menu.is_open());
}

// ===========================================================================
// 4. disabled 跳过
// ===========================================================================

TEST(MenuItemDisabledTest, DisabledActivateIsSkipped) {
    Menu menu;       // 先声明
    MenuItem item;   // 后声明
    item.set_command_id("test.disabled_cmd");

    CommandRegistry reg;
    int fired = 0;
    Command cmd;
    cmd.id      = "test.disabled_cmd";
    cmd.handler = [&fired] { ++fired; };
    reg.register_command(cmd);
    menu.set_registry(&reg);

    menu.add_item(&item);
    item.set_disabled(true);
    menu.open();
    ASSERT_TRUE(menu.is_open());

    item.activate();  // disabled → 跳过：不 emit、不触发 handler、不 close

    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(menu.is_open());  // 仍打开
}

// ===========================================================================
// 5. separator 渲染
// ===========================================================================

TEST(MenuItemSeparatorTest, SeparatorDrawsOnlyHorizontalLine) {
    MenuItem sep;
    sep.set_separator(true);
    sep.set_size({120.0f, 6.0f});
    sep.set_text("should-not-render");  // separator 忽略文本

    RecordingControlCanvas rec;
    ControlCanvasScope     scope(rec);
    sep.queue_redraw();
    sep.draw_if_dirty();

    // 仅 1 个填充矩形（1px 横线），无文本调用。
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_TRUE(rec.rects[0].filled);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.y, 1.0f);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 120.0f);
    EXPECT_EQ(rec.texts.size(), 0u);
}

TEST(MenuItemSeparatorTest, SeparatorActivateIsSkipped) {
    Menu menu;
    MenuItem sep;
    sep.set_separator(true);
    sep.set_command_id("ignored");
    menu.add_item(&sep);
    menu.open();

    sep.activate();  // separator → 跳过
    EXPECT_TRUE(menu.is_open());  // 不 close
}

// ===========================================================================
// 6. 同一时间只一个 Popup open
// ===========================================================================

TEST(MenuBarSingleOpenTest, OpenMenuClosesOthers) {
    MenuBar bar;
    Menu*   file_menu = bar.get_menu(0);
    Menu*   edit_menu = bar.get_menu(1);

    bar.open_menu(file_menu);
    EXPECT_TRUE(file_menu->is_open());
    EXPECT_FALSE(edit_menu->is_open());

    bar.open_menu(edit_menu);  // 切换到 Edit：File 自动 close
    EXPECT_FALSE(file_menu->is_open());
    EXPECT_TRUE(edit_menu->is_open());
}

TEST(MenuBarSingleOpenTest, ClickMenuTitleEquivalentToOpenMenu) {
    MenuBar bar;
    Menu*   file_menu = bar.get_menu(0);
    Menu*   view_menu = bar.get_menu(2);

    EXPECT_TRUE(bar.click_menu_title(file_menu));
    EXPECT_TRUE(file_menu->is_open());

    EXPECT_TRUE(bar.click_menu_title(view_menu));
    EXPECT_TRUE(view_menu->is_open());
    EXPECT_FALSE(file_menu->is_open());  // File 已自动 close

    // nullptr：仅 close_all，返回 false。
    EXPECT_FALSE(bar.click_menu_title(nullptr));
    EXPECT_FALSE(view_menu->is_open());
}

// ===========================================================================
// 7. Esc / Alt 助记符
// ===========================================================================

TEST(MenuBarKeyHandlingTest, EscapeClosesAllPopups) {
    MenuBar bar;
    Menu*   file_menu = bar.get_menu(0);
    bar.open_menu(file_menu);
    ASSERT_TRUE(file_menu->is_open());

    bar.handle_escape();
    EXPECT_FALSE(file_menu->is_open());
}

TEST(MenuBarKeyHandlingTest, AltMnemonicOpensMatchingMenu) {
    MenuBar bar;
    // GLFW 字母键与 ASCII 大写一致：'F' = 70, 'V' = 86。
    EXPECT_TRUE(bar.handle_alt_mnemonic(70));  // Alt+F → File
    EXPECT_TRUE(bar.get_menu(0)->is_open());

    EXPECT_TRUE(bar.handle_alt_mnemonic(86));  // Alt+V → View
    EXPECT_TRUE(bar.get_menu(2)->is_open());
    EXPECT_FALSE(bar.get_menu(0)->is_open());  // File 已 close

    // 小写 ASCII 也兼容。
    EXPECT_TRUE(bar.handle_alt_mnemonic(static_cast<int>('e')));  // Alt+e → Edit
    EXPECT_TRUE(bar.get_menu(1)->is_open());

    // 数字键不匹配字母 → false。
    EXPECT_FALSE(bar.handle_alt_mnemonic(static_cast<int>('9')));
}

// ===========================================================================
// 8. MenuItem _draw hover 态
// ===========================================================================

TEST(MenuItemDrawTest, HoverEmitsHighlightBackground) {
    MenuItem item;
    item.set_text("Cut");
    item.set_size({100.0f, 22.0f});
    item.set_hover(true);

    RecordingControlCanvas rec;
    ControlCanvasScope     scope(rec);
    item.queue_redraw();
    item.draw_if_dirty();

    // hover → 1 个填充背景（highlight）+ 1 个文本调用。
    ASSERT_EQ(rec.rects.size(), 1u);
    EXPECT_TRUE(rec.rects[0].filled);
    EXPECT_FLOAT_EQ(rec.rects[0].rect.size.x, 100.0f);
    ASSERT_EQ(rec.texts.size(), 1u);
    EXPECT_EQ(rec.texts[0].first, "Cut");
}

TEST(MenuItemDrawTest, NonHoverSkipsBackground) {
    MenuItem item;
    item.set_text("Copy");
    item.set_size({100.0f, 22.0f});
    // 默认非 hover

    RecordingControlCanvas rec;
    ControlCanvasScope     scope(rec);
    item.queue_redraw();
    item.draw_if_dirty();

    // 非 hover：不画背景矩形；仅画文本。
    EXPECT_EQ(rec.rects.size(), 0u);
    ASSERT_EQ(rec.texts.size(), 1u);
    EXPECT_EQ(rec.texts[0].first, "Copy");
}
