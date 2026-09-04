/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * test_encode.c - End-to-end H.264 bitstream syntax & decode test
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "encoder_h264.h"
#include "bitstream.h"

int main(void) {
    printf("[test_encode] Starting H.264 end-to-end bitstream encoding test...\n");

    const uint32_t width = 1920;
    const uint32_t height = 1080;
    const uint32_t fps = 60;
    const uint32_t bitrate = 5000000; /* 5 Mbps */

    h264_encoder_t *enc = h264_encoder_create(NULL, width, height, fps, bitrate, PROFILE_BASELINE);
    assert(enc != NULL);
    printf("[test_encode] Created 1080p60 H.264 Baseline encoder.\n");

    const size_t out_cap = width * height * 2;
    uint8_t *out_buf = malloc(out_cap);
    assert(out_buf != NULL);

    FILE *f_stream = fopen("bc250_test_stream.h264", "wb");
    assert(f_stream != NULL);

    gpu_image_t dummy_img;
    memset(&dummy_img, 0, sizeof(dummy_img));

    /* Encode 30 frames (1 GOP): 1 IDR frame followed by 29 P-frames */
    const int num_frames = 30;
    size_t total_bytes = 0;

    for (int i = 0; i < num_frames; i++) {
        int written = h264_encoder_encode_frame(enc, NULL, dummy_img, out_buf, out_cap);
        assert(written > 0);

        /* Verify 4-byte start code at the beginning of each frame (AUD) */
        assert(out_buf[0] == 0x00);
        assert(out_buf[1] == 0x00);
        assert(out_buf[2] == 0x00);
        assert(out_buf[3] == 0x01);
        assert(out_buf[4] == 0x09); /* NAL type 9 (AUD) */

        if (i == 0) {
            /* IDR frame must contain SPS (type 7) and PPS (type 8) */
            bool has_sps = false;
            bool has_pps = false;
            bool has_idr_slice = false;

            for (int p = 0; p < written - 4; p++) {
                if (out_buf[p] == 0x00 && out_buf[p+1] == 0x00 &&
                    out_buf[p+2] == 0x00 && out_buf[p+3] == 0x01) {
                    uint8_t nal_type = out_buf[p+4] & 0x1F;
                    if (nal_type == 7) has_sps = true;
                    if (nal_type == 8) has_pps = true;
                    if (nal_type == 5) has_idr_slice = true;
                }
            }
            assert(has_sps && "Frame 0 missing SPS NAL unit");
            assert(has_pps && "Frame 0 missing PPS NAL unit");
            assert(has_idr_slice && "Frame 0 missing IDR Slice NAL unit");
            printf("[test_encode] Frame %02d (IDR): %d bytes (AUD, SPS, PPS, IDR-Slice verified)\n", i, written);
        } else {
            /* P-frame must contain non-IDR slice (type 1) */
            bool has_p_slice = false;
            for (int p = 0; p < written - 4; p++) {
                if (out_buf[p] == 0x00 && out_buf[p+1] == 0x00 &&
                    out_buf[p+2] == 0x00 && out_buf[p+3] == 0x01) {
                    uint8_t nal_type = out_buf[p+4] & 0x1F;
                    if (nal_type == 1) has_p_slice = true;
                }
            }
            assert(has_p_slice && "P-frame missing Slice NAL unit");
            if (i < 5 || i == num_frames - 1) {
                printf("[test_encode] Frame %02d (P):   %d bytes (AUD, P-Slice verified)\n", i, written);
            }
        }

        fwrite(out_buf, 1, (size_t)written, f_stream);
        total_bytes += (size_t)written;
    }

    fclose(f_stream);
    free(out_buf);
    h264_encoder_destroy(enc);

    printf("[test_encode] Successfully encoded %d frames (%zu total bytes) to bc250_test_stream.h264!\n",
           num_frames, total_bytes);
    printf("[test_encode] ALL TESTS PASSED!\n");

    return 0;
}
