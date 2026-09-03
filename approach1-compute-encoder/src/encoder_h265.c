/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * encoder_h265.c - H.265/HEVC Compute Shader Encoder Implementation
 */

#include "encoder_h265.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define NAL_UNIT_VPS 32
#define NAL_UNIT_SPS 33
#define NAL_UNIT_PPS 34
#define NAL_UNIT_CODED_SLICE_TRAIL_R 1
#define NAL_UNIT_CODED_SLICE_IDR_W_RADL 19

typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t bit_offset;
} HEVCBitstreamWriter;

static void hevc_bs_init(HEVCBitstreamWriter *bs, uint8_t *buffer, size_t size) {
    bs->buffer = buffer;
    bs->size = size;
    bs->bit_offset = 0;
}

static void hevc_bs_write_bits(HEVCBitstreamWriter *bs, uint32_t val, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
        size_t byte_idx = bs->bit_offset / 8;
        size_t bit_idx = 7 - (bs->bit_offset % 8);
        if (byte_idx < bs->size) {
            uint8_t bit = (val >> i) & 1;
            if (bit) {
                bs->buffer[byte_idx] |= (1 << bit_idx);
            } else {
                bs->buffer[byte_idx] &= ~(1 << bit_idx);
            }
        }
        bs->bit_offset++;
    }
}

static void hevc_write_start_code(HEVCBitstreamWriter *bs) {
    hevc_bs_write_bits(bs, 0x00000001, 32);
}

static void hevc_write_nal_header(HEVCBitstreamWriter *bs, int nal_unit_type, int nuh_layer_id, int nuh_temporal_id_plus1) {
    hevc_bs_write_bits(bs, 0, 1); /* forbidden_zero_bit */
    hevc_bs_write_bits(bs, nal_unit_type, 6);
    hevc_bs_write_bits(bs, nuh_layer_id, 6);
    hevc_bs_write_bits(bs, nuh_temporal_id_plus1, 3);
}

static int generate_hevc_vps(HEVCBitstreamWriter *bs) {
    hevc_write_start_code(bs);
    hevc_write_nal_header(bs, NAL_UNIT_VPS, 0, 1);

    hevc_bs_write_bits(bs, 0, 4);   /* vps_video_parameter_set_id */
    hevc_bs_write_bits(bs, 3, 2);   /* vps_base_layer_internal_flag & available_flag */
    hevc_bs_write_bits(bs, 0, 6);   /* vps_max_layers_minus1 */
    hevc_bs_write_bits(bs, 0, 3);   /* vps_max_sub_layers_minus1 */
    hevc_bs_write_bits(bs, 1, 1);   /* vps_temporal_id_nesting_flag */
    hevc_bs_write_bits(bs, 0xffff, 16);

    /* PTL: Main Profile, Main Tier, Level 4.1 */
    hevc_bs_write_bits(bs, 0, 2);
    hevc_bs_write_bits(bs, 0, 1);
    hevc_bs_write_bits(bs, 1, 5);   /* profile_idc = Main */
    hevc_bs_write_bits(bs, 0, 32);
    hevc_bs_write_bits(bs, 0, 48);
    hevc_bs_write_bits(bs, 123, 8); /* level_idc */

    return (int)(bs->bit_offset / 8);
}

static int generate_hevc_sps(HEVCBitstreamWriter *bs, int width, int height) {
    hevc_write_start_code(bs);
    hevc_write_nal_header(bs, NAL_UNIT_SPS, 0, 1);

    hevc_bs_write_bits(bs, 0, 4);
    hevc_bs_write_bits(bs, 0, 3);
    hevc_bs_write_bits(bs, 1, 1);

    /* Profile Tier Level */
    hevc_bs_write_bits(bs, 0, 2);
    hevc_bs_write_bits(bs, 0, 1);
    hevc_bs_write_bits(bs, 1, 5);
    hevc_bs_write_bits(bs, 0, 32);
    hevc_bs_write_bits(bs, 0, 48);
    hevc_bs_write_bits(bs, 123, 8);

    /* Dimensions */
    hevc_bs_write_bits(bs, 0, 4);  /* sps_seq_parameter_set_id */
    hevc_bs_write_bits(bs, 1, 2);  /* chroma_format_idc = 1 (4:2:0) */
    hevc_bs_write_bits(bs, (uint32_t)width, 16);
    hevc_bs_write_bits(bs, (uint32_t)height, 16);

    return (int)(bs->bit_offset / 8);
}

static int generate_hevc_pps(HEVCBitstreamWriter *bs) {
    hevc_write_start_code(bs);
    hevc_write_nal_header(bs, NAL_UNIT_PPS, 0, 1);

    hevc_bs_write_bits(bs, 0, 6);  /* pps_pic_parameter_set_id */
    hevc_bs_write_bits(bs, 0, 4);  /* pps_seq_parameter_set_id */
    hevc_bs_write_bits(bs, 0, 1);  /* dependent_slice_segments_enabled_flag */
    hevc_bs_write_bits(bs, 0, 1);  /* output_flag_present_flag */
    hevc_bs_write_bits(bs, 0, 3);  /* num_extra_slice_header_bits */

    return (int)(bs->bit_offset / 8);
}

struct hevc_encoder {
    bc250_gpu_context_t *gpu;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate;
    uint32_t frame_count;
    uint8_t *output_buf;
    size_t output_buf_size;
};

hevc_encoder_t *hevc_encoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height,
                                    uint32_t fps, uint32_t bitrate)
{
    hevc_encoder_t *enc = calloc(1, sizeof(hevc_encoder_t));
    if (!enc) return NULL;

    enc->gpu = gpu_ctx;
    enc->width = width;
    enc->height = height;
    enc->fps = fps;
    enc->bitrate = bitrate;
    enc->output_buf_size = width * height * 2 + 65536;
    enc->output_buf = malloc(enc->output_buf_size);

    return enc;
}

int hevc_encoder_encode_frame(hevc_encoder_t *encoder,
                              bc250_gpu_context_t *gpu_ctx,
                              gpu_image_t input_surface,
                              uint8_t *output_buf, size_t output_size)
{
    if (!encoder || !gpu_ctx || !output_buf) return -1;

    bool is_idr = (encoder->frame_count % 30 == 0);
    size_t total_written = 0;

    HEVCBitstreamWriter bs;
    hevc_bs_init(&bs, encoder->output_buf, encoder->output_buf_size);

    if (is_idr) {
        generate_hevc_vps(&bs);
        generate_hevc_sps(&bs, encoder->width, encoder->height);
        generate_hevc_pps(&bs);
    }

    hevc_write_start_code(&bs);
    hevc_write_nal_header(&bs, is_idr ? NAL_UNIT_CODED_SLICE_IDR_W_RADL : NAL_UNIT_CODED_SLICE_TRAIL_R, 0, 1);
    hevc_bs_write_bits(&bs, 1, 1); /* first_slice_segment_in_pic_flag */

    total_written = (bs.bit_offset + 7) / 8;

    /* Execute compute shaders */
    gpu_compute_begin_picture(gpu_ctx, input_surface);
    gpu_compute_dispatch_encode(gpu_ctx, input_surface, encoder->width, encoder->height);
    gpu_compute_end_picture(gpu_ctx);
    gpu_compute_sync(gpu_ctx);

    if (output_size < total_written) return -1;
    memcpy(output_buf, encoder->output_buf, total_written);

    encoder->frame_count++;
    return (int)total_written;
}

void hevc_encoder_destroy(hevc_encoder_t *encoder)
{
    if (!encoder) return;
    if (encoder->output_buf) free(encoder->output_buf);
    free(encoder);
}
