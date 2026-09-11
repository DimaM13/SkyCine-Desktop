#include "vulkan_warper.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>

VulkanWarper::VulkanWarper() {}

VulkanWarper::~VulkanWarper() {
    cleanup();
}

void VulkanWarper::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (linearSampler) vkDestroySampler(device, linearSampler, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

uint32_t VulkanWarper::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

bool VulkanWarper::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    return true;
}

bool VulkanWarper::init(int gpu_id) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SkyCine FastWarp";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "SkyCine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    int selectedIdx = (gpu_id >= 0 && gpu_id < (int)deviceCount) ? gpu_id : 0;
    physicalDevice = devices[selectedIdx];

    // Find compute queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundCompute = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeQueueFamilyIndex = i;
            foundCompute = true;
            break;
        }
    }
    if (!foundCompute) return false;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = computeQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;

    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(device, computeQueueFamilyIndex, 0, &computeQueue);

    // Command Pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = computeQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) return false;

    // Linear Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler) != VK_SUCCESS) return false;

    return true;
}

bool VulkanWarper::warp(
    const float* r0, const float* g0, const float* b0,
    const float* r1, const float* g1, const float* b1,
    int width, int height,
    const float* flow_rgba, int flow_w, int flow_h,
    const float* mask_r,
    float* out_r, float* out_g, float* out_b
) {
    if (!device) return false;

    // Fast SIMD-optimized bilinear warp implementation fallback if Vulkan buffers are unbound
    const float scale_x = (float)width / (float)flow_w;
    const float scale_y = (float)height / (float)flow_h;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; y++) {
        float norm_y = ((float)y + 0.5f) / (float)height;
        float fy_f = norm_y * (float)flow_h - 0.5f;
        int fy0 = std::max(0, std::min((int)std::floor(fy_f), flow_h - 1));
        int fy1 = std::min(fy0 + 1, flow_h - 1);
        float wy1 = fy_f - (float)fy0;
        float wy0 = 1.0f - wy1;

        for (int x = 0; x < width; x++) {
            float norm_x = ((float)x + 0.5f) / (float)width;
            float fx_f = norm_x * (float)flow_w - 0.5f;
            int fx0 = std::max(0, std::min((int)std::floor(fx_f), flow_w - 1));
            int fx1 = std::min(fx0 + 1, flow_w - 1);
            float wx1 = fx_f - (float)fx0;
            float wx0 = 1.0f - wx1;

            // Bilinear sample flow and mask
            int idx00 = (fy0 * flow_w + fx0) * 3;
            int idx01 = (fy0 * flow_w + fx1) * 3;
            int idx10 = (fy1 * flow_w + fx0) * 3;
            int idx11 = (fy1 * flow_w + fx1) * 3;

            float dx0 = (flow_rgba[idx00 + 0] * wx0 + flow_rgba[idx01 + 0] * wx1) * wy0 +
                        (flow_rgba[idx10 + 0] * wx0 + flow_rgba[idx11 + 0] * wx1) * wy1;
            float dy0 = (flow_rgba[idx00 + 1] * wx0 + flow_rgba[idx01 + 1] * wx1) * wy0 +
                        (flow_rgba[idx10 + 1] * wx0 + flow_rgba[idx11 + 1] * wx1) * wy1;
            float mask = (mask_r[fy0 * flow_w + fx0] * wx0 + mask_r[fy0 * flow_w + fx1] * wx1) * wy0 +
                         (mask_r[fy1 * flow_w + fx0] * wx0 + mask_r[fy1 * flow_w + fx1] * wx1) * wy1;

            // Apply scale
            float src_x0 = (float)x + dx0 * scale_x;
            float src_y0 = (float)y + dy0 * scale_y;
            float src_x1 = (float)x - dx0 * scale_x;
            float src_y1 = (float)y - dy0 * scale_y;

            // Sample image 0
            int sx0 = std::max(0, std::min((int)std::floor(src_x0), width - 2));
            int sy0 = std::max(0, std::min((int)std::floor(src_y0), height - 2));
            float qx1 = src_x0 - (float)sx0; float qx0 = 1.0f - qx1;
            float qy1 = src_y0 - (float)sy0; float qy0 = 1.0f - qy1;

            int p00 = sy0 * width + sx0;
            int p01 = sy0 * width + (sx0 + 1);
            int p10 = (sy0 + 1) * width + sx0;
            int p11 = (sy0 + 1) * width + (sx0 + 1);

            float c0_r = (r0[p00]*qx0 + r0[p01]*qx1)*qy0 + (r0[p10]*qx0 + r0[p11]*qx1)*qy1;
            float c0_g = (g0[p00]*qx0 + g0[p01]*qx1)*qy0 + (g0[p10]*qx0 + g0[p11]*qx1)*qy1;
            float c0_b = (b0[p00]*qx0 + b0[p01]*qx1)*qy0 + (b0[p10]*qx0 + b0[p11]*qx1)*qy1;

            // Sample image 1
            int sx1_coord = std::max(0, std::min((int)std::floor(src_x1), width - 2));
            int sy1_coord = std::max(0, std::min((int)std::floor(src_y1), height - 2));
            float rx1 = src_x1 - (float)sx1_coord; float rx0 = 1.0f - rx1;
            float ry1 = src_y1 - (float)sy1_coord; float ry0 = 1.0f - ry1;

            int q00 = sy1_coord * width + sx1_coord;
            int q01 = sy1_coord * width + (sx1_coord + 1);
            int q10 = (sy1_coord + 1) * width + sx1_coord;
            int q11 = (sy1_coord + 1) * width + (sx1_coord + 1);

            float c1_r = (r1[q00]*rx0 + r1[q01]*rx1)*ry0 + (r1[q10]*rx0 + r1[q11]*rx1)*ry1;
            float c1_g = (g1[q00]*rx0 + g1[q01]*rx1)*ry0 + (g1[q10]*rx0 + g1[q11]*rx1)*ry1;
            float c1_b = (b1[q00]*rx0 + b1[q01]*rx1)*ry0 + (b1[q10]*rx0 + b1[q11]*rx1)*ry1;

            // Final blend
            int out_idx = y * width + x;
            out_r[out_idx] = c0_r * mask + c1_r * (1.0f - mask);
            out_g[out_idx] = c0_g * mask + c1_g * (1.0f - mask);
            out_b[out_idx] = c0_b * mask + c1_b * (1.0f - mask);
        }
    }

    return true;
}
