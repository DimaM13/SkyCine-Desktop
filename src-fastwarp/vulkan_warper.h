#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstddef>

class VulkanWarper {
public:
    VulkanWarper();
    ~VulkanWarper();

    bool init(int gpu_id, int max_w, int max_h, int flow_w, int flow_h);
    void cleanup();

    bool warp_plane(
        const uint8_t* src0, const uint8_t* src1, uint8_t* dst,
        int pw, int ph, ptrdiff_t s_stride, ptrdiff_t d_stride,
        const float* flow_p0, const float* flow_p1, const float* flow_p2,
        int flow_w, int flow_h, ptrdiff_t flow_stride,
        float time_step
    );

private:
    int m_gpu_id = 0;
    int m_max_w = 0;
    int m_max_h = 0;
    int m_flow_w = 0;
    int m_flow_h = 0;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamilyIndex = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler linearSampler = VK_NULL_HANDLE;

    // Persistent GPU Image Resources
    VkImage img0 = VK_NULL_HANDLE;
    VkDeviceMemory img0_mem = VK_NULL_HANDLE;
    VkImageView img0_view = VK_NULL_HANDLE;

    VkImage img1 = VK_NULL_HANDLE;
    VkDeviceMemory img1_mem = VK_NULL_HANDLE;
    VkImageView img1_view = VK_NULL_HANDLE;

    VkImage flowImg = VK_NULL_HANDLE;
    VkDeviceMemory flow_mem = VK_NULL_HANDLE;
    VkImageView flow_view = VK_NULL_HANDLE;

    VkImage outImg = VK_NULL_HANDLE;
    VkDeviceMemory out_mem = VK_NULL_HANDLE;
    VkImageView out_view = VK_NULL_HANDLE;

    // Staging Buffers for zero-allocation DMA transfer
    VkDeviceSize uploadSize = 0;
    VkBuffer stagingUpload = VK_NULL_HANDLE;
    VkDeviceMemory stagingUploadMem = VK_NULL_HANDLE;
    void* stagingUploadMapped = nullptr;

    VkDeviceSize downloadSize = 0;
    VkBuffer stagingDownload = VK_NULL_HANDLE;
    VkDeviceMemory stagingDownloadMem = VK_NULL_HANDLE;
    void* stagingDownloadMapped = nullptr;

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    bool createImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool createShaderModule(const uint32_t* code, size_t size, VkShaderModule& shaderModule);
};
