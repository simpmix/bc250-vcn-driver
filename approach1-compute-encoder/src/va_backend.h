/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * va_backend.h - VA-API Driver Backend Interface for AMD BC-250
 */
#ifndef BC250_VA_BACKEND_H
#define BC250_VA_BACKEND_H

#include <va/va.h>
#include <va/va_backend.h>
#include <va/va_enc_h264.h>
#include <va/va_enc_hevc.h>
#include <va/va_vpp.h>
#include "gpu_compute.h"
#include "encoder_h264.h"
#include "encoder_h265.h"
#include "decoder_h264.h"

#define MAX_PROFILES 16
#define MAX_ENTRYPOINTS 16
#define MAX_CONFIGS 256
#define MAX_SURFACES 1024
#define MAX_CONTEXTS 64
#define MAX_BUFFERS 4096
#define MAX_IMAGES 256

#define VALID_ID(id, max) ((unsigned int)(id) < (unsigned int)(max))
#define GET_OBJ(pool, id) (&pool[id])

typedef struct bc250_surface bc250_surface;
typedef struct bc250_config bc250_config;
typedef struct bc250_context bc250_context;
typedef struct bc250_buffer bc250_buffer;
typedef struct bc250_image bc250_image;

struct bc250_surface {
    int allocated;
    int format;
    int width;
    int height;
    gpu_image_t image;
    gpu_memory_t memory;
    int ref_count;
};

struct bc250_config {
    int allocated;
    VAProfile profile;
    VAEntrypoint entrypoint;
    VAConfigAttrib attribs[64];
    int num_attribs;
};

struct bc250_context {
    int allocated;
    VAConfigID config_id;
    int width;
    int height;
    int flag;
    VASurfaceID *render_targets;
    int num_render_targets;
    VASurfaceID current_render_target;
    VABufferID coded_buf_id;

    /* Encoders & Decoders */
    h264_encoder_t *h264_enc;
    hevc_encoder_t *hevc_enc;
    h264_decoder_t *h264_dec;

    /* Codec parameters accumulated during vaRenderPicture */
    struct {
        VAEncSequenceParameterBufferH264 seq_param;
        VAEncPictureParameterBufferH264 pic_param;
        VAEncSliceParameterBufferH264 slice_param;
        int has_seq;
        int has_pic;
        int has_slice;
    } h264_state;

    struct {
        VAEncSequenceParameterBufferHEVC seq_param;
        VAEncPictureParameterBufferHEVC pic_param;
        VAEncSliceParameterBufferHEVC slice_param;
        int has_seq;
        int has_pic;
        int has_slice;
    } hevc_state;
};

struct bc250_buffer {
    int allocated;
    VABufferType type;
    unsigned int size;
    unsigned int num_elements;
    void *data;
    int mapped;
};

struct bc250_image {
    int allocated;
    VAImage image;
    VASurfaceID surface_id;
    VABufferID buffer_id;
};

typedef struct {
    gpu_context_t gpu;

    bc250_surface surfaces[MAX_SURFACES];
    bc250_config configs[MAX_CONFIGS];
    bc250_context contexts[MAX_CONTEXTS];
    bc250_buffer buffers[MAX_BUFFERS];
    bc250_image images[MAX_IMAGES];

    int max_width;
    int max_height;
} bc250_driver_data;

/* Core VA-API Driver Functions */
VAStatus __vaDriverInit_1_0(VADriverContextP ctx);
VAStatus __vaDriverInit_0_32(VADriverContextP ctx);
VAStatus bc250_Initialize(VADriverContextP ctx, int *major_version, int *minor_version);
VAStatus bc250_Terminate(VADriverContextP ctx);

VAStatus bc250_QueryConfigProfiles(VADriverContextP ctx, VAProfile *profile_list, int *num_profiles);
VAStatus bc250_QueryConfigEntrypoints(VADriverContextP ctx, VAProfile profile, VAEntrypoint *entrypoint_list, int *num_entrypoints);
VAStatus bc250_GetConfigAttributes(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib *attrib_list, int num_attribs);
VAStatus bc250_CreateConfig(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id);
VAStatus bc250_DestroyConfig(VADriverContextP ctx, VAConfigID config_id);
VAStatus bc250_QueryConfigAttributes(VADriverContextP ctx, VAConfigID config_id, VAProfile *profile, VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list, int *num_attribs);

VAStatus bc250_QuerySurfaceAttributes(VADriverContextP ctx, VAConfigID config, VASurfaceAttrib *attrib_list, unsigned int *num_attribs);
VAStatus bc250_CreateSurfaces(VADriverContextP ctx, int width, int height, int format, int num_surfaces, VASurfaceID *surfaces);
VAStatus bc250_CreateSurfaces2(VADriverContextP ctx, unsigned int format, unsigned int width, unsigned int height,
                              VASurfaceID *surfaces, unsigned int num_surfaces,
                              VASurfaceAttrib *attrib_list, unsigned int num_attribs);
VAStatus bc250_DestroySurfaces(VADriverContextP ctx, VASurfaceID *surface_list, int num_surfaces);

VAStatus bc250_CreateContext(VADriverContextP ctx, VAConfigID config_id, int picture_width, int picture_height, int flag, VASurfaceID *render_targets, int num_render_targets, VAContextID *context);
VAStatus bc250_DestroyContext(VADriverContextP ctx, VAContextID context);

VAStatus bc250_CreateBuffer(VADriverContextP ctx, VAContextID context, VABufferType type, unsigned int size, unsigned int num_elements, void *data, VABufferID *buf_id);
VAStatus bc250_BufferSetNumElements(VADriverContextP ctx, VABufferID buf_id, unsigned int num_elements);
VAStatus bc250_MapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuf);
VAStatus bc250_UnmapBuffer(VADriverContextP ctx, VABufferID buf_id);
VAStatus bc250_DestroyBuffer(VADriverContextP ctx, VABufferID buffer_id);

VAStatus bc250_BeginPicture(VADriverContextP ctx, VAContextID context, VASurfaceID render_target);
VAStatus bc250_RenderPicture(VADriverContextP ctx, VAContextID context, VABufferID *buffers, int num_buffers);
VAStatus bc250_EndPicture(VADriverContextP ctx, VAContextID context);
VAStatus bc250_SyncSurface(VADriverContextP ctx, VASurfaceID render_target);
VAStatus bc250_QuerySurfaceStatus(VADriverContextP ctx, VASurfaceID render_target, VASurfaceStatus *status);

VAStatus bc250_QueryImageFormats(VADriverContextP ctx, VAImageFormat *format_list, int *num_formats);
VAStatus bc250_CreateImage(VADriverContextP ctx, VAImageFormat *format, int width, int height, VAImage *image);
VAStatus bc250_DestroyImage(VADriverContextP ctx, VAImageID image);
VAStatus bc250_DeriveImage(VADriverContextP ctx, VASurfaceID surface, VAImage *image);
VAStatus bc250_GetImage(VADriverContextP ctx, VASurfaceID surface, int x, int y, unsigned int width, unsigned int height, VAImageID image);
VAStatus bc250_PutImage(VADriverContextP ctx, VASurfaceID surface, VAImageID image, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height);

/* Video Processing (VPP) */
VAStatus bc250_QueryVideoProcFilters(VADriverContextP ctx, VAContextID context, VAProcFilterType *filters, unsigned int *num_filters);
VAStatus bc250_QueryVideoProcFilterCaps(VADriverContextP ctx, VAContextID context, VAProcFilterType type, void *filter_caps, unsigned int *num_filter_caps);
VAStatus bc250_QueryVideoProcPipelineCaps(VADriverContextP ctx, VAContextID context, VABufferID *filters, unsigned int num_filters, VAProcPipelineCaps *pipeline_caps);

#endif // BC250_VA_BACKEND_H
