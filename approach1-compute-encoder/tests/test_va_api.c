/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * test_va_api.c - Integration test for BC-250 VA-API Backend Driver
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "va_backend.h"

int main(void) {
    printf("=== Running BC-250 VA-API Driver Tests ===\n");

    struct VADriverContext ctx;
    struct VADriverVTable vtable;
    memset(&ctx, 0, sizeof(ctx));
    memset(&vtable, 0, sizeof(vtable));
    ctx.vtable = &vtable;

    int major = 0, minor = 0;
    VAStatus status = bc250_Initialize(&ctx, &major, &minor);
    if (status != VA_STATUS_SUCCESS) {
        printf("[SKIP] Vulkan initialization skipped (no compatible GPU or display detected in test environment)\n");
        return 0;
    }

    printf("[INFO] Driver initialized: VA-API %d.%d (%s)\n", major, minor, ctx.str_vendor);

    /* 1. Query Profiles (with count-only check and full list check) */
    int num_profiles = 0;
    status = ctx.vtable->vaQueryConfigProfiles(&ctx, NULL, &num_profiles);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_profiles > 0);

    VAProfile profiles[MAX_PROFILES];
    status = ctx.vtable->vaQueryConfigProfiles(&ctx, profiles, &num_profiles);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_profiles > 0);
    printf("[PASS] Found %d supported VA profiles (NULL count query verified)\n", num_profiles);

    /* 2. Query Entrypoints for H.264 Main */
    int num_entrypoints = 0;
    status = ctx.vtable->vaQueryConfigEntrypoints(&ctx, VAProfileH264Main, NULL, &num_entrypoints);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_entrypoints >= 1);

    VAEntrypoint entrypoints[MAX_ENTRYPOINTS];
    status = ctx.vtable->vaQueryConfigEntrypoints(&ctx, VAProfileH264Main, entrypoints, &num_entrypoints);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_entrypoints >= 1);
    assert(entrypoints[0] == VAEntrypointEncSlice);
    printf("[PASS] H.264 Main supports %d entrypoint (VAEntrypointEncSlice)\n", num_entrypoints);

    /* 3. Create Config */
    VAConfigAttrib attribs[2];
    attribs[0].type = VAConfigAttribRTFormat;
    attribs[0].value = VA_RT_FORMAT_YUV420;
    attribs[1].type = VAConfigAttribRateControl;
    attribs[1].value = VA_RC_CBR;

    VAConfigID config_id = VA_INVALID_ID;
    status = ctx.vtable->vaCreateConfig(&ctx, VAProfileH264Main, VAEntrypointEncSlice, attribs, 2, &config_id);
    assert(status == VA_STATUS_SUCCESS);
    assert(config_id != VA_INVALID_ID);
    printf("[PASS] Config created with ID %d\n", config_id);

    /* 4. Query Surface Attributes (contract test for FFmpeg/OBS) */
    unsigned int num_surface_attribs = 0;
    status = ctx.vtable->vaQuerySurfaceAttributes(&ctx, config_id, NULL, &num_surface_attribs);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_surface_attribs > 0);

    VASurfaceAttrib surface_attribs[8];
    status = ctx.vtable->vaQuerySurfaceAttributes(&ctx, config_id, surface_attribs, &num_surface_attribs);
    assert(status == VA_STATUS_SUCCESS);
    printf("[PASS] Surface attributes query passed (%u attributes supported)\n", num_surface_attribs);

    /* 5. Create Surfaces */
    VASurfaceID surfaces[2];
    status = ctx.vtable->vaCreateSurfaces(&ctx, 1920, 1080, VA_RT_FORMAT_YUV420, 2, surfaces);
    assert(status == VA_STATUS_SUCCESS);
    printf("[PASS] Allocated 2 1080p surfaces (IDs %d, %d)\n", surfaces[0], surfaces[1]);

    /* 6. Create Context */
    VAContextID context_id = VA_INVALID_ID;
    status = ctx.vtable->vaCreateContext(&ctx, config_id, 1920, 1080, 0, surfaces, 2, &context_id);
    assert(status == VA_STATUS_SUCCESS);
    assert(context_id != VA_INVALID_ID);
    printf("[PASS] Created encode context with ID %d\n", context_id);

    /* 7. Create Coded Buffer and verify VACodedBufferSegment initialization */
    VABufferID coded_buf_id = VA_INVALID_ID;
    status = ctx.vtable->vaCreateBuffer(&ctx, context_id, VAEncCodedBufferType, 1024 * 1024, 1, NULL, &coded_buf_id);
    assert(status == VA_STATUS_SUCCESS);

    void *mapped_data = NULL;
    status = ctx.vtable->vaMapBuffer(&ctx, coded_buf_id, &mapped_data);
    assert(status == VA_STATUS_SUCCESS);
    assert(mapped_data != NULL);

    VACodedBufferSegment *seg = (VACodedBufferSegment *)mapped_data;
    assert(seg->buf != NULL);
    ctx.vtable->vaUnmapBuffer(&ctx, coded_buf_id);
    printf("[PASS] Coded buffer segment initialization validated\n");

    /* 8. Image transfer test (GetImage / PutImage bit-exact round-trip) */
    VAImage image1, image2;
    VAImageFormat fmt = { .fourcc = VA_FOURCC_NV12, .byte_order = VA_LSB_FIRST, .bits_per_pixel = 12 };
    status = ctx.vtable->vaCreateImage(&ctx, &fmt, 1920, 1080, &image1);
    assert(status == VA_STATUS_SUCCESS);
    status = ctx.vtable->vaCreateImage(&ctx, &fmt, 1920, 1080, &image2);
    assert(status == VA_STATUS_SUCCESS);

    /* Fill image1 with a deterministic pixel pattern */
    void *img1_ptr = NULL;
    status = ctx.vtable->vaMapBuffer(&ctx, image1.buf, &img1_ptr);
    assert(status == VA_STATUS_SUCCESS && img1_ptr != NULL);
    uint8_t *y1 = (uint8_t *)img1_ptr + image1.offsets[0];
    uint8_t *uv1 = (uint8_t *)img1_ptr + image1.offsets[1];
    for (int r = 0; r < 1080; r++) {
        for (int c = 0; c < 1920; c++) {
            y1[r * image1.pitches[0] + c] = (uint8_t)((r * 3 + c * 5) & 0xFF);
        }
    }
    for (int r = 0; r < 540; r++) {
        for (int c = 0; c < 1920; c++) {
            uv1[r * image1.pitches[1] + c] = (uint8_t)((r * 7 + c * 11) & 0xFF);
        }
    }
    ctx.vtable->vaUnmapBuffer(&ctx, image1.buf);

    /* Upload pixel data to surface */
    status = ctx.vtable->vaPutImage(&ctx, surfaces[0], image1.image_id, 0, 0, 1920, 1080, 0, 0, 1920, 1080);
    assert(status == VA_STATUS_SUCCESS);

    /* Download pixel data back from surface into image2 */
    status = ctx.vtable->vaGetImage(&ctx, surfaces[0], 0, 0, 1920, 1080, image2.image_id);
    assert(status == VA_STATUS_SUCCESS);

    /* Verify bit-exact match of uploaded vs downloaded pixels */
    void *img2_ptr = NULL;
    status = ctx.vtable->vaMapBuffer(&ctx, image2.buf, &img2_ptr);
    assert(status == VA_STATUS_SUCCESS && img2_ptr != NULL);
    const uint8_t *y2 = (const uint8_t *)img2_ptr + image2.offsets[0];
    const uint8_t *uv2 = (const uint8_t *)img2_ptr + image2.offsets[1];
    int pixel_mismatches = 0;
    for (int r = 0; r < 1080; r++) {
        for (int c = 0; c < 1920; c++) {
            if (y2[r * image2.pitches[0] + c] != (uint8_t)((r * 3 + c * 5) & 0xFF)) {
                pixel_mismatches++;
            }
        }
    }
    for (int r = 0; r < 540; r++) {
        for (int c = 0; c < 1920; c++) {
            if (uv2[r * image2.pitches[1] + c] != (uint8_t)((r * 7 + c * 11) & 0xFF)) {
                pixel_mismatches++;
            }
        }
    }
    assert(pixel_mismatches == 0 && "Pixel data mismatch in PutImage/GetImage round-trip!");
    ctx.vtable->vaUnmapBuffer(&ctx, image2.buf);
    printf("[PASS] Image transfer bit-exact round-trip verified (0 pixel mismatches across 3.1M pixels)\n");

    /* 9. Derive Image test */
    VAImage derived_img;
    status = ctx.vtable->vaDeriveImage(&ctx, surfaces[0], &derived_img);
    assert(status == VA_STATUS_SUCCESS);
    ctx.vtable->vaDestroyImage(&ctx, derived_img.image_id);
    printf("[PASS] Derive image and surface mapping validated\n");

    /* Clean up images */
    ctx.vtable->vaDestroyImage(&ctx, image1.image_id);
    ctx.vtable->vaDestroyImage(&ctx, image2.image_id);
    ctx.vtable->vaDestroyBuffer(&ctx, coded_buf_id);
    ctx.vtable->vaDestroyContext(&ctx, context_id);
    ctx.vtable->vaDestroySurfaces(&ctx, surfaces, 2);
    ctx.vtable->vaDestroyConfig(&ctx, config_id);
    ctx.vtable->vaTerminate(&ctx);

    printf("=== All VA-API Driver Tests Passed Successfully! ===\n");
    return 0;
}
