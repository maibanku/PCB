// editor/command_palette/command_palette.cpp
//
// CommandPalette 实现 + 模糊匹配算法。
//
// ============== 模糊匹配 scoring 设计 ==============
//
// 输入：query（用户输入）、text（候选字符串，通常为 Command.name）。
// 输出：FuzzyMatch{matched, score}，matched=false 表示 query 非 text 子序列。
//
// 算法分两阶段：
//
// 1. 子序列扫描（决定 matched）：
//    顺序在 text 中找 query 每个字符的下一个出现位置；任一未找到 → matched=false。
//    大小写不敏感（先 to_lower 两侧）。
//
// 2. 加分累计（matched=true 时计算 score，越大越优）：
//    每个命中字符累加：
//      + kBaseChar         基础分（每命中一字符）
//      + kWordBoundary     命中位 = 0（首字符）或前导字符为分隔符（空格 / _ / - / / .）
//      + kConsecutive      当前命中位 = 上次命中位 + 1（连续相邻匹配）
//    全部命中后整串级加分：
//      + kExact            query == text（大小写不敏感）
//      + kPrefix           text 以 query 开头（非精确等值时）
//    密度调整：
//      + matched_count * kDensity             命中越多越优（鼓励更长 query）
//      - (text_len - matched_count) * kLen    未命中字符轻微扣分（短 name 同等命中更优）
//
// 单调性验证（手工算例，确保排序可预期）：
//   - "open" vs "open"      → 精确 4 连命中，最高分（base + 首字 + 3×连续 + exact + 密度）
//   - "open" vs "open file" → 前缀 4 连命中，次高分（少 exact，但有 prefix）
//   - "os"   vs "close"     → 2 连命中（o@2, s@3），中等分
//   - "os"   vs "open"      → 无 's'，matched=false（排除）
//   - "o"    vs "open"      → 首字符命中，高于 "o" vs "loop"（内部命中）
//
// 同分时排序策略：name 字典序升序（稳定可预期，便于回归测试）。

#include "editor/command_palette/command_palette.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace eda {

// ===========================================================================
// fuzzy_match 实现
// ===========================================================================
namespace {

// Scoring 常量（集中在匿名命名空间，便于调参 / 阅读单调性证明）。
constexpr double kBaseChar      = 10.0;  // 每命中一字符
constexpr double kConsecutive   = 25.0;  // 与上次命中相邻
constexpr double kWordBoundary  = 30.0;  // 命中 text[0] 或词首（前导分隔符）
constexpr double kPrefix        = 80.0;  // text 以 query 开头（非精确等值）
constexpr double kExact         = 200.0; // text == query（大小写不敏感）
constexpr double kDensity       = 5.0;   // 每命中字符的密度奖励
constexpr double kLengthPenalty = 1.0;   // 每个未命中字符的扣分

// ASCII 小写转换（不依赖 locale；命令面板只匹配 ASCII 命令名）。
char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(to_lower_ascii(c));
    return out;
}

// 词分隔符：命中字符的前导是这些之一时视为词首，给边界加分。
// （camelCase 边界未识别，留待后续；当前覆盖 snake_case / kebab-case / 路径 / 空格。）
bool is_word_separator(char c) {
    return c == ' ' || c == '_' || c == '-' || c == '/' || c == '\\' || c == '.' || c == '\t';
}

}  // namespace

FuzzyMatch fuzzy_match(const std::string& query, const std::string& text) {
    // 空查询：视为匹配全部，score=0（命令面板"空查询展示全部命令"语义）。
    if (query.empty()) {
        return {true, 0.0};
    }

    std::string q = to_lower(query);
    std::string t = to_lower(text);

    std::size_t  ti            = 0;
    int          prev_pos      = -2;  // 初始化 -2，使首次命中不可能被判定为"连续"
    std::size_t  matched_count = 0;
    double       score         = 0.0;

    for (char qc : q) {
        // 在 t[ti..) 中线性找 qc 的下一个出现位置。
        // 万级命令 × 短 query 的整体复杂度仍可接受；后续可按需上 BM/Galil 优化。
        std::size_t found = std::string::npos;
        for (std::size_t i = ti; i < t.size(); ++i) {
            if (t[i] == qc) { found = i; break; }
        }
        if (found == std::string::npos) {
            return {false, 0.0};  // 子序列失败：query 有字符未在 text 中按序出现
        }

        // —— 命中加分 ——
        score += kBaseChar;
        if (found == 0) {
            score += kWordBoundary;  // 命中 text 首字符
        } else if (is_word_separator(t[found - 1])) {
            score += kWordBoundary;  // 词边界
        }
        if (prev_pos == static_cast<int>(found) - 1) {
            score += kConsecutive;   // 与上次命中相邻
        }

        prev_pos = static_cast<int>(found);
        ti       = found + 1;
        ++matched_count;
    }

    // —— 整串级加分 ——
    if (q == t) {
        score += kExact;
    } else if (t.size() >= q.size() && t.compare(0, q.size(), q) == 0) {
        score += kPrefix;
    }

    // —— 密度调整：命中占比越高（text 短 / 命中多）越优 ——
    score += static_cast<double>(matched_count) * kDensity;
    score -= static_cast<double>(t.size() - matched_count) * kLengthPenalty;

    return {true, score};
}

// ===========================================================================
// CommandPalette 实现
// ===========================================================================

CommandPalette::CommandPalette() {
    // 默认绑进程单例；测试可用 set_registry 替换为本地 registry。
    registry_ = &CommandRegistry::get();
    // 命令面板需接收全键事件（含 Enter / Esc / 方向键）。
    set_focus_mode(FocusMode::ALL);
}

void CommandPalette::set_registry(CommandRegistry* registry) {
    registry_ = registry;
    recompute_matches_();
    queue_redraw();
}

void CommandPalette::set_query(const std::string& query) {
    query_ = query;
    recompute_matches_();
    selected_index_ = 0;  // 新查询：选中重置为首项
    queue_redraw();
}

void CommandPalette::recompute_matches_() {
    matches_.clear();
    if (registry_ == nullptr) {
        // 无 registry：选中 clamp 到空。
        selected_index_ = 0;
        return;
    }

    auto all = registry_->all();
    for (const Command* cmd : all) {
        if (cmd == nullptr) continue;

        // 主匹配字段：name（用户可见标签）。name 未命中时回落 id（命令面板可达性）。
        FuzzyMatch m = fuzzy_match(query_, cmd->name);
        double score = m.score;
        if (!m.matched) {
            FuzzyMatch m2 = fuzzy_match(query_, cmd->id);
            if (!m2.matched) continue;
            // id 命中略低于同分 name 命中（同等情形下优先 name 匹配的命令）。
            score = m2.score - 5.0;
        }
        matches_.push_back({cmd, score});
    }

    // 排序：score 降序；同分按 name 字典序升序（稳定可预期，回归测试可重现）。
    std::sort(matches_.begin(), matches_.end(),
              [](const CommandMatch& a, const CommandMatch& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.command->name < b.command->name;
              });

    // 截断到 top N。
    if (matches_.size() > max_results_) {
        matches_.resize(max_results_);
    }

    // 选中索引 clamp 到合法区间（无结果则 0）。
    if (selected_index_ < 0) selected_index_ = 0;
    if (!matches_.empty() &&
        selected_index_ >= static_cast<int>(matches_.size())) {
        selected_index_ = static_cast<int>(matches_.size()) - 1;
    } else if (matches_.empty()) {
        selected_index_ = 0;
    }
}

const Command* CommandPalette::get_selected_command() const {
    if (matches_.empty()) return nullptr;
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(matches_.size())) {
        return nullptr;
    }
    return matches_[static_cast<std::size_t>(selected_index_)].command;
}

int CommandPalette::move_selection_up() {
    if (matches_.empty()) {
        selected_index_ = 0;
        queue_redraw();
        return selected_index_;
    }
    if (selected_index_ > 0) {
        --selected_index_;
        queue_redraw();
    }
    // 已为首项：不动（clamp，不回绕）。
    return selected_index_;
}

int CommandPalette::move_selection_down() {
    if (matches_.empty()) {
        selected_index_ = 0;
        queue_redraw();
        return selected_index_;
    }
    if (selected_index_ < static_cast<int>(matches_.size()) - 1) {
        ++selected_index_;
        queue_redraw();
    }
    // 已为末项：不动（clamp，不回绕）。
    return selected_index_;
}

bool CommandPalette::invoke_selected() {
    const Command* cmd = get_selected_command();
    if (cmd == nullptr) return false;
    if (!cmd->handler) return false;  // 占位命令：handler 空，安全跳过
    cmd->handler();
    return true;
}

void CommandPalette::open() {
    is_open_ = true;
    query_.clear();
    recompute_matches_();
    selected_index_ = 0;
    // 模态聚焦：弹层显示期间独占键盘事件。
    set_focus_mode(FocusMode::ALL);
    set_focus(true);
    queue_redraw();
}

void CommandPalette::close() {
    is_open_ = false;
    set_focus(false);
    queue_redraw();
}

void CommandPalette::_draw() {
    if (!is_open_) return;
    auto* cv = current_canvas();
    if (cv == nullptr) return;

    Rect2 r = get_rect();
    if (r.size.x <= 0.0f || r.size.y <= 0.0f) return;

    // 弹层背景（深色半透明）。
    cv->draw_rect(r, Color{0.12f, 0.13f, 0.16f, 0.98f}, /*filled=*/true);

    // 查询行（顶部 28px）。
    const float query_h = 28.0f;
    Rect2 query_row{r.pos, {r.size.x, query_h}};
    cv->draw_rect(query_row, Color{0.18f, 0.20f, 0.24f, 1.0f}, /*filled=*/true);
    cv->draw_text("> " + query_, query_row.pos + Vector2{8.0f, 6.0f},
                  Color{0.92f, 0.93f, 0.96f, 1.0f});

    // 结果列表（每行 22px；选中项高亮）。
    const float row_h = 22.0f;
    const float list_y = query_row.pos.y + query_row.size.y;
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        float y = list_y + static_cast<float>(i) * row_h;
        bool selected = static_cast<int>(i) == selected_index_;
        if (selected) {
            cv->draw_rect(Rect2{{r.pos.x, y}, {r.size.x, row_h}},
                          Color{0.27f, 0.51f, 0.83f, 1.0f}, /*filled=*/true);
        }
        cv->draw_text(matches_[i].command->name,
                      Vector2{r.pos.x + 12.0f, y + 4.0f},
                      Color{0.95f, 0.95f, 0.97f, 1.0f});
    }
}

}  // namespace eda
