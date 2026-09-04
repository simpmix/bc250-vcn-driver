/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * encoder_h264.c - H.264/AVC Compute Shader Encoder Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "bitstream.h"
#include "cavlc.h"
#include "rate_control.h"
#include "gpu_compute.h"
#include "encoder_h264.h"

/* Decoded Picture Buffer entry */
typedef struct dpb_entry {
    gpu_image_t image;          /* Reconstructed frame on GPU */
    gpu_memory_t memory;
    int frame_num;              /* H.264 frame_num */
    int poc;                    /* Picture order count */
    bool is_reference;          /* Used as reference? */
    bool is_long_term;          /* Long-term reference */
} dpb_entry_t;

/* H.264 encoder state */
struct h264_encoder {
    /* Dimensions */
    uint32_t width, height;
    uint32_t width_in_mbs, height_in_mbs;
    uint32_t total_mbs;

    /* GOP & Stream structure */
    uint32_t fps;
    uint32_t gop_size;          /* IDR interval */
    uint32_t frame_count;       /* Total frames encoded */
    uint32_t frame_num;         /* H.264 frame_num (resets at IDR) */
    uint32_t idr_pic_id;        /* Increments at each IDR */
    int poc;                    /* Picture order count */
    bool force_idr;             /* Dynamic keyframe request flag */

    /* Parameters */
    h264_sps_t sps;
    h264_pps_t pps;
    rate_control_t rc;

    /* DPB */
    dpb_entry_t dpb[16];
    int dpb_count;
    int dpb_max;

    /* GPU context reference */
    bc250_gpu_context_t *gpu;

    /* Output buffer */
    uint8_t *output_buf;
    size_t output_buf_size;
};

static void manage_dpb(h264_encoder_t *encoder, int new_frame_num, int new_poc)
{
    if (encoder->dpb_count >= encoder->dpb_max) {
        memmove(&encoder->dpb[0], &encoder->dpb[1],
                sizeof(dpb_entry_t) * (encoder->dpb_max - 1));
        encoder->dpb_count--;
    }

    dpb_entry_t *entry = &encoder->dpb[encoder->dpb_count++];
    memset(entry, 0, sizeof(*entry));
    entry->frame_num = new_frame_num;
    entry->poc = new_poc;
    entry->is_reference = true;
    entry->is_long_term = false;
}

/*
 * write_aud - Writes Access Unit Delimiter (NAL type 9)
 * Essential for Sunshine / Moonlight / WebRTC to identify frame boundaries.
 */
static size_t write_aud(uint8_t *buf, size_t buf_size, bool is_idr) {
    if (buf_size < 6) return 0;
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x01;
    buf[4] = 0x09; /* NAL header: forbidden=0, ref_idc=0, type=9 (AUD) */
    buf[5] = is_idr ? 0x10 : 0x30; /* primary_pic_type: 0 for I, 1 for P (shifted) + stop bit */
    return 6;
}

h264_encoder_t *h264_encoder_create(bc250_gpu_context_t *gpu_ctx,
                                    uint32_t width, uint32_t height,
                                    uint32_t fps, uint32_t bitrate,
                                    int profile)
{
    h264_encoder_t *encoder = calloc(1, sizeof(h264_encoder_t));
    if (!encoder) return NULL;

    encoder->gpu = gpu_ctx;
    encoder->width = width;
    encoder->height = height;
    encoder->width_in_mbs = (width + 15) / 16;
    encoder->height_in_mbs = (height + 15) / 16;
    encoder->total_mbs = encoder->width_in_mbs * encoder->height_in_mbs;

    encoder->fps = fps > 0 ? fps : 60;
    encoder->gop_size = encoder->fps; /* 1-second default keyframe interval */
    encoder->dpb_max = 16;
    encoder->force_idr = false;

    uint8_t prof_idc = PROFILE_BASELINE;
    if (profile == 0x64 /* VAProfileH264High */) prof_idc = PROFILE_HIGH;
    else if (profile == 0x4D /* VAProfileH264Main */) prof_idc = PROFILE_MAIN;

    h264_sps_default(&encoder->sps, width, height, encoder->fps, prof_idc);
    h264_pps_default(&encoder->pps, encoder->sps.sps_id, false, 26);

    rc_init(&encoder->rc, RC_CBR, bitrate, (double)encoder->fps);

    encoder->output_buf_size = width * height * 2 + 65536;
    encoder->output_buf = malloc(encoder->output_buf_size);
    if (!encoder->output_buf) {
        free(encoder);
        return NULL;
    }

    fprintf(stderr, "[bc250-h264] Encoder initialized: %ux%u @ %u fps, %u bps, profile %d\n",
            width, height, encoder->fps, bitrate, prof_idc);

    return encoder;
}

void h264_encoder_force_idr(h264_encoder_t *encoder) {
    if (encoder) {
        encoder->force_idr = true;
    }
}

void h264_encoder_set_bitrate(h264_encoder_t *encoder, uint32_t bitrate_bps) {
    if (encoder && bitrate_bps > 0) {
        rc_init(&encoder->rc, RC_CBR, bitrate_bps, (double)encoder->fps);
    }
}

int h264_encoder_encode_frame(h264_encoder_t *encoder,
                              bc250_gpu_context_t *gpu_ctx,
                              gpu_image_t input_surface,
                              uint8_t *output_buf, size_t output_size)
{
    if (!encoder || !output_buf) return -1;

    bool is_idr = (encoder->frame_count % encoder->gop_size == 0) || encoder->force_idr;
    encoder->force_idr = false;

    if (is_idr) {
        encoder->frame_num = 0;
        encoder->idr_pic_id++;
        encoder->poc = 0;
        encoder->dpb_count = 0;
    }

    int qp = rc_get_frame_qp(&encoder->rc, 0);
    if (qp < 12) qp = 12;
    if (qp > 51) qp = 51;

    size_t total_written = 0;

    /* 1. Write AUD (Access Unit Delimiter) NALU */
    total_written += write_aud(encoder->output_buf + total_written,
                               encoder->output_buf_size - total_written,
                               is_idr);

    /* 2. Write SPS and PPS NALUs on IDR frames */
    if (is_idr) {
        size_t sps_size = bs_write_sps(
            encoder->output_buf + total_written,
            encoder->output_buf_size - total_written,
            &encoder->sps);
        total_written += sps_size;

        size_t pps_size = bs_write_pps(
            encoder->output_buf + total_written,
            encoder->output_buf_size - total_written,
            &encoder->pps);
        total_written += pps_size;
    }

    /* 3. Dispatch GPU compute encoding pipeline if available */
    if (gpu_ctx && input_surface.y_plane != VK_NULL_HANDLE) {
        gpu_compute_begin_picture(gpu_ctx, input_surface);
        gpu_compute_dispatch_encode(gpu_ctx, input_surface, encoder->width, encoder->height);
        gpu_compute_end_picture(gpu_ctx);
        gpu_compute_sync(gpu_ctx);
    }

    /* 4. Encode Slice RBSP into temporary buffer */
    size_t rbsp_buf_size = encoder->total_mbs * 64 + 4096;
    uint8_t *slice_rbsp = malloc(rbsp_buf_size);
    if (!slice_rbsp) return -1;

    bitstream_t bs;
    bs_init(&bs, slice_rbsp, rbsp_buf_size);

    /* 4a. Slice Header per H.264 Section 7.3.3 */
    int slice_type = is_idr ? SLICE_TYPE_I : SLICE_TYPE_P;
    bs_write_ue(&bs, 0); /* first_mb_in_slice = 0 */
    bs_write_ue(&bs, (uint32_t)slice_type);
    bs_write_ue(&bs, (uint32_t)encoder->pps.pps_id);
    bs_write_u(&bs, encoder->sps.log2_max_frame_num + 4, encoder->frame_num);

    if (is_idr) {
        bs_write_ue(&bs, encoder->idr_pic_id);
    }

    int poc_bits = encoder->sps.log2_max_poc_lsb + 4;
    bs_write_u(&bs, poc_bits, encoder->poc & ((1 << poc_bits) - 1));

    if (!is_idr) {
        bs_write1(&bs, 0); /* num_ref_idx_active_override_flag = 0 */
        bs_write1(&bs, 0); /* ref_pic_list_modification_flag_l0 = 0 */
        bs_write1(&bs, 0); /* adaptive_ref_pic_marking_mode_flag = 0 */
    } else {
        bs_write1(&bs, 0); /* no_output_of_prior_pics_flag = 0 */
        bs_write1(&bs, 0); /* long_term_reference_flag = 0 */
    }

    int slice_qp_delta = qp - 26 - encoder->pps.pic_init_qp;
    bs_write_se(&bs, slice_qp_delta);

    /* Deblocking filter control: 1 = disabled (fast gaming mode) */
    const char *fm = getenv("BC250_FAST_MODE");
    int deblock_idc = (fm && (strcmp(fm, "1") == 0 || strcmp(fm, "true") == 0)) ? 1 : 0;
    bs_write_ue(&bs, (uint32_t)deblock_idc);
    bs_write_se(&bs, 0);
    bs_write_se(&bs, 0);

    /* 4b. Slice Data (Macroblock Layer) using CAVLC per Section 7.3.4 */
    if (is_idr) {
        /* I-slice: all macroblocks coded as Intra 16x16 DC */
        for (uint32_t mb = 0; mb < encoder->total_mbs; mb++) {
            cavlc_write_mb_i16x16_header(&bs, H264_I16x16_DC, 0, 0, 0);
        }
    } else {
        /* P-slice: encode all macroblocks as P_Skip (skip_run = total_mbs) */
        cavlc_write_p_skip_run(&bs, encoder->total_mbs);
    }

    /* 4c. RBSP Trailing bits (1 followed by zero bits to byte boundary) */
    cavlc_write_slice_trailing_bits(&bs);
    bs_flush(&bs);

    size_t rbsp_len = bs_bytes_written(&bs);

    /* 5. Assemble Slice NAL unit: 4-byte start code + NAL header + EBSP */
    if (total_written + 5 + rbsp_len * 2 <= encoder->output_buf_size) {
        uint8_t *nal_dst = encoder->output_buf + total_written;
        nal_dst[0] = 0x00;
        nal_dst[1] = 0x00;
        nal_dst[2] = 0x00;
        nal_dst[3] = 0x01;
        nal_dst[4] = is_idr ? ((NAL_REF_IDC_HIGH << 5) | NAL_TYPE_IDR_SLICE)
                            : ((NAL_REF_IDC_MEDIUM << 5) | NAL_TYPE_SLICE);

        size_t ebsp_len = bs_rbsp_to_ebsp(nal_dst + 5,
                                          encoder->output_buf_size - total_written - 5,
                                          slice_rbsp,
                                          rbsp_len);
        total_written += 5 + ebsp_len;
    }
    free(slice_rbsp);

    if (output_size < total_written) {
        fprintf(stderr, "[bc250-h264] Output buffer too small: need %zu, have %zu\n",
                total_written, output_size);
        return -1;
    }
    memcpy(output_buf, encoder->output_buf, total_written);

    rc_update_stats(&encoder->rc, (int)(total_written * 8));
    manage_dpb(encoder, encoder->frame_num, encoder->poc);

    encoder->frame_num++;
    encoder->poc += 2;
    encoder->frame_count++;

    return (int)total_written;
}

void h264_encoder_destroy(h264_encoder_t *encoder)
{
    if (!encoder) return;
    if (encoder->output_buf) free(encoder->output_buf);
    free(encoder);
}
