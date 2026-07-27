// Control —— GUI 控件基类实现（P4 Task 1）。
//
// 含锚点布局算法、最小尺寸缓存、焦点遍历三大核心逻辑。

#include "scene/gui/control.h"

#include <algorithm>
#include <vector>

namespace eda {

Control::Control() = default;

// ====== 锚点 + 父尺寸 → 实际几何 ======
//
// 设父尺寸 ps，对每条边 i：
//     像素坐标 = anchors_[i] * ps + offsets_[i]
// LEFT/TOP 给左上角，RIGHT/BOTTOM 给右下角；size = 右下 - 左上。

Vector2 Control::parent_size_() const {
    // 父为 Control 时取其 size（可能也是锚点算出的）；否则退回 viewport_size_，
    // 供顶层 Control（挂在 Window 下而非 Control 下）由外部注入布局尺寸。
    if (auto* p = get_parent()) {
        if (auto* cp = dynamic_cast<Control*>(p)) {
            return cp->get_size();
        }
    }
    return viewport_size_;
}

Vector2 Control::get_position() const {
    Vector2 ps = parent_size_();
    return {anchors_[0] * ps.x + offsets_[0],   // LEFT
            anchors_[1] * ps.y + offsets_[1]};  // TOP
}

Vector2 Control::get_size() const {
    Vector2 ps = parent_size_();
    float left   = anchors_[0] * ps.x + offsets_[0];
    float top    = anchors_[1] * ps.y + offsets_[1];
    float right  = anchors_[2] * ps.x + offsets_[2];
    float bottom = anchors_[3] * ps.y + offsets_[3];
    return {right - left, bottom - top};
}

void Control::set_position(Vector2 p) {
    // 保持尺寸：LEFT/RIGHT 整体平移 d.x，TOP/BOTTOM 整体平移 d.y，
    // 使 width = right - left 与 height = bottom - top 不变。
    Vector2 d = p - get_position();
    offsets_[0] += d.x;  // LEFT
    offsets_[2] += d.x;  // RIGHT
    offsets_[1] += d.y;  // TOP
    offsets_[3] += d.y;  // BOTTOM
    _notification(NOTIFICATION_RESIZED);
    update_minimum_size();
    queue_redraw();
}

void Control::set_size(Vector2 s) {
    // 保持左上位置：仅调整 RIGHT/BOTTOM 偏移（对齐 Godot 行为）。
    Vector2 d = s - get_size();
    offsets_[2] += d.x;  // RIGHT 调整宽
    offsets_[3] += d.y;  // BOTTOM 调整高
    _notification(NOTIFICATION_RESIZED);
    update_minimum_size();
    queue_redraw();
}

void Control::set_rect(Rect2 r) {
    // 位置 + 尺寸一起设：复用 set_position + set_size（保持锚点不变）。
    set_position(r.pos);
    set_size(r.size);
}

void Control::set_anchor(Side side, float value) {
    anchors_[static_cast<int>(side)] = value;
    queue_redraw();
}

void Control::set_offset(Side side, float value) {
    offsets_[static_cast<int>(side)] = value;
    queue_redraw();
}

void Control::set_anchors_preset(LayoutPreset preset) {
    // 按当前 size（w × h）重算 anchors + offsets，使预设语义立即成立，
    // 且后续父尺寸变化时锚定生效（CENTER 保持居中、BOTTOM_RIGHT 保持贴右下角…）。
    Vector2 s = get_size();
    float w = s.x;
    float h = s.y;

    auto set_anc = [&](float l, float t, float r, float b) {
        anchors_[0] = l; anchors_[1] = t; anchors_[2] = r; anchors_[3] = b;
    };
    auto set_off = [&](float l, float t, float r, float b) {
        offsets_[0] = l; offsets_[1] = t; offsets_[2] = r; offsets_[3] = b;
    };

    switch (preset) {
        case LayoutPreset::FULL_RECT:
            // 四边锚到父四角，零偏移：完全铺满父。
            set_anc(0.0f, 0.0f, 1.0f, 1.0f);
            set_off(0.0f, 0.0f, 0.0f, 0.0f);
            break;
        case LayoutPreset::LEFT_WIDE:
            set_anc(0.0f, 0.0f, 0.0f, 1.0f);
            set_off(0.0f, 0.0f, 0.0f, 0.0f);
            break;
        case LayoutPreset::RIGHT_WIDE:
            set_anc(1.0f, 0.0f, 1.0f, 1.0f);
            set_off(0.0f, 0.0f, 0.0f, 0.0f);
            break;
        case LayoutPreset::TOP_WIDE:
            set_anc(0.0f, 0.0f, 1.0f, 0.0f);
            set_off(0.0f, 0.0f, 0.0f, 0.0f);
            break;
        case LayoutPreset::BOTTOM_WIDE:
            set_anc(0.0f, 1.0f, 1.0f, 1.0f);
            set_off(0.0f, 0.0f, 0.0f, 0.0f);
            break;
        case LayoutPreset::CENTER:
            // 四角锚到父中心 0.5，偏移 ±w/2 / ±h/2：尺寸保持 w×h 且居中。
            set_anc(0.5f, 0.5f, 0.5f, 0.5f);
            set_off(-w / 2.0f, -h / 2.0f, w / 2.0f, h / 2.0f);
            break;
        case LayoutPreset::TOP_LEFT:
            // 锚到左上角，偏移 0..w / 0..h：左上对齐，尺寸 w×h。
            set_anc(0.0f, 0.0f, 0.0f, 0.0f);
            set_off(0.0f, 0.0f, w, h);
            break;
        case LayoutPreset::TOP_RIGHT:
            set_anc(1.0f, 0.0f, 1.0f, 0.0f);
            set_off(-w, 0.0f, 0.0f, h);
            break;
        case LayoutPreset::BOTTOM_LEFT:
            set_anc(0.0f, 1.0f, 0.0f, 1.0f);
            set_off(0.0f, -h, w, 0.0f);
            break;
        case LayoutPreset::BOTTOM_RIGHT:
            // 锚到右下角，偏移 -w..0 / -h..0：右下对齐，尺寸 w×h。
            set_anc(1.0f, 1.0f, 1.0f, 1.0f);
            set_off(-w, -h, 0.0f, 0.0f);
            break;
    }
    queue_redraw();
    update_minimum_size();
}

// ====== 最小尺寸（带缓存 + 向父冒泡）======

Vector2 Control::get_combined_minimum_size() const {
    // 懒计算：脏则重算并缓存，clean 直接返回缓存（容器布局多次查询时不重算）。
    if (min_size_dirty_) {
        cached_min_size_ = get_minimum_size();
        min_size_dirty_ = false;
    }
    return cached_min_size_;
}

void Control::update_minimum_size() {
    // 仅在 clean → dirty 的过渡时向父冒泡（已是 dirty 则父已失效，无需重复冒泡，
    // 避免子树频繁变更引发指数级传播）。
    if (!min_size_dirty_) {
        min_size_dirty_ = true;
        if (auto* p = dynamic_cast<Control*>(get_parent())) {
            p->update_minimum_size();
        }
    }
}

// ====== 自绘 ======

void Control::draw_if_dirty() {
    if (!draw_dirty_) return;
    draw_dirty_ = false;
    _notification(NOTIFICATION_DRAW);
    _draw();   // 派生控件自绘（默认空）
    if (has_focus_) {
        // 无障碍：焦点可视指示（WCAG 2.1 AA：2px 对比色边框，不依赖 hover）。
        if (auto* cv = current_canvas()) {
            cv->draw_rect(get_rect(), Color{0.40f, 0.62f, 1.00f, 1.00f},
                          /*filled=*/false);
        }
    }
}

// ====== 焦点 ======

void Control::set_focus(bool focus) {
    if (focus_mode_ == FocusMode::NONE) focus = false;  // NONE 拒绝聚焦
    if (has_focus_ == focus) return;
    has_focus_ = focus;
    _notification(focus ? NOTIFICATION_FOCUS_ENTER : NOTIFICATION_FOCUS_EXIT);
    queue_redraw();  // 焦点高亮需重绘
}

namespace {
// DFS 前序收集子树中所有可聚焦控件（focus_mode != NONE），按深度顺序入序。
// 用于 Tab/Shift+Tab 遍历——视觉顺序与场景树前序一致。
void collect_focusable(const Node* n, std::vector<Control*>& out) {
    if (n == nullptr) return;
    if (auto* ctrl = dynamic_cast<const Control*>(n)) {
        if (ctrl->get_focus_mode() != FocusMode::NONE) {
            out.push_back(const_cast<Control*>(ctrl));
        }
    }
    for (Node* c : n->get_children()) {
        collect_focusable(c, out);
    }
}
}  // namespace

Control* Control::find_next_focus(bool reverse) const {
    // 从根节点 DFS 收集可聚焦链，定位自身后取相邻（末尾回绕到首）。
    const Node* root = this;
    while (root->get_parent() != nullptr) {
        root = root->get_parent();
    }
    std::vector<Control*> chain;
    collect_focusable(root, chain);
    if (chain.empty()) return nullptr;

    auto it = std::find(chain.begin(), chain.end(), this);
    if (it == chain.end()) return chain.front();  // 自身不可聚焦：退回首项
    size_t idx = static_cast<size_t>(it - chain.begin());
    size_t n   = chain.size();
    // reverse: 取前一项（idx - 1，回绕到末尾）；forward: 取后一项（idx + 1，回绕到首）。
    return chain[reverse ? (idx + n - 1) % n : (idx + 1) % n];
}

}  // namespace eda
