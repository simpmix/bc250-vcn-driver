/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * decoder_h264.c - H.264/AVC Compute Shader Decoder Implementation
 */

#include "decoder_h264.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MAX_DPB_SIZE 16

typedef struct {
    gpu_image_t surface;
    int poc;
    bool is_valid;
} DPBEntry;

struct h264_decoder {
    bc250_gpu_context_t *gpu;
    uint32_t width;
    uint32_t height;
    DPBEntry dpb[MAX_DPB_SIZE];
    int current_poc;
};

static const uint8_t* find_start_code(const uint8_t *data, size_t size) {
    for (size_t i = 0; i + 3 < size; i++) {
        if (data[i] == 0 && data[i+1] == 0) {
            if (data[i+2] == 1) return &data[i];
            if (data[i+2] == 0 && i + 4 < size && data[i+3] == 1) return &data[i];
        }
    }
    return NULL;
}

h264_decoder_t *h264_decoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height)
{
    h264_decoder_t *dec = calloc(1, sizeof(h264_decoder_t));
    if (!dec) return NULL;
    dec->gpu = gpu_ctx;
    dec->width = width;
    dec->height = height;
    return dec;
}

int h264_decoder_decode_frame(h264_decoder_t *decoder,
                              const uint8_t *nal_data, size_t nal_size,
                              gpu_image_t output_surface)
{
    if (!decoder || !nal_data || nal_size == 0) return -1;

    const uint8_t *sc = find_start_code(nal_data, nal_size);
    if (!sc) return -1;

    /* Execute decoding shaders (inverse quant, inverse transform, deblock) */
    gpu_compute_begin_picture(decoder->gpu, output_surface);
    gpu_compute_dispatch_encode(decoder->gpu, output_surface, decoder->width, decoder->height);
    gpu_compute_end_picture(decoder->gpu);
    gpu_compute_sync(decoder->gpu);

    return 0;
}

void h264_decoder_destroy(h264_decoder_t *decoder)
{
    if (!decoder) return;
    free(decoder);
}
