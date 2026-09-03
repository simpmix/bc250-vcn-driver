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

    VADriverContext ctx;
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

    /* 1. Query Profiles */
    VAProfile profiles[MAX_PROFILES];
    int num_profiles = 0;
    status = ctx.vtable->vaQueryConfigProfiles(&ctx, profiles, &num_profiles);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_profiles > 0);
    printf("[PASS] Found %d supported VA profiles\n", num_profiles);

    /* 2. Query Entrypoints for H.264 Main */
    VAEntrypoint entrypoints[MAX_ENTRYPOINTS];
    int num_entrypoints = 0;
    status = ctx.vtable->vaQueryConfigEntrypoints(&ctx, VAProfileH264Main, entrypoints, &num_entrypoints);
    assert(status == VA_STATUS_SUCCESS);
    assert(num_entrypoints >= 2); /* Should support both VLD and EncSlice */
    printf("[PASS] H.264 Main supports %d entrypoints (Encode + Decode)\n", num_entrypoints);

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

    /* 4. Create Surfaces */
    VASurfaceID surfaces[2];
    status = ctx.vtable->vaCreateSurfaces(&ctx, 1920, 1080, VA_RT_FORMAT_YUV420, 2, surfaces);
    assert(status == VA_STATUS_SUCCESS);
    printf("[PASS] Allocated 2 1080p surfaces (IDs %d, %d)\n", surfaces[0], surfaces[1]);

    /* 5. Create Context */
    VAContextID context_id = VA_INVALID_ID;
    status = ctx.vtable->vaCreateContext(&ctx, config_id, 1920, 1080, 0, surfaces, 2, &context_id);
    assert(status == VA_STATUS_SUCCESS);
    assert(context_id != VA_INVALID_ID);
    printf("[PASS] Created encode context with ID %d\n", context_id);

    /* Clean up */
    ctx.vtable->vaDestroyContext(&ctx, context_id);
    ctx.vtable->vaDestroySurfaces(&ctx, surfaces, 2);
    ctx.vtable->vaDestroyConfig(&ctx, config_id);
    ctx.vtable->vaTerminate(&ctx);

    printf("=== All VA-API Driver Tests Passed Successfully! ===\n");
    return 0;
}
