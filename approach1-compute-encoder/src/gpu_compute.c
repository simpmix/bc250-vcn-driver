/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * gpu_compute.c - Vulkan compute backend for encoding
 */
#include "gpu_compute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC250_DEVICE_ID 0x13FE
#define VK_CHECK(x) if ((x) != VK_SUCCESS) { fprintf(stderr, "Vulkan error: %d\n", x); return -1; }

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

static int create_buffer_with_memory(gpu_context_t *ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VK_CHECK(vkCreateBuffer(ctx->device, &buffer_info, NULL, buffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, *buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits, properties)
    };
    VK_CHECK(vkAllocateMemory(ctx->device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindBufferMemory(ctx->device, *buffer, *memory, 0));
    return 0;
}

static int allocate_encoding_buffers(gpu_context_t *ctx, uint32_t width, uint32_t height) {
    uint32_t width_in_mbs = (width + 15) / 16;
    uint32_t height_in_mbs = (height + 15) / 16;
    uint32_t num_mbs = width_in_mbs * height_in_mbs;

    VkDeviceSize mv_size = num_mbs * sizeof(uint32_t) * 3; // ivec2 + uint approx
    VkDeviceSize residual_size = num_mbs * 24 * 16 * sizeof(int);
    VkDeviceSize coeff_size = residual_size;
    VkDeviceSize quant_levels_size = residual_size;
    VkDeviceSize nz_count_size = num_mbs * 24 * sizeof(uint32_t);
    VkDeviceSize entropy_size = width * height; // Generous

    create_buffer_with_memory(ctx, mv_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->mv_buffer, &ctx->mv_memory);
    create_buffer_with_memory(ctx, residual_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->residual_buffer, &ctx->residual_memory);
    create_buffer_with_memory(ctx, coeff_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->coeff_buffer, &ctx->coeff_memory);
    create_buffer_with_memory(ctx, quant_levels_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->quant_levels_buffer, &ctx->quant_levels_memory);
    create_buffer_with_memory(ctx, nz_count_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->nz_count_buffer, &ctx->nz_count_memory);
    create_buffer_with_memory(ctx, entropy_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->entropy_buffer, &ctx->entropy_memory);
    create_buffer_with_memory(ctx, entropy_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->staging_buffer, &ctx->staging_memory);

    return 0;
}

static VkShaderModule load_spirv_shader(VkDevice device, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return VK_NULL_HANDLE;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint32_t *code = malloc(size);
    if (!code) {
        fclose(f);
        return VK_NULL_HANDLE;
    }
    fread(code, 1, size, f);
    fclose(f);
    
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code
    };
    VkShaderModule shader;
    vkCreateShaderModule(device, &create_info, NULL, &shader);
    free(code);
    return shader;
}

static VkPipeline create_compute_pipeline(VkDevice device, VkShaderModule shader, VkPipelineLayout layout) {
    VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader,
            .pName = "main"
        },
        .layout = layout
    };
    VkPipeline pipeline;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline);
    return pipeline;
}

int bc250_gpu_init(bc250_gpu_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "BC-250 VCN Driver",
        .apiVersion = VK_API_VERSION_1_2
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info
    };
    VK_CHECK(vkCreateInstance(&inst_info, NULL, &ctx->instance));
    
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, NULL);
    VkPhysicalDevice *devices = malloc(dev_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, devices);
    
    for (uint32_t i = 0; i < dev_count; i++) {
        vkGetPhysicalDeviceProperties(devices[i], &ctx->dev_props);
        if (ctx->dev_props.deviceID == BC250_DEVICE_ID) {
            ctx->physical_device = devices[i];
            break;
        }
    }
    free(devices);
    
    if (!ctx->physical_device) return -1;
    
    ctx->is_rdna2 = true;
    ctx->max_workgroup_size = ctx->dev_props.limits.maxComputeWorkGroupSize[0];
    
    uint32_t qf_count;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = malloc(qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &qf_count, qf_props);
    
    ctx->compute_queue_family = (uint32_t)-1;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            ctx->compute_queue_family = i;
            break;
        }
    }
    free(qf_props);
    if (ctx->compute_queue_family == (uint32_t)-1) return -1;
    
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo q_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };
    
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .timelineSemaphore = VK_TRUE
    };
    
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features12,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &q_info
    };
    VK_CHECK(vkCreateDevice(ctx->physical_device, &dev_info, NULL, &ctx->device));
    vkGetDeviceQueue(ctx->device, ctx->compute_queue_family, 0, &ctx->compute_queue);
    
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->compute_queue_family
    };
    VK_CHECK(vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->cmd_pool));
    
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2
    };
    VK_CHECK(vkAllocateCommandBuffers(ctx->device, &alloc_info, ctx->cmd_bufs));
    
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &ctx->fences[0]));
    VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &ctx->fences[1]));
    
    VkSemaphoreTypeCreateInfo sem_type_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0
    };
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &sem_type_info
    };
    VK_CHECK(vkCreateSemaphore(ctx->device, &sem_info, NULL, &ctx->timeline_sem));
    ctx->timeline_value = 0;
    
    /* Create descriptor set layouts */
    VkDescriptorSetLayoutBinding me_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo me_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = me_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &me_layout_info, NULL, &ctx->me_desc_layout);
    
    VkDescriptorSetLayoutBinding dct_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo dct_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = dct_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &dct_layout_info, NULL, &ctx->dct_desc_layout);

    VkDescriptorSetLayoutBinding quant_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo quant_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = quant_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &quant_layout_info, NULL, &ctx->quant_desc_layout);

    VkDescriptorSetLayoutBinding deblock_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo deblock_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = deblock_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &deblock_layout_info, NULL, &ctx->deblock_desc_layout);

    VkDescriptorSetLayoutBinding entropy_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo entropy_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = entropy_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &entropy_layout_info, NULL, &ctx->entropy_desc_layout);

    VkDescriptorSetLayoutBinding cc_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo cc_layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = cc_bindings };
    vkCreateDescriptorSetLayout(ctx->device, &cc_layout_info, NULL, &ctx->cc_desc_layout);

    /* Descriptor Pool */
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10}
    };
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 10,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes
    };
    vkCreateDescriptorPool(ctx->device, &pool_info_desc, NULL, &ctx->desc_pool);

    /* Push constants */
    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 4
    };

    /* Pipeline Layouts */
    VkPipelineLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc_range };
    
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &ctx->me_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->motion_est_layout);

    layout_info.pSetLayouts = &ctx->dct_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->transform_layout);

    layout_info.pSetLayouts = &ctx->quant_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->quantize_layout);
    
    layout_info.pSetLayouts = &ctx->deblock_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->deblock_layout);

    layout_info.pSetLayouts = &ctx->entropy_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->entropy_layout);

    layout_info.pSetLayouts = &ctx->cc_desc_layout;
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->color_convert_layout);

    /* Allocate sets */
    VkDescriptorSetAllocateInfo alloc_set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = ctx->desc_pool, .descriptorSetCount = 1 };
    
    alloc_set_info.pSetLayouts = &ctx->me_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->me_desc_set);
    
    alloc_set_info.pSetLayouts = &ctx->dct_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->dct_desc_set);
    
    alloc_set_info.pSetLayouts = &ctx->quant_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->quant_desc_set);
    
    alloc_set_info.pSetLayouts = &ctx->deblock_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->deblock_desc_set);
    
    alloc_set_info.pSetLayouts = &ctx->entropy_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->entropy_desc_set);
    
    alloc_set_info.pSetLayouts = &ctx->cc_desc_layout;
    vkAllocateDescriptorSets(ctx->device, &alloc_set_info, &ctx->cc_desc_set);

    /* Shaders & Pipelines */
    VkShaderModule me_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/motion_estimation.comp.spv");
    if (me_shader) {
        ctx->motion_est_pipeline = create_compute_pipeline(ctx->device, me_shader, ctx->motion_est_layout);
        vkDestroyShaderModule(ctx->device, me_shader, NULL);
    }
    VkShaderModule dct_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/dct.comp.spv");
    if (dct_shader) {
        ctx->transform_pipeline = create_compute_pipeline(ctx->device, dct_shader, ctx->transform_layout);
        vkDestroyShaderModule(ctx->device, dct_shader, NULL);
    }
    VkShaderModule quant_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/quantize.comp.spv");
    if (quant_shader) {
        ctx->quantize_pipeline = create_compute_pipeline(ctx->device, quant_shader, ctx->quantize_layout);
        vkDestroyShaderModule(ctx->device, quant_shader, NULL);
    }
    VkShaderModule deblock_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/deblock.comp.spv");
    if (deblock_shader) {
        ctx->deblock_pipeline = create_compute_pipeline(ctx->device, deblock_shader, ctx->deblock_layout);
        vkDestroyShaderModule(ctx->device, deblock_shader, NULL);
    }
    VkShaderModule entropy_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/entropy.comp.spv");
    if (entropy_shader) {
        ctx->entropy_pipeline = create_compute_pipeline(ctx->device, entropy_shader, ctx->entropy_layout);
        vkDestroyShaderModule(ctx->device, entropy_shader, NULL);
    }
    VkShaderModule cc_shader = load_spirv_shader(ctx->device, "/usr/lib64/dri/shaders/color_convert.comp.spv");
    if (cc_shader) {
        ctx->color_convert_pipeline = create_compute_pipeline(ctx->device, cc_shader, ctx->color_convert_layout);
        vkDestroyShaderModule(ctx->device, cc_shader, NULL);
    }

    allocate_encoding_buffers(ctx, 3840, 2160);

    return 0;
}

void bc250_gpu_destroy(bc250_gpu_context_t *ctx) {
    if (!ctx->device) return;
    
    vkDestroyPipeline(ctx->device, ctx->motion_est_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->transform_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->quantize_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->deblock_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->entropy_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->color_convert_pipeline, NULL);

    vkDestroyPipelineLayout(ctx->device, ctx->motion_est_layout, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->transform_layout, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->quantize_layout, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->deblock_layout, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->entropy_layout, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->color_convert_layout, NULL);

    vkDestroyDescriptorSetLayout(ctx->device, ctx->me_desc_layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->dct_desc_layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->quant_desc_layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->deblock_desc_layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->entropy_desc_layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->cc_desc_layout, NULL);

    vkDestroyDescriptorPool(ctx->device, ctx->desc_pool, NULL);

    vkDestroyBuffer(ctx->device, ctx->mv_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->mv_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->residual_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->residual_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->coeff_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->coeff_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->quant_levels_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->quant_levels_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->nz_count_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->nz_count_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->entropy_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->entropy_memory, NULL);
    vkDestroyBuffer(ctx->device, ctx->staging_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->staging_memory, NULL);

    vkDestroySemaphore(ctx->device, ctx->timeline_sem, NULL);
    vkDestroyFence(ctx->device, ctx->fences[0], NULL);
    vkDestroyFence(ctx->device, ctx->fences[1], NULL);
    vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
    vkDestroyDevice(ctx->device, NULL);
    vkDestroyInstance(ctx->instance, NULL);
}

int gpu_compute_init(gpu_context_t *ctx) {
    return bc250_gpu_init(ctx);
}

void gpu_compute_terminate(gpu_context_t *ctx) {
    bc250_gpu_destroy(ctx);
}

int gpu_compute_create_image(gpu_context_t *ctx, int width, int height, int format, gpu_image_t *image, gpu_memory_t *memory) {
    VkImageCreateInfo y_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VK_CHECK(vkCreateImage(ctx->device, &y_info, NULL, &image->y_plane));
    
    VkImageCreateInfo uv_info = y_info;
    uv_info.format = VK_FORMAT_R8G8_UNORM;
    uv_info.extent.width = width / 2;
    uv_info.extent.height = height / 2;
    VK_CHECK(vkCreateImage(ctx->device, &uv_info, NULL, &image->uv_plane));
    
    VkMemoryRequirements y_req, uv_req;
    vkGetImageMemoryRequirements(ctx->device, image->y_plane, &y_req);
    vkGetImageMemoryRequirements(ctx->device, image->uv_plane, &uv_req);
    
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = y_req.size + uv_req.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, y_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VK_CHECK(vkAllocateMemory(ctx->device, &alloc_info, NULL, &memory->memory));
    VK_CHECK(vkBindImageMemory(ctx->device, image->y_plane, memory->memory, 0));
    VK_CHECK(vkBindImageMemory(ctx->device, image->uv_plane, memory->memory, y_req.size));
    
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->y_plane,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VK_CHECK(vkCreateImageView(ctx->device, &view_info, NULL, &image->y_view));
    
    view_info.image = image->uv_plane;
    view_info.format = VK_FORMAT_R8G8_UNORM;
    VK_CHECK(vkCreateImageView(ctx->device, &view_info, NULL, &image->uv_view));
    
    return 0;
}

void gpu_compute_destroy_image(gpu_context_t *ctx, gpu_image_t image, gpu_memory_t memory) {
    vkDestroyImageView(ctx->device, image.y_view, NULL);
    vkDestroyImageView(ctx->device, image.uv_view, NULL);
    vkDestroyImage(ctx->device, image.y_plane, NULL);
    vkDestroyImage(ctx->device, image.uv_plane, NULL);
    vkFreeMemory(ctx->device, memory.memory, NULL);
}

static void insert_compute_barrier(VkCommandBuffer cmd_buf) {
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

int gpu_compute_begin_picture(gpu_context_t *ctx, gpu_image_t render_target) {
    vkWaitForFences(ctx->device, 1, &ctx->fences[ctx->current_buf], VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->device, 1, &ctx->fences[ctx->current_buf]);
    
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(ctx->cmd_bufs[ctx->current_buf], &begin_info);
    
    return 0;
}

int gpu_compute_dispatch_encode(gpu_context_t *ctx, gpu_image_t render_target, int width, int height) {
    VkCommandBuffer cmd_buf = ctx->cmd_bufs[ctx->current_buf];
    uint32_t width_mbs = (width + 15) / 16;
    uint32_t height_mbs = (height + 15) / 16;
    uint32_t pc[4] = {width, height, width_mbs, height_mbs};
    
    /* Stage 1: Color Convert */
    if (ctx->color_convert_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->color_convert_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->color_convert_layout, 0, 1, &ctx->cc_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->color_convert_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width / 16, height / 16, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Stage 2: Motion Estimation */
    if (ctx->motion_est_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->motion_est_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->motion_est_layout, 0, 1, &ctx->me_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->motion_est_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Stage 3: DCT */
    if (ctx->transform_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->transform_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->transform_layout, 0, 1, &ctx->dct_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->transform_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Stage 4: Quantize */
    if (ctx->quantize_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->quantize_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->quantize_layout, 0, 1, &ctx->quant_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->quantize_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Stage 5: Deblock */
    if (ctx->deblock_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->deblock_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->deblock_layout, 0, 1, &ctx->deblock_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->deblock_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Stage 6: Entropy */
    if (ctx->entropy_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->entropy_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->entropy_layout, 0, 1, &ctx->entropy_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->entropy_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
        insert_compute_barrier(cmd_buf);
    }
    
    /* Copy entropy buffer to staging for CPU readback */
    VkBufferCopy copy_region = { .srcOffset = 0, .dstOffset = 0, .size = width * height };
    vkCmdCopyBuffer(cmd_buf, ctx->entropy_buffer, ctx->staging_buffer, 1, &copy_region);
    
    return 0;
}

int gpu_compute_end_picture(gpu_context_t *ctx) {
    vkEndCommandBuffer(ctx->cmd_bufs[ctx->current_buf]);
    
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->cmd_bufs[ctx->current_buf]
    };
    vkQueueSubmit(ctx->compute_queue, 1, &submit_info, ctx->fences[ctx->current_buf]);
    
    ctx->current_buf = (ctx->current_buf + 1) % 2;
    return 0;
}

int gpu_compute_sync(gpu_context_t *ctx) {
    int prev_buf = (ctx->current_buf + 1) % 2;
    vkWaitForFences(ctx->device, 1, &ctx->fences[prev_buf], VK_TRUE, UINT64_MAX);
    
    void *data;
    vkMapMemory(ctx->device, ctx->staging_memory, 0, VK_WHOLE_SIZE, 0, &data);
    vkUnmapMemory(ctx->device, ctx->staging_memory);
    return 0;
}
