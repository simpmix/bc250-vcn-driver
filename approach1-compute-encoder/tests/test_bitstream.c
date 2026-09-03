/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * test_bitstream.c - Unit tests for H.264 Bitstream & Exp-Golomb Writer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bitstream.h"

static void test_exp_golomb_unsigned(void) {
    uint8_t buf[16];
    bitstream_t bs;

    /* ue(0) -> '1' (1 bit) */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_ue(&bs, 0);
    bs_rbsp_trailing_bits(&bs);
    assert(buf[0] == 0x80);

    /* ue(1) -> '010' */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_ue(&bs, 1);
    bs_rbsp_trailing_bits(&bs);
    assert((buf[0] & 0xe0) == 0x40);

    /* ue(2) -> '011' */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_ue(&bs, 2);
    bs_rbsp_trailing_bits(&bs);
    assert((buf[0] & 0xe0) == 0x60);

    printf("[PASS] Exp-Golomb Unsigned (ue) tests\n");
}

static void test_exp_golomb_signed(void) {
    uint8_t buf[16];
    bitstream_t bs;

    /* se(0) -> ue(0) -> '1' */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_se(&bs, 0);
    bs_rbsp_trailing_bits(&bs);
    assert(buf[0] == 0x80);

    /* se(1) -> ue(1) -> '010' */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_se(&bs, 1);
    bs_rbsp_trailing_bits(&bs);
    assert((buf[0] & 0xe0) == 0x40);

    /* se(-1) -> ue(2) -> '011' */
    bs_init(&bs, buf, sizeof(buf));
    bs_write_se(&bs, -1);
    bs_rbsp_trailing_bits(&bs);
    assert((buf[0] & 0xe0) == 0x60);

    printf("[PASS] Exp-Golomb Signed (se) tests\n");
}

static void test_emulation_prevention(void) {
    uint8_t raw[4] = {0x00, 0x00, 0x01, 0xAA};
    uint8_t escaped[8];
    size_t out_len = bs_rbsp_to_ebsp(escaped, sizeof(escaped), raw, sizeof(raw));

    /* Expected: 00 00 03 01 AA (5 bytes) */
    assert(out_len == 5);
    assert(escaped[0] == 0x00);
    assert(escaped[1] == 0x00);
    assert(escaped[2] == 0x03);
    assert(escaped[3] == 0x01);
    assert(escaped[4] == 0xAA);

    printf("[PASS] Emulation prevention (EBSP) test\n");
}

static void test_sps_pps_generation(void) {
    uint8_t sps_buf[512];
    uint8_t pps_buf[512];
    h264_sps_t sps;
    h264_pps_t pps;

    h264_sps_default(&sps, 1920, 1080, 60, PROFILE_HIGH);
    size_t sps_bytes = bs_write_sps(sps_buf, sizeof(sps_buf), &sps);

    /* Verify NAL start code (0x00000001) and SPS NAL type (0x67 for High profile) */
    assert(sps_bytes > 10);
    assert(sps_buf[0] == 0x00 && sps_buf[1] == 0x00 && sps_buf[2] == 0x00 && sps_buf[3] == 0x01);
    assert((sps_buf[4] & 0x1F) == NAL_TYPE_SPS);

    h264_pps_default(&pps, sps.sps_id, false, 26);
    size_t pps_bytes = bs_write_pps(pps_buf, sizeof(pps_buf), &pps);

    assert(pps_bytes > 5);
    assert(pps_buf[0] == 0x00 && pps_buf[1] == 0x00 && pps_buf[2] == 0x00 && pps_buf[3] == 0x01);
    assert((pps_buf[4] & 0x1F) == NAL_TYPE_PPS);

    printf("[PASS] SPS/PPS serialization test: SPS %zu bytes, PPS %zu bytes\n", sps_bytes, pps_bytes);
}

int main(void) {
    printf("=== Running BC-250 Bitstream Unit Tests ===\n");
    test_exp_golomb_unsigned();
    test_exp_golomb_signed();
    test_emulation_prevention();
    test_sps_pps_generation();
    printf("=== All Bitstream Tests Passed Successfully! ===\n");
    return 0;
}
