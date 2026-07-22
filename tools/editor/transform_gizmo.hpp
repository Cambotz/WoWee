#pragma once

#include "rendering/camera.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace wowee {
namespace rendering { class VkContext; }

namespace editor {

enum class TransformMode { None, Move, Rotate, Scale };
enum class TransformAxis { All, X, Y, Z };

class TransformGizmo {
public:
    TransformGizmo();
    ~TransformGizmo();

    bool initialize(rendering::VkContext* ctx, VkRenderPass renderPass,
                    VkDescriptorSetLayout perFrameLayout);
    void shutdown();

    void setTarget(const glm::vec3& position, float scale = 1.0f);
    void setMode(TransformMode mode) { mode_ = mode; }
    TransformMode getMode() const { return mode_; }
    void setAxis(TransformAxis axis) { axis_ = axis; }
    TransformAxis getAxis() const { return axis_; }
    bool isActive() const { return mode_ != TransformMode::None; }

    // Begin/end drag
    void beginDrag(const glm::vec2& screenPos);
    void updateDrag(const glm::vec2& screenPos, const rendering::Camera& camera,
                    float screenW, float screenH);
    void endDrag();
    bool isDragging() const { return dragging_; }

    // Get accumulated transform delta since beginDrag
    glm::vec3 getMoveDelta() const { return moveDelta_; }
    glm::vec3 getRotateDelta() const { return rotateDelta_; }
    float getScaleDelta() const { return scaleDelta_; }

    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet);

private:
    bool createPipeline();
    void updateBuffers();

    rendering::VkContext* vkCtx_ = nullptr;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout perFrameLayout_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;

    TransformMode mode_ = TransformMode::None;
    TransformAxis axis_ = TransformAxis::All;
    glm::vec3 targetPos_{0};
    float targetScale_ = 1.0f;
    bool visible_ = false;

    bool dragging_ = false;
    glm::vec2 dragStart_{0};
    glm::vec2 dragCurrent_{0};
    glm::vec3 moveDelta_{0};
    glm::vec3 rotateDelta_{0};
    float scaleDelta_ = 0.0f;

    struct GizmoVertex { float pos[3]; float color[4]; };
};

} // namespace editor
} // namespace wowee
