/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * decoder_h264.h - H.264/AVC Compute Shader Decoder API
 */

#ifndef BC250_DECODER_H264_H
#define BC250_DECODER_H264_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gpu_compute.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct h264_decoder h264_decoder_t;

h264_decoder_t *h264_decoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height);

int h264_decoder_decode_frame(h264_decoder_t *decoder,
                              const uint8_t *nal_data, size_t nal_size,
                              gpu_image_t output_surface);

void h264_decoder_destroy(h264_decoder_t *decoder);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DECODER_H264_H */
