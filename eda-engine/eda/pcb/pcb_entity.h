#pragma once

// eda/pcb/pcb_entity.h
//
// P8 PCB 编辑器 · 对象树基类 PcbEntity（REQ-008 六属性契约）。
//
// 评审 REQ-008：Board > Layer > {Track, Pad, Via, Footprint, Net}，每对象实现
//   UUID / Geometry / Properties / Constraints / History / Serialization 六属性契约。
// 本头定义所有 PCB 图元的公共基类 PcbEntity，派生类（Track/Pad/Via/Net/Board）
// 各自实现 geometry/properties/serialize 的特化。
//
// 六属性契约（与 P2 / P3 集成）：
//   1. UUID          —— 构造时分配稳定 UUID（eda::pcb::generate_uuid），重命名/移动
//                       不破坏引用（ECO/网表/选择集的引用主键）。
//   2. Geometry      —— bounding_box() 返回 AABB（geometry::Box，int64 nm 坐标），
//                       供 P3 R-tree 空间索引、DRC 邻域预筛选、视图剔除使用。
//   3. Properties    —— properties() 返回 PropertySet（name → Variant），承载参数系统
//                       （REQ-010：value/type/unit/visibility/editable，本期落 value）。
//   4. Constraints   —— constraints() 占位，返回 Constraint*（REQ-011：作用域+求值+
//                       优先级合并，待 07-核心数据模型 / P8-03 落地具体子类）。
//   5. History       —— history() 挂接 eda::command::UndoStack（P3 命令栈）；编辑动作
//                       经 stack->push(Command) 落地，自动获得 undo/redo 能力。
//   6. Serialization —— serialize()/deserialize() 基于 Variant JSON 往返（P2 Task 3），
//                       Git-diff 友好，schema 版本化由 Board 顶层字段承载。
//
// 与 P2 ClassDB 集成：
//   - 继承 eda::Object；override get_class / is_class / get_class_static 以便 cast_to
//     与调试。注册到 ClassDB 可选（GDREGISTER_CLASS_WITH），不在本骨架强制。
//   - notification(int) 接 ClassDB 生命周期（PREDELETE / POSTINITIALIZE）。
//   - 自定义属性走 set_property / get_property（写入 custom_properties_），兼容
//     eda::command::SetPropertyCommand 的通用属性变更通道（先查 ClassDB，回落 Object::set/get）。
//
// 命名规范：namespace eda::pcb、PascalCase 类型、snake_case 方法、成员尾下划线 _、
//           include 从仓库根（#include "eda/pcb/pcb_entity.h"）。

#include <cstdint>
#include <string>
#include <unordered_map>

#include "core/object/object.h"
#include "core/object/variant.h"
#include "eda/geometry/types.h"

namespace eda::command {
class UndoStack;  // 前向声明，避免在头中拖入 signal.h / command.h 的全部重量
}

namespace eda::pcb {

// ============================================================================
// UUID v4 生成 / 校验（与 eda::generate_uuid 同算法，但内置于 pcb 命名空间，
// 避免 eda_pcb 被迫链接 eda_project——见 eda/pcb/CMakeLists.txt 链接边界）。
// ============================================================================
std::string generate_uuid();
bool is_valid_uuid(const std::string& s);

// ============================================================================
// Constraint —— 约束抽象占位（REQ-011）。
// 当前仅作骨架返回类型；P8-03 / 07-核心数据模型 落地后派生为 ClearanceConstraint /
// WidthConstraint / LengthMatchingConstraint / ImpedanceConstraint 等。
// ============================================================================
class Constraint {
public:
    virtual ~Constraint() = default;
    virtual std::string describe() const { return "Constraint"; }
};

// ============================================================================
// PropertySet —— REQ-010 参数系统的值承载（name → Variant）。
// 本期为单字段 value（Variant 承载类型擦除值）；后续扩展为
//   struct Property { Variant value; std::string unit; bool visible; bool editable; };
// 时无需改动调用点（仍以 name 为键）。
// ============================================================================
using PropertySet = std::unordered_map<std::string, Variant>;

// ============================================================================
// PcbEntity —— PCB 对象树公共基类（REQ-008 六属性契约）。
// 抽象类：bounding_box() 由派生类实现。
// ============================================================================
class PcbEntity : public Object {
public:
    PcbEntity() = default;
    ~PcbEntity() override = default;

    PcbEntity(const PcbEntity&) = delete;
    PcbEntity& operator=(const PcbEntity&) = delete;

    // ---------------- 1. UUID（稳定主键）----------------
    // 派生类构造时通过 uuid_ = generate_uuid() 分配。
    const std::string& uuid() const { return uuid_; }
    // 测试专用：注入固定 UUID 验证确定性输出（生产代码勿用）。
    void assign_uuid_for_test(const std::string& u) { uuid_ = u; }

    // ---------------- 2. Geometry（包围盒）----------------
    // 轴对齐包围盒（nm 坐标）。派生类根据自身几何计算。
    virtual geometry::Box bounding_box() const = 0;

    // ---------------- 3. Properties（参数集）----------------
    // 返回当前对象的属性快照（派生类聚合自身字段 + custom_properties_）。
    // 默认实现返回 custom_properties_。
    virtual PropertySet properties() const;

    // 自定义属性读写（落入 custom_properties_；亦兼容 SetPropertyCommand）。
    void set_property(const std::string& name, const Variant& value);
    Variant get_property(const std::string& name,
                         const Variant& default_value = Variant()) const;

    // ---------------- 4. Constraints（占位）----------------
    // 派生类可覆写返回具体约束对象。默认无约束。
    virtual Constraint* constraints() { return nullptr; }
    const Constraint* constraints() const {
        return const_cast<PcbEntity*>(this)->constraints();
    }

    // ---------------- 5. History（命令栈挂点）----------------
    // set_history 后，派生类编辑动作可经 stack->push(...) 落地，
    // 自动获得 undo/redo（接 P3 UndoStack）。
    void set_history(command::UndoStack* stack) { history_ = stack; }
    command::UndoStack* history() const { return history_; }

    // ---------------- 6. Serialization（文本格式）----------------
    // 基于 Variant JSON 往返（P2 Task 3）。派生类覆写时先调 write_base/read_base。
    virtual std::string serialize() const;
    virtual bool deserialize(const std::string& text);

    // ---------------- ClassDB 元信息 ----------------
    static std::string get_class_static() { return "PcbEntity"; }
    std::string get_class() const override { return "PcbEntity"; }
    bool is_class(const std::string& n) const override {
        return n == "PcbEntity" || Object::is_class(n);
    }

protected:
    // 派生类 serialize 实现工具：写入 type/uuid/custom 三项基类字段。
    // 派生类先调用本方法填充字典，再追加自身字段，最后用 Variant(d).serialize() 输出。
    void write_base(std::unordered_map<std::string, Variant>& d) const;

    // 派生类 deserialize 实现工具：从反序列化后的 Variant 读取 uuid/custom。
    // 返回 false 表示输入不是 DICTIONARY。派生类继续读取自身字段。
    bool read_base(const Variant& v);

    std::string uuid_;
    PropertySet custom_properties_;
    command::UndoStack* history_ = nullptr;
};

}  // namespace eda::pcb
