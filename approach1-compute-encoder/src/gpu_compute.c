/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * gpu_compute.c - Vulkan compute backend for AMD BC-250 encoding
 */
#include "gpu_compute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC250_DEVICE_ID 0x13FE
#define AMD_VENDOR_ID   0x1002

#define VK_CHECK(x) do { \
    VkResult err = (x); \
    if (err != VK_SUCCESS) { \
        fprintf(stderr, "[bc250-gpu] Vulkan error %d at %s:%d\n", err, __FILE__, __LINE__); \
        return -1; \
    } \
} while(0)

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    /* Fallback to any matching type */
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (type_filter & (1 << i)) {
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

static void update_storage_buffer_descriptor(VkDevice device, VkDescriptorSet set, uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo buf_info = {
        .buffer = buffer,
        .offset = 0,
        .range = size
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &buf_info
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

static void update_storage_image_descriptor(VkDevice device, VkDescriptorSet set, uint32_t binding, VkImageView view) {
    VkDescriptorImageInfo img_info = {
        .sampler = VK_NULL_HANDLE,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &img_info
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

static int allocate_encoding_buffers(gpu_context_t *ctx, uint32_t width, uint32_t height) {
    if (ctx->mv_buffer) {
        vkDestroyBuffer(ctx->device, ctx->mv_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->mv_memory, NULL);
        ctx->mv_buffer = VK_NULL_HANDLE;
    }
    if (ctx->residual_buffer) {
        vkDestroyBuffer(ctx->device, ctx->residual_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->residual_memory, NULL);
        ctx->residual_buffer = VK_NULL_HANDLE;
    }
    if (ctx->coeff_buffer) {
        vkDestroyBuffer(ctx->device, ctx->coeff_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->coeff_memory, NULL);
        ctx->coeff_buffer = VK_NULL_HANDLE;
    }
    if (ctx->quant_levels_buffer) {
        vkDestroyBuffer(ctx->device, ctx->quant_levels_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->quant_levels_memory, NULL);
        ctx->quant_levels_buffer = VK_NULL_HANDLE;
    }
    if (ctx->nz_count_buffer) {
        vkDestroyBuffer(ctx->device, ctx->nz_count_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->nz_count_memory, NULL);
        ctx->nz_count_buffer = VK_NULL_HANDLE;
    }
    if (ctx->entropy_buffer) {
        vkDestroyBuffer(ctx->device, ctx->entropy_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->entropy_memory, NULL);
        ctx->entropy_buffer = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; i++) {
        if (ctx->staging_buffers[i]) {
            vkDestroyBuffer(ctx->device, ctx->staging_buffers[i], NULL);
            vkFreeMemory(ctx->device, ctx->staging_memories[i], NULL);
            ctx->staging_buffers[i] = VK_NULL_HANDLE;
        }
    }

    uint32_t width_in_mbs = (width + 15) / 16;
    uint32_t height_in_mbs = (height + 15) / 16;
    uint32_t num_mbs = width_in_mbs * height_in_mbs;

    VkDeviceSize mv_size = num_mbs * sizeof(uint32_t) * 4;
    VkDeviceSize residual_size = num_mbs * 24 * 16 * sizeof(int);
    VkDeviceSize coeff_size = residual_size;
    VkDeviceSize quant_levels_size = residual_size;
    VkDeviceSize nz_count_size = num_mbs * 24 * sizeof(uint32_t);
    VkDeviceSize entropy_size = width * height * 2; /* Generous */

    ctx->staging_size = entropy_size;

    create_buffer_with_memory(ctx, mv_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->mv_buffer, &ctx->mv_memory);
    create_buffer_with_memory(ctx, residual_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->residual_buffer, &ctx->residual_memory);
    create_buffer_with_memory(ctx, coeff_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->coeff_buffer, &ctx->coeff_memory);
    create_buffer_with_memory(ctx, quant_levels_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->quant_levels_buffer, &ctx->quant_levels_memory);
    create_buffer_with_memory(ctx, nz_count_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->nz_count_buffer, &ctx->nz_count_memory);
    create_buffer_with_memory(ctx, entropy_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->entropy_buffer, &ctx->entropy_memory);
    create_buffer_with_memory(ctx, entropy_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->staging_buffers[0], &ctx->staging_memories[0]);
    create_buffer_with_memory(ctx, entropy_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->staging_buffers[1], &ctx->staging_memories[1]);

    /* Persistently map both staging buffers to eliminate per-frame map/unmap syscall overhead */
    vkMapMemory(ctx->device, ctx->staging_memories[0], 0, entropy_size, 0, &ctx->staging_mapped[0]);
    vkMapMemory(ctx->device, ctx->staging_memories[1], 0, entropy_size, 0, &ctx->staging_mapped[1]);

    /* Update buffer descriptors */
    update_storage_buffer_descriptor(ctx->device, ctx->me_desc_set, 2, ctx->mv_buffer, mv_size);

    update_storage_buffer_descriptor(ctx->device, ctx->dct_desc_set, 0, ctx->residual_buffer, residual_size);
    update_storage_buffer_descriptor(ctx->device, ctx->dct_desc_set, 1, ctx->coeff_buffer, coeff_size);

    update_storage_buffer_descriptor(ctx->device, ctx->quant_desc_set, 0, ctx->coeff_buffer, coeff_size);
    update_storage_buffer_descriptor(ctx->device, ctx->quant_desc_set, 1, ctx->quant_levels_buffer, quant_levels_size);
    update_storage_buffer_descriptor(ctx->device, ctx->quant_desc_set, 2, ctx->nz_count_buffer, nz_count_size);

    update_storage_buffer_descriptor(ctx->device, ctx->deblock_desc_set, 1, ctx->quant_levels_buffer, quant_levels_size);
    update_storage_buffer_descriptor(ctx->device, ctx->deblock_desc_set, 2, ctx->mv_buffer, mv_size);

    update_storage_buffer_descriptor(ctx->device, ctx->entropy_desc_set, 0, ctx->quant_levels_buffer, quant_levels_size);
    update_storage_buffer_descriptor(ctx->device, ctx->entropy_desc_set, 1, ctx->entropy_buffer, entropy_size);

    return 0;
}

static VkShaderModule load_spirv_shader(VkDevice device, const char *filename) {
    const char *search_paths[] = {
        "/usr/share/bc250/shaders",
        "/usr/local/share/bc250/shaders",
        "/usr/lib64/dri/shaders",
        "/usr/lib/dri/shaders",
        "./shaders",
        "../shaders",
        "../../approach1-compute-encoder/shaders",
        NULL
    };

    FILE *f = NULL;
    char full_path[512];

    const char *env_dir = getenv("BC250_SHADER_DIR");
    if (env_dir && env_dir[0] != '\0') {
        snprintf(full_path, sizeof(full_path), "%s/%s", env_dir, filename);
        f = fopen(full_path, "rb");
    }

    if (!f) {
        for (int i = 0; search_paths[i] != NULL; i++) {
            snprintf(full_path, sizeof(full_path), "%s/%s", search_paths[i], filename);
            f = fopen(full_path, "rb");
            if (f) break;
        }
    }

    if (!f) {
        /* Fallback: try raw filename */
        f = fopen(filename, "rb");
    }

    if (!f) {
        fprintf(stderr, "[bc250-gpu] Could not find SPIR-V shader: %s\n", filename);
        return VK_NULL_HANDLE;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint32_t *code = malloc(size);
    if (!code) {
        fclose(f);
        return VK_NULL_HANDLE;
    }
    size_t read_bytes = fread(code, 1, size, f);
    fclose(f);

    if (read_bytes != size || size % 4 != 0) {
        free(code);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code
    };
    VkShaderModule shader;
    VkResult res = vkCreateShaderModule(device, &create_info, NULL, &shader);
    free(code);

    if (res != VK_SUCCESS) {
        fprintf(stderr, "[bc250-gpu] Failed to create shader module for %s\n", filename);
        return VK_NULL_HANDLE;
    }

    return shader;
}

static VkPipeline create_compute_pipeline(VkDevice device, VkShaderModule shader, VkPipelineLayout layout) {
    if (!shader || !layout) return VK_NULL_HANDLE;

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
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

int bc250_gpu_init(bc250_gpu_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "BC-250 VCN VA-API Compute Driver",
        .apiVersion = VK_API_VERSION_1_2
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info
    };
    VK_CHECK(vkCreateInstance(&inst_info, NULL, &ctx->instance));

    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, NULL);
    if (dev_count == 0) {
        fprintf(stderr, "[bc250-gpu] No Vulkan physical devices found!\n");
        return -1;
    }

    VkPhysicalDevice *devices = malloc(dev_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, devices);

    /* 1. Prioritize BC-250 (0x13FE) */
    for (uint32_t i = 0; i < dev_count; i++) {
        vkGetPhysicalDeviceProperties(devices[i], &ctx->dev_props);
        if (ctx->dev_props.deviceID == BC250_DEVICE_ID) {
            ctx->physical_device = devices[i];
            ctx->is_rdna2 = true;
            fprintf(stderr, "[bc250-gpu] Found AMD BC-250 APU (0x13FE) - %s\n", ctx->dev_props.deviceName);
            break;
        }
    }

    /* 2. Fallback: Any AMD device */
    if (!ctx->physical_device) {
        for (uint32_t i = 0; i < dev_count; i++) {
            vkGetPhysicalDeviceProperties(devices[i], &ctx->dev_props);
            if (ctx->dev_props.vendorID == AMD_VENDOR_ID) {
                ctx->physical_device = devices[i];
                ctx->is_rdna2 = true;
                fprintf(stderr, "[bc250-gpu] BC-250 not found, using AMD GPU: %s\n", ctx->dev_props.deviceName);
                break;
            }
        }
    }

    /* 3. Fallback: Primary compute device */
    if (!ctx->physical_device) {
        ctx->physical_device = devices[0];
        vkGetPhysicalDeviceProperties(devices[0], &ctx->dev_props);
        fprintf(stderr, "[bc250-gpu] Using primary Vulkan device: %s\n", ctx->dev_props.deviceName);
    }
    free(devices);

    ctx->max_workgroup_size = ctx->dev_props.limits.maxComputeWorkGroupSize[0];

    /* Find compute queue family */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = malloc(qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &qf_count, qf_props);

    /* 1. Prioritize dedicated hardware async compute queue (ACE on RDNA2) */
    ctx->compute_queue_family = (uint32_t)-1;
    for (uint32_t i = 0; i < qf_count; i++) {
        if ((qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            ctx->compute_queue_family = i;
            fprintf(stderr, "[bc250-gpu] Using dedicated async compute queue family %u\n", i);
            break;
        }
    }
    /* 2. Fallback to any compute-capable queue */
    if (ctx->compute_queue_family == (uint32_t)-1) {
        for (uint32_t i = 0; i < qf_count; i++) {
            if (qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                ctx->compute_queue_family = i;
                fprintf(stderr, "[bc250-gpu] Using general compute queue family %u\n", i);
                break;
            }
        }
    }
    free(qf_props);
    if (ctx->compute_queue_family == (uint32_t)-1) {
        fprintf(stderr, "[bc250-gpu] No compute queue family available!\n");
        return -1;
    }

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

    /* Command Pool */
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

    /* Fences */
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &ctx->fences[0]));
    VK_CHECK(vkCreateFence(ctx->device, &fence_info, NULL, &ctx->fences[1]));

    /* Timeline Semaphore */
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

    /* Create Descriptor Set Layouts */
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
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32}
    };
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 32,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes
    };
    vkCreateDescriptorPool(ctx->device, &pool_info_desc, NULL, &ctx->desc_pool);

    /* Push constants */
    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 8
    };

    /* Pipeline Layouts */
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc_range,
        .setLayoutCount = 1
    };

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

    /* Allocate Descriptor Sets */
    VkDescriptorSetAllocateInfo alloc_set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->desc_pool,
        .descriptorSetCount = 1
    };

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
    VkShaderModule me_shader = load_spirv_shader(ctx->device, "motion_estimation.comp.spv");
    if (me_shader) {
        ctx->motion_est_pipeline = create_compute_pipeline(ctx->device, me_shader, ctx->motion_est_layout);
        vkDestroyShaderModule(ctx->device, me_shader, NULL);
    }
    VkShaderModule dct_shader = load_spirv_shader(ctx->device, "dct_transform.comp.spv");
    if (dct_shader) {
        ctx->transform_pipeline = create_compute_pipeline(ctx->device, dct_shader, ctx->transform_layout);
        vkDestroyShaderModule(ctx->device, dct_shader, NULL);
    }
    VkShaderModule quant_shader = load_spirv_shader(ctx->device, "quantize.comp.spv");
    if (quant_shader) {
        ctx->quantize_pipeline = create_compute_pipeline(ctx->device, quant_shader, ctx->quantize_layout);
        vkDestroyShaderModule(ctx->device, quant_shader, NULL);
    }
    VkShaderModule deblock_shader = load_spirv_shader(ctx->device, "deblock_filter.comp.spv");
    if (deblock_shader) {
        ctx->deblock_pipeline = create_compute_pipeline(ctx->device, deblock_shader, ctx->deblock_layout);
        vkDestroyShaderModule(ctx->device, deblock_shader, NULL);
    }
    VkShaderModule entropy_shader = load_spirv_shader(ctx->device, "entropy_encode.comp.spv");
    if (entropy_shader) {
        ctx->entropy_pipeline = create_compute_pipeline(ctx->device, entropy_shader, ctx->entropy_layout);
        vkDestroyShaderModule(ctx->device, entropy_shader, NULL);
    }
    VkShaderModule cc_shader = load_spirv_shader(ctx->device, "color_convert.comp.spv");
    if (cc_shader) {
        ctx->color_convert_pipeline = create_compute_pipeline(ctx->device, cc_shader, ctx->color_convert_layout);
        vkDestroyShaderModule(ctx->device, cc_shader, NULL);
    }

    /* Allocate device buffers for 4K maximum resolution */
    allocate_encoding_buffers(ctx, 3840, 2160);

    return 0;
}

void bc250_gpu_destroy(bc250_gpu_context_t *ctx) {
    if (!ctx->device) return;

    vkDeviceWaitIdle(ctx->device);

    if (ctx->motion_est_pipeline) vkDestroyPipeline(ctx->device, ctx->motion_est_pipeline, NULL);
    if (ctx->transform_pipeline) vkDestroyPipeline(ctx->device, ctx->transform_pipeline, NULL);
    if (ctx->quantize_pipeline) vkDestroyPipeline(ctx->device, ctx->quantize_pipeline, NULL);
    if (ctx->deblock_pipeline) vkDestroyPipeline(ctx->device, ctx->deblock_pipeline, NULL);
    if (ctx->entropy_pipeline) vkDestroyPipeline(ctx->device, ctx->entropy_pipeline, NULL);
    if (ctx->color_convert_pipeline) vkDestroyPipeline(ctx->device, ctx->color_convert_pipeline, NULL);

    if (ctx->motion_est_layout) vkDestroyPipelineLayout(ctx->device, ctx->motion_est_layout, NULL);
    if (ctx->transform_layout) vkDestroyPipelineLayout(ctx->device, ctx->transform_layout, NULL);
    if (ctx->quantize_layout) vkDestroyPipelineLayout(ctx->device, ctx->quantize_layout, NULL);
    if (ctx->deblock_layout) vkDestroyPipelineLayout(ctx->device, ctx->deblock_layout, NULL);
    if (ctx->entropy_layout) vkDestroyPipelineLayout(ctx->device, ctx->entropy_layout, NULL);
    if (ctx->color_convert_layout) vkDestroyPipelineLayout(ctx->device, ctx->color_convert_layout, NULL);

    if (ctx->me_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->me_desc_layout, NULL);
    if (ctx->dct_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->dct_desc_layout, NULL);
    if (ctx->quant_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->quant_desc_layout, NULL);
    if (ctx->deblock_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->deblock_desc_layout, NULL);
    if (ctx->entropy_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->entropy_desc_layout, NULL);
    if (ctx->cc_desc_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->cc_desc_layout, NULL);

    if (ctx->desc_pool) vkDestroyDescriptorPool(ctx->device, ctx->desc_pool, NULL);

    if (ctx->mv_buffer) { vkDestroyBuffer(ctx->device, ctx->mv_buffer, NULL); vkFreeMemory(ctx->device, ctx->mv_memory, NULL); }
    if (ctx->residual_buffer) { vkDestroyBuffer(ctx->device, ctx->residual_buffer, NULL); vkFreeMemory(ctx->device, ctx->residual_memory, NULL); }
    if (ctx->coeff_buffer) { vkDestroyBuffer(ctx->device, ctx->coeff_buffer, NULL); vkFreeMemory(ctx->device, ctx->coeff_memory, NULL); }
    if (ctx->quant_levels_buffer) { vkDestroyBuffer(ctx->device, ctx->quant_levels_buffer, NULL); vkFreeMemory(ctx->device, ctx->quant_levels_memory, NULL); }
    if (ctx->nz_count_buffer) { vkDestroyBuffer(ctx->device, ctx->nz_count_buffer, NULL); vkFreeMemory(ctx->device, ctx->nz_count_memory, NULL); }
    if (ctx->entropy_buffer) { vkDestroyBuffer(ctx->device, ctx->entropy_buffer, NULL); vkFreeMemory(ctx->device, ctx->entropy_memory, NULL); }
    for (int i = 0; i < 2; i++) {
        if (ctx->staging_mapped[i]) {
            vkUnmapMemory(ctx->device, ctx->staging_memories[i]);
            ctx->staging_mapped[i] = NULL;
        }
        if (ctx->staging_buffers[i]) {
            vkDestroyBuffer(ctx->device, ctx->staging_buffers[i], NULL);
            vkFreeMemory(ctx->device, ctx->staging_memories[i], NULL);
        }
    }

    if (ctx->timeline_sem) vkDestroySemaphore(ctx->device, ctx->timeline_sem, NULL);
    if (ctx->fences[0]) vkDestroyFence(ctx->device, ctx->fences[0], NULL);
    if (ctx->fences[1]) vkDestroyFence(ctx->device, ctx->fences[1], NULL);
    if (ctx->cmd_pool) vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
    if (ctx->device) vkDestroyDevice(ctx->device, NULL);
    if (ctx->instance) vkDestroyInstance(ctx->instance, NULL);
}

int gpu_compute_init(gpu_context_t *ctx) {
    return bc250_gpu_init(ctx);
}

void gpu_compute_terminate(gpu_context_t *ctx) {
    bc250_gpu_destroy(ctx);
}

int gpu_compute_create_image(gpu_context_t *ctx, int width, int height, int format, gpu_image_t *image, gpu_memory_t *memory) {
    (void)format;
    image->width = width;
    image->height = height;

    VkImageCreateInfo y_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED
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

    VkDeviceSize uv_offset = (y_req.size + y_req.alignment - 1) & ~(y_req.alignment - 1);
    VkDeviceSize total_size = uv_offset + uv_req.size;

    memory->size = total_size;

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = total_size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, y_req.memoryTypeBits | uv_req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    VK_CHECK(vkAllocateMemory(ctx->device, &alloc_info, NULL, &memory->memory));
    VK_CHECK(vkBindImageMemory(ctx->device, image->y_plane, memory->memory, 0));
    VK_CHECK(vkBindImageMemory(ctx->device, image->uv_plane, memory->memory, uv_offset));

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
    if (image.y_view) vkDestroyImageView(ctx->device, image.y_view, NULL);
    if (image.uv_view) vkDestroyImageView(ctx->device, image.uv_view, NULL);
    if (image.y_plane) vkDestroyImage(ctx->device, image.y_plane, NULL);
    if (image.uv_plane) vkDestroyImage(ctx->device, image.uv_plane, NULL);
    if (memory.memory) vkFreeMemory(ctx->device, memory.memory, NULL);
}

int gpu_compute_upload_nv12(gpu_context_t *ctx, gpu_image_t *image,
                           const uint8_t *y_plane, int y_pitch,
                           const uint8_t *uv_plane, int uv_pitch,
                           int width, int height) {
    VkImageSubresource subresource_y = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout layout_y;
    vkGetImageSubresourceLayout(ctx->device, image->y_plane, &subresource_y, &layout_y);

    VkMemoryRequirements y_req;
    vkGetImageMemoryRequirements(ctx->device, image->y_plane, &y_req);
    VkDeviceSize uv_offset = (y_req.size + y_req.alignment - 1) & ~(y_req.alignment - 1);

    VkImageSubresource subresource_uv = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout layout_uv;
    vkGetImageSubresourceLayout(ctx->device, image->uv_plane, &subresource_uv, &layout_uv);

    /* Note: If memory is host-visible, we can write directly */
    return 0;
}

int gpu_compute_download_nv12(gpu_context_t *ctx, gpu_image_t *image,
                             uint8_t *y_plane, int y_pitch,
                             uint8_t *uv_plane, int uv_pitch,
                             int width, int height) {
    (void)ctx; (void)image; (void)y_plane; (void)y_pitch; (void)uv_plane; (void)uv_pitch; (void)width; (void)height;
    return 0;
}

static void insert_compute_barrier(VkCommandBuffer cmd_buf) {
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

int gpu_compute_begin_picture(gpu_context_t *ctx, gpu_image_t render_target) {
    (void)render_target;
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
    if (!ctx) return -1;

    /* Ensure pipeline buffers are allocated for current dimensions */
    if (ctx->staging_buffers[0] == VK_NULL_HANDLE || ctx->frame_width != (uint32_t)width || ctx->frame_height != (uint32_t)height) {
        allocate_encoding_buffers(ctx, (uint32_t)width, (uint32_t)height);
        ctx->frame_width = (uint32_t)width;
        ctx->frame_height = (uint32_t)height;
    }

    VkCommandBuffer cmd_buf = ctx->cmd_bufs[ctx->current_buf];
    uint32_t width_mbs = (width + 15) / 16;
    uint32_t height_mbs = (height + 15) / 16;
    uint32_t pc[8] = { (uint32_t)width, (uint32_t)height, width_mbs, height_mbs, 26, 0, 0, 5 };

    /* Update image descriptors to point to the current surface */
    if (render_target.y_view && render_target.uv_view) {
        update_storage_image_descriptor(ctx->device, ctx->me_desc_set, 0, render_target.y_view);
        update_storage_image_descriptor(ctx->device, ctx->me_desc_set, 1, render_target.y_view); /* self or ref */
        update_storage_image_descriptor(ctx->device, ctx->deblock_desc_set, 0, render_target.y_view);
    }

    /* Stage 1: Color Convert */
    if (ctx->color_convert_pipeline) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->color_convert_pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->color_convert_layout, 0, 1, &ctx->cc_desc_set, 0, NULL);
        vkCmdPushConstants(cmd_buf, ctx->color_convert_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        vkCmdDispatch(cmd_buf, width_mbs, height_mbs, 1);
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

    /* Stage 5: Deblock (Skipped in BC250_FAST_MODE to maximize gaming framerates) */
    const char *fm = getenv("BC250_FAST_MODE");
    int fast_mode = (fm && (strcmp(fm, "1") == 0 || strcmp(fm, "true") == 0)) ? 1 : 0;

    if (!fast_mode && ctx->deblock_pipeline) {
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

    /* Copy entropy output buffer to current staging buffer for overlapped CPU readback */
    VkDeviceSize copy_size = width * height;
    if (copy_size > ctx->staging_size) copy_size = ctx->staging_size;
    VkBufferCopy copy_region = { .srcOffset = 0, .dstOffset = 0, .size = copy_size };
    vkCmdCopyBuffer(cmd_buf, ctx->entropy_buffer, ctx->staging_buffers[ctx->current_buf], 1, &copy_region);

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
    return 0;
}

int gpu_compute_get_staging_data(gpu_context_t *ctx, void **data, size_t *size) {
    if (!ctx || !data || !size) return -1;
    int prev_buf = (ctx->current_buf + 1) % 2;
    *size = ctx->staging_size;
    *data = ctx->staging_mapped[prev_buf];
    return (*data != NULL) ? 0 : -1;
}

int gpu_compute_release_staging_data(gpu_context_t *ctx) {
    (void)ctx;
    /* Persistently mapped: zero syscall overhead */
    return 0;
}
