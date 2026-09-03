/*
 * Copyright (c) 2026 BC-250 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * bitstream.h - Bit-level stream writer for H.264/H.265 NAL output
 *
 * This module provides the fundamental building block for generating
 * compliant H.264 bitstreams. Every parameter set (SPS, PPS) and
 * slice header is serialized through this interface.
 */

#ifndef BC250_BITSTREAM_H
#define BC250_BITSTREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum NAL unit size (4 MB should be plenty for any single NAL) */
#define BS_MAX_NAL_SIZE  (4 * 1024 * 1024)

/* NAL unit types (H.264 / AVC) */
#define NAL_TYPE_SLICE        1
#define NAL_TYPE_DPA          2
#define NAL_TYPE_DPB          3
#define NAL_TYPE_DPC          4
#define NAL_TYPE_IDR_SLICE    5
#define NAL_TYPE_SEI          6
#define NAL_TYPE_SPS          7
#define NAL_TYPE_PPS          8
#define NAL_TYPE_AUD          9
#define NAL_TYPE_FILLER       12

/* NAL reference IDC values */
#define NAL_REF_IDC_NONE      0
#define NAL_REF_IDC_LOW       1
#define NAL_REF_IDC_MEDIUM    2
#define NAL_REF_IDC_HIGH      3

/* H.264 Profile IDCs */
#define PROFILE_BASELINE  66
#define PROFILE_MAIN      77
#define PROFILE_HIGH      100

/* H.264 Slice types */
#define SLICE_TYPE_P      0
#define SLICE_TYPE_B      1
#define SLICE_TYPE_I      2
#define SLICE_TYPE_SP     3
#define SLICE_TYPE_SI     4

/**
 * Bitstream writer context.
 *
 * Writes bits MSB-first into a byte buffer. Tracks current byte/bit
 * position for sequential writes. Callers should ensure the buffer
 * is large enough before writing (or use bs_bytes_remaining()).
 */
typedef struct bitstream {
    uint8_t *buffer;       /* Output byte buffer */
    size_t   size;         /* Total buffer capacity in bytes */
    size_t   byte_offset;  /* Current byte position */
    int      bit_offset;   /* Current bit position within current byte (0-7, 0=MSB) */
    bool     overflow;     /* Set if any write exceeded buffer capacity */
} bitstream_t;

/**
 * H.264 Sequence Parameter Set (SPS) - key encoding parameters
 */
typedef struct h264_sps {
    uint8_t  profile_idc;          /* 66=Baseline, 77=Main, 100=High */
    uint8_t  level_idc;            /* e.g., 40 = Level 4.0 */
    uint8_t  sps_id;               /* SPS identifier (0-31) */
    uint8_t  chroma_format_idc;    /* 1 = 4:2:0 (standard) */
    uint8_t  bit_depth_luma;       /* 8 or 10 */
    uint8_t  bit_depth_chroma;     /* 8 or 10 */
    uint8_t  log2_max_frame_num;   /* log2(max_frame_num) - 4 */
    uint8_t  pic_order_cnt_type;   /* 0, 1, or 2 */
    uint8_t  log2_max_poc_lsb;     /* log2(max_pic_order_cnt_lsb) - 4 */
    uint32_t max_num_ref_frames;   /* Max reference frames in DPB */
    uint32_t pic_width_in_mbs;     /* Picture width in macroblocks */
    uint32_t pic_height_in_mbs;    /* Picture height in macroblocks */
    bool     frame_mbs_only;       /* true = progressive only */
    bool     direct_8x8_inference; /* Required for Level >= 3.0 */
    bool     frame_cropping;       /* Whether to signal crop offsets */
    uint32_t crop_left;
    uint32_t crop_right;
    uint32_t crop_top;
    uint32_t crop_bottom;
    /* VUI parameters */
    bool     vui_present;
    uint32_t sar_width;            /* Sample aspect ratio */
    uint32_t sar_height;
    bool     timing_info_present;
    uint32_t num_units_in_tick;    /* For framerate signaling */
    uint32_t time_scale;
} h264_sps_t;

/**
 * H.264 Picture Parameter Set (PPS) - per-picture encoding parameters
 */
typedef struct h264_pps {
    uint8_t  pps_id;                   /* PPS identifier (0-255) */
    uint8_t  sps_id;                   /* Referenced SPS */
    bool     entropy_coding_mode;      /* 0=CAVLC, 1=CABAC */
    bool     pic_order_present;        /* Bottom field POC */
    uint8_t  num_ref_idx_l0_default;   /* Default L0 ref count - 1 */
    uint8_t  num_ref_idx_l1_default;   /* Default L1 ref count - 1 */
    bool     weighted_pred;            /* Weighted prediction for P */
    uint8_t  weighted_bipred_idc;      /* Weighted prediction for B */
    int8_t   pic_init_qp;             /* Initial QP - 26 */
    int8_t   chroma_qp_offset;        /* Cb QP offset */
    int8_t   second_chroma_qp_offset; /* Cr QP offset */
    bool     deblocking_filter_control; /* Deblocking filter control */
    bool     constrained_intra_pred;   /* Constrained intra prediction */
    bool     transform_8x8_mode;       /* 8x8 transform (High profile) */
} h264_pps_t;

/* ===== Core bitstream operations ===== */

/** Initialize a bitstream writer over the given buffer. */
void bs_init(bitstream_t *bs, uint8_t *buf, size_t size);

/** Write `bits` bits of `val` into the stream (1-32 bits, MSB-first). */
void bs_write_u(bitstream_t *bs, int bits, uint32_t val);

/** Write a single bit. */
static inline void bs_write1(bitstream_t *bs, uint32_t val) {
    bs_write_u(bs, 1, val);
}

/** Write unsigned Exp-Golomb coded value (ue(v) in the spec). */
void bs_write_ue(bitstream_t *bs, uint32_t val);

/** Write signed Exp-Golomb coded value (se(v) in the spec). */
void bs_write_se(bitstream_t *bs, int32_t val);

/** Write RBSP trailing bits (stop bit + alignment zeros). */
void bs_rbsp_trailing_bits(bitstream_t *bs);

/** Get total bytes written so far (rounded up if mid-byte). */
size_t bs_bytes_written(const bitstream_t *bs);

/** Get remaining capacity in bytes. */
size_t bs_bytes_remaining(const bitstream_t *bs);

/** Flush any partial byte (zero-pad remaining bits in current byte). */
void bs_flush(bitstream_t *bs);

/* ===== NAL unit framing ===== */

/**
 * Write a NAL start code (0x00 0x00 0x00 0x01) and NAL header.
 * Returns the byte offset where the NAL payload begins.
 */
size_t bs_write_nal_header(bitstream_t *bs, int nal_ref_idc, int nal_type);

/**
 * Perform RBSP-to-EBSP emulation prevention (stuffs 0x03 bytes).
 * Takes raw RBSP data, outputs EBSP. Returns output size.
 */
size_t bs_rbsp_to_ebsp(uint8_t *dst, size_t dst_size,
                       const uint8_t *src, size_t src_size);

/* ===== H.264 parameter set serialization ===== */

/** Write a complete SPS NAL unit. Returns bytes written. */
size_t bs_write_sps(uint8_t *buf, size_t buf_size, const h264_sps_t *sps);

/** Write a complete PPS NAL unit. Returns bytes written. */
size_t bs_write_pps(uint8_t *buf, size_t buf_size, const h264_pps_t *pps);

/* ===== Utility ===== */

/** Fill an SPS struct with sensible defaults for given resolution/framerate. */
void h264_sps_default(h264_sps_t *sps, uint32_t width, uint32_t height,
                      uint32_t fps, uint8_t profile);

/** Fill a PPS struct with sensible defaults. */
void h264_pps_default(h264_pps_t *pps, uint8_t sps_id, bool cabac, int32_t qp);

#ifdef __cplusplus
}
#endif

#endif /* BC250_BITSTREAM_H */
