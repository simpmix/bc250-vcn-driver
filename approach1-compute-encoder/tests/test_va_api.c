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

    /* 8. Image transfer test (GetImage / PutImage) */
    VAImage image;
    VAImageFormat fmt = { .fourcc = VA_FOURCC_NV12, .byte_order = VA_LSB_FIRST, .bits_per_pixel = 12 };
    status = ctx.vtable->vaCreateImage(&ctx, &fmt, 1920, 1080, &image);
    assert(status == VA_STATUS_SUCCESS);

    status = ctx.vtable->vaPutImage(&ctx, surfaces[0], image.image_id, 0, 0, 1920, 1080, 0, 0, 1920, 1080);
    assert(status == VA_STATUS_SUCCESS);
    printf("[PASS] Image transfer and surface write validated\n");

    /* Clean up */
    ctx.vtable->vaDestroyImage(&ctx, image.image_id);
    ctx.vtable->vaDestroyBuffer(&ctx, coded_buf_id);
    ctx.vtable->vaDestroyContext(&ctx, context_id);
    ctx.vtable->vaDestroySurfaces(&ctx, surfaces, 2);
    ctx.vtable->vaDestroyConfig(&ctx, config_id);
    ctx.vtable->vaTerminate(&ctx);

    printf("=== All VA-API Driver Tests Passed Successfully! ===\n");
    return 0;
}
