// .po 解析器实现（P4 Task 8 / 04-08-01）。
//
// 状态机：按行扫描 .po 文本，每个空行分隔一个条目。条目内根据关键字切换
// "当前累积目标"（CTXT/ID/ID_PLURAL/STR_*），后续以 " 开头的续行追加到对应缓冲。
//
// 关键字识别顺序：msgid_plural 必须先于 msgid 检查（前者是后者的前缀）。
// 续行只在 target != NONE 时生效（避免文件起始的孤立续行污染状态）。

#include "scene/gui/i18n/po_loader.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eda {

namespace {

// ===========================================================================
// 字符串工具
// ===========================================================================

// 按行拆分（保留空行；CRLF 容错——后续 trim \r）。
std::vector<std::string> split_lines(const std::string& content) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= content.size()) {
        std::size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(content.substr(pos));
            break;
        }
        lines.push_back(content.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

// 去除行首行尾空白（仅空格/制表/回车）。
std::string trim_line(const std::string& line) {
    std::size_t a = 0;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) {
        ++a;
    }
    std::size_t b = line.size();
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) {
        --b;
    }
    return line.substr(a, b - a);
}

// 提取首个 ".." 之间的内容（不含引号）。未找到成对引号返回空串。
// 形如 `msgstr "hello"` 传入 "msgstr " 后 -> 返回 "hello"。
std::string extract_quoted(const std::string& s) {
    std::size_t a = s.find('"');
    if (a == std::string::npos) return std::string{};
    // 从末尾找最后一个 "（容忍内容中的转义引号 \" —— 最后一个 " 必是闭引号）
    std::size_t b = s.rfind('"');
    if (b == std::string::npos || b <= a) return std::string{};
    return s.substr(a + 1, b - a - 1);
}

// PO 转义反转义：\\n -> \n / \\t -> \t / \\r -> \r / \\\\-/> \\ / \\" -> " / \\0 -> \0。
// 未知转义（如 \\x）保留反斜杠 + 原字符（与 gettext msgfmt 宽松行为一致）。
std::string unescape_po(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[i + 1];
            switch (c) {
                case 'n':  out.push_back('\n'); break;
                case 't':  out.push_back('\t'); break;
                case 'r':  out.push_back('\r'); break;
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '0':  out.push_back('\0'); break;
                default:
                    // 未知转义：保留 "\X" 不变（保守，避免静默丢字符）
                    out.push_back('\\');
                    out.push_back(c);
                    break;
            }
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool starts_with(const std::string& line, std::size_t pos, const char* kw) {
    std::size_t klen = std::strlen(kw);
    if (pos + klen > line.size()) return false;
    return line.compare(pos, klen, kw) == 0;
}

}  // namespace

// ===========================================================================
// PoLoader::parse —— 状态机
// ===========================================================================
//
// 单条目累积缓冲：
//   ctx / id / id_plural —— 三个字符串缓冲，target 标记续行写入哪个
//   strs / str_n          —— 复数形式向量 + 当前写入下标
//   comments              —— #. 注释列表
//   fuzzy                 —— #, fuzzy 标记
//   has_plural            —— 见过 msgid_plural
//   has_id                —— 见过 msgid（即便 id 为空也置位，用于区分"未开始"与"header"）

TranslationTable PoLoader::parse(const std::string& content) {
    TranslationTable table;

    enum class Target { NONE, CTXT, ID, ID_PLURAL, STR };

    // 单条目状态
    std::string ctx;
    std::string id;
    // id_plural 仅在解析时累积 msgid_plural 续行内容——条目内不存储（source 作 key），
    // 但必须消费续行以避免污染下一关键字。声明 maybe_unused 避免 set-but-not-used 警告。
    [[maybe_unused]] std::string id_plural;
    std::vector<std::string> strs;
    std::vector<std::string> comments;
    Target target = Target::NONE;
    std::size_t str_n = 0;
    bool fuzzy = false;
    bool has_plural = false;
    bool has_id = false;

    auto flush_entry = [&]() {
        // 仅当见过 msgid 且 id 非空（跳过 PO header：msgid ""）时入表。
        // id_plural 暂未单独校验——gettext 容许 msgid_plural 缺失 msgstr[N]，
        // 此种条目 plurals 为空，tr_n 会自然退回源串。
        if (has_id && !id.empty()) {
            TranslationEntry e;
            e.context    = ctx;
            e.source     = id;
            e.fuzzy      = fuzzy;
            e.comments   = comments;
            if (has_plural) {
                e.plurals    = strs;
                // 复数条目 translation 留空（与 gettext 一致）
            } else if (!strs.empty()) {
                e.translation = strs[0];
            }
            table.add(std::move(e));
        }
        // 重置——准备下一条目
        ctx.clear();
        id.clear();
        id_plural.clear();
        strs.clear();
        comments.clear();
        target     = Target::NONE;
        str_n      = 0;
        fuzzy      = false;
        has_plural = false;
        has_id     = false;
    };

    const auto lines = split_lines(content);
    for (const auto& raw_line : lines) {
        std::string line = trim_line(raw_line);

        // 空行 = 条目分隔符：flush 当前条目（即便未完整也保留已读部分）
        if (line.empty()) {
            flush_entry();
            continue;
        }

        // 续行：以 " 开头（且前面无关键字时 target==NONE，忽略孤立续行）
        if (line[0] == '"') {
            const std::string frag = unescape_po(extract_quoted(line));
            switch (target) {
                case Target::CTXT:      ctx += frag; break;
                case Target::ID:        id += frag; break;
                case Target::ID_PLURAL: id_plural += frag; break;
                case Target::STR:
                    if (str_n < strs.size()) strs[str_n] += frag;
                    break;
                case Target::NONE:      break;  // 孤立续行，忽略
            }
            continue;
        }

        // 注释行：# 前缀
        if (line[0] == '#') {
            // 至少 2 字符时取第二个字符作分支；单 "#" 视作译者注释跳过
            char tag = (line.size() >= 2) ? line[1] : ' ';
            switch (tag) {
                case '.':
                    // #. developer comment —— 抽取进 TranslationEntry.comments
                    // 内容从 "#." 之后开始（跳过可能的空格）
                    if (line.size() > 2) {
                        std::size_t c = 2;
                        if (c < line.size() && line[c] == ' ') ++c;
                        comments.push_back(line.substr(c));
                    } else {
                        comments.push_back(std::string{});
                    }
                    break;
                case ',':
                    // #, flags —— 含 "fuzzy" 则置位
                    if (line.find("fuzzy", 2) != std::string::npos) {
                        fuzzy = true;
                    }
                    break;
                case ':':
                    // #: reference —— 跳过
                    break;
                case '~':
                    // #~ obsolete —— 跳过（整个条目在 gettext 工具中本就忽略）
                    break;
                case '|':
                    // #| previous-msgctxt/msgid —— 跳过
                    break;
                default:
                    // # translator comment 或空 "#" —— 跳过（不混入 developer comments）
                    break;
            }
            continue;
        }

        // 关键字行：msgctxt / msgid_plural / msgid / msgstr / msgstr[N]
        // 顺序：msgid_plural 必须先于 msgid 检查（后者是前者前缀）。

        std::size_t pos = 0;
        if (starts_with(line, pos, "msgctxt")) {
            target = Target::CTXT;
            ctx    = unescape_po(extract_quoted(line.substr(pos + 7)));
        } else if (starts_with(line, pos, "msgid_plural")) {
            target     = Target::ID_PLURAL;
            has_plural = true;
            id_plural  = unescape_po(extract_quoted(line.substr(pos + 12)));
        } else if (starts_with(line, pos, "msgid")) {
            target = Target::ID;
            has_id = true;
            id     = unescape_po(extract_quoted(line.substr(pos + 5)));
        } else if (starts_with(line, pos, "msgstr")) {
            // msgstr 或 msgstr[N]
            std::size_t p = pos + 6;
            if (p < line.size() && line[p] == '[') {
                std::size_t close = line.find(']', p);
                if (close != std::string::npos) {
                    // 解析 [N]——手写整数解析（引擎核心禁用异常约定；数字无效则取 0）
                    int idx = 0;
                    bool ok = false;
                    for (std::size_t i = p + 1; i < close; ++i) {
                        if (line[i] < '0' || line[i] > '9') {
                            ok = false;
                            break;
                        }
                        idx = idx * 10 + (line[i] - '0');
                        ok = true;
                    }
                    if (!ok) idx = 0;
                    target = Target::STR;
                    str_n  = static_cast<std::size_t>(idx);
                    if (str_n >= strs.size()) strs.resize(str_n + 1);
                    strs[str_n] = unescape_po(extract_quoted(line.substr(close + 1)));
                }
            } else {
                target = Target::STR;
                str_n  = 0;
                if (strs.empty()) strs.resize(1);
                strs[0] = unescape_po(extract_quoted(line.substr(p)));
            }
        }
        // 其他无法识别的行：忽略（宽松解析，与 gettext 行为一致）
    }

    // 文件末尾无尾随空行时，flush 最后一个条目
    flush_entry();
    return table;
}

// ===========================================================================
// PoLoader::load_file
// ===========================================================================

namespace {

// 进程级单例错误存储（函数内 static，线程安全初始化）。
// load_file 写入、last_error 读取——同一存储，避免每个函数各自 static 互不可见。
std::string& last_error_storage() {
    static std::string err;
    return err;
}

}  // namespace

TranslationTable PoLoader::load_file(const std::string& path) {
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        // 文件打不开：返回空表；错误描述写共享存储，last_error() 可取到。
        last_error_storage() = "PoLoader: cannot open file: " + path;
        return TranslationTable{};
    }
    std::ostringstream ss;
    ss << fin.rdbuf();
    return parse(ss.str());
}

std::string PoLoader::last_error() {
    return last_error_storage();
}

}  // namespace eda
