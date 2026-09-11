#include "vulkan_warper.h"
#include "warp_spv.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <vector>

struct PushConstants {
    float out_w;
    float out_h;
    float flow_w;
    float flow_h;
    float time_step;
    float pad;
};

VulkanWarper::VulkanWarper() {}

VulkanWarper::~VulkanWarper() {
    cleanup();
}

void VulkanWarper::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        if (stagingUploadMapped && stagingUploadMem) {
            vkUnmapMemory(device, stagingUploadMem);
            stagingUploadMapped = nullptr;
        }
        if (stagingUpload) vkDestroyBuffer(device, stagingUpload, nullptr);
        if (stagingUploadMem) vkFreeMemory(device, stagingUploadMem, nullptr);

        if (stagingDownloadMapped && stagingDownloadMem) {
            vkUnmapMemory(device, stagingDownloadMem);
            stagingDownloadMapped = nullptr;
        }
        if (stagingDownload) vkDestroyBuffer(device, stagingDownload, nullptr);
        if (stagingDownloadMem) vkFreeMemory(device, stagingDownloadMem, nullptr);

        if (img0_view) vkDestroyImageView(device, img0_view, nullptr);
        if (img0) vkDestroyImage(device, img0, nullptr);
        if (img0_mem) vkFreeMemory(device, img0_mem, nullptr);

        if (img1_view) vkDestroyImageView(device, img1_view, nullptr);
        if (img1) vkDestroyImage(device, img1, nullptr);
        if (img1_mem) vkFreeMemory(device, img1_mem, nullptr);

        if (flow_view) vkDestroyImageView(device, flow_view, nullptr);
        if (flowImg) vkDestroyImage(device, flowImg, nullptr);
        if (flow_mem) vkFreeMemory(device, flow_mem, nullptr);

        if (out_view) vkDestroyImageView(device, out_view, nullptr);
        if (outImg) vkDestroyImage(device, outImg, nullptr);
        if (out_mem) vkFreeMemory(device, out_mem, nullptr);

        if (fence) vkDestroyFence(device, fence, nullptr);
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

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    return true;
}

bool VulkanWarper::createImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = w;
    imageInfo.extent.height = h;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, image, memory, 0);
    return true;
}

VkImageView VulkanWarper::createImageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    return imageView;
}

bool VulkanWarper::createShaderModule(const uint32_t* code, size_t size, VkShaderModule& shaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = code;
    return (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);
}

bool VulkanWarper::init(int gpu_id, int max_w, int max_h, int flow_w, int flow_h) {
    m_gpu_id = gpu_id;
    m_max_w = max_w;
    m_max_h = max_h;
    m_flow_w = flow_w;
    m_flow_h = flow_h;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SkyCine FastWarp GPU";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "FastWarp";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) return false;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    int selectedIdx = (gpu_id >= 0 && gpu_id < (int)deviceCount) ? gpu_id : 0;
    physicalDevice = devices[selectedIdx];

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

    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) return false;

    vkGetDeviceQueue(device, computeQueueFamilyIndex, 0, &computeQueue);

    // Command Pool & Buffer
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = computeQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer) != VK_SUCCESS) return false;

    // Fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) return false;

    // Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler) != VK_SUCCESS) return false;

    // Shader Module
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!createShaderModule(warp_spv, sizeof(warp_spv), shaderModule)) return false;

    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    // Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    // Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";

    VkResult pipeRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    if (pipeRes != VK_SUCCESS) return false;

    // Descriptor Pool & Set
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 1;
    descPoolInfo.poolSizeCount = 2;
    descPoolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo descAllocInfo{};
    descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo.descriptorPool = descriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &descAllocInfo, &descriptorSet) != VK_SUCCESS) return false;

    // Allocate GPU Textures
    VkImageUsageFlags srcUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageUsageFlags dstUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (!createImage(max_w, max_h, VK_FORMAT_R8_UNORM, srcUsage, img0, img0_mem)) return false;
    img0_view = createImageView(img0, VK_FORMAT_R8_UNORM);

    if (!createImage(max_w, max_h, VK_FORMAT_R8_UNORM, srcUsage, img1, img1_mem)) return false;
    img1_view = createImageView(img1, VK_FORMAT_R8_UNORM);

    if (!createImage(flow_w, flow_h, VK_FORMAT_R32G32B32A32_SFLOAT, srcUsage, flowImg, flow_mem)) return false;
    flow_view = createImageView(flowImg, VK_FORMAT_R32G32B32A32_SFLOAT);

    if (!createImage(max_w, max_h, VK_FORMAT_R8_UNORM, dstUsage, outImg, out_mem)) return false;
    out_view = createImageView(outImg, VK_FORMAT_R8_UNORM);

    // Update Descriptor Set
    VkDescriptorImageInfo imageInfos[4]{};
    imageInfos[0].sampler = linearSampler;
    imageInfos[0].imageView = img0_view;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler = linearSampler;
    imageInfos[1].imageView = img1_view;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[2].sampler = linearSampler;
    imageInfos[2].imageView = flow_view;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[3].imageView = out_view;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i < 3) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);

    // Persistent Staging Buffers
    uploadSize = (VkDeviceSize)max_w * max_h * 2 + (VkDeviceSize)flow_w * flow_h * 4 * sizeof(float);
    downloadSize = (VkDeviceSize)max_w * max_h;

    VkMemoryPropertyFlags hostProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!createBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostProps, stagingUpload, stagingUploadMem)) return false;
    if (vkMapMemory(device, stagingUploadMem, 0, uploadSize, 0, &stagingUploadMapped) != VK_SUCCESS) return false;

    if (!createBuffer(downloadSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, hostProps, stagingDownload, stagingDownloadMem)) return false;
    if (vkMapMemory(device, stagingDownloadMem, 0, downloadSize, 0, &stagingDownloadMapped) != VK_SUCCESS) return false;

    return true;
}

bool VulkanWarper::warp_plane(
    const uint8_t* src0, const uint8_t* src1, uint8_t* dst,
    int pw, int ph, ptrdiff_t s_stride, ptrdiff_t d_stride,
    const float* flow_p0, const float* flow_p1, const float* flow_p2,
    int flow_w, int flow_h, ptrdiff_t flow_stride,
    float time_step
) {
    if (!device || !stagingUploadMapped || !stagingDownloadMapped) return false;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    size_t plane_bytes = (size_t)pw * ph;
    uint8_t* upload_ptr = static_cast<uint8_t*>(stagingUploadMapped);

    // Copy src0
    if (s_stride == pw) {
        std::memcpy(upload_ptr, src0, plane_bytes);
    } else {
        for (int y = 0; y < ph; y++) {
            std::memcpy(upload_ptr + (size_t)y * pw, src0 + y * s_stride, pw);
        }
    }

    // Copy src1
    uint8_t* upload_src1 = upload_ptr + plane_bytes;
    if (s_stride == pw) {
        std::memcpy(upload_src1, src1, plane_bytes);
    } else {
        for (int y = 0; y < ph; y++) {
            std::memcpy(upload_src1 + (size_t)y * pw, src1 + y * s_stride, pw);
        }
    }

    // Copy and pack flow (dx, dy, mask) into RGBA32F
    float* upload_flow = reinterpret_cast<float*>(upload_src1 + plane_bytes);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < flow_h; y++) {
        const float* r0 = flow_p0 + y * flow_stride;
        const float* r1 = flow_p1 + y * flow_stride;
        const float* r2 = flow_p2 ? (flow_p2 + y * flow_stride) : nullptr;
        float* dst_row = upload_flow + (size_t)y * flow_w * 4;

        for (int x = 0; x < flow_w; x++) {
            dst_row[x * 4 + 0] = r0[x];
            dst_row[x * 4 + 1] = r1[x];
            dst_row[x * 4 + 2] = r2 ? r2[x] : 0.5f;
            dst_row[x * 4 + 3] = 1.0f;
        }
    }

    // Record Commands
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Barrier: Transition staging buffer to transfer src & images to transfer dst
    VkImageMemoryBarrier barriers[4]{};
    for (int i = 0; i < 3; i++) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].srcAccessMask = 0;
        barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[i].subresourceRange.baseMipLevel = 0;
        barriers[i].subresourceRange.levelCount = 1;
        barriers[i].subresourceRange.baseArrayLayer = 0;
        barriers[i].subresourceRange.layerCount = 1;
    }
    barriers[0].image = img0;
    barriers[1].image = img1;
    barriers[2].image = flowImg;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 3, barriers);

    // Copy Buffer to Images
    VkBufferImageCopy copy0{};
    copy0.bufferOffset = 0;
    copy0.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy0.imageSubresource.layerCount = 1;
    copy0.imageExtent = { (uint32_t)pw, (uint32_t)ph, 1 };
    vkCmdCopyBufferToImage(cmdBuffer, stagingUpload, img0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy0);

    VkBufferImageCopy copy1{};
    copy1.bufferOffset = plane_bytes;
    copy1.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy1.imageSubresource.layerCount = 1;
    copy1.imageExtent = { (uint32_t)pw, (uint32_t)ph, 1 };
    vkCmdCopyBufferToImage(cmdBuffer, stagingUpload, img1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy1);

    VkBufferImageCopy copyFlow{};
    copyFlow.bufferOffset = plane_bytes * 2;
    copyFlow.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyFlow.imageSubresource.layerCount = 1;
    copyFlow.imageExtent = { (uint32_t)flow_w, (uint32_t)flow_h, 1 };
    vkCmdCopyBufferToImage(cmdBuffer, stagingUpload, flowImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyFlow);

    // Transition images to Shader Read / General
    VkImageMemoryBarrier computeBarriers[4]{};
    for (int i = 0; i < 4; i++) {
        computeBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        computeBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        computeBarriers[i].subresourceRange.baseMipLevel = 0;
        computeBarriers[i].subresourceRange.levelCount = 1;
        computeBarriers[i].subresourceRange.baseArrayLayer = 0;
        computeBarriers[i].subresourceRange.layerCount = 1;
    }
    computeBarriers[0].image = img0;
    computeBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    computeBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    computeBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    computeBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    computeBarriers[1].image = img1;
    computeBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    computeBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    computeBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    computeBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    computeBarriers[2].image = flowImg;
    computeBarriers[2].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    computeBarriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    computeBarriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    computeBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    computeBarriers[3].image = outImg;
    computeBarriers[3].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    computeBarriers[3].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    computeBarriers[3].srcAccessMask = 0;
    computeBarriers[3].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 4, computeBarriers);

    // Bind Pipeline & Dispatch
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    PushConstants pc{};
    pc.out_w = (float)pw;
    pc.out_h = (float)ph;
    pc.flow_w = (float)flow_w;
    pc.flow_h = (float)flow_h;
    pc.time_step = time_step;
    pc.pad = 0.0f;
    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    uint32_t groupX = (pw + 15) / 16;
    uint32_t groupY = (ph + 15) / 16;
    vkCmdDispatch(cmdBuffer, groupX, groupY, 1);

    // Barrier: outImg general to transfer src
    VkImageMemoryBarrier readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    readBarrier.image = outImg;
    readBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    readBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    readBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    readBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    readBarrier.subresourceRange.baseMipLevel = 0;
    readBarrier.subresourceRange.levelCount = 1;
    readBarrier.subresourceRange.baseArrayLayer = 0;
    readBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &readBarrier);

    // Copy outImg to staging download buffer
    VkBufferImageCopy downloadCopy{};
    downloadCopy.bufferOffset = 0;
    downloadCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    downloadCopy.imageSubresource.layerCount = 1;
    downloadCopy.imageExtent = { (uint32_t)pw, (uint32_t)ph, 1 };
    vkCmdCopyImageToBuffer(cmdBuffer, outImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingDownload, 1, &downloadCopy);

    vkEndCommandBuffer(cmdBuffer);

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    if (vkQueueSubmit(computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) return false;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Copy readback to dst
    const uint8_t* download_ptr = static_cast<const uint8_t*>(stagingDownloadMapped);
    if (d_stride == pw) {
        std::memcpy(dst, download_ptr, plane_bytes);
    } else {
        for (int y = 0; y < ph; y++) {
            std::memcpy(dst + y * d_stride, download_ptr + (size_t)y * pw, pw);
        }
    }

    return true;
}
