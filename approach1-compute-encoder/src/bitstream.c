/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * bitstream.c - Bitstream NAL writer implementation
 */
#include "bitstream.h"
#include <string.h>

void bs_init(bitstream_t *bs, uint8_t *buf, size_t size) {
    bs->buffer = buf;
    bs->size = size;
    bs->byte_offset = 0;
    bs->bit_offset = 0;
    bs->overflow = false;
}

void bs_write_u(bitstream_t *bs, int bits, uint32_t val) {
    if (bs->overflow) return;
    
    while (bits > 0) {
        if (bs->byte_offset >= bs->size) {
            bs->overflow = true;
            return;
        }
        
        int bits_to_write = 8 - bs->bit_offset;
        if (bits_to_write > bits) {
            bits_to_write = bits;
        }
        
        uint32_t mask = (1 << bits_to_write) - 1;
        uint32_t val_part = (val >> (bits - bits_to_write)) & mask;
        
        bs->buffer[bs->byte_offset] &= ~(mask << (8 - bs->bit_offset - bits_to_write));
        bs->buffer[bs->byte_offset] |= (val_part << (8 - bs->bit_offset - bits_to_write));
        
        bs->bit_offset += bits_to_write;
        if (bs->bit_offset == 8) {
            bs->bit_offset = 0;
            bs->byte_offset++;
            if (bs->byte_offset < bs->size) {
                bs->buffer[bs->byte_offset] = 0;
            }
        }
        
        bits -= bits_to_write;
    }
}

void bs_write_ue(bitstream_t *bs, uint32_t val) {
    uint32_t temp = val + 1;
    int zeros = 0;
    while (temp > 1) {
        zeros++;
        temp >>= 1;
    }
    bs_write_u(bs, zeros, 0);
    bs_write_u(bs, zeros + 1, val + 1);
}

void bs_write_se(bitstream_t *bs, int32_t val) {
    uint32_t uval;
    if (val <= 0) {
        uval = -2 * val;
    } else {
        uval = 2 * val - 1;
    }
    bs_write_ue(bs, uval);
}

void bs_rbsp_trailing_bits(bitstream_t *bs) {
    bs_write1(bs, 1);
    while (bs->bit_offset != 0) {
        bs_write1(bs, 0);
    }
}

size_t bs_bytes_written(const bitstream_t *bs) {
    return bs->byte_offset + (bs->bit_offset > 0 ? 1 : 0);
}

size_t bs_bytes_remaining(const bitstream_t *bs) {
    if (bs->size <= bs_bytes_written(bs)) return 0;
    return bs->size - bs_bytes_written(bs);
}

void bs_flush(bitstream_t *bs) {
    if (bs->bit_offset > 0) {
        bs->byte_offset++;
        bs->bit_offset = 0;
    }
}

size_t bs_write_nal_header(bitstream_t *bs, int nal_ref_idc, int nal_type) {
    bs_write_u(bs, 32, 0x00000001);
    uint32_t header = (0 << 7) | ((nal_ref_idc & 3) << 5) | (nal_type & 0x1f);
    bs_write_u(bs, 8, header);
    return bs_bytes_written(bs);
}

size_t bs_rbsp_to_ebsp(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size) {
    size_t i, j = 0;
    int zero_count = 0;
    for (i = 0; i < src_size; i++) {
        if (j >= dst_size) break;
        if (zero_count == 2 && src[i] <= 0x03) {
            dst[j++] = 0x03;
            zero_count = 0;
            if (j >= dst_size) break;
        }
        dst[j++] = src[i];
        if (src[i] == 0x00) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }
    return j;
}

size_t bs_write_sps(uint8_t *buf, size_t buf_size, const h264_sps_t *sps) {
    uint8_t rbsp[1024];
    bitstream_t bs;
    bs_init(&bs, rbsp, sizeof(rbsp));
    
    bs_write_u(&bs, 8, sps->profile_idc);
    bs_write1(&bs, sps->constrained_intra_pred ? 1 : 0);
    bs_write1(&bs, 0); // constraint_set1_flag
    bs_write1(&bs, 0); // constraint_set2_flag
    bs_write1(&bs, 0); // constraint_set3_flag
    bs_write1(&bs, 0); // constraint_set4_flag
    bs_write1(&bs, 0); // constraint_set5_flag
    bs_write_u(&bs, 2, 0); // reserved
    bs_write_u(&bs, 8, sps->level_idc);
    bs_write_ue(&bs, sps->sps_id);
    
    if (sps->profile_idc == PROFILE_HIGH) {
        bs_write_ue(&bs, sps->chroma_format_idc);
        bs_write_ue(&bs, sps->bit_depth_luma - 8);
        bs_write_ue(&bs, sps->bit_depth_chroma - 8);
        bs_write1(&bs, 0); // qpprime_y_zero_transform_bypass_flag
        bs_write1(&bs, 0); // seq_scaling_matrix_present_flag
    }
    
    bs_write_ue(&bs, sps->log2_max_frame_num);
    bs_write_ue(&bs, sps->pic_order_cnt_type);
    if (sps->pic_order_cnt_type == 0) {
        bs_write_ue(&bs, sps->log2_max_poc_lsb);
    }
    bs_write_ue(&bs, sps->max_num_ref_frames);
    bs_write1(&bs, 0); // gaps_in_frame_num_value_allowed_flag
    bs_write_ue(&bs, sps->pic_width_in_mbs - 1);
    bs_write_ue(&bs, sps->pic_height_in_mbs - 1);
    bs_write1(&bs, sps->frame_mbs_only ? 1 : 0);
    if (!sps->frame_mbs_only) {
        bs_write1(&bs, 0); // mb_adaptive_frame_field_flag
    }
    bs_write1(&bs, sps->direct_8x8_inference ? 1 : 0);
    bs_write1(&bs, sps->frame_cropping ? 1 : 0);
    if (sps->frame_cropping) {
        bs_write_ue(&bs, sps->crop_left);
        bs_write_ue(&bs, sps->crop_right);
        bs_write_ue(&bs, sps->crop_top);
        bs_write_ue(&bs, sps->crop_bottom);
    }
    bs_write1(&bs, sps->vui_present ? 1 : 0);
    if (sps->vui_present) {
        bs_write1(&bs, 1); // aspect_ratio_info_present_flag
        bs_write_u(&bs, 8, 255); // Extended_SAR
        bs_write_u(&bs, 16, sps->sar_width);
        bs_write_u(&bs, 16, sps->sar_height);
        bs_write1(&bs, 0); // overscan_info_present_flag
        bs_write1(&bs, 0); // video_signal_type_present_flag
        bs_write1(&bs, 0); // chroma_loc_info_present_flag
        bs_write1(&bs, sps->timing_info_present ? 1 : 0);
        if (sps->timing_info_present) {
            bs_write_u(&bs, 32, sps->num_units_in_tick);
            bs_write_u(&bs, 32, sps->time_scale);
            bs_write1(&bs, 0); // fixed_frame_rate_flag
        }
        bs_write1(&bs, 0); // nal_hrd_parameters_present_flag
        bs_write1(&bs, 0); // vcl_hrd_parameters_present_flag
        bs_write1(&bs, 0); // pic_struct_present_flag
        bs_write1(&bs, 0); // bitstream_restriction_flag
    }
    
    bs_rbsp_trailing_bits(&bs);
    
    bitstream_t out_bs;
    bs_init(&out_bs, buf, buf_size);
    bs_write_nal_header(&out_bs, NAL_REF_IDC_HIGH, NAL_TYPE_SPS);
    size_t payload_offset = bs_bytes_written(&out_bs);
    size_t ebsp_size = bs_rbsp_to_ebsp(buf + payload_offset, buf_size - payload_offset, rbsp, bs_bytes_written(&bs));
    
    return payload_offset + ebsp_size;
}

size_t bs_write_pps(uint8_t *buf, size_t buf_size, const h264_pps_t *pps) {
    uint8_t rbsp[1024];
    bitstream_t bs;
    bs_init(&bs, rbsp, sizeof(rbsp));
    
    bs_write_ue(&bs, pps->pps_id);
    bs_write_ue(&bs, pps->sps_id);
    bs_write1(&bs, pps->entropy_coding_mode ? 1 : 0);
    bs_write1(&bs, pps->pic_order_present ? 1 : 0);
    bs_write_ue(&bs, 0); // num_slice_groups_minus1
    bs_write_ue(&bs, pps->num_ref_idx_l0_default);
    bs_write_ue(&bs, pps->num_ref_idx_l1_default);
    bs_write1(&bs, pps->weighted_pred ? 1 : 0);
    bs_write_u(&bs, 2, pps->weighted_bipred_idc);
    bs_write_se(&bs, pps->pic_init_qp);
    bs_write_se(&bs, 0); // pic_init_qs
    bs_write_se(&bs, pps->chroma_qp_offset);
    bs_write1(&bs, pps->deblocking_filter_control ? 1 : 0);
    bs_write1(&bs, pps->constrained_intra_pred ? 1 : 0);
    bs_write1(&bs, 0); // redundant_pic_cnt_present_flag
    if (pps->transform_8x8_mode) {
        bs_write1(&bs, 1);
        bs_write1(&bs, 0); // pic_scaling_matrix_present_flag
        bs_write_se(&bs, pps->second_chroma_qp_offset);
    }
    
    bs_rbsp_trailing_bits(&bs);
    
    bitstream_t out_bs;
    bs_init(&out_bs, buf, buf_size);
    bs_write_nal_header(&out_bs, NAL_REF_IDC_HIGH, NAL_TYPE_PPS);
    size_t payload_offset = bs_bytes_written(&out_bs);
    size_t ebsp_size = bs_rbsp_to_ebsp(buf + payload_offset, buf_size - payload_offset, rbsp, bs_bytes_written(&bs));
    
    return payload_offset + ebsp_size;
}

void h264_sps_default(h264_sps_t *sps, uint32_t width, uint32_t height, uint32_t fps, uint8_t profile) {
    memset(sps, 0, sizeof(h264_sps_t));
    sps->profile_idc = profile;
    sps->level_idc = 40;
    sps->sps_id = 0;
    sps->chroma_format_idc = 1;
    sps->bit_depth_luma = 8;
    sps->bit_depth_chroma = 8;
    sps->log2_max_frame_num = 4;
    sps->pic_order_cnt_type = 0;
    sps->log2_max_poc_lsb = 4;
    sps->max_num_ref_frames = 1;
    sps->pic_width_in_mbs = (width + 15) / 16;
    sps->pic_height_in_mbs = (height + 15) / 16;
    sps->frame_mbs_only = true;
    sps->direct_8x8_inference = true;
    sps->frame_cropping = (width % 16 != 0 || height % 16 != 0);
    sps->crop_left = 0;
    sps->crop_right = (sps->pic_width_in_mbs * 16 - width) / 2;
    sps->crop_top = 0;
    sps->crop_bottom = (sps->pic_height_in_mbs * 16 - height) / 2;
    sps->vui_present = true;
    sps->sar_width = 1;
    sps->sar_height = 1;
    sps->timing_info_present = true;
    sps->num_units_in_tick = 1;
    sps->time_scale = fps * 2;
}

void h264_pps_default(h264_pps_t *pps, uint8_t sps_id, bool cabac, int32_t qp) {
    memset(pps, 0, sizeof(h264_pps_t));
    pps->pps_id = 0;
    pps->sps_id = sps_id;
    pps->entropy_coding_mode = cabac;
    pps->pic_order_present = false;
    pps->num_ref_idx_l0_default = 0;
    pps->num_ref_idx_l1_default = 0;
    pps->weighted_pred = false;
    pps->weighted_bipred_idc = 0;
    pps->pic_init_qp = qp - 26;
    pps->chroma_qp_offset = 0;
    pps->second_chroma_qp_offset = 0;
    pps->deblocking_filter_control = true;
    pps->constrained_intra_pred = false;
    pps->transform_8x8_mode = false;
}
