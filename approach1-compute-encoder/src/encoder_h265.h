/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * encoder_h265.h - H.265/HEVC Compute Shader Encoder API
 */

#ifndef BC250_ENCODER_H265_H
#define BC250_ENCODER_H265_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gpu_compute.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hevc_encoder hevc_encoder_t;

hevc_encoder_t *hevc_encoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height,
                                    uint32_t fps, uint32_t bitrate);

int hevc_encoder_encode_frame(hevc_encoder_t *encoder,
                              bc250_gpu_context_t *gpu_ctx,
                              gpu_image_t input_surface,
                              uint8_t *output_buf, size_t output_size);

void hevc_encoder_destroy(hevc_encoder_t *encoder);

#ifdef __cplusplus
}
#endif

#endif /* BC250_ENCODER_H265_H */
