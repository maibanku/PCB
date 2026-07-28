// .pot 模板抽取工具（P4 Task 8 / 04-08-01，CI 工具占位）。
//
// 目标：CI 中扫描源码所有 `tr()` / `tr_n()` / `tr_ctxt()` 调用，抽取 source 字符串
// 生成 / 更新 `.pot` 模板，再与各 locale `.po` 比对计算覆盖率（[[04-08-01]] §"抽取工具"）。
//
// 本实现先落地"给定 source 列表 → 输出 .pot 文本"——CI 扫描源码部分（xgettext 风格）
// 待 Task 8 后续 / CI 阶段接入（可用 xgettext / 自写 clang 工具）。
//
// 输出格式遵循 GNU gettext .pot 规范：
//   - 头部 metadata（Project-Id / MIME / Content-Type UTF-8 等）
//   - 每条目：#. 注释 / #: 引用 / msgctxt / msgid / msgid_plural / msgstr

#pragma once

#include <string>
#include <vector>

namespace eda {

class PoExtractor {
public:
    // 单条抽取记录（对应源码中一处 tr/tr_n/tr_ctxt 调用）。
    struct SourceEntry {
        std::string source;            // 英文 source（必填，msgid）
        std::string context;           // tr_ctxt 时填，否则空（msgctxt）
        std::string plural;            // tr_n 时填复数源串，否则空（msgid_plural）
        std::string comment;           // 可选：开发者注释（#.）
        std::string reference;         // 可选：代码位置（#: file.cpp:42）
    };

    // 给定 source 列表，生成 .pot 模板文本（含 PO 头）。
    // 多条同 (context, source) 的条目会合并引用（去重保序）。
    static std::string generate(const std::vector<SourceEntry>& entries);

    // 便捷：纯 source 列表（无 context/plural/comment/reference），生成 .pot 文本。
    static std::string generate_simple(const std::vector<std::string>& sources);
};

}  // namespace eda
