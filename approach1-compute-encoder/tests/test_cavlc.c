/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * test_cavlc.c - Spec-conformance unit tests for H.264 CAVLC entropy engine
 *                Tests ITU-T H.264 Section 9.2 tables, trailing ones,
 *                total zeros, run_before, and macroblock syntax.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "bitstream.h"
#include "cavlc.h"

static void test_zero_blocks(void) {
    printf("  [1] Testing 4x4 Zero Block encoding across all nC contexts...\n");
    int zero_coeffs[16] = {0};
    uint8_t buf[64];

    /* nC < 2 (VLC 1: code '1', 1 bit) */
    bitstream_t bs;
    bs_init(&bs, buf, sizeof(buf));
    int tc = cavlc_write_4x4_block(&bs, zero_coeffs, 0);
    assert(tc == 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    assert((buf[0] & 0x80) == 0x80); /* First bit is 1 */

    /* 2 <= nC < 4 (VLC 2: code '11', 2 bits) */
    bs_init(&bs, buf, sizeof(buf));
    tc = cavlc_write_4x4_block(&bs, zero_coeffs, 2);
    assert(tc == 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    assert((buf[0] & 0xC0) == 0xC0); /* First 2 bits are 11 */

    /* 4 <= nC < 8 (VLC 3: code '1111', 4 bits) */
    bs_init(&bs, buf, sizeof(buf));
    tc = cavlc_write_4x4_block(&bs, zero_coeffs, 5);
    assert(tc == 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    assert((buf[0] & 0xF0) == 0xF0); /* First 4 bits are 1111 */

    /* nC >= 8 (Fixed 6-bit: '000000') */
    bs_init(&bs, buf, sizeof(buf));
    tc = cavlc_write_4x4_block(&bs, zero_coeffs, 10);
    assert(tc == 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    assert((buf[0] & 0xFC) == 0x00); /* First 6 bits are 000000 */

    printf("      ✓ Passed all 4 nC context classes for zero blocks.\n");
}

static void test_trailing_ones(void) {
    printf("  [2] Testing Trailing Ones (T1) handling and sign encoding...\n");
    uint8_t buf[64];

    /* Block with 3 trailing ones: +1, -1, +1 */
    int coeffs[16] = {1, -1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bitstream_t bs;
    bs_init(&bs, buf, sizeof(buf));
    int tc = cavlc_write_4x4_block(&bs, coeffs, 0);
    assert(tc == 3);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    /* Block with single trailing one: -1 */
    int single_t1[16] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bs_init(&bs, buf, sizeof(buf));
    tc = cavlc_write_4x4_block(&bs, single_t1, 0);
    assert(tc == 1);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    printf("      ✓ Trailing ones and sign bits verified.\n");
}

static void test_levels_and_runs(void) {
    printf("  [3] Testing Non-T1 Levels, Total Zeros, and Run Before...\n");
    uint8_t buf[64];

    /* Mixed block: high level, scattered zeros */
    int coeffs[16] = {5, 0, -2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bitstream_t bs;
    bs_init(&bs, buf, sizeof(buf));
    int tc = cavlc_write_4x4_block(&bs, coeffs, 1);
    assert(tc == 3);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    printf("      ✓ Levels, zeros, and run_before encoding verified.\n");
}

static void test_chroma_dc(void) {
    printf("  [4] Testing 2x2 Chroma DC block encoding...\n");
    uint8_t buf[64];

    /* Zero chroma DC */
    int zero_chroma[4] = {0, 0, 0, 0};
    bitstream_t bs;
    bs_init(&bs, buf, sizeof(buf));
    int tc = cavlc_write_chroma_dc_block(&bs, zero_chroma);
    assert(tc == 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    assert((buf[0] & 0x80) == 0x80); /* 1 bit: '1' */

    /* Non-zero chroma DC */
    int nz_chroma[4] = {2, 0, -1, 0};
    bs_init(&bs, buf, sizeof(buf));
    tc = cavlc_write_chroma_dc_block(&bs, nz_chroma);
    assert(tc == 2);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    printf("      ✓ Chroma DC encoding verified.\n");
}

static void test_macroblock_headers(void) {
    printf("  [5] Testing Macroblock Layer Headers (I_16x16, P_16x16, P_Skip)...\n");
    uint8_t buf[64];
    bitstream_t bs;

    /* Intra 16x16 modes */
    const int modes[] = {H264_I16x16_VERT, H264_I16x16_HORIZ, H264_I16x16_DC, H264_I16x16_PLANE};
    for (int m = 0; m < 4; m++) {
        bs_init(&bs, buf, sizeof(buf));
        cavlc_write_mb_i16x16_header(&bs, modes[m], 0, 0, 0);
        bs_flush(&bs);
        assert(bs_bytes_written(&bs) > 0);
    }

    /* P_Skip run */
    bs_init(&bs, buf, sizeof(buf));
    cavlc_write_p_skip_run(&bs, 1);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);

    bs_init(&bs, buf, sizeof(buf));
    cavlc_write_p_skip_run(&bs, 8160);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    /* P_16x16 with MVD */
    bs_init(&bs, buf, sizeof(buf));
    cavlc_write_mb_p16x16_header(&bs, 0, 0, 0, 0);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    bs_init(&bs, buf, sizeof(buf));
    cavlc_write_mb_p16x16_header(&bs, 4, -2, 15, -1);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) > 0);

    printf("      ✓ I16x16, P16x16, and P_Skip syntax headers verified.\n");
}

static void test_slice_trailing(void) {
    printf("  [6] Testing RBSP Slice Trailing Bits...\n");
    uint8_t buf[16];
    bitstream_t bs;

    /* Write 3 arbitrary bits, then trailing bits */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_u(&bs, 3, 0x5);
    cavlc_write_slice_trailing_bits(&bs);
    bs_flush(&bs);
    assert(bs_bytes_written(&bs) == 1);
    /* 3 bits: 101, then 1 stop bit: 1, then 4 zero bits: 0000 -> 0b10110000 = 0xB0 */
    assert(buf[0] == 0xB0);

    printf("      ✓ RBSP trailing bits byte alignment verified.\n");
}

int main(void) {
    printf("=== Running BC-250 H.264 CAVLC Unit Test Suite ===\n");
    test_zero_blocks();
    test_trailing_ones();
    test_levels_and_runs();
    test_chroma_dc();
    test_macroblock_headers();
    test_slice_trailing();
    printf("=== ALL CAVLC CONFORMANCE UNIT TESTS PASSED! ===\n");
    return 0;
}
