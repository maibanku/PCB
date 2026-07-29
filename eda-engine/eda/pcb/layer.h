#pragma once

// eda/pcb/layer.h
//
// P8 PCB 编辑器 · 图层定义与叠层管理器（08-02 图层体系与叠层管理器）。
//
// 图层分类（参考 08-02 README）：
//   - 电气层（Copper）：Top / Inner1..N / Bottom，可作信号层或平面层（Power/GND）。
//   - 非电气层：阻焊（Solder Mask）、丝印（Silkscreen）、锡膏（Paste）、钻孔（Drill）、
//     文档层、用户自定义层。
//
// Layer 是"定义"而非"放置图元"——不继承 PcbEntity（六属性契约适用于 placed primitive，
// Layer 的 UUID/序列化由 Stackup / Board 顶层统一管理）。Stackup 持有 Layer 列表，
// 提供 08-02 叠层管理器的数据骨架（可视化截面 / 添加 / 删除 / 重排 / 阻抗参数占位）。
//
// 命名规范：namespace eda::pcb、PascalCase、snake_case、成员尾下划线、include 从仓库根。

#include <cstdint>
#include <string>
#include <vector>

namespace eda::pcb {

// 图层类型枚举（08-02）。序列化字符串见 layer_type_string / parse_layer_type。
enum class LayerType {
    COPPER,        // 铜层（信号 / 平面）
    SOLDER_MASK,   // 阻焊
    SILKSCREEN,    // 丝印
    PASTE,         // 锡膏（钢网）
    DRILL,         // 钻孔
    DOCUMENT,      // 文档 / 尺寸标注
    USER,          // 用户自定义
};

// LayerType ↔ 字符串（序列化用）。
std::string layer_type_string(LayerType t);
LayerType parse_layer_type(const std::string& s);

// ============================================================================
// Layer —— 单个图层定义。
// layer_id 为对外稳定标识（如 "F.Cu" / "B.Cu" / "F.Mask" / "F.Silk"），
// 对齐 KiCad/嘉立创专业版约定（IPC 用户层命名）。
// copper_index 仅对 COPPER 类型有意义：0=Top，N-1=Bottom，中间为内层序号。
// ============================================================================
class Layer {
public:
    Layer() = default;
    Layer(std::string layer_id, LayerType type, int copper_index = -1)
        : layer_id_(std::move(layer_id)), type_(type), copper_index_(copper_index) {}

    const std::string& layer_id() const { return layer_id_; }
    LayerType type() const { return type_; }
    int copper_index() const { return copper_index_; }

    void set_layer_id(std::string id) { layer_id_ = std::move(id); }
    void set_type(LayerType t) { type_ = t; }
    void set_copper_index(int idx) { copper_index_ = idx; }

    // 是否为电气层（铜层）。
    bool is_copper() const { return type_ == LayerType::COPPER; }

private:
    std::string layer_id_;        // 稳定标识，如 "F.Cu"
    LayerType type_ = LayerType::COPPER;
    int copper_index_ = -1;       // 铜层序号：0=Top, N-1=Bottom, -1=非铜层
};

// ============================================================================
// Stackup —— 叠层管理器（08-02）数据骨架。
// 持有有序 Layer 列表，提供添加 / 删除 / 查询；介质参数 / 阻抗模型占位（08-02 后续 Task）。
// ============================================================================
class Stackup {
public:
    Stackup() = default;

    void add_layer(Layer l) { layers_.push_back(std::move(l)); }

    std::size_t layer_count() const { return layers_.size(); }
    const Layer& layer(std::size_t i) const { return layers_[i]; }
    Layer& layer(std::size_t i) { return layers_[i]; }
    const std::vector<Layer>& layers() const { return layers_; }

    // 按层 ID 查找（找不到返回 nullptr）。
    const Layer* find(const std::string& layer_id) const;

    // 铜层数量（用于过孔默认层对、阻抗计算）。
    std::size_t copper_count() const;

private:
    std::vector<Layer> layers_;
};

}  // namespace eda::pcb
