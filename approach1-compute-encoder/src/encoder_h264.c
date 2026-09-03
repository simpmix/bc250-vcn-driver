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

static size_t write_slice_header(h264_encoder_t *encoder, bitstream_t *bs,
                                 int slice_type, int qp,
                                 size_t *out_payload_offset)
{
    bool is_idr = (slice_type == SLICE_TYPE_I);

    size_t payload_offset = bs_write_nal_header(bs,
        is_idr ? NAL_REF_IDC_HIGH : NAL_REF_IDC_MEDIUM,
        is_idr ? NAL_TYPE_IDR_SLICE : NAL_TYPE_SLICE);
    *out_payload_offset = payload_offset;

    /* first_mb_in_slice = 0 */
    bs_write_ue(bs, 0);
    /* slice_type */
    bs_write_ue(bs, slice_type);
    /* pic_parameter_set_id */
    bs_write_ue(bs, encoder->pps.pps_id);
    /* frame_num */
    bs_write_u(bs, encoder->sps.log2_max_frame_num + 4, encoder->frame_num);

    if (is_idr) {
        bs_write_ue(bs, encoder->idr_pic_id);
    }

    int poc_bits = encoder->sps.log2_max_poc_lsb + 4;
    bs_write_u(bs, poc_bits, encoder->poc & ((1 << poc_bits) - 1));

    if (!is_idr) {
        bs_write1(bs, 0); /* ref_pic_list_modification_flag_l0 = 0 */
        bs_write1(bs, 0); /* adaptive_ref_pic_marking_mode_flag = 0 */
    }

    if (is_idr) {
        bs_write1(bs, 0); /* no_output_of_prior_pics_flag = 0 */
        bs_write1(bs, 0); /* long_term_reference_flag = 0 */
    }

    int slice_qp_delta = qp - 26 - encoder->pps.pic_init_qp;
    bs_write_se(bs, slice_qp_delta);

    bs_write_ue(bs, 0);  /* disable_deblocking_filter_idc = 0 */
    bs_write_se(bs, 0);  /* slice_alpha_c0_offset_div2 = 0 */
    bs_write_se(bs, 0);  /* slice_beta_offset_div2 = 0 */

    bs_rbsp_trailing_bits(bs);

    return bs_bytes_written(bs);
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

    uint8_t prof_idc = PROFILE_MAIN;
    if (profile == 0x64 /* VAProfileH264High */) prof_idc = PROFILE_HIGH;
    else if (profile == 0x42 /* VAProfileH264Baseline */ || profile == 0x4D) prof_idc = PROFILE_BASELINE;

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
    if (!encoder || !gpu_ctx || !output_buf) return -1;

    bool is_idr = (encoder->frame_count % encoder->gop_size == 0) || encoder->force_idr;
    encoder->force_idr = false;

    if (is_idr) {
        encoder->frame_num = 0;
        encoder->idr_pic_id++;
        encoder->poc = 0;
        encoder->dpb_count = 0;
    }

    int qp = rc_get_frame_qp(&encoder->rc, 0);
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

    /* 3. Write slice header */
    uint8_t slice_hdr_buf[512];
    bitstream_t bs;
    bs_init(&bs, slice_hdr_buf, sizeof(slice_hdr_buf));

    size_t payload_offset = 0;
    size_t rbsp_size = write_slice_header(encoder, &bs,
        is_idr ? SLICE_TYPE_I : SLICE_TYPE_P, qp, &payload_offset);

    memcpy(encoder->output_buf + total_written, slice_hdr_buf, payload_offset);
    total_written += payload_offset;

    size_t ebsp_size = bs_rbsp_to_ebsp(
        encoder->output_buf + total_written,
        encoder->output_buf_size - total_written,
        slice_hdr_buf + payload_offset,
        rbsp_size - payload_offset);
    total_written += ebsp_size;

    /* 4. Dispatch GPU compute encoding pipeline */
    gpu_compute_begin_picture(gpu_ctx, input_surface);
    gpu_compute_dispatch_encode(gpu_ctx, input_surface, encoder->width, encoder->height);
    gpu_compute_end_picture(gpu_ctx);
    gpu_compute_sync(gpu_ctx);

    /* 5. Read back entropy data from GPU staging buffer */
    void *staging_data = NULL;
    size_t staging_size = 0;
    if (gpu_compute_get_staging_data(gpu_ctx, &staging_data, &staging_size) == 0 && staging_data) {
        size_t entropy_bytes = encoder->total_mbs * 8; /* Nominal entropy output size */
        if (entropy_bytes > staging_size) entropy_bytes = staging_size;

        if (total_written + entropy_bytes <= encoder->output_buf_size) {
            memcpy(encoder->output_buf + total_written, staging_data, entropy_bytes);
            total_written += entropy_bytes;
        }
        gpu_compute_release_staging_data(gpu_ctx);
    }

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
