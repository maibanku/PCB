#pragma once

// core/io/path.h
//
// P5 Task 1 · 工程与文件系统 —— 路径工具（跨平台、跨正反斜杠）。
//
// 设计：
//   - 纯字符串处理，不依赖 std::filesystem（部分嵌入式 / 旧工具链支持差）。
//   - 统一正斜杠 `/`：工程自包含、可离线移交，禁止入库绝对路径与盘符。
//   - 识别三类工程级前缀（与 P2 Resource、P5 工程树一致）：
//       res://   工程内相对路径（相对工程根）
//       user://  用户级配置 / 模板
//       lib://   库引用（可被依赖锁定快照替换为 res:// 以离线化）
//   - normalize 折叠 `.` / `..` 与重复斜杠，但保留协议前缀的双斜杠。
//
// 不在范围内：磁盘 IO（由 ProjectLoader/ProjectSaver 用 std::fstream 直接处理）、
//             路径存在性校验、符号链接解析。

#include <string>

namespace eda {

// Path —— 全部为静态工具函数（值无状态）。
struct Path {
    // 拼接两段路径，自动补足分隔符（仅当 a 末尾或 b 开头之一无斜杠时补一个）。
    //   join("res://sheets", "x.toml")        -> "res://sheets/x.toml"
    //   join("res://sheets/", "x.toml")       -> "res://sheets/x.toml"
    //   join("res://sheets", "/x.toml")       -> "res://sheets/x.toml"
    static std::string join(const std::string& a, const std::string& b);

    // 取父目录（去掉最后一段，保留协议前缀）。
    //   parent("res://sheets/x.toml")  -> "res://sheets"
    //   parent("res://x.toml")         -> "res://"
    //   parent("x.toml")               -> ""
    static std::string parent(const std::string& p);

    // 取文件名（最后一段，含扩展名）。
    //   filename("res://sheets/x.toml")  -> "x.toml"
    //   filename("x.toml")               -> "x.toml"
    static std::string filename(const std::string& p);

    // 取扩展名（最后一个点之后，小写，不含点）；无扩展名返回空串。
    //   extension("a/b/x.TOML")   -> "toml"
    //   extension("a/b/x")        -> ""
    //   extension("a/.hidden")    -> ""
    static std::string extension(const std::string& p);

    // 规范化：
    //   1) 反斜杠 -> 正斜杠（跨平台）
    //   2) 合并重复斜杠（保留 res:// / user:// / lib:// 协议头的双斜杠）
    //   3) 折叠 `.` 与 `..`（不能越过协议根；相对路径允许保留前导 `..`）
    //   normalize("res://sheets//../sheets/x.toml")  -> "res://sheets/x.toml"
    //   normalize("a/./b/../c")                      -> "a/c"
    //   normalize("res://foo\\bar.toml")             -> "res://foo/bar.toml"
    static std::string normalize(const std::string& p);

    // 协议识别
    static bool is_res(const std::string& p);    // 以 res:// 开头
    static bool is_user(const std::string& p);   // 以 user:// 开头
    static bool is_lib(const std::string& p);    // 以 lib:// 开头

    // res:// 工程内路径 -> 磁盘路径（project_root + suffix）。
    // 自动补足 project_root 末尾分隔符。非 res:// 路径原样返回。
    //   res_to_native("res://sheets/x.toml", "/proj")  -> "/proj/sheets/x.toml"
    static std::string res_to_native(const std::string& res_path,
                                     const std::string& project_root);
};

}  // namespace eda
