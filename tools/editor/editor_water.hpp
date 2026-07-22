#pragma once

#include "pipeline/adt_loader.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace wowee {
namespace rendering { class VkContext; }

namespace editor {

class EditorWater {
public:
    EditorWater();
    ~EditorWater();

    bool initialize(rendering::VkContext* ctx, VkRenderPass renderPass,
                    VkDescriptorSetLayout perFrameLayout);
    void shutdown();

    void update(const pipeline::ADTTerrain& terrain, int tileX, int tileY);
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet);

    void clear();
    VkPipeline getPipeline() const { return pipeline_; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }

private:
    bool createPipeline();

    rendering::VkContext* vkCtx_ = nullptr;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout perFrameLayout_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;

    struct WaterVertex {
        float pos[3];
        float color[4];
    };
};

} // namespace editor
} // namespace wowee
