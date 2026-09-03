/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
#include "va_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BC250_MAX_WIDTH 3840
#define BC250_MAX_HEIGHT 2160

static bc250_driver_data* get_driver_data(VADriverContextP ctx) {
    return (bc250_driver_data*)ctx->pDriverData;
}

VAStatus bc250_QueryConfigProfiles(VADriverContextP ctx, VAProfile *profile_list, int *num_profiles) {
    if (!profile_list || !num_profiles) return VA_STATUS_ERROR_INVALID_PARAMETER;
    
    int i = 0;
    profile_list[i++] = VAProfileH264Baseline;
    profile_list[i++] = VAProfileH264Main;
    profile_list[i++] = VAProfileH264High;
    profile_list[i++] = VAProfileHEVCMain;
    
    *num_profiles = i;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryConfigEntrypoints(VADriverContextP ctx, VAProfile profile, VAEntrypoint *entrypoint_list, int *num_entrypoints) {
    if (!entrypoint_list || !num_entrypoints) return VA_STATUS_ERROR_INVALID_PARAMETER;
    
    int i = 0;
    if (profile == VAProfileH264Baseline || profile == VAProfileH264Main || 
        profile == VAProfileH264High || profile == VAProfileHEVCMain) {
        entrypoint_list[i++] = VAEntrypointVLD;         // Decode
        entrypoint_list[i++] = VAEntrypointEncSlice;    // Encode
    }
    
    *num_entrypoints = i;
    return (i > 0) ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus bc250_GetConfigAttributes(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib *attrib_list, int num_attribs) {
    if (!attrib_list) return VA_STATUS_ERROR_INVALID_PARAMETER;
    
    for (int i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
            case VAConfigAttribRTFormat:
                attrib_list[i].value = VA_RT_FORMAT_YUV420 | VA_RT_FORMAT_YUV420_10;
                break;
            case VAConfigAttribRateControl:
                attrib_list[i].value = VA_RC_CBR | VA_RC_VBR;
                break;
            case VAConfigAttribEncPackedHeaders:
                attrib_list[i].value = VA_ENC_PACKED_HEADER_SEQUENCE | VA_ENC_PACKED_HEADER_PICTURE;
                break;
            case VAConfigAttribEncMaxRefFrames:
                attrib_list[i].value = 1;
                break;
            default:
                attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
                break;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateConfig(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    
    for (int i = 0; i < MAX_CONFIGS; i++) {
        if (!data->configs[i].allocated) {
            data->configs[i].allocated = 1;
            data->configs[i].profile = profile;
            data->configs[i].entrypoint = entrypoint;
            data->configs[i].num_attribs = num_attribs;
            if (num_attribs > 0 && attrib_list) {
                memcpy(data->configs[i].attribs, attrib_list, num_attribs * sizeof(VAConfigAttrib));
            }
            *config_id = i;
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_DestroyConfig(VADriverContextP ctx, VAConfigID config_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(config_id, MAX_CONFIGS)) return VA_STATUS_ERROR_INVALID_CONFIG;
    data->configs[config_id].allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryConfigAttributes(VADriverContextP ctx, VAConfigID config_id, VAProfile *profile, VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list, int *num_attribs) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(config_id, MAX_CONFIGS) || !data->configs[config_id].allocated) return VA_STATUS_ERROR_INVALID_CONFIG;
    
    *profile = data->configs[config_id].profile;
    *entrypoint = data->configs[config_id].entrypoint;
    *num_attribs = data->configs[config_id].num_attribs;
    if (attrib_list) {
        memcpy(attrib_list, data->configs[config_id].attribs, data->configs[config_id].num_attribs * sizeof(VAConfigAttrib));
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QuerySurfaceAttributes(VADriverContextP ctx, VAConfigID config, VASurfaceAttrib *attrib_list, unsigned int *num_attribs) {
    if (!attrib_list || !num_attribs) return VA_STATUS_ERROR_INVALID_PARAMETER;
    
    int i = 0;
    attrib_list[i].type = VASurfaceAttribPixelFormat;
    attrib_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[i].value.type = VAGenericValueTypeInteger;
    attrib_list[i].value.value.i = VA_FOURCC_NV12;
    i++;
    
    attrib_list[i].type = VASurfaceAttribMaxWidth;
    attrib_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
    attrib_list[i].value.type = VAGenericValueTypeInteger;
    attrib_list[i].value.value.i = BC250_MAX_WIDTH;
    i++;
    
    attrib_list[i].type = VASurfaceAttribMaxHeight;
    attrib_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
    attrib_list[i].value.type = VAGenericValueTypeInteger;
    attrib_list[i].value.value.i = BC250_MAX_HEIGHT;
    i++;
    
    *num_attribs = i;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateSurfaces(VADriverContextP ctx, int width, int height, int format, int num_surfaces, VASurfaceID *surfaces) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (width > data->max_width || height > data->max_height) return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
    
    int allocated = 0;
    for (int i = 0; i < MAX_SURFACES && allocated < num_surfaces; i++) {
        if (!data->surfaces[i].allocated) {
            bc250_surface *surf = &data->surfaces[i];
            surf->allocated = 1;
            surf->width = width;
            surf->height = height;
            surf->format = format;
            surf->ref_count = 1;
            
            // Call vulkan backend to create the image
            gpu_compute_create_image(&data->gpu, width, height, format, &surf->image, &surf->memory);
            
            surfaces[allocated++] = i;
        }
    }
    
    if (allocated < num_surfaces) {
        // Rollback
        bc250_DestroySurfaces(ctx, surfaces, allocated);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_DestroySurfaces(VADriverContextP ctx, VASurfaceID *surface_list, int num_surfaces) {
    bc250_driver_data *data = get_driver_data(ctx);
    for (int i = 0; i < num_surfaces; i++) {
        VASurfaceID id = surface_list[i];
        if (VALID_ID(id, MAX_SURFACES) && data->surfaces[id].allocated) {
            bc250_surface *surf = &data->surfaces[id];
            gpu_compute_destroy_image(&data->gpu, surf->image, surf->memory);
            surf->allocated = 0;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateContext(VADriverContextP ctx, VAConfigID config_id, int picture_width, int picture_height, int flag, VASurfaceID *render_targets, int num_render_targets, VAContextID *context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(config_id, MAX_CONFIGS) || !data->configs[config_id].allocated) return VA_STATUS_ERROR_INVALID_CONFIG;
    
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (!data->contexts[i].allocated) {
            bc250_context *c = &data->contexts[i];
            memset(c, 0, sizeof(bc250_context));
            c->allocated = 1;
            c->config_id = config_id;
            c->width = picture_width;
            c->height = picture_height;
            c->flag = flag;
            c->num_render_targets = num_render_targets;
            if (num_render_targets > 0 && render_targets) {
                c->render_targets = malloc(num_render_targets * sizeof(VASurfaceID));
                memcpy(c->render_targets, render_targets, num_render_targets * sizeof(VASurfaceID));
            }
            *context = i;
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_DestroyContext(VADriverContextP ctx, VAContextID context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(context, MAX_CONTEXTS)) return VA_STATUS_ERROR_INVALID_CONTEXT;
    bc250_context *c = &data->contexts[context];
    if (c->render_targets) free(c->render_targets);
    c->allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateBuffer(VADriverContextP ctx, VAContextID context, VABufferType type, unsigned int size, unsigned int num_elements, void *data_ptr, VABufferID *buf_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (!data->buffers[i].allocated) {
            bc250_buffer *b = &data->buffers[i];
            b->allocated = 1;
            b->type = type;
            b->size = size;
            b->num_elements = num_elements;
            b->mapped = 0;
            b->data = malloc(size * num_elements);
            if (data_ptr) {
                memcpy(b->data, data_ptr, size * num_elements);
            }
            *buf_id = i;
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_BufferSetNumElements(VADriverContextP ctx, VABufferID buf_id, unsigned int num_elements) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    data->buffers[buf_id].num_elements = num_elements;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_MapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuf) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    data->buffers[buf_id].mapped = 1;
    *pbuf = data->buffers[buf_id].data;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_UnmapBuffer(VADriverContextP ctx, VABufferID buf_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    data->buffers[buf_id].mapped = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_DestroyBuffer(VADriverContextP ctx, VABufferID buffer_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(buffer_id, MAX_BUFFERS) || !data->buffers[buffer_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    free(data->buffers[buffer_id].data);
    data->buffers[buffer_id].allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_BeginPicture(VADriverContextP ctx, VAContextID context, VASurfaceID render_target) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) return VA_STATUS_ERROR_INVALID_CONTEXT;
    if (!VALID_ID(render_target, MAX_SURFACES) || !data->surfaces[render_target].allocated) return VA_STATUS_ERROR_INVALID_SURFACE;
    
    bc250_context *c = &data->contexts[context];
    c->current_render_target = render_target;
    
    // Clear encode state for new picture
    c->h264_state.has_seq = 0;
    c->h264_state.has_pic = 0;
    c->h264_state.has_slice = 0;
    
    // Start vulkan command buffer recording
    gpu_compute_begin_picture(&data->gpu, data->surfaces[render_target].image);
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_RenderPicture(VADriverContextP ctx, VAContextID context, VABufferID *buffers, int num_buffers) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) return VA_STATUS_ERROR_INVALID_CONTEXT;
    bc250_context *c = &data->contexts[context];
    
    for (int i = 0; i < num_buffers; i++) {
        VABufferID buf_id = buffers[i];
        if (!VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) continue;
        
        bc250_buffer *b = &data->buffers[buf_id];
        switch (b->type) {
            case VAEncSequenceParameterBufferType:
                if (b->size >= sizeof(VAEncSequenceParameterBufferH264)) {
                    memcpy(&c->h264_state.seq_param, b->data, sizeof(VAEncSequenceParameterBufferH264));
                    c->h264_state.has_seq = 1;
                }
                break;
            case VAEncPictureParameterBufferType:
                if (b->size >= sizeof(VAEncPictureParameterBufferH264)) {
                    memcpy(&c->h264_state.pic_param, b->data, sizeof(VAEncPictureParameterBufferH264));
                    c->h264_state.has_pic = 1;
                }
                break;
            case VAEncSliceParameterBufferType:
                if (b->size >= sizeof(VAEncSliceParameterBufferH264)) {
                    memcpy(&c->h264_state.slice_param, b->data, sizeof(VAEncSliceParameterBufferH264));
                    c->h264_state.has_slice = 1;
                }
                break;
            case VAEncPackedHeaderDataBufferType:
                // Handle packed headers...
                break;
            default:
                break;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_EndPicture(VADriverContextP ctx, VAContextID context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) return VA_STATUS_ERROR_INVALID_CONTEXT;
    
    bc250_context *c = &data->contexts[context];
    bc250_surface *surf = &data->surfaces[c->current_render_target];
    
    // Dispatch compute shader pipeline based on gathered state
    if (c->h264_state.has_pic) {
        gpu_compute_dispatch_encode(&data->gpu, surf->image, c->width, c->height);
    }
    
    gpu_compute_end_picture(&data->gpu);
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_SyncSurface(VADriverContextP ctx, VASurfaceID render_target) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(render_target, MAX_SURFACES) || !data->surfaces[render_target].allocated) return VA_STATUS_ERROR_INVALID_SURFACE;
    
    // Wait for the encode compute operations to complete
    gpu_compute_sync(&data->gpu);
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QuerySurfaceStatus(VADriverContextP ctx, VASurfaceID render_target, VASurfaceStatus *status) {
    if (!status) return VA_STATUS_ERROR_INVALID_PARAMETER;
    *status = VASurfaceReady; // Simplified
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryImageFormats(VADriverContextP ctx, VAImageFormat *format_list, int *num_formats) {
    if (!format_list || !num_formats) return VA_STATUS_ERROR_INVALID_PARAMETER;
    
    format_list[0].fourcc = VA_FOURCC_NV12;
    format_list[0].byte_order = VA_LSB_FIRST;
    format_list[0].bits_per_pixel = 12;
    
    *num_formats = 1;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_Terminate(VADriverContextP ctx) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (data) {
        gpu_compute_terminate(&data->gpu);
        free(data);
        ctx->pDriverData = NULL;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_Initialize(VADriverContextP ctx, int *major_version, int *minor_version) {
    if (!ctx) return VA_STATUS_ERROR_INVALID_CONTEXT;
    
    bc250_driver_data *data = calloc(1, sizeof(bc250_driver_data));
    if (!data) return VA_STATUS_ERROR_ALLOCATION_FAILED;
    
    if (gpu_compute_init(&data->gpu) != 0) {
        free(data);
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }
    
    data->max_width = BC250_MAX_WIDTH;
    data->max_height = BC250_MAX_HEIGHT;
    
    ctx->pDriverData = data;
    
    // Set up vtable
    ctx->vtable->vaTerminate = bc250_Terminate;
    ctx->vtable->vaQueryConfigProfiles = bc250_QueryConfigProfiles;
    ctx->vtable->vaQueryConfigEntrypoints = bc250_QueryConfigEntrypoints;
    ctx->vtable->vaGetConfigAttributes = bc250_GetConfigAttributes;
    ctx->vtable->vaCreateConfig = bc250_CreateConfig;
    ctx->vtable->vaDestroyConfig = bc250_DestroyConfig;
    ctx->vtable->vaQueryConfigAttributes = bc250_QueryConfigAttributes;
    ctx->vtable->vaCreateSurfaces = bc250_CreateSurfaces;
    ctx->vtable->vaDestroySurfaces = bc250_DestroySurfaces;
    ctx->vtable->vaCreateContext = bc250_CreateContext;
    ctx->vtable->vaDestroyContext = bc250_DestroyContext;
    ctx->vtable->vaCreateBuffer = bc250_CreateBuffer;
    ctx->vtable->vaBufferSetNumElements = bc250_BufferSetNumElements;
    ctx->vtable->vaMapBuffer = bc250_MapBuffer;
    ctx->vtable->vaUnmapBuffer = bc250_UnmapBuffer;
    ctx->vtable->vaDestroyBuffer = bc250_DestroyBuffer;
    ctx->vtable->vaBeginPicture = bc250_BeginPicture;
    ctx->vtable->vaRenderPicture = bc250_RenderPicture;
    ctx->vtable->vaEndPicture = bc250_EndPicture;
    ctx->vtable->vaSyncSurface = bc250_SyncSurface;
    ctx->vtable->vaQuerySurfaceStatus = bc250_QuerySurfaceStatus;
    ctx->vtable->vaQueryImageFormats = bc250_QueryImageFormats;
    ctx->vtable->vaQuerySurfaceAttributes = bc250_QuerySurfaceAttributes;
    
    if (major_version) *major_version = VA_MAJOR_VERSION;
    if (minor_version) *minor_version = VA_MINOR_VERSION;
    
    return VA_STATUS_SUCCESS;
}

VA_DRIVER_INIT_FUNC(__vaDriverInit_1_0) {
    return bc250_Initialize(ctx, major_version, minor_version);
}
