#pragma once

#include "servers/rendering_server.h"

#include <vector>

namespace eda {

// RasterizerVK —— Vulkan 后端渲染驱动（接口骨架）。
//
// 评审 REQ-013：MVP 阶段仅启用 OpenGL 后端，Vulkan 实现后置。
// 本类为接口骨架：实现 RenderingServer 全部纯虚方法，但所有 GPU 调用
// 暂为空 stub —— 不 include <vulkan/vulkan.h>，不依赖 Vulkan SDK，可独立编译。
//
// 待 Vulkan 后端正式落地时，按 docs/01-引擎地基/plan/引擎地基实现计划.md
// Task 7 的标准序列补全各方法：
//   instance → surface → physical device → logical device → swapchain →
//   render pass → pipeline → framebuffers → command pool/buffers →
//   sync objects → vertex buffer。
class RasterizerVK : public RenderingServer {
public:
    RenderingDriver backend() const override { return RenderingDriver::VULKAN; }

    void initialize(void* native_window_handle) override;
    void shutdown() override;

    void begin_frame() override;
    void end_frame() override;
    void clear(Color color) override;
    void set_viewport(int x, int y, int w, int h) override;

    void draw_triangles(const std::vector<CanvasVertex>& vertices) override;

private:
    // 仅缓存语义状态，供后续 Vulkan 实现复用；当前 stub 不提交任何 GPU 工作。
    bool initialized_ = false;
    Color clear_color_{};
    int viewport_x_ = 0;
    int viewport_y_ = 0;
    int viewport_w_ = 0;
    int viewport_h_ = 0;
};

}  // namespace eda
