#pragma once

// eda/geometry/units.h
//
// P3 几何内核 · 单位边界转换（Task 1）
//
// 用户层 mm/mil/inch 仅在 API 边界转成内部 nm（D1），内部流转一律 nm。
// 全部 constexpr，常量求值安全。
//
// 约定（D1）：
//   1 mm   = 1_000_000 nm（1e6）
//   1 inch = 25.4 mm     = 25_400_000 nm（25.4e6）
//   1 mil  = 0.001 inch  = 25_400 nm

#include "eda/geometry/types.h"

namespace eda::geometry {

// ---------- 用户层单位 → 内部 nm ----------

constexpr Coord mm_to_nm(double mm) { return static_cast<Coord>(mm * 1e6); }
constexpr Coord mil_to_nm(double mil) { return static_cast<Coord>(mil * 25400.0); }
constexpr Coord inch_to_nm(double in) { return static_cast<Coord>(in * 25.4e6); }

// ---------- 内部 nm → 用户层单位（显示、导出） ----------

constexpr double nm_to_mm(Coord nm) { return static_cast<double>(nm) / 1e6; }
constexpr double nm_to_mil(Coord nm) { return static_cast<double>(nm) / 25400.0; }
constexpr double nm_to_inch(Coord nm) { return static_cast<double>(nm) / 25.4e6; }

}  // namespace eda::geometry
