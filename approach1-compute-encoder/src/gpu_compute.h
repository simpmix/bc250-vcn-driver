/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * gpu_compute.h - Vulkan compute orchestration for AMD BC-250
 */
#ifndef GPU_COMPUTE_H
#define GPU_COMPUTE_H

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VkImage y_plane;
    VkImageView y_view;
    VkImage uv_plane;
    VkImageView uv_view;
    uint32_t width;
    uint32_t height;
} gpu_image_t;

typedef struct {
    VkDeviceMemory memory;
    VkDeviceSize size;
} gpu_memory_t;

typedef struct bc250_gpu_context {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue compute_queue;
    uint32_t compute_queue_family;
    VkCommandPool cmd_pool;
    VkDescriptorPool desc_pool;
    
    /* Descriptor set layouts */
    VkDescriptorSetLayout me_desc_layout;
    VkDescriptorSetLayout dct_desc_layout;
    VkDescriptorSetLayout quant_desc_layout;
    VkDescriptorSetLayout deblock_desc_layout;
    VkDescriptorSetLayout entropy_desc_layout;
    VkDescriptorSetLayout cc_desc_layout;

    /* Pipeline layouts */
    VkPipelineLayout motion_est_layout;
    VkPipelineLayout transform_layout;
    VkPipelineLayout quantize_layout;
    VkPipelineLayout deblock_layout;
    VkPipelineLayout entropy_layout;
    VkPipelineLayout color_convert_layout;

    /* Compute pipelines */
    VkPipeline motion_est_pipeline;
    VkPipeline transform_pipeline;
    VkPipeline quantize_pipeline;
    VkPipeline deblock_pipeline;
    VkPipeline entropy_pipeline;
    VkPipeline color_convert_pipeline;
    
    /* Descriptor sets */
    VkDescriptorSet me_desc_set;
    VkDescriptorSet dct_desc_set;
    VkDescriptorSet quant_desc_set;
    VkDescriptorSet deblock_desc_set;
    VkDescriptorSet entropy_desc_set;
    VkDescriptorSet cc_desc_set;

    /* Encoding Buffers */
    VkBuffer mv_buffer;
    VkDeviceMemory mv_memory;
    
    VkBuffer residual_buffer;
    VkDeviceMemory residual_memory;
    
    VkBuffer coeff_buffer;
    VkDeviceMemory coeff_memory;
    
    VkBuffer quant_levels_buffer;
    VkDeviceMemory quant_levels_memory;
    
    VkBuffer nz_count_buffer;
    VkDeviceMemory nz_count_memory;
    
    VkBuffer entropy_buffer;
    VkDeviceMemory entropy_memory;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    VkDeviceSize staging_size;
    
    /* Reconstructed frame for DPB */
    gpu_image_t recon_image;
    gpu_memory_t recon_memory;

    /* Double-buffering for pipeline overlap */
    VkCommandBuffer cmd_bufs[2];
    VkFence fences[2];
    VkSemaphore timeline_sem;
    uint64_t timeline_value;
    int current_buf;
    
    /* Frame state */
    uint32_t frame_width;
    uint32_t frame_height;
    
    /* Device properties */
    VkPhysicalDeviceProperties dev_props;
    uint32_t max_workgroup_size;
    bool is_rdna2;
} bc250_gpu_context_t;

typedef bc250_gpu_context_t gpu_context_t;

/* Core lifecycle */
int bc250_gpu_init(bc250_gpu_context_t *ctx);
void bc250_gpu_destroy(bc250_gpu_context_t *ctx);

int gpu_compute_init(gpu_context_t *ctx);
void gpu_compute_terminate(gpu_context_t *ctx);

/* Image allocation & transfers */
int gpu_compute_create_image(gpu_context_t *ctx, int width, int height, int format, gpu_image_t *image, gpu_memory_t *memory);
void gpu_compute_destroy_image(gpu_context_t *ctx, gpu_image_t image, gpu_memory_t memory);

int gpu_compute_upload_nv12(gpu_context_t *ctx, gpu_image_t *image,
                           const uint8_t *y_plane, int y_pitch,
                           const uint8_t *uv_plane, int uv_pitch,
                           int width, int height);

int gpu_compute_download_nv12(gpu_context_t *ctx, gpu_image_t *image,
                             uint8_t *y_plane, int y_pitch,
                             uint8_t *uv_plane, int uv_pitch,
                             int width, int height);

/* Picture encoding orchestration */
int gpu_compute_begin_picture(gpu_context_t *ctx, gpu_image_t render_target);
int gpu_compute_dispatch_encode(gpu_context_t *ctx, gpu_image_t render_target, int width, int height);
int gpu_compute_end_picture(gpu_context_t *ctx);
int gpu_compute_sync(gpu_context_t *ctx);
int gpu_compute_get_staging_data(gpu_context_t *ctx, void **data, size_t *size);

#ifdef __cplusplus
}
#endif

#endif // GPU_COMPUTE_H
