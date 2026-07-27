// DockManager 实现（P4 Task 4）。
//
// 三大块：
//   1. 注册表 + 停靠状态机（dock / undock / float_panel + 状态查询）
//   2. border layout（_layout_children：计算五区域 Rect，reparent 面板到区域）
//   3. .layout.toml 序列化 / 反序列化（手写最小 TOML 子集，复用 P5 风格）
//
// 所有权：
//   - DockManager 构造期 new 五个区域 Container 并 add_child（manager 拥有 areas）。
//   - DockPanel 由调用方拥有（栈或注册表外部持有）；dock 仅 reparent 到 area，
//     undock/float 把 panel 从 area 剥离（parent=nullptr），不 delete。
//   - 栈对象测试约定：manager 先声明（后析构），panel 后声明（先析构自剥离）。

#include "editor/dock/dock_manager.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "core/math/vector2.h"

namespace eda {

namespace {

// ============================================================================
// TOML 字面量序列化 / 解析（与 P5 snapshot.cpp 的子集保持一致）
// ============================================================================

std::string toml_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += "\"";
    return out;
}

std::string trim_ws(const std::string& s) {
    size_t b = 0, e = s.size();
    const auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool parse_toml_string_literal(std::string_view raw, std::string& out) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\') {
            if (i + 1 >= raw.size() - 1) return false;
            const char e = raw[++i];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                default:   out += e; break;
            }
        } else {
            out += c;
        }
    }
    return true;
}

bool parse_toml_float_literal(std::string_view raw, float& out) {
    if (raw.empty()) return false;
    try {
        size_t pos = 0;
        const float v = std::stof(std::string(raw), &pos);
        if (pos != raw.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_toml_bool_literal(std::string_view raw, bool& out) {
    if (raw == "true") { out = true; return true; }
    if (raw == "false") { out = false; return true; }
    return false;
}

// 区域枚举 <-> TOML 字符串
const char* area_to_name(DockArea a) {
    switch (a) {
        case DockArea::LEFT:   return "LEFT";
        case DockArea::TOP:    return "TOP";
        case DockArea::RIGHT:  return "RIGHT";
        case DockArea::BOTTOM: return "BOTTOM";
        case DockArea::CENTER: return "CENTER";
    }
    return "CENTER";
}

bool area_from_name(std::string_view s, DockArea& out) {
    if (s == "LEFT")   { out = DockArea::LEFT;   return true; }
    if (s == "TOP")    { out = DockArea::TOP;    return true; }
    if (s == "RIGHT")  { out = DockArea::RIGHT;  return true; }
    if (s == "BOTTOM") { out = DockArea::BOTTOM; return true; }
    if (s == "CENTER") { out = DockArea::CENTER; return true; }
    return false;
}

constexpr int kLayoutSchemaVersion = 1;

}  // namespace

// ============================================================================
// 构造与区域容器搭建
// ============================================================================

DockManager::DockManager() {
    build_areas_();
}

void DockManager::build_areas_() {
    // 五个区域 Container：LEFT/RIGHT=VBox（垂直栈），TOP/BOTTOM=HBox（水平栈），
    // CENTER=Container（单面板铺满；多面板时退化为默认 FULL_RECT 铺满，首个可见）。
    // heap-allocated，由本 manager 拥有（~Node 级联 delete）。
    areas_[static_cast<int>(DockArea::LEFT)]   = new VBox();
    areas_[static_cast<int>(DockArea::TOP)]    = new HBox();
    areas_[static_cast<int>(DockArea::RIGHT)]  = new VBox();
    areas_[static_cast<int>(DockArea::BOTTOM)] = new HBox();
    areas_[static_cast<int>(DockArea::CENTER)] = new Container();
    for (Container* area : areas_) {
        if (area != nullptr) {
            // 经 Node::add_child 挂入（manager 拥有 area；area 不参与停靠命中，
            // 仅作为子布局容器）。
            Node::add_child(area);
        }
    }
}

// ============================================================================
// 注册表查询
// ============================================================================

DockManager::PanelRecord* DockManager::find_record_(DockPanel* panel) {
    for (auto& r : records_) {
        if (r.panel == panel) return &r;
    }
    return nullptr;
}

const DockManager::PanelRecord* DockManager::find_record_(const DockPanel* panel) const {
    for (const auto& r : records_) {
        if (r.panel == panel) return &r;
    }
    return nullptr;
}

DockManager::PanelRecord* DockManager::find_record_by_id_(const std::string& id) {
    for (auto& r : records_) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

std::string DockManager::next_auto_id_() const {
    // 生成不冲突的 "panel_<n>"。
    size_t n = records_.size();
    while (true) {
        std::string id = "panel_" + std::to_string(n);
        bool collision = false;
        for (const auto& r : records_) {
            if (r.id == id) { collision = true; break; }
        }
        if (!collision) return id;
        ++n;
    }
}

void DockManager::register_panel(const std::string& id, DockPanel* panel) {
    if (panel == nullptr) return;
    // 幂等：同 id 已绑定到同一 panel 视作成功；同 id 不同 panel 或 panel 已注册则拒绝。
    if (PanelRecord* existing = find_record_by_id_(id); existing != nullptr) {
        if (existing->panel == panel) return;  // 幂等
        return;  // id 冲突，拒绝
    }
    if (find_record_(panel) != nullptr) return;  // panel 已注册，拒绝
    PanelRecord r;
    r.id      = id;
    r.panel   = panel;
    r.area    = DockArea::CENTER;
    r.state   = DockState::UNDOCKED;
    r.visible = true;
    r.size    = 0.0f;
    records_.push_back(std::move(r));
}

DockPanel* DockManager::get_panel(const std::string& id) const {
    for (const auto& r : records_) {
        if (r.id == id) return r.panel;
    }
    return nullptr;
}

std::string DockManager::get_panel_id(const DockPanel* panel) const {
    if (const PanelRecord* r = find_record_(panel)) return r->id;
    return {};
}

// ============================================================================
// 停靠 / 卸下 / 浮动
// ============================================================================

void DockManager::dock(DockArea area, DockPanel* panel) {
    if (panel == nullptr) return;
    // 未注册则自动登记。
    if (find_record_(panel) == nullptr) {
        register_panel(next_auto_id_(), panel);
    }
    PanelRecord* r = find_record_(panel);
    if (r == nullptr) return;  // 理论不可达（刚注册）

    bool changed = (r->state != DockState::DOCKED) || (r->area != area);
    r->state   = DockState::DOCKED;
    r->area    = area;
    r->visible = true;  // dock 默认显示（隐藏由 set_panel_visible 单独控制）
    panel->clear_float_request();
    reparent_to_area_(area, panel);

    if (changed) {
        queue_sort();
        layout_changed_.emit();
    }
}

void DockManager::undock(DockPanel* panel) {
    if (panel == nullptr) return;
    PanelRecord* r = find_record_(panel);
    if (r == nullptr) return;
    if (r->state == DockState::UNDOCKED && panel->get_parent() == nullptr) return;

    r->state = DockState::UNDOCKED;
    // 从任何区域 Container 剥离（不 delete；调用方拥有 panel）。
    if (panel->get_parent() != nullptr) {
        panel->set_parent(nullptr);
    }
    queue_sort();
    layout_changed_.emit();
}

void DockManager::float_panel(DockPanel* panel) {
    if (panel == nullptr) return;
    if (find_record_(panel) == nullptr) {
        register_panel(next_auto_id_(), panel);
    }
    PanelRecord* r = find_record_(panel);
    if (r == nullptr) return;
    if (r->state == DockState::FLOATING) return;

    r->state = DockState::FLOATING;
    // 浮动 = 从区域布局剔除（简化为状态标记；不创建真实窗口实体）。
    if (panel->get_parent() != nullptr) {
        panel->set_parent(nullptr);
    }
    queue_sort();
    layout_changed_.emit();
}

void DockManager::reparent_to_area_(DockArea area, DockPanel* panel) {
    Container* area_c = areas_[static_cast<int>(area)];
    if (area_c == nullptr) return;
    if (panel->get_parent() == area_c) return;
    // CanvasItem::set_parent 内部先从旧父 remove_child 再 add_child 到新父。
    panel->set_parent(area_c);
    // 重新挂入后 area 的 size_flag 提示丢失（若先前设过）；默认 FILL 即可。
    area_c->queue_sort();
}

// ============================================================================
// 状态查询
// ============================================================================

DockState DockManager::get_state(const DockPanel* panel) const {
    if (const PanelRecord* r = find_record_(panel)) return r->state;
    return DockState::UNDOCKED;
}

DockArea DockManager::get_area_of(const DockPanel* panel) const {
    if (const PanelRecord* r = find_record_(panel)) {
        return r->state == DockState::DOCKED ? r->area : DockArea::CENTER;
    }
    return DockArea::CENTER;
}

bool DockManager::is_floating(const DockPanel* panel) const {
    if (const PanelRecord* r = find_record_(panel)) return r->state == DockState::FLOATING;
    return false;
}

bool DockManager::is_panel_visible(const DockPanel* panel) const {
    if (const PanelRecord* r = find_record_(panel)) return r->visible;
    return false;
}

void DockManager::set_panel_visible(DockPanel* panel, bool visible) {
    if (panel == nullptr) return;
    PanelRecord* r = find_record_(panel);
    if (r == nullptr) return;
    if (r->visible == visible) return;
    r->visible = visible;
    panel->set_visible(visible);
    queue_sort();
    layout_changed_.emit();
}

std::vector<DockPanel*> DockManager::get_panels_in(DockArea area) const {
    std::vector<DockPanel*> out;
    for (const auto& r : records_) {
        if (r.state == DockState::DOCKED && r.area == area) {
            out.push_back(r.panel);
        }
    }
    return out;
}

std::vector<DockPanel*> DockManager::get_visible_panels_in(DockArea area) const {
    std::vector<DockPanel*> out;
    for (const auto& r : records_) {
        if (r.state == DockState::DOCKED && r.area == area && r.visible) {
            out.push_back(r.panel);
        }
    }
    return out;
}

std::vector<DockPanel*> DockManager::get_floating_panels() const {
    std::vector<DockPanel*> out;
    for (const auto& r : records_) {
        if (r.state == DockState::FLOATING) out.push_back(r.panel);
    }
    return out;
}

// ============================================================================
// 区域尺寸
// ============================================================================

void DockManager::set_area_size(DockArea area, float size) {
    if (area == DockArea::CENTER) return;  // CENTER 填剩余空间，无独立尺寸
    const int idx = static_cast<int>(area);
    if (area_sizes_[idx] == size) return;
    area_sizes_[idx] = size;
    queue_sort();
    layout_changed_.emit();
}

float DockManager::get_area_size(DockArea area) const {
    return area_sizes_[static_cast<int>(area)];
}

// ============================================================================
// border layout
// ============================================================================

Rect2 DockManager::compute_area_rect_(DockArea a) const {
    Vector2 total = get_size();
    const float left_w   = area_sizes_[static_cast<int>(DockArea::LEFT)];
    const float right_w  = area_sizes_[static_cast<int>(DockArea::RIGHT)];
    const float top_h    = area_sizes_[static_cast<int>(DockArea::TOP)];
    const float bottom_h = area_sizes_[static_cast<int>(DockArea::BOTTOM)];

    // 内列横向起止（排除左右列）
    const float inner_x = left_w;
    const float inner_w = std::max(0.0f, total.x - left_w - right_w);
    // 内列纵向起止（排除上下行）
    const float inner_y = top_h;
    const float inner_h = std::max(0.0f, total.y - top_h - bottom_h);

    switch (a) {
        case DockArea::LEFT:
            return Rect2{{0.0f, 0.0f}, {left_w, total.y}};
        case DockArea::RIGHT:
            return Rect2{{total.x - right_w, 0.0f}, {right_w, total.y}};
        case DockArea::TOP:
            return Rect2{{inner_x, 0.0f}, {inner_w, top_h}};
        case DockArea::BOTTOM:
            return Rect2{{inner_x, total.y - bottom_h}, {inner_w, bottom_h}};
        case DockArea::CENTER:
            return Rect2{{inner_x, inner_y}, {inner_w, inner_h}};
    }
    return Rect2{};
}

void DockManager::_layout_children() {
    // 1) 排布五个区域 Container 到各自 border layout Rect。
    //    隐藏区域（无可见面板）尺寸仍占据位置但可被业务后续压缩；本 Task 简化：
    //    只要 area_sizes_ 非零就保留位置，无论是否有面板。
    for (int i = 0; i < 5; ++i) {
        Container* area = areas_[i];
        if (area == nullptr) continue;
        Rect2 r = compute_area_rect_(static_cast<DockArea>(i));
        fit_child_in_rect(area, r);
    }
    // 2) 级联：每个区域 Container 排布其内部 docked 面板。
    //    fit_child_in_rect 已触发 NOTIFICATION_RESIZED → area.queue_sort 标脏；
    //    显式调 sort_children 立即落地（ VBox/HBox 按尺寸提示分配）。
    for (Container* area : areas_) {
        if (area != nullptr) area->sort_children();
    }
}

// ============================================================================
// 序列化（serialize_layout / deserialize_layout）
// ============================================================================

std::string DockManager::serialize_layout() const {
    std::ostringstream out;
    out << "# eda-layout schema_version = " << kLayoutSchemaVersion << "\n";
    out << "# auto-generated by DockManager::save_layout\n\n";

    // —— 区域尺寸 ——
    out << "[area]\n";
    out << "LEFT = "   << area_sizes_[static_cast<int>(DockArea::LEFT)]   << "\n";
    out << "TOP = "    << area_sizes_[static_cast<int>(DockArea::TOP)]    << "\n";
    out << "RIGHT = "  << area_sizes_[static_cast<int>(DockArea::RIGHT)]  << "\n";
    out << "BOTTOM = " << area_sizes_[static_cast<int>(DockArea::BOTTOM)] << "\n";
    out << "\n";

    // —— 每面板记录 ——
    for (const auto& r : records_) {
        out << "[[panel]]\n";
        out << "id = "       << toml_quote(r.id) << "\n";
        out << "area = "     << (r.state == DockState::DOCKED ? area_to_name(r.area) : "NONE")
            << "\n";
        out << "state = ";
        switch (r.state) {
            case DockState::UNDOCKED: out << "UNDOCKED"; break;
            case DockState::DOCKED:   out << "DOCKED";   break;
            case DockState::FLOATING: out << "FLOATING"; break;
        }
        out << "\n";
        out << "visible = "  << (r.visible ? "true" : "false") << "\n";
        out << "size = "     << r.size << "\n";
        out << "\n";
    }
    return out.str();
}

bool DockManager::deserialize_layout(const std::string& text) {
    // 行扫描器：识别 [area] / [[panel]] 段，键值解析。
    // 状态：current_section ∈ {NONE, AREA, PANEL}；PANEL 段累积字段，遇到下一段或
    // 文件结束时把累积的 panel 应用到注册表。
    enum class Sec { NONE, AREA, PANEL };
    Sec sec = Sec::NONE;

    // 累积的 panel 字段
    struct PanelFields {
        bool        has_id    = false;
        std::string id;
        std::string area_name = "NONE";
        std::string state_name = "UNDOCKED";
        bool        visible   = true;
        float       size      = 0.0f;
        bool        has_size  = false;
        bool        has_visible = false;
    } pf;

    auto flush_panel = [&]() {
        if (!pf.has_id) return;  // 无 id 跳过
        PanelRecord* r = find_record_by_id_(pf.id);
        if (r == nullptr) {
            // 未注册的面板 id：跳过（不报错；调用方需先 register_panel）
            pf = PanelFields{};
            return;
        }
        // 解析 state / area
        if (pf.state_name == "FLOATING") {
            r->state = DockState::FLOATING;
            r->area  = DockArea::CENTER;
            if (r->panel->get_parent() != nullptr) r->panel->set_parent(nullptr);
        } else if (pf.state_name == "DOCKED") {
            DockArea a = DockArea::CENTER;
            area_from_name(pf.area_name, a);
            r->state = DockState::DOCKED;
            r->area  = a;
            reparent_to_area_(a, r->panel);
        } else {
            r->state = DockState::UNDOCKED;
            r->area  = DockArea::CENTER;
            if (r->panel->get_parent() != nullptr) r->panel->set_parent(nullptr);
        }
        if (pf.has_visible) {
            r->visible = pf.visible;
            r->panel->set_visible(pf.visible);
        }
        if (pf.has_size) r->size = pf.size;
        pf = PanelFields{};
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim_ws(line);
        if (t.empty()) continue;
        if (t[0] == '#') continue;  // 注释行

        if (t == "[area]") { flush_panel(); sec = Sec::AREA; continue; }
        if (t == "[[panel]]") { flush_panel(); sec = Sec::PANEL; continue; }
        if (!t.empty() && t[0] == '[') {
            // 未知段
            flush_panel(); sec = Sec::NONE; continue;
        }

        // key = value
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = trim_ws(t.substr(0, eq));
        std::string value = trim_ws(t.substr(eq + 1));
        // 剥离行尾注释（简化：仅当 value 后跟 " #..."）
        // 注意 TOML 字符串内 '#' 合法，但本序列化器输出的字符串总在行尾；
        // 为稳健起见仅对非字符串值剥注释。
        if (value.size() > 0 && value.front() != '"') {
            auto hash = value.find(' ');
            if (hash != std::string::npos) value = trim_ws(value.substr(0, hash));
        }

        if (sec == Sec::AREA) {
            float v = 0.0f;
            if (parse_toml_float_literal(value, v)) {
                if (key == "LEFT")   area_sizes_[static_cast<int>(DockArea::LEFT)]   = v;
                else if (key == "TOP")    area_sizes_[static_cast<int>(DockArea::TOP)]    = v;
                else if (key == "RIGHT")  area_sizes_[static_cast<int>(DockArea::RIGHT)]  = v;
                else if (key == "BOTTOM") area_sizes_[static_cast<int>(DockArea::BOTTOM)] = v;
            }
        } else if (sec == Sec::PANEL) {
            if (key == "id") {
                std::string s;
                if (parse_toml_string_literal(value, s)) { pf.id = s; pf.has_id = true; }
            } else if (key == "area") {
                pf.area_name = value;  // 裸标识符（LEFT/CENTER/NONE）
            } else if (key == "state") {
                pf.state_name = value;
            } else if (key == "visible") {
                bool b = false;
                if (parse_toml_bool_literal(value, b)) { pf.visible = b; pf.has_visible = true; }
            } else if (key == "size") {
                float f = 0.0f;
                if (parse_toml_float_literal(value, f)) { pf.size = f; pf.has_size = true; }
            }
        }
    }
    flush_panel();

    queue_sort();
    layout_changed_.emit();
    return true;
}

// ============================================================================
// 文件 IO（save_layout / load_layout）
// ============================================================================

std::string DockManager::save_layout(const std::string& path) const {
    std::string text = serialize_layout();
    if (!path.empty()) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    }
    return text;
}

bool DockManager::load_layout(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return deserialize_layout(ss.str());
}

}  // namespace eda
