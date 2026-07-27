#include "drivers/vulkan/rasterizer_vk.h"

namespace eda {

// 所有方法的完整实现见 docs/01-引擎地基/plan/引擎地基实现计划.md Task 7。
// 当前为 stub：不调用任何真实 Vulkan API，仅缓存语义状态以保证接口可编译。

void RasterizerVK::initialize(void* /*native_window_handle*/) {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期序列：vkCreateInstance → glfwCreateWindowSurface → pick physical device
    // → vkCreateDevice → create swapchain/render_pass/pipeline/framebuffers
    // /command pool/buffers/sync objects/VBO。
    initialized_ = true;
}

void RasterizerVK::shutdown() {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：vkDeviceWaitIdle 后按创建逆序销毁所有 Vk* 资源。
    initialized_ = false;
}

void RasterizerVK::begin_frame() {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：vkWaitForFences → vkResetFences → vkAcquireNextImageKHR →
    // 录制对应 image 的 command buffer（begin render pass，loadOp 用 clear_color_）。
}

void RasterizerVK::end_frame() {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：vkQueueSubmit（wait img_available_，signal render_done_）
    // → vkQueuePresentKHR。
}

void RasterizerVK::clear(Color color) {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：缓存 clear 值，render pass 的 loadOp=CLEAR 时使用。
    clear_color_ = color;
}

void RasterizerVK::set_viewport(int x, int y, int w, int h) {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：缓存视口/裁剪矩形，绘制时通过动态 viewport/scissor 状态下发。
    viewport_x_ = x;
    viewport_y_ = y;
    viewport_w_ = w;
    viewport_h_ = h;
}

void RasterizerVK::draw_triangles(const std::vector<CanvasVertex>& /*vertices*/) {
    // TODO: Vulkan implementation deferred per REQ-013 (MVP OpenGL-only).
    // 预期实现：vkMapMemory 拷贝顶点 → vkUnmapMemory → 记录三角形计数 →
    // command buffer 录制 bind pipeline / bind vbo / vkCmdDraw。
}

}  // namespace eda
