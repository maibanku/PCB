// Container 实现（P4 Task 2）。
//
// 含四大块布局算法：
//   1. Container 基类：queue_sort / sort_children / fit_child_in_rect
//                      / 默认 _layout_children（子铺满）
//   2. BoxContainer：一维排列
//      - 主轴：子最小尺寸之和 + spacing；EXPAND 瓜分正余量；其余取最小
//      - 交叉轴：FILL 铺满 / SHRINK_CENTER 居中 / SHRINK_END 尾对齐
//      - 整体对齐：BEGIN/CENTER/END 决定子组在主轴上的打包起点
//   3. Grid：行列对齐
//      - 列宽 = 列内子最小宽最大值；行高 = 行内子最小高最大值
//      - 单元格内子左上对齐，尺寸取自身最小
//   4. MarginContainer：子铺满去四向边距后的内矩形

#include "scene/gui/container.h"

#include <algorithm>

namespace eda {

// ====== Container 基类 ======

void Container::add_child(Node* child) {
    // 重用 Node 树挂接（ownership 归父；环检测 / 树内传播由 Node::add_child 处理），
    // 完成后标脏触发重排。
    Node::add_child(child);
    queue_sort();
}

void Container::remove_child(Node* child) {
    Node::remove_child(child);
    // 清掉侧记的 size_flag 提示，避免悬空指针残留。
    if (auto* cp = dynamic_cast<Control*>(child)) {
        hints_.erase(cp);
    }
    queue_sort();
}

void Container::queue_sort() {
    layout_dirty_ = true;
}

void Container::sort_children() {
    if (!layout_dirty_) return;
    layout_dirty_ = false;
    _layout_children();
}

void Container::fit_child_in_rect(Control* child, Rect2 rect) {
    // 锚点全置 0：使子几何脱离父尺寸（绝对像素，不被容器尺寸二次拉伸）。
    // 注意：必须在 set_position/set_size 之前完成，否则旧的锚点会让位置/尺寸
    // 计算混入父尺寸项。
    child->set_anchor(Side::LEFT, 0.0f);
    child->set_anchor(Side::TOP, 0.0f);
    child->set_anchor(Side::RIGHT, 0.0f);
    child->set_anchor(Side::BOTTOM, 0.0f);
    // set_position + set_size 触发 NOTIFICATION_RESIZED（嵌套容器可借此重排）
    // 并 update_minimum_size（向父冒泡；本容器已在此路径上，幂等）。
    child->set_position(rect.pos);
    child->set_size(rect.size);
}

void Container::set_h_size_flag(Control* child, SizeFlag flag) {
    hints_[child].h = flag;
    queue_sort();
}

void Container::set_v_size_flag(Control* child, SizeFlag flag) {
    hints_[child].v = flag;
    queue_sort();
}

SizeFlag Container::get_h_size_flag(const Control* child) const {
    auto it = hints_.find(child);
    return it != hints_.end() ? it->second.h : SizeFlag::FILL;
}

SizeFlag Container::get_v_size_flag(const Control* child) const {
    auto it = hints_.find(child);
    return it != hints_.end() ? it->second.v : SizeFlag::FILL;
}

void Container::_notification(int what) {
    // 链到基类（Control::_notification 当前为空，保留未来 focus_enter/exit 等的分发）。
    Control::_notification(what);
    // 自身几何变化 → 子布局失效。Control::set_position/set_size/set_rect 均发此通知。
    if (what == NOTIFICATION_RESIZED) {
        queue_sort();
    }
}

std::vector<Control*> Container::get_control_children() const {
    std::vector<Control*> out;
    for (Node* n : get_children()) {
        if (auto* c = dynamic_cast<Control*>(n)) {
            out.push_back(c);
        }
    }
    return out;
}

void Container::_layout_children() {
    // 默认：所有 Control 子节点铺满容器（FULL_RECT 语义，对齐 Godot Container 默认）。
    Vector2 s = get_size();
    Rect2   full{{0, 0}, s};
    for (Control* c : get_control_children()) {
        fit_child_in_rect(c, full);
    }
}

// ====== BoxContainer（HBox / VBox）======
//
// 设主轴 main = (axis==0 ? x : y)，交叉轴 cross = 另一维。
//   1. 收集每子最小 main/cross；统计主轴 EXPAND 数。
//   2. 主轴余量 extra = main_size - sum(min_main) - total_spacing。
//      EXPAND 子均分 extra（仅正余量）；非 EXPAND 子取 min_main。
//   3. 打包起点：按 alignment 偏移（EXPAND 吞余量后 free→0，对齐退化为 BEGIN）。
//   4. 交叉轴：FILL/EXPAND 铺满；SHRINK_CENTER 居中；SHRINK_END 尾对齐。

Vector2 BoxContainer::get_minimum_size() const {
    // 主轴 = 子最小尺寸之和 + spacing*(n-1)；交叉轴 = 子最小尺寸的最大值。
    auto children = get_control_children();
    if (children.empty()) return Vector2{0, 0};

    float main_sum  = 0.0f;
    float cross_max = 0.0f;
    for (Control* c : children) {
        Vector2 m = c->get_combined_minimum_size();
        main_sum += main_of(m, axis_);
        cross_max = std::max(cross_max, cross_of(m, axis_));
    }
    if (children.size() > 1) {
        main_sum += spacing_ * static_cast<float>(children.size() - 1);
    }
    if (axis_ == 0) return Vector2{main_sum, cross_max};
    return Vector2{cross_max, main_sum};
}

void BoxContainer::_layout_children() {
    auto children = get_control_children();
    if (children.empty()) return;
    const int n = static_cast<int>(children.size());

    Vector2 self_size = get_size();
    float   main_size  = main_of(self_size, axis_);
    float   cross_size = cross_of(self_size, axis_);

    // 1) 收集每子最小主/交叉尺寸 + 统计 EXPAND 数。
    std::vector<float> min_main(n);
    std::vector<float> cross_min(n);
    int   expand_count = 0;
    float main_sum     = 0.0f;
    for (int i = 0; i < n; ++i) {
        Vector2 m = children[i]->get_combined_minimum_size();
        min_main[i]  = main_of(m, axis_);
        cross_min[i] = cross_of(m, axis_);
        main_sum += min_main[i];
        SizeFlag main_flag = (axis_ == 0) ? get_h_size_flag(children[i])
                                          : get_v_size_flag(children[i]);
        if (main_flag == SizeFlag::EXPAND) ++expand_count;
    }

    const float total_spacing = (n > 1) ? spacing_ * static_cast<float>(n - 1) : 0.0f;
    const float extra         = main_size - main_sum - total_spacing;

    // 2) 分配主轴尺寸：EXPAND 瓜分正余量；其余取最小。
    std::vector<float> child_main(n);
    for (int i = 0; i < n; ++i) {
        SizeFlag main_flag = (axis_ == 0) ? get_h_size_flag(children[i])
                                          : get_v_size_flag(children[i]);
        if (main_flag == SizeFlag::EXPAND && extra > 0.0f && expand_count > 0) {
            child_main[i] = min_main[i] + extra / static_cast<float>(expand_count);
        } else {
            child_main[i] = min_main[i];
        }
    }

    // 3) 打包起点：按 alignment；free 为子组实际占用后的剩余（负则截 0，溢出不偏移）。
    float used = total_spacing;
    for (int i = 0; i < n; ++i) used += child_main[i];
    float free = main_size - used;
    if (free < 0.0f) free = 0.0f;

    float cursor = 0.0f;
    switch (alignment_) {
        case BoxAlignment::BEGIN:  cursor = 0.0f;       break;
        case BoxAlignment::CENTER: cursor = free / 2.0f; break;
        case BoxAlignment::END:    cursor = free;        break;
    }

    // 4) 放置每个子：主轴 cursor 累加；交叉轴按 size_flag（FILL 铺满 / SHRINK 局部）。
    for (int i = 0; i < n; ++i) {
        SizeFlag cross_flag = (axis_ == 0) ? get_v_size_flag(children[i])
                                           : get_h_size_flag(children[i]);

        float cross_dim;
        float cross_pos;
        if (cross_flag == SizeFlag::SHRINK_CENTER) {
            cross_dim = cross_min[i];
            cross_pos = (cross_size - cross_dim) / 2.0f;
        } else if (cross_flag == SizeFlag::SHRINK_END) {
            cross_dim = cross_min[i];
            cross_pos = cross_size - cross_dim;
        } else {
            // FILL / EXPAND：交叉轴铺满。
            cross_dim = cross_size;
            cross_pos = 0.0f;
        }

        Rect2 r;
        if (axis_ == 0) {
            r.pos  = {cursor, cross_pos};
            r.size = {child_main[i], cross_dim};
        } else {
            r.pos  = {cross_pos, cursor};
            r.size = {cross_dim, child_main[i]};
        }
        fit_child_in_rect(children[i], r);

        cursor += child_main[i] + spacing_;
    }
}

// ====== Grid（行列布局）======
//
//   - children 按索引顺序填入 (row, col)：i → (i/cols, i%cols)。
//   - 列宽 = 该列所有子最小宽的最大值；行高 = 该行所有子最小高的最大值。
//   - 单元格起点 = 前面所有列宽（含 col_spacing）累加；行同理。
//   - 子在单元格内左上对齐，尺寸取自身最小（对齐 Godot GridContainer）。

Vector2 Grid::get_minimum_size() const {
    auto children = get_control_children();
    if (children.empty()) return Vector2{0, 0};

    const int cols = columns_ > 0 ? columns_ : 1;
    const int n    = static_cast<int>(children.size());
    const int rows = (n + cols - 1) / cols;

    std::vector<float> col_w(cols, 0.0f);
    std::vector<float> row_h(rows, 0.0f);
    for (int i = 0; i < n; ++i) {
        int     col = i % cols;
        int     row = i / cols;
        Vector2 m   = children[i]->get_combined_minimum_size();
        col_w[col] = std::max(col_w[col], m.x);
        row_h[row] = std::max(row_h[row], m.y);
    }

    float total_w = 0.0f;
    float total_h = 0.0f;
    for (float w : col_w) total_w += w;
    for (float h : row_h) total_h += h;
    if (cols > 1) total_w += col_spacing_ * static_cast<float>(cols - 1);
    if (rows > 1) total_h += row_spacing_ * static_cast<float>(rows - 1);
    return Vector2{total_w, total_h};
}

void Grid::_layout_children() {
    auto children = get_control_children();
    if (children.empty()) return;

    const int cols = columns_ > 0 ? columns_ : 1;
    const int n    = static_cast<int>(children.size());
    const int rows = (n + cols - 1) / cols;

    // 1) 列宽 / 行高 = 该列/行内子最小尺寸的最大值。
    std::vector<float> col_w(cols, 0.0f);
    std::vector<float> row_h(rows, 0.0f);
    for (int i = 0; i < n; ++i) {
        int     col = i % cols;
        int     row = i / cols;
        Vector2 m   = children[i]->get_combined_minimum_size();
        col_w[col] = std::max(col_w[col], m.x);
        row_h[row] = std::max(row_h[row], m.y);
    }

    // 2) 列 / 行起始坐标（累加 + spacing）。
    std::vector<float> col_x(cols);
    std::vector<float> row_y(rows);
    float acc = 0.0f;
    for (int c = 0; c < cols; ++c) {
        col_x[c] = acc;
        acc += col_w[c] + col_spacing_;
    }
    acc = 0.0f;
    for (int r = 0; r < rows; ++r) {
        row_y[r] = acc;
        acc += row_h[r] + row_spacing_;
    }

    // 3) 放置每个子到 (col_x, row_y)，尺寸取自身最小（单元格内左上对齐）。
    for (int i = 0; i < n; ++i) {
        int     col = i % cols;
        int     row = i / cols;
        Vector2 m   = children[i]->get_combined_minimum_size();
        Rect2   r{{col_x[col], row_y[row]}, m};
        fit_child_in_rect(children[i], r);
    }
}

// ====== MarginContainer（四向内边距）======

Vector2 MarginContainer::get_minimum_size() const {
    // 子最小尺寸（取最大）+ 四向边距。
    float max_w = 0.0f;
    float max_h = 0.0f;
    for (Control* c : get_control_children()) {
        Vector2 m = c->get_combined_minimum_size();
        max_w = std::max(max_w, m.x);
        max_h = std::max(max_h, m.y);
    }
    return Vector2{
        max_w + margins_[static_cast<int>(Side::LEFT)] + margins_[static_cast<int>(Side::RIGHT)],
        max_h + margins_[static_cast<int>(Side::TOP)] + margins_[static_cast<int>(Side::BOTTOM)],
    };
}

void MarginContainer::_layout_children() {
    // 内矩形：从 (left, top) 起，到 (size - right, size - bottom) 止。
    Vector2 s = get_size();
    float   left   = margins_[static_cast<int>(Side::LEFT)];
    float   top    = margins_[static_cast<int>(Side::TOP)];
    float   right  = margins_[static_cast<int>(Side::RIGHT)];
    float   bottom = margins_[static_cast<int>(Side::BOTTOM)];
    Rect2   inner{{left, top},
                  {s.x - left - right, s.y - top - bottom}};
    for (Control* c : get_control_children()) {
        fit_child_in_rect(c, inner);
    }
}

}  // namespace eda
