#pragma once

#include "core/math/color.h"
#include "servers/canvas_vertex.h"

#include <vector>

namespace eda {

// 渲染后端枚举 —— 决定窗口 client API 与 drivers/* 实现选择。
enum class RenderingDriver { OPENGL, VULKAN };

// RenderingServer —— 后端无关的 GPU 契约（对齐 Godot servers/RenderingServer 的精简版）。
//
// 本接口仅暴露 2D 画布所需的最小绘制能力；完整 RID 资源管理
// （canvas_item_*、material_* 等）留到 Plan 2/3 扩展。
//
// 关键约束：本头文件绝不 include <GL/gl.h> / <vulkan/vulkan.h> 等 GPU 头文件；
// driver 实现归 drivers/opengl（Task 6）与 drivers/vulkan（Task 7）。
// 业务层（main）只持有 RenderingServer*，永不直接碰 driver。
class RenderingServer {
public:
    virtual ~RenderingServer() = default;

    // —— 生命周期 ——
    virtual RenderingDriver backend() const = 0;
    virtual void initialize(void* native_window_handle) = 0;
    virtual void shutdown() = 0;

    // —— 帧边界 ——
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    // —— 帧内绘制 ——
    virtual void clear(Color color) = 0;
    virtual void set_viewport(int x, int y, int w, int h) = 0;

    // Canvas2D 唯一绘制入口：上传顶点并绘制三角形列表。
    virtual void draw_triangles(const std::vector<CanvasVertex>& vertices) = 0;
};

}  // namespace eda
