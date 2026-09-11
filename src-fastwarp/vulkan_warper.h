#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstddef>

struct PlaneInfo {
    const uint8_t* s0;
    const uint8_t* s1;
    uint8_t* dst;
    int w;
    int h;
    ptrdiff_t s_stride;
    ptrdiff_t d_stride;
};

struct FlowInfo {
    const float* p0;
    const float* p1;
    const float* p2;
    int w;
    int h;
    ptrdiff_t stride;
};

class VulkanWarper {
public:
    VulkanWarper();
    ~VulkanWarper();

    bool init(int gpu_id, int width, int height, int flow_w, int flow_h);
    void cleanup();

    bool warp_frame_yuv420(
        const PlaneInfo& y,
        const PlaneInfo& u,
        const PlaneInfo& v,
        const FlowInfo& flow,
        float time_step
    );

private:
    int m_gpu_id = 0;
    int m_width = 0;
    int m_height = 0;
    int m_uv_w = 0;
    int m_uv_h = 0;
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
    VkDescriptorSet descSetY = VK_NULL_HANDLE;
    VkDescriptorSet descSetU = VK_NULL_HANDLE;
    VkDescriptorSet descSetV = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler linearSampler = VK_NULL_HANDLE;

    // Y Plane Images
    VkImage img0_y = VK_NULL_HANDLE; VkDeviceMemory img0_y_mem = VK_NULL_HANDLE; VkImageView img0_y_view = VK_NULL_HANDLE;
    VkImage img1_y = VK_NULL_HANDLE; VkDeviceMemory img1_y_mem = VK_NULL_HANDLE; VkImageView img1_y_view = VK_NULL_HANDLE;
    VkImage out_y = VK_NULL_HANDLE; VkDeviceMemory out_y_mem = VK_NULL_HANDLE; VkImageView out_y_view = VK_NULL_HANDLE;

    // U Plane Images
    VkImage img0_u = VK_NULL_HANDLE; VkDeviceMemory img0_u_mem = VK_NULL_HANDLE; VkImageView img0_u_view = VK_NULL_HANDLE;
    VkImage img1_u = VK_NULL_HANDLE; VkDeviceMemory img1_u_mem = VK_NULL_HANDLE; VkImageView img1_u_view = VK_NULL_HANDLE;
    VkImage out_u = VK_NULL_HANDLE; VkDeviceMemory out_u_mem = VK_NULL_HANDLE; VkImageView out_u_view = VK_NULL_HANDLE;

    // V Plane Images
    VkImage img0_v = VK_NULL_HANDLE; VkDeviceMemory img0_v_mem = VK_NULL_HANDLE; VkImageView img0_v_view = VK_NULL_HANDLE;
    VkImage img1_v = VK_NULL_HANDLE; VkDeviceMemory img1_v_mem = VK_NULL_HANDLE; VkImageView img1_v_view = VK_NULL_HANDLE;
    VkImage out_v = VK_NULL_HANDLE; VkDeviceMemory out_v_mem = VK_NULL_HANDLE; VkImageView out_v_view = VK_NULL_HANDLE;

    // Flow Texture
    VkImage flowImg = VK_NULL_HANDLE; VkDeviceMemory flow_mem = VK_NULL_HANDLE; VkImageView flow_view = VK_NULL_HANDLE;

    // Unified Staging Buffers
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
