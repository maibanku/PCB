#pragma once

// eda/geometry/boolean.h
//
// P3 几何内核 · Task 3：布尔运算（Clipper2 薄包装）。
//
// 对应 plan「几何与命令栈实现计划.md」Task 3 与子功能 README「02-布尔运算」。
// 对两个 Polygon（外环 + 孔）执行 union / difference / intersection / xor，
// 返回 vector<Polygon>。服务于铺铜合并、挖空避让、板框叠加、拼板。
//
// 设计要点（D4 薄包装原则）：
//   - 业务层只 include 本头，永不直接 include <clipper2/clipper.h>；
//     Clipper2 完全封装在 boolean.cpp 内，可在不影响业务的前提下替换实现。
//   - 坐标基础 D1：eda::geometry::Coord == int64_t（nm）与 Clipper2 的
//     Point64（int64_t）一一对应，透传零精度损失，不做任何缩放。
//   - FillRule 固定 NonZero：与 plan「环方向约定：外环 CCW，孔 CW」配合，
//     多个外环 + 孔送入同一 Paths64 即可正确叠加与挖孔。
//   - 含孔结果：通过 PolyTree64 还原 outer/holes 层级——每个 outer 节点
//     成为一个 Polygon，其直接子节点（hole）填入 holes；嵌套岛（hole 内
//     的 outer）递归为独立 Polygon，保证任意深度的嵌套都能正确摊平。
//
// 命名注记：异或函数命名为 xor_op 而非 xor——后者是 C++ 保留替代记号
// （[lex.digraph]/2，等同于 ^ 运算符），不能作为标识符。MSVC /permissive-
// 下同样禁止。
//
// 返回值约定：结果可能包含 0、1 或多个 Polygon：
//   - 空结果：两输入无重叠且运算后无面积（如不相交的 intersect、被完全
//     包含的 subtract）。
//   - 多结果：不相交输入的 unite、或 XOR 把重叠区域扣除断成多块。
//   - 单结果：常规重叠情形。
// 输入退化（outer < 3 点）静默跳过；所有输入都退化时返回空。
//
// 这些纯查询函数无需进命令栈（plan「全局约束」第 5 条）。

#include "eda/geometry/types.h"

#include <vector>

namespace eda::geometry {

// 并集 a ∪ b。两多边形（含各自孔）合并为一个或多个结果多边形。
// 含孔输入的孔在结果中按 NonZero 规则正确合并 / 抵消。
std::vector<Polygon> unite(const Polygon& a, const Polygon& b);

// 差集 a \ b。从 a 中挖去与 b 重叠的部分；结果可能为空或多块。
// b 的孔在差集中表现为"不挖除"区域（b 的孔不属于 b，故不从 a 扣除）。
std::vector<Polygon> subtract(const Polygon& a, const Polygon& b);

// 交集 a ∩ b。仅保留两多边形共同覆盖的区域；不相交时返回空。
std::vector<Polygon> intersect(const Polygon& a, const Polygon& b);

// 对称差 a ⊕ b（即 (a ∪ b) \ (a ∩ b)）。仅在恰好一方覆盖的区域保留。
// 命名 xor_op：xor 是 C++ 保留替代记号，不能作为标识符（详见文件头注记）。
std::vector<Polygon> xor_op(const Polygon& a, const Polygon& b);

}  // namespace eda::geometry
