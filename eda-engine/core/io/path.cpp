// core/io/path.cpp
//
// P5 Task 1 · 路径工具实现（纯字符串处理，跨平台正反斜杠）。

#include "core/io/path.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace eda {

namespace {

// 三类工程级前缀（与 P2 Resource、P5 工程树一致）。顺序长的优先匹配（user:// 比 res:// 长）。
struct Scheme {
    const char* name;       // "res://"
    size_t len;
};

constexpr Scheme kSchemes[] = {
    {"user://", 7},
    {"res://",  6},
    {"lib://",  6},
};

// 返回协议前缀长度（命中为 6/7；未命中为 0）。
size_t scheme_prefix_len(const std::string& p) {
    for (const Scheme& s : kSchemes) {
        if (p.size() >= s.len && p.compare(0, s.len, s.name) == 0) {
            return s.len;
        }
    }
    return 0;
}

bool is_slash(char c) { return c == '/' || c == '\\'; }

// 按斜杠切分路径（跳过空段，不折叠 . / ..）。
std::vector<std::string> split_segments(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t i = start;
        while (i < s.size() && !is_slash(s[i])) ++i;
        if (i > start) {
            out.emplace_back(s.substr(start, i - start));
        }
        if (i >= s.size()) break;
        start = i + 1;
    }
    return out;
}

}  // namespace

std::string Path::normalize(const std::string& p) {
    if (p.empty()) return p;

    // 1) 反斜杠 -> 正斜杠
    std::string s;
    s.reserve(p.size());
    for (char c : p) s += (c == '\\') ? '/' : c;

    // 2) 协议前缀检测（保留 res:// 等双斜杠）
    std::string prefix;
    std::string rest = s;
    size_t scheme_len = scheme_prefix_len(s);
    if (scheme_len > 0) {
        prefix = s.substr(0, scheme_len);
        rest = s.substr(scheme_len);
    }

    // Windows 盘符路径（C:/foo）：原样返回（仅做了反斜杠转换）
    // 仅在无协议前缀时识别。
    if (prefix.empty() && rest.size() >= 2 &&
        std::isalpha(static_cast<unsigned char>(rest[0])) && rest[1] == ':') {
        return s;
    }

    // 3) 相对路径前导 `..` 保留；绝对路径（协议外的前导 /）记录并保留前导斜杠
    bool absolute = false;
    if (prefix.empty() && !rest.empty() && rest[0] == '/') {
        absolute = true;
    }

    // 4) 切分并折叠 . / ..
    std::vector<std::string> segments = split_segments(rest);
    std::vector<std::string> folded;
    for (const std::string& seg : segments) {
        if (seg == ".") {
            continue;
        } else if (seg == "..") {
            if (!folded.empty() && folded.back() != "..") {
                folded.pop_back();
            } else if (prefix.empty() && !absolute) {
                folded.push_back("..");  // 相对路径允许保留前导 ..
            }
            // 协议头 / 绝对路径下：不能越过根，丢弃
        } else {
            folded.push_back(seg);
        }
    }

    // 5) 重新拼接
    std::string out = prefix;
    if (absolute) out += '/';
    for (size_t i = 0; i < folded.size(); ++i) {
        if (i > 0) out += '/';
        out += folded[i];
    }
    return out;
}

std::string Path::join(const std::string& a, const std::string& b) {
    if (a.empty()) return normalize(b);
    if (b.empty()) return normalize(a);

    // b 若带协议前缀，直接返回 b 的规范化形式（绝对路径替换基）
    if (scheme_prefix_len(b) > 0) return normalize(b);

    // 去掉 b 的前导斜杠（视为相对 a；与多数 path.join 语义一致）
    std::string bb = b;
    while (!bb.empty() && is_slash(bb.front())) bb.erase(bb.begin());

    std::string joined = a;
    if (!joined.empty() && !is_slash(joined.back())) joined += '/';
    joined += bb;
    return normalize(joined);
}

std::string Path::parent(const std::string& p) {
    std::string n = normalize(p);
    if (n.empty()) return n;

    // 找最后一个非尾随斜杠段
    // 协议前缀长度
    size_t scheme_len = scheme_prefix_len(n);
    std::string prefix = (scheme_len > 0) ? n.substr(0, scheme_len) : std::string();
    std::string rest = (scheme_len > 0) ? n.substr(scheme_len) : n;

    // 去掉 rest 末尾的 `/`
    while (!rest.empty() && rest.back() == '/') rest.pop_back();

    size_t slash = rest.find_last_of('/');
    if (slash == std::string::npos) {
        // rest 只有一段：parent 就是协议头（或空）
        return prefix;
    }
    return prefix + rest.substr(0, slash);
}

std::string Path::filename(const std::string& p) {
    std::string n = normalize(p);
    if (n.empty()) return n;
    // 去掉末尾斜杠
    while (!n.empty() && n.back() == '/') n.pop_back();
    if (n.empty()) return n;
    size_t slash = n.find_last_of('/');
    if (slash == std::string::npos) return n;
    return n.substr(slash + 1);
}

std::string Path::extension(const std::string& p) {
    std::string leaf = filename(p);
    // 跳过前导点（隐藏文件如 .gitignore 无扩展名）
    size_t start = 0;
    while (start < leaf.size() && leaf[start] == '.') ++start;
    size_t dot = leaf.find_last_of('.');
    if (dot == std::string::npos || dot < start) return std::string();
    std::string ext = leaf.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return ext;
}

bool Path::is_res(const std::string& p) {
    return p.size() >= 6 && p.compare(0, 6, "res://") == 0;
}

bool Path::is_user(const std::string& p) {
    return p.size() >= 7 && p.compare(0, 7, "user://") == 0;
}

bool Path::is_lib(const std::string& p) {
    return p.size() >= 6 && p.compare(0, 6, "lib://") == 0;
}

std::string Path::res_to_native(const std::string& res_path,
                                const std::string& project_root) {
    if (!is_res(res_path)) return res_path;
    std::string suffix = res_path.substr(6);  // 跳过 "res://"
    std::string root = project_root;
    // 自动补足尾部分隔符（容忍正反斜杠）
    if (!root.empty() && root.back() != '/' && root.back() != '\\') {
        root += '/';
    }
    std::string joined = root + suffix;
    // 反斜杠 -> 正斜杠（与 normalize 一致；不入库盘符由调用方保证）
    std::string out;
    out.reserve(joined.size());
    for (char c : joined) out += (c == '\\') ? '/' : c;
    return out;
}

}  // namespace eda
