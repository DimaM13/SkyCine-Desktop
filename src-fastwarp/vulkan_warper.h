#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class VulkanWarper {
public:
    VulkanWarper();
    ~VulkanWarper();

    bool init(int gpu_id = 0);
    void cleanup();

    bool warp(
        const float* r0, const float* g0, const float* b0,
        const float* r1, const float* g1, const float* b1,
        int width, int height,
        const float* flow_rgba, int flow_w, int flow_h,
        const float* mask_r,
        float* out_r, float* out_g, float* out_b
    );

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamilyIndex = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler linearSampler = VK_NULL_HANDLE;

    // Buffer helper
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool createShaderModule(const uint32_t* code, size_t size, VkShaderModule& shaderModule);
};
