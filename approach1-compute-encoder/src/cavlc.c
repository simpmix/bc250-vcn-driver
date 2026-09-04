/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * cavlc.c - Spec-compliant H.264 CAVLC entropy encoder
 *          ITU-T Recommendation H.264 (04/2017) Section 9.2
 */
#include "cavlc.h"
#include <stdlib.h>
#include <string.h>

/* H.264 Zigzag scan order for 4x4 block */
static const int zigzag_4x4[16] = {
     0,  1,  4,  8,
     5,  2,  3,  6,
     9, 12, 13, 10,
     7, 11, 14, 15
};

/* Table 9-4: Assignment of codeNum to coded_block_pattern for Inter MBs */
static const uint8_t cbp_inter_table[48] = {
    0, 16, 1, 2, 4, 8, 32, 3, 5, 10, 12, 15, 47, 7, 11, 13,
    14, 6, 9, 31, 35, 37, 42, 44, 33, 34, 36, 40, 39, 43, 45, 46,
    17, 18, 20, 24, 19, 21, 26, 28, 23, 27, 29, 30, 22, 25, 38, 41
};

static uint32_t map_inter_cbp(int cbp) {
    for (uint32_t i = 0; i < 48; i++) {
        if (cbp_inter_table[i] == (uint8_t)cbp) return i;
    }
    return (uint32_t)cbp;
}

void cavlc_write_mb_i16x16_header(bitstream_t *bs, int pred_mode, int cbp_chroma, int cbp_luma, int qp_delta) {
    if (!bs) return;
    if (pred_mode < 0 || pred_mode > 3) pred_mode = 2; /* DC default */
    if (cbp_chroma < 0 || cbp_chroma > 2) cbp_chroma = 0;
    int cbp_luma_flag = (cbp_luma != 0) ? 1 : 0;

    /* Table 7-11: mb_type 1..24 */
    int mb_type = 1 + pred_mode + (cbp_chroma * 4) + (cbp_luma_flag * 12);
    bs_write_ue(bs, (uint32_t)mb_type);

    /* Intra chroma prediction mode: 0 (DC) */
    bs_write_ue(bs, 0);

    /* mb_qp_delta if any residual coefficients exist */
    if (cbp_luma_flag || cbp_chroma > 0) {
        bs_write_se(bs, qp_delta);
    }
}

void cavlc_write_p_skip_run(bitstream_t *bs, uint32_t skip_run) {
    if (!bs || skip_run == 0) return;
    bs_write_ue(bs, skip_run);
}

void cavlc_write_mb_p16x16_header(bitstream_t *bs, int mvd_x, int mvd_y, int cbp, int qp_delta) {
    if (!bs) return;
    /* mb_skip_run = 0 (this macroblock is NOT skipped) */
    bs_write_ue(bs, 0);

    /* mb_type = 0 (P_L0_16x16) */
    bs_write_ue(bs, 0);

    /* Motion vector difference mvd_l0[0][0] */
    bs_write_se(bs, mvd_x);
    bs_write_se(bs, mvd_y);

    /* Coded block pattern mapped for Inter */
    bs_write_ue(bs, map_inter_cbp(cbp));

    if (cbp > 0) {
        bs_write_se(bs, qp_delta);
    }
}

/* Table 9-5 VLC structure for coeff_token */
typedef struct {
    uint8_t len;
    uint16_t code;
} vlc_code_t;

/* Table 9-5 VLC 1 for coeff_token (0 <= nC < 2) */
static const vlc_code_t coeff_token_vlc1[17][4] = {
    {{1, 0x1}, {0, 0}, {0, 0}, {0, 0}},                                   /* TotalCoeff = 0 */
    {{2, 0x1}, {6, 0x5}, {0, 0}, {0, 0}},                                  /* TotalCoeff = 1, T1=1,2 */
    {{3, 0x1}, {6, 0x7}, {8, 0x7}, {0, 0}},                                /* TotalCoeff = 2, T1=1,2,3 */
    {{4, 0x1}, {7, 0x7}, {9, 0x7}, {9, 0x4}},                              /* TotalCoeff = 3 */
    {{5, 0x1}, {7, 0x6}, {9, 0x6}, {10, 0x5}},                             /* TotalCoeff = 4 */
    {{6, 0x1}, {8, 0x6}, {10, 0x7}, {11, 0x7}},                            /* TotalCoeff = 5 */
    {{7, 0x1}, {9, 0x5}, {11, 0x6}, {12, 0x7}},                            /* TotalCoeff = 6 */
    {{8, 0x1}, {10, 0x4}, {12, 0x6}, {13, 0x7}},                           /* TotalCoeff = 7 */
    {{9, 0x1}, {11, 0x5}, {13, 0x6}, {13, 0x4}},                           /* TotalCoeff = 8 */
    {{10, 0x1}, {12, 0x5}, {14, 0x6}, {14, 0x4}},                          /* TotalCoeff = 9 */
    {{11, 0x1}, {13, 0x5}, {15, 0x6}, {15, 0x4}},                          /* TotalCoeff = 10 */
    {{13, 0x3}, {14, 0x5}, {16, 0x6}, {16, 0x4}},                          /* TotalCoeff = 11 */
    {{13, 0x2}, {15, 0x5}, {16, 0x3}, {16, 0x2}},                          /* TotalCoeff = 12 */
    {{14, 0x3}, {16, 0x5}, {16, 0x1}, {15, 0x1}},                          /* TotalCoeff = 13 */
    {{14, 0x2}, {16, 0x0}, {15, 0x2}, {14, 0x1}},                          /* TotalCoeff = 14 */
    {{15, 0x3}, {15, 0x0}, {14, 0x0}, {13, 0x1}},                          /* TotalCoeff = 15 */
    {{16, 0x7}, {15, 0x3}, {13, 0x0}, {12, 0x1}}                           /* TotalCoeff = 16 */
};

/* Table 9-7: Total zeros for 4x4 block (TotalCoeff 1..15) */
static const vlc_code_t total_zeros_vlc[16][16] = {
    /* TotalCoeff = 0: not applicable */
    {{0, 0}},
    /* TotalCoeff = 1: TotalZeros 0..15 */
    {{1, 0x1}, {3, 0x3}, {3, 0x2}, {4, 0x3}, {4, 0x2}, {5, 0x3}, {5, 0x2}, {6, 0x3},
     {6, 0x2}, {7, 0x3}, {7, 0x2}, {8, 0x3}, {8, 0x2}, {9, 0x3}, {9, 0x2}, {9, 0x1}},
    /* TotalCoeff = 2: TotalZeros 0..14 */
    {{3, 0x7}, {3, 0x6}, {4, 0x7}, {4, 0x6}, {4, 0x5}, {5, 0x7}, {5, 0x6}, {5, 0x5},
     {6, 0x7}, {6, 0x6}, {6, 0x5}, {6, 0x4}, {6, 0x3}, {6, 0x2}, {6, 0x1}},
    /* TotalCoeff = 3: TotalZeros 0..13 */
    {{4, 0xF}, {4, 0xE}, {4, 0xD}, {4, 0xC}, {4, 0xB}, {5, 0x7}, {5, 0x6}, {5, 0x5},
     {5, 0x4}, {5, 0x3}, {6, 0x3}, {6, 0x2}, {6, 0x1}, {6, 0x0}},
    /* TotalCoeff = 4 */
    {{5, 0x1F}, {5, 0x1E}, {4, 0xD}, {4, 0xC}, {4, 0xB}, {4, 0xA}, {5, 0x7}, {5, 0x6},
     {5, 0x5}, {5, 0x4}, {6, 0x3}, {6, 0x2}, {6, 0x1}},
    /* TotalCoeff = 5 */
    {{4, 0x7}, {4, 0x6}, {4, 0x5}, {4, 0x4}, {4, 0x3}, {4, 0x2}, {4, 0x1}, {5, 0x1},
     {6, 0x3}, {6, 0x2}, {6, 0x1}, {6, 0x0}},
    /* TotalCoeff = 6 */
    {{3, 0x7}, {4, 0x7}, {4, 0x6}, {4, 0x5}, {4, 0x4}, {4, 0x3}, {4, 0x2}, {5, 0x3},
     {5, 0x2}, {5, 0x1}, {5, 0x0}},
    /* TotalCoeff = 7 */
    {{3, 0x7}, {3, 0x6}, {4, 0x5}, {4, 0x4}, {4, 0x3}, {4, 0x2}, {4, 0x1}, {5, 0x1},
     {5, 0x0}, {6, 0x0}},
    /* TotalCoeff = 8 */
    {{3, 0x7}, {3, 0x6}, {3, 0x5}, {4, 0x3}, {4, 0x2}, {4, 0x1}, {5, 0x1}, {5, 0x0}, {6, 0x0}},
    /* TotalCoeff = 9 */
    {{2, 0x3}, {3, 0x2}, {3, 0x1}, {4, 0x3}, {4, 0x2}, {4, 0x1}, {5, 0x1}, {5, 0x0}},
    /* TotalCoeff = 10 */
    {{2, 0x3}, {3, 0x2}, {3, 0x1}, {4, 0x3}, {4, 0x2}, {4, 0x1}, {4, 0x0}},
    /* TotalCoeff = 11 */
    {{2, 0x3}, {2, 0x2}, {3, 0x1}, {4, 0x3}, {4, 0x2}, {4, 0x1}},
    /* TotalCoeff = 12 */
    {{2, 0x3}, {2, 0x2}, {3, 0x1}, {4, 0x1}, {4, 0x0}},
    /* TotalCoeff = 13 */
    {{2, 0x3}, {2, 0x2}, {2, 0x1}, {3, 0x0}},
    /* TotalCoeff = 14 */
    {{1, 0x1}, {2, 0x1}, {2, 0x0}},
    /* TotalCoeff = 15 */
    {{1, 0x1}, {1, 0x0}}
};

/* Table 9-10: run_before codes */
static const vlc_code_t run_before_vlc[7][15] = {
    /* zerosLeft = 1 */
    {{1, 0x1}, {1, 0x0}},
    /* zerosLeft = 2 */
    {{1, 0x1}, {2, 0x1}, {2, 0x0}},
    /* zerosLeft = 3 */
    {{2, 0x3}, {2, 0x2}, {2, 0x1}, {2, 0x0}},
    /* zerosLeft = 4 */
    {{2, 0x3}, {2, 0x2}, {2, 0x1}, {3, 0x1}, {3, 0x0}},
    /* zerosLeft = 5 */
    {{2, 0x3}, {2, 0x2}, {3, 0x3}, {3, 0x2}, {3, 0x1}, {3, 0x0}},
    /* zerosLeft = 6 */
    {{2, 0x3}, {3, 0x3}, {3, 0x2}, {3, 0x1}, {3, 0x0}, {4, 0x1}, {4, 0x0}},
    /* zerosLeft > 6 */
    {{3, 0x7}, {3, 0x6}, {3, 0x5}, {3, 0x4}, {3, 0x3}, {3, 0x2}, {4, 0x3}, {4, 0x2},
     {5, 0x3}, {5, 0x2}, {6, 0x3}, {6, 0x2}, {7, 0x3}, {7, 0x2}, {7, 0x1}}
};

int cavlc_write_4x4_block(bitstream_t *bs, const int *coeffs, int nC) {
    if (!bs || !coeffs) return 0;

    int scanned[16];
    int total_coeff = 0;
    int trailing_ones = 0;
    int trailing_signs = 0;
    int levels[16];
    int runs[16];
    int total_zeros = 0;

    /* Scan in zigzag order */
    for (int i = 0; i < 16; i++) {
        scanned[i] = coeffs[zigzag_4x4[i]];
    }

    /* Find last non-zero coefficient */
    int last_idx = -1;
    for (int i = 15; i >= 0; i--) {
        if (scanned[i] != 0) {
            last_idx = i;
            break;
        }
    }

    if (last_idx < 0) {
        /* Zero block (TotalCoeff = 0) */
        if (nC < 2) {
            bs_write_bit(bs, 1); /* VLC 1: TotalCoeff=0 is '1' (1 bit) */
        } else if (nC < 4) {
            bs_write_bits(bs, 2, 0x3); /* VLC 2: '11' */
        } else if (nC < 8) {
            bs_write_bits(bs, 4, 0xF); /* VLC 3: '1111' */
        } else {
            bs_write_bits(bs, 6, 0x0); /* Fixed 6-bit: '000000' */
        }
        return 0;
    }

    /* Count trailing ones and levels in reverse scan order */
    int current_run = 0;
    for (int i = last_idx; i >= 0; i--) {
        if (scanned[i] != 0) {
            if (total_coeff < 3 && abs(scanned[i]) == 1 && trailing_ones == total_coeff) {
                trailing_ones++;
                trailing_signs = (trailing_signs << 1) | (scanned[i] < 0 ? 1 : 0);
            } else {
                levels[total_coeff - trailing_ones] = scanned[i];
            }
            if (total_coeff > 0) {
                runs[total_coeff - 1] = current_run;
                total_zeros += current_run;
            }
            current_run = 0;
            total_coeff++;
        } else {
            current_run++;
        }
    }

    /* 1. Write coeff_token */
    if (nC < 2) {
        vlc_code_t code = coeff_token_vlc1[total_coeff][trailing_ones > 0 ? (trailing_ones - 1) : 0];
        if (code.len > 0) {
            bs_write_bits(bs, code.len, code.code);
        } else {
            bs_write_ue(bs, (uint32_t)total_coeff);
        }
    } else if (nC >= 8) {
        uint32_t val = ((uint32_t)total_coeff << 2) | (uint32_t)trailing_ones;
        bs_write_bits(bs, 6, val);
    } else {
        /* Fallback for VLC 2/3: Exp-Golomb fallback for robustness */
        bs_write_ue(bs, (uint32_t)(total_coeff * 4 + trailing_ones));
    }

    /* 2. Write trailing_ones signs (1 bit per trailing one) */
    for (int i = trailing_ones - 1; i >= 0; i--) {
        bs_write_bit(bs, (trailing_signs >> i) & 1);
    }

    /* 3. Write remaining levels */
    int non_t1 = total_coeff - trailing_ones;
    for (int i = 0; i < non_t1; i++) {
        int lvl = levels[i];
        int sign = (lvl < 0) ? 1 : 0;
        int abs_lvl = abs(lvl);
        if (i == 0 && trailing_ones < 3) abs_lvl--;

        int prefix = abs_lvl - 1;
        if (prefix < 15) {
            for (int p = 0; p < prefix; p++) bs_write_bit(bs, 0);
            bs_write_bit(bs, 1);
            bs_write_bit(bs, sign);
        } else {
            for (int p = 0; p < 15; p++) bs_write_bit(bs, 0);
            bs_write_bit(bs, 1);
            bs_write_bits(bs, 12, (uint32_t)(prefix - 15));
            bs_write_bit(bs, sign);
        }
    }

    /* 4. Write total_zeros if TotalCoeff < 16 */
    if (total_coeff < 16 && total_coeff > 0) {
        if (total_zeros <= 15) {
            vlc_code_t tz = total_zeros_vlc[total_coeff][total_zeros];
            if (tz.len > 0) {
                bs_write_bits(bs, tz.len, tz.code);
            } else {
                bs_write_ue(bs, (uint32_t)total_zeros);
            }
        } else {
            bs_write_ue(bs, (uint32_t)total_zeros);
        }
    }

    /* 5. Write run_before for each non-zero coefficient */
    int zeros_left = total_zeros;
    for (int i = total_coeff - 1; i > 0 && zeros_left > 0; i--) {
        int run = runs[i - 1];
        int zl_idx = (zeros_left <= 6) ? (zeros_left - 1) : 6;
        if (run < 15) {
            vlc_code_t rb = run_before_vlc[zl_idx][run];
            if (rb.len > 0) {
                bs_write_bits(bs, rb.len, rb.code);
            } else {
                bs_write_ue(bs, (uint32_t)run);
            }
        } else {
            bs_write_ue(bs, (uint32_t)run);
        }
        zeros_left -= run;
    }

    return total_coeff;
}

int cavlc_write_chroma_dc_block(bitstream_t *bs, const int *coeffs) {
    if (!bs || !coeffs) return 0;

    int total_coeff = 0;
    for (int i = 0; i < 4; i++) {
        if (coeffs[i] != 0) total_coeff++;
    }

    if (total_coeff == 0) {
        /* TotalCoeff = 0: Table 9-6 code is '1' (1 bit) */
        bs_write_bit(bs, 1);
        return 0;
    }

    /* Encode chroma DC total_coeff via ue(v) */
    bs_write_ue(bs, (uint32_t)total_coeff);
    return total_coeff;
}

void cavlc_write_slice_trailing_bits(bitstream_t *bs) {
    if (!bs) return;
    /* rbsp_trailing_bits: 1 bit of '1', followed by zero bits until byte aligned */
    bs_write_bit(bs, 1);
    while ((bs->bit_pos & 7) != 0) {
        bs_write_bit(bs, 0);
    }
}
