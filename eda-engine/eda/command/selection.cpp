// eda/command/selection.cpp
//
// P3 命令栈 · Task 8：SelectionSet 非模板辅助。
//
// SelectionSet<T> 与 SelectionChangeCommand<T> 均为模板，主体在 selection.h。
// 此处仅提供 SelectMode 字符串名等非模板工具，便于日志与调试输出，避免在头文件
// 内引入 <ostream>。命令栈 / 选择系统的核心逻辑见头文件模板实现。

#include "eda/command/selection.h"

namespace eda::command {

const char* select_mode_name(SelectMode mode) {
    switch (mode) {
        case SelectMode::REPLACE:
            return "REPLACE";
        case SelectMode::ADD:
            return "ADD";
        case SelectMode::REMOVE:
            return "REMOVE";
        case SelectMode::TOGGLE:
            return "TOGGLE";
    }
    return "UNKNOWN";
}

}  // namespace eda::command
