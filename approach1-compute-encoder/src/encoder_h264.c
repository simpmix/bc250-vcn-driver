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

    /* Software reference frame for host/CPU encoding and test paths */
    uint8_t *prev_y_frame;
    bool has_prev_frame;
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

    encoder->prev_y_frame = malloc((size_t)width * height);
    encoder->has_prev_frame = false;

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

void h264_encoder_set_gop_size(h264_encoder_t *encoder, uint32_t gop_size) {
    if (encoder && gop_size > 0) {
        encoder->gop_size = gop_size;
    }
}

void h264_encoder_set_fps(h264_encoder_t *encoder, uint32_t fps) {
    if (encoder && fps > 0) {
        encoder->fps = fps;
        encoder->rc.fps = (double)fps;
    }
}

void h264_encoder_set_qp(h264_encoder_t *encoder, int qp) {
    if (encoder) {
        if (qp < 0) qp = 0;
        if (qp > 51) qp = 51;
        encoder->pps.pic_init_qp = qp;
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
    void *gpu_staging = NULL;
    size_t staging_size = 0;
    if (gpu_ctx && input_surface.y_plane != VK_NULL_HANDLE) {
        gpu_compute_begin_picture(gpu_ctx, input_surface);
        gpu_compute_dispatch_encode(gpu_ctx, input_surface, encoder->width, encoder->height);
        gpu_compute_end_picture(gpu_ctx);
        gpu_compute_sync(gpu_ctx);
        gpu_compute_get_staging_data(gpu_ctx, &gpu_staging, &staging_size);
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
        /* I-slice: macroblocks coded as Intra 16x16 with GPU-informed mode selection */
        for (uint32_t mb = 0; mb < encoder->total_mbs; mb++) {
            int pred_mode = H264_I16x16_DC;
            if (gpu_staging && staging_size >= (mb + 1) * 24 * sizeof(uint32_t)) {
                uint32_t *mb_blocks = ((uint32_t *)gpu_staging) + mb * 24;
                uint32_t top_act = (mb_blocks[0] & 0xFF) + (mb_blocks[1] & 0xFF);
                uint32_t left_act = (mb_blocks[0] & 0xFF) + (mb_blocks[4] & 0xFF);
                if (top_act > left_act * 2) pred_mode = H264_I16x16_VERT;
                else if (left_act > top_act * 2) pred_mode = H264_I16x16_HORIZ;
            }
            cavlc_write_mb_i16x16_header(&bs, pred_mode, 0, 0, 0);
        }
    } else {
        /* P-slice: encode macroblocks with motion-adaptive skip runs */
        uint32_t current_skip_run = 0;
        for (uint32_t mb = 0; mb < encoder->total_mbs; mb++) {
            bool mb_changed = false;
            if (gpu_staging && staging_size >= (mb + 1) * 24 * sizeof(uint32_t)) {
                uint32_t *mb_blocks = ((uint32_t *)gpu_staging) + mb * 24;
                for (int b = 0; b < 16; b++) {
                    if ((mb_blocks[b] & 0xFF) > 0) {
                        mb_changed = true;
                        break;
                    }
                }
            }

            if (!mb_changed) {
                current_skip_run++;
            } else {
                if (current_skip_run > 0) {
                    cavlc_write_p_skip_run(&bs, current_skip_run);
                    current_skip_run = 0;
                }
                cavlc_write_mb_p16x16_header(&bs, 0, 0, 0, 0);
            }
        }
        if (current_skip_run > 0) {
            cavlc_write_p_skip_run(&bs, current_skip_run);
        }
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

int h264_encoder_encode_raw(h264_encoder_t *encoder,
                            const uint8_t *y_plane, int y_pitch,
                            const uint8_t *uv_plane, int uv_pitch,
                            uint8_t *output_buf, size_t output_size)
{
    (void)uv_plane; (void)uv_pitch;
    if (!encoder || !output_buf || !y_plane) return -1;

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

    /* 1. Write AUD */
    total_written += write_aud(encoder->output_buf + total_written,
                               encoder->output_buf_size - total_written,
                               is_idr);

    /* 2. Write SPS / PPS on IDR */
    if (is_idr) {
        total_written += bs_write_sps(encoder->output_buf + total_written,
                                      encoder->output_buf_size - total_written,
                                      &encoder->sps);
        total_written += bs_write_pps(encoder->output_buf + total_written,
                                      encoder->output_buf_size - total_written,
                                      &encoder->pps);
    }

    /* 3. Encode Slice RBSP */
    size_t rbsp_buf_size = encoder->total_mbs * 64 + 4096;
    uint8_t *slice_rbsp = malloc(rbsp_buf_size);
    if (!slice_rbsp) return -1;

    bitstream_t bs;
    bs_init(&bs, slice_rbsp, rbsp_buf_size);

    int slice_type = is_idr ? SLICE_TYPE_I : SLICE_TYPE_P;
    bs_write_ue(&bs, 0);
    bs_write_ue(&bs, (uint32_t)slice_type);
    bs_write_ue(&bs, (uint32_t)encoder->pps.pps_id);
    bs_write_u(&bs, encoder->sps.log2_max_frame_num + 4, encoder->frame_num);

    if (is_idr) {
        bs_write_ue(&bs, encoder->idr_pic_id);
    }

    int poc_bits = encoder->sps.log2_max_poc_lsb + 4;
    bs_write_u(&bs, poc_bits, encoder->poc & ((1 << poc_bits) - 1));

    if (!is_idr) {
        bs_write1(&bs, 0);
        bs_write1(&bs, 0);
        bs_write1(&bs, 0);
    } else {
        bs_write1(&bs, 0);
        bs_write1(&bs, 0);
    }

    int slice_qp_delta = qp - 26 - encoder->pps.pic_init_qp;
    bs_write_se(&bs, slice_qp_delta);

    const char *fm = getenv("BC250_FAST_MODE");
    int deblock_idc = (fm && (strcmp(fm, "1") == 0 || strcmp(fm, "true") == 0)) ? 1 : 0;
    bs_write_ue(&bs, (uint32_t)deblock_idc);
    bs_write_se(&bs, 0);
    bs_write_se(&bs, 0);

    /* Macroblock layer */
    if (is_idr) {
        for (uint32_t mby = 0; mby < encoder->height_in_mbs; mby++) {
            for (uint32_t mbx = 0; mbx < encoder->width_in_mbs; mbx++) {
                uint32_t v_diff = 0, h_diff = 0;
                for (int r = 0; r < 15; r++) {
                    uint32_t py = mby * 16 + r;
                    if (py >= encoder->height - 1) break;
                    for (int c = 0; c < 15; c++) {
                        uint32_t px = mbx * 16 + c;
                        if (px >= encoder->width - 1) break;
                        const uint8_t *p = y_plane + py * y_pitch + px;
                        v_diff += abs((int)p[0] - (int)p[y_pitch]);
                        h_diff += abs((int)p[0] - (int)p[1]);
                    }
                }
                int mode = H264_I16x16_DC;
                if (v_diff * 3 < h_diff * 2) mode = H264_I16x16_VERT;
                else if (h_diff * 3 < v_diff * 2) mode = H264_I16x16_HORIZ;
                cavlc_write_mb_i16x16_header(&bs, mode, 0, 0, 0);
            }
        }
    } else {
        uint32_t current_skip_run = 0;
        for (uint32_t mby = 0; mby < encoder->height_in_mbs; mby++) {
            for (uint32_t mbx = 0; mbx < encoder->width_in_mbs; mbx++) {
                uint32_t sad = 0;
                if (encoder->has_prev_frame && encoder->prev_y_frame) {
                    for (int r = 0; r < 16; r++) {
                        uint32_t py = mby * 16 + r;
                        if (py >= encoder->height) break;
                        for (int c = 0; c < 16; c++) {
                            uint32_t px = mbx * 16 + c;
                            if (px >= encoder->width) break;
                            int curr = y_plane[py * y_pitch + px];
                            int prev = encoder->prev_y_frame[py * encoder->width + px];
                            sad += abs(curr - prev);
                        }
                    }
                }
                if (sad < 512) {
                    current_skip_run++;
                } else {
                    if (current_skip_run > 0) {
                        cavlc_write_p_skip_run(&bs, current_skip_run);
                        current_skip_run = 0;
                    }
                    cavlc_write_mb_p16x16_header(&bs, 0, 0, 0, 0);
                }
            }
        }
        if (current_skip_run > 0) {
            cavlc_write_p_skip_run(&bs, current_skip_run);
        }
    }

    if (encoder->prev_y_frame) {
        for (uint32_t r = 0; r < encoder->height; r++) {
            memcpy(encoder->prev_y_frame + r * encoder->width,
                   y_plane + r * y_pitch,
                   encoder->width);
        }
        encoder->has_prev_frame = true;
    }

    cavlc_write_slice_trailing_bits(&bs);
    bs_flush(&bs);

    size_t rbsp_len = bs_bytes_written(&bs);
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
    if (encoder->prev_y_frame) free(encoder->prev_y_frame);
    free(encoder);
}
