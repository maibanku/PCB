// .po 资源解析器（P4 Task 8 / 04-08-01）。
//
// 手写最小 .po（gettext PO）解析器——**不依赖 gettext 库**。
//
// 支持的语法子集（覆盖生产 i18n 必需）：
//   - msgctxt "..."            （上下文消歧，可选，单数条目内出现在 msgid 之前）
//   - msgid "..."              （源串，必填；空 msgid "" 视为 PO 头条目，跳过）
//   - msgid_plural "..."       （复数源串，可选）
//   - msgstr "..."             （单数译法）
//   - msgstr[N] "..."          （复数形式 N 译法；N 从 0 起）
//   - #. developer comment     （抽取进 TranslationEntry.comments）
//   - #, flags                 （含 fuzzy 则置 TranslationEntry.fuzzy = true）
//   - #: reference             （代码引用，跳过）
//   - # translator comment     （# 后空格，译者注释，跳过——不进 developer comments）
//   - #~ obsolete / #| previous（前缀行，跳过）
//   - 跨行字符串：msgid "" 后续以 "..." 开头的行追加拼接
//   - 转义：\\n \\t \\r \\" \\\\ \\0
//
// 不在范围内（YAGNI）：
//   - previous-msgctxt / previous-msgid（#| 前缀，跳过）
//   - PO 扩展（msglen 等）
//   - .mo 二进制加载（运行时可由 .po 直接吃，.mo 是优化项）
//
// 容错策略：解析按行扫描，无法识别的行忽略不报错（last_error 仅在严重结构性问题
// 时填，多数情形静默返回部分结果——与 gettext 工具的宽松解析行为一致）。
//
// 调用约定：parse() 接受文本内容（调用方读文件后传入）；load_file() 便捷封装。

#pragma once

#include "scene/gui/i18n/translation.h"

#include <string>

namespace eda {

class PoLoader {
public:
    // 从 .po 文本内容解析为 TranslationTable（不依赖文件系统）。
    // 解析异常（如 IO / 严重语法）通过 last_error() 暴露；本实现按行宽松解析，
    // 通常不报错——无法识别的行忽略。
    static TranslationTable parse(const std::string& content);

    // 从文件路径解析（std::ifstream 读后转发到 parse）。文件打不开返回空表，
    // 并设置 last_error()。
    static TranslationTable load_file(const std::string& path);

    // 最近一次解析的错误描述（空字符串 = 无错误）。
    // 注：单例全局状态——多线程并发解析需各自调 parse 后立即取结果。
    static std::string last_error();
};

}  // namespace eda
