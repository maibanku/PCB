// .pot 模板抽取工具实现（P4 Task 8 / 04-08-01，CI 工具占位）。
//
// 输出格式（GNU gettext .pot）：
//   #. developer comment (可选)
//   #: reference (可选)
//   msgctxt "..." (可选)
//   msgid "source"
//   msgstr ""                  —— 单数条目
// 或：
//   msgid "singular"
//   msgid_plural "plural"
//   msgstr[0] ""
//   msgstr[1] ""

#include "scene/gui/i18n/po_extractor.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace eda {

namespace {

// PO 字符串转义（与 PoLoader::unescape_po 互逆）：
//   \n -> \\n / \t -> \\t / \r -> \\r / \\ -> \\\\ / " -> \\"
std::string escape_po(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

// 合并同 (context, source) 条目：保留首次出现的 plural/comment，引用拼接到一行。
// 与 .pot 惯例一致——同 source 多处引用合并多条 "#: ..." 行，避免译者在多条重复
// 条目间来回切换。
struct Merged {
    std::string context;
    std::string source;
    std::string plural;
    std::string comment;
    std::vector<std::string> references;
};

std::vector<Merged> merge_entries(const std::vector<PoExtractor::SourceEntry>& entries) {
    std::vector<Merged> out;
    // key = context + "\x01" + source（与 TranslationTable::make_key 同构）
    std::unordered_map<std::string, std::size_t> idx;
    for (const auto& e : entries) {
        std::string key = e.context + "\x01" + e.source;
        auto it = idx.find(key);
        if (it == idx.end()) {
            Merged m;
            m.context = e.context;
            m.source  = e.source;
            m.plural  = e.plural;
            m.comment = e.comment;
            if (!e.reference.empty()) m.references.push_back(e.reference);
            idx[key] = out.size();
            out.push_back(std::move(m));
        } else {
            Merged& m = out[it->second];
            // plural/comment 若原空则补（首条目可能没填）
            if (m.plural.empty() && !e.plural.empty())  m.plural  = e.plural;
            if (m.comment.empty() && !e.comment.empty()) m.comment = e.comment;
            if (!e.reference.empty()) {
                // 引用去重（同 reference 不重复列）
                bool dup = false;
                for (const auto& r : m.references) {
                    if (r == e.reference) { dup = true; break; }
                }
                if (!dup) m.references.push_back(e.reference);
            }
        }
    }
    return out;
}

}  // namespace

std::string PoExtractor::generate(const std::vector<SourceEntry>& entries) {
    std::ostringstream out;

    // —— PO 头（metadata）——
    // msgid "" / msgstr "" 头部，遵循 GNU gettext 约定。POT-Creation-Date 留空
    // 由 CI 注入（避免本地时区污染模板）；charset 固定 UTF-8。
    out << "# EDA Engine translations template.\n"
        << "# This file is distributed under the same license as the eda-engine package.\n"
        << "msgid \"\"\n"
        << "msgstr \"\"\n"
        << "\"Project-Id-Version: eda-engine 1.0\\n\"\n"
        << "\"Report-Msgid-Bugs-To: \\n\"\n"
        << "\"POT-Creation-Date: \\n\"\n"
        << "\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\n\"\n"
        << "\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\n\"\n"
        << "\"Language-Team: LANGUAGE <LL@li.org>\\n\"\n"
        << "\"Language: \\n\"\n"
        << "\"MIME-Version: 1.0\\n\"\n"
        << "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
        << "\"Content-Transfer-Encoding: 8bit\\n\"\n"
        << "\"Plural-Forms: nplurals=2; plural=(n != 1);\\n\"\n";

    const auto merged = merge_entries(entries);

    for (const auto& m : merged) {
        out << "\n";
        // #. developer comment（仅在有注释时输出）
        if (!m.comment.empty()) {
            out << "#. " << m.comment << "\n";
        }
        // #: references（每条引用一行，便于译者点击跳转）
        for (const auto& r : m.references) {
            out << "#: " << r << "\n";
        }
        // msgctxt（仅 tr_ctxt 来源的条目输出）
        if (!m.context.empty()) {
            out << "msgctxt \"" << escape_po(m.context) << "\"\n";
        }
        // msgid
        out << "msgid \"" << escape_po(m.source) << "\"\n";
        // msgid_plural + msgstr[0..1]（仅 tr_n 来源的条目输出复数行）
        if (!m.plural.empty()) {
            out << "msgid_plural \"" << escape_po(m.plural) << "\"\n"
                << "msgstr[0] \"\"\n"
                << "msgstr[1] \"\"\n";
        } else {
            out << "msgstr \"\"\n";
        }
    }

    return out.str();
}

std::string PoExtractor::generate_simple(const std::vector<std::string>& sources) {
    std::vector<SourceEntry> entries;
    entries.reserve(sources.size());
    for (const auto& s : sources) {
        SourceEntry e;
        e.source = s;
        entries.push_back(std::move(e));
    }
    return generate(entries);
}

}  // namespace eda
