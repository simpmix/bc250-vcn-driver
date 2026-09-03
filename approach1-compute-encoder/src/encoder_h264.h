/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * encoder_h264.h - H.264/AVC Compute Shader Encoder API
 */

#ifndef BC250_ENCODER_H264_H
#define BC250_ENCODER_H264_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gpu_compute.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct h264_encoder h264_encoder_t;

/**
 * h264_encoder_create - Allocate and configure an H.264 encoder
 * @gpu_ctx: Vulkan compute context
 * @width: Frame width in pixels
 * @height: Frame height in pixels
 * @fps: Framerate (e.g. 30 or 60)
 * @bitrate: Target bitrate in bits per second
 * @profile: VAProfile (Baseline, Main, High)
 */
h264_encoder_t *h264_encoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height,
                                    uint32_t fps, uint32_t bitrate,
                                    int profile);

/**
 * h264_encoder_encode_frame - Encodes a frame to H.264 Annex B byte stream
 * @encoder: Encoder context
 * @gpu_ctx: Vulkan compute context
 * @input_surface: Input GPU surface
 * @output_buf: Destination buffer for NALUs
 * @output_size: Size of output buffer
 *
 * Returns number of bytes written, or -1 on error.
 */
int h264_encoder_encode_frame(h264_encoder_t *encoder,
                              bc250_gpu_context_t *gpu_ctx,
                              gpu_image_t input_surface,
                              uint8_t *output_buf, size_t output_size);

/**
 * h264_encoder_force_idr - Request next frame to be an instantaneous decoder refresh (IDR)
 */
void h264_encoder_force_idr(h264_encoder_t *encoder);

/**
 * h264_encoder_set_bitrate - Dynamically adjust target bitrate
 */
void h264_encoder_set_bitrate(h264_encoder_t *encoder, uint32_t bitrate_bps);

/**
 * h264_encoder_destroy - Teardown and free resources
 */
void h264_encoder_destroy(h264_encoder_t *encoder);

#ifdef __cplusplus
}
#endif

#endif /* BC250_ENCODER_H264_H */
