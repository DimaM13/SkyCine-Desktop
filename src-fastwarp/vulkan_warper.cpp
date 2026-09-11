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

        auto destroyImg = [this](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            if (view) vkDestroyImageView(device, view, nullptr);
            if (img) vkDestroyImage(device, img, nullptr);
            if (mem) vkFreeMemory(device, mem, nullptr);
            view = VK_NULL_HANDLE; img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
        };

        destroyImg(img0_y, img0_y_mem, img0_y_view);
        destroyImg(img1_y, img1_y_mem, img1_y_view);
        destroyImg(out_y, out_y_mem, out_y_view);

        destroyImg(img0_u, img0_u_mem, img0_u_view);
        destroyImg(img1_u, img1_u_mem, img1_u_view);
        destroyImg(out_u, out_u_mem, out_u_view);

        destroyImg(img0_v, img0_v_mem, img0_v_view);
        destroyImg(img1_v, img1_v_mem, img1_v_view);
        destroyImg(out_v, out_v_mem, out_v_view);

        destroyImg(flowImg, flow_mem, flow_view);

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

bool VulkanWarper::init(int gpu_id, int width, int height, int flow_w, int flow_h) {
    m_gpu_id = gpu_id;
    m_width = width;
    m_height = height;
    m_uv_w = width / 2;
    m_uv_h = height / 2;
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
    for (int i = 0; i < 3; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
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

    // Descriptor Pool
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 9;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 3;
    descPoolInfo.poolSizeCount = 2;
    descPoolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts[3] = { descriptorSetLayout, descriptorSetLayout, descriptorSetLayout };
    VkDescriptorSetAllocateInfo descAllocInfo{};
    descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo.descriptorPool = descriptorPool;
    descAllocInfo.descriptorSetCount = 3;
    descAllocInfo.pSetLayouts = layouts;
    VkDescriptorSet descSets[3];
    if (vkAllocateDescriptorSets(device, &descAllocInfo, descSets) != VK_SUCCESS) return false;
    descSetY = descSets[0];
    descSetU = descSets[1];
    descSetV = descSets[2];

    // Allocate GPU Images
    VkImageUsageFlags srcUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageUsageFlags dstUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    // Y Plane (width x height)
    if (!createImage(width, height, VK_FORMAT_R8_UNORM, srcUsage, img0_y, img0_y_mem)) return false;
    img0_y_view = createImageView(img0_y, VK_FORMAT_R8_UNORM);
    if (!createImage(width, height, VK_FORMAT_R8_UNORM, srcUsage, img1_y, img1_y_mem)) return false;
    img1_y_view = createImageView(img1_y, VK_FORMAT_R8_UNORM);
    if (!createImage(width, height, VK_FORMAT_R8_UNORM, dstUsage, out_y, out_y_mem)) return false;
    out_y_view = createImageView(out_y, VK_FORMAT_R8_UNORM);

    // U Plane (uv_w x uv_h)
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, srcUsage, img0_u, img0_u_mem)) return false;
    img0_u_view = createImageView(img0_u, VK_FORMAT_R8_UNORM);
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, srcUsage, img1_u, img1_u_mem)) return false;
    img1_u_view = createImageView(img1_u, VK_FORMAT_R8_UNORM);
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, dstUsage, out_u, out_u_mem)) return false;
    out_u_view = createImageView(out_u, VK_FORMAT_R8_UNORM);

    // V Plane (uv_w x uv_h)
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, srcUsage, img0_v, img0_v_mem)) return false;
    img0_v_view = createImageView(img0_v, VK_FORMAT_R8_UNORM);
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, srcUsage, img1_v, img1_v_mem)) return false;
    img1_v_view = createImageView(img1_v, VK_FORMAT_R8_UNORM);
    if (!createImage(m_uv_w, m_uv_h, VK_FORMAT_R8_UNORM, dstUsage, out_v, out_v_mem)) return false;
    out_v_view = createImageView(out_v, VK_FORMAT_R8_UNORM);

    // Flow Image (flow_w x flow_h, RGBA32F)
    if (!createImage(flow_w, flow_h, VK_FORMAT_R32G32B32A32_SFLOAT, srcUsage, flowImg, flow_mem)) return false;
    flow_view = createImageView(flowImg, VK_FORMAT_R32G32B32A32_SFLOAT);

    auto setupDescSet = [this](VkDescriptorSet dSet, VkImageView v0, VkImageView v1, VkImageView vOut) {
        VkDescriptorImageInfo imageInfos[4]{};
        imageInfos[0].sampler = linearSampler;
        imageInfos[0].imageView = v0;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        imageInfos[1].sampler = linearSampler;
        imageInfos[1].imageView = v1;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        imageInfos[2].sampler = linearSampler;
        imageInfos[2].imageView = flow_view;
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        imageInfos[3].imageView = vOut;
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[4]{};
        for (int i = 0; i < 4; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = dSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = (i < 3) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    };

    setupDescSet(descSetY, img0_y_view, img1_y_view, out_y_view);
    setupDescSet(descSetU, img0_u_view, img1_u_view, out_u_view);
    setupDescSet(descSetV, img0_v_view, img1_v_view, out_v_view);

    size_t frame_bytes = (size_t)width * height + 2 * ((size_t)m_uv_w * m_uv_h);
    uploadSize = (VkDeviceSize)frame_bytes * 2 + (VkDeviceSize)flow_w * flow_h * 4 * sizeof(float);
    downloadSize = (VkDeviceSize)frame_bytes;

    VkMemoryPropertyFlags hostProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!createBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostProps, stagingUpload, stagingUploadMem)) return false;
    if (vkMapMemory(device, stagingUploadMem, 0, uploadSize, 0, &stagingUploadMapped) != VK_SUCCESS) return false;

    if (!createBuffer(downloadSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, hostProps, stagingDownload, stagingDownloadMem)) return false;
    if (vkMapMemory(device, stagingDownloadMem, 0, downloadSize, 0, &stagingDownloadMapped) != VK_SUCCESS) return false;

    return true;
}

bool VulkanWarper::warp_frame_yuv420(
    const PlaneInfo& y,
    const PlaneInfo& u,
    const PlaneInfo& v,
    const FlowInfo& flow,
    float time_step
) {
    if (!device || !stagingUploadMapped || !stagingDownloadMapped) return false;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    size_t y_bytes = (size_t)y.w * y.h;
    size_t uv_bytes = (size_t)u.w * u.h;
    size_t frame_bytes = y_bytes + 2 * uv_bytes;

    uint8_t* up = static_cast<uint8_t*>(stagingUploadMapped);

    auto copyPlane = [](uint8_t* dst, const uint8_t* src, int pw, int ph, ptrdiff_t stride) {
        if (stride == pw) {
            std::memcpy(dst, src, (size_t)pw * ph);
        } else {
            for (int r = 0; r < ph; r++) {
                std::memcpy(dst + (size_t)r * pw, src + r * stride, pw);
            }
        }
    };

    // 1. Pack Frame 0
    copyPlane(up, y.s0, y.w, y.h, y.s_stride);
    copyPlane(up + y_bytes, u.s0, u.w, u.h, u.s_stride);
    copyPlane(up + y_bytes + uv_bytes, v.s0, v.w, v.h, v.s_stride);

    // 2. Pack Frame 1
    uint8_t* up1 = up + frame_bytes;
    copyPlane(up1, y.s1, y.w, y.h, y.s_stride);
    copyPlane(up1 + y_bytes, u.s1, u.w, u.h, u.s_stride);
    copyPlane(up1 + y_bytes + uv_bytes, v.s1, v.w, v.h, v.s_stride);

    // 3. Pack Flow
    float* upload_flow = reinterpret_cast<float*>(up1 + frame_bytes);
    #pragma omp parallel for schedule(static)
    for (int r = 0; r < flow.h; r++) {
        const float* r0 = flow.p0 + r * flow.stride;
        const float* r1 = flow.p1 + r * flow.stride;
        const float* r2 = flow.p2 ? (flow.p2 + r * flow.stride) : nullptr;
        float* dst_row = upload_flow + (size_t)r * flow.w * 4;

        for (int c = 0; c < flow.w; c++) {
            dst_row[c * 4 + 0] = r0[c];
            dst_row[c * 4 + 1] = r1[c];
            dst_row[c * 4 + 2] = r2 ? r2[c] : 0.5f;
            dst_row[c * 4 + 3] = 1.0f;
        }
    }

    // 4. Record Single Command Buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkImage inImages[7] = { img0_y, img0_u, img0_v, img1_y, img1_u, img1_v, flowImg };
    VkImageMemoryBarrier barriers[7]{};
    for (int i = 0; i < 7; i++) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].image = inImages[i];
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].srcAccessMask = 0;
        barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[i].subresourceRange.levelCount = 1;
        barriers[i].subresourceRange.layerCount = 1;
    }
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 7, barriers);

    auto copyToImg = [this](VkDeviceSize offset, VkImage img, uint32_t pw, uint32_t ph) {
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = offset;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = { pw, ph, 1 };
        vkCmdCopyBufferToImage(cmdBuffer, stagingUpload, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    };

    copyToImg(0, img0_y, y.w, y.h);
    copyToImg(y_bytes, img0_u, u.w, u.h);
    copyToImg(y_bytes + uv_bytes, img0_v, v.w, v.h);

    copyToImg(frame_bytes, img1_y, y.w, y.h);
    copyToImg(frame_bytes + y_bytes, img1_u, u.w, u.h);
    copyToImg(frame_bytes + y_bytes + uv_bytes, img1_v, v.w, v.h);

    copyToImg(frame_bytes * 2, flowImg, flow.w, flow.h);

    VkImageMemoryBarrier computeBarriers[10]{};
    for (int i = 0; i < 7; i++) {
        computeBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        computeBarriers[i].image = inImages[i];
        computeBarriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        computeBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        computeBarriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        computeBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        computeBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        computeBarriers[i].subresourceRange.levelCount = 1;
        computeBarriers[i].subresourceRange.layerCount = 1;
    }

    VkImage outImages[3] = { out_y, out_u, out_v };
    for (int i = 0; i < 3; i++) {
        computeBarriers[7 + i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        computeBarriers[7 + i].image = outImages[i];
        computeBarriers[7 + i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        computeBarriers[7 + i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        computeBarriers[7 + i].srcAccessMask = 0;
        computeBarriers[7 + i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarriers[7 + i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        computeBarriers[7 + i].subresourceRange.levelCount = 1;
        computeBarriers[7 + i].subresourceRange.layerCount = 1;
    }
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 10, computeBarriers);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // Dispatch Y Plane
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSetY, 0, nullptr);
    PushConstants pcY{ (float)y.w, (float)y.h, (float)flow.w, (float)flow.h, time_step, 0.0f };
    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pcY);
    vkCmdDispatch(cmdBuffer, (y.w + 15) / 16, (y.h + 15) / 16, 1);

    // Dispatch U Plane
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSetU, 0, nullptr);
    PushConstants pcUV{ (float)u.w, (float)u.h, (float)flow.w, (float)flow.h, time_step, 0.0f };
    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pcUV);
    vkCmdDispatch(cmdBuffer, (u.w + 15) / 16, (u.h + 15) / 16, 1);

    // Dispatch V Plane
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSetV, 0, nullptr);
    vkCmdDispatch(cmdBuffer, (v.w + 15) / 16, (v.h + 15) / 16, 1);

    // Barrier: outImages GENERAL -> TRANSFER_SRC
    VkImageMemoryBarrier readBarriers[3]{};
    for (int i = 0; i < 3; i++) {
        readBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        readBarriers[i].image = outImages[i];
        readBarriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        readBarriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        readBarriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        readBarriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        readBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        readBarriers[i].subresourceRange.levelCount = 1;
        readBarriers[i].subresourceRange.layerCount = 1;
    }
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 3, readBarriers);

    auto copyFromImg = [this](VkDeviceSize offset, VkImage img, uint32_t pw, uint32_t ph) {
        VkBufferImageCopy downloadCopy{};
        downloadCopy.bufferOffset = offset;
        downloadCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        downloadCopy.imageSubresource.layerCount = 1;
        downloadCopy.imageExtent = { pw, ph, 1 };
        vkCmdCopyImageToBuffer(cmdBuffer, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingDownload, 1, &downloadCopy);
    };

    copyFromImg(0, out_y, y.w, y.h);
    copyFromImg(y_bytes, out_u, u.w, u.h);
    copyFromImg(y_bytes + uv_bytes, out_v, v.w, v.h);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    if (vkQueueSubmit(computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) return false;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    const uint8_t* dl = static_cast<const uint8_t*>(stagingDownloadMapped);

    copyPlane(y.dst, dl, y.w, y.h, y.d_stride);
    copyPlane(u.dst, dl + y_bytes, u.w, u.h, u.d_stride);
    copyPlane(v.dst, dl + y_bytes + uv_bytes, v.w, v.h, v.d_stride);

    return true;
}
