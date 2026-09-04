/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * va_backend.c - Complete VA-API Backend Driver Implementation for AMD BC-250
 */
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
    if (!ctx || !num_profiles) return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!profile_list) {
        *num_profiles = 4;
        return VA_STATUS_SUCCESS;
    }

    int i = 0;
    profile_list[i++] = VAProfileH264ConstrainedBaseline;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    profile_list[i++] = VAProfileH264Baseline;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    profile_list[i++] = VAProfileH264Main;
    profile_list[i++] = VAProfileH264High;

    *num_profiles = i;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryConfigEntrypoints(VADriverContextP ctx, VAProfile profile, VAEntrypoint *entrypoint_list, int *num_entrypoints) {
    if (!ctx || !num_entrypoints) return VA_STATUS_ERROR_INVALID_PARAMETER;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    int is_supported_profile = (profile == VAProfileH264ConstrainedBaseline ||
                                profile == VAProfileH264Baseline ||
                                profile == VAProfileH264Main ||
                                profile == VAProfileH264High);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    if (!is_supported_profile) {
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    if (!entrypoint_list) {
        *num_entrypoints = 1;
        return VA_STATUS_SUCCESS;
    }

    entrypoint_list[0] = VAEntrypointEncSlice;
    *num_entrypoints = 1;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_GetConfigAttributes(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib *attrib_list, int num_attribs) {
    (void)ctx; (void)profile; (void)entrypoint;
    if (!attrib_list) return VA_STATUS_ERROR_INVALID_PARAMETER;

    for (int i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
            case VAConfigAttribRTFormat:
                attrib_list[i].value = VA_RT_FORMAT_YUV420;
                break;
            case VAConfigAttribRateControl:
                attrib_list[i].value = VA_RC_CBR | VA_RC_VBR | VA_RC_CQP;
                break;
            case VAConfigAttribEncPackedHeaders:
                attrib_list[i].value = 0;
                break;
            case VAConfigAttribEncMaxRefFrames:
                attrib_list[i].value = 1;
                break;
            case VAConfigAttribMaxPictureWidth:
                attrib_list[i].value = BC250_MAX_WIDTH;
                break;
            case VAConfigAttribMaxPictureHeight:
                attrib_list[i].value = BC250_MAX_HEIGHT;
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
    if (!data || !config_id) return VA_STATUS_ERROR_INVALID_PARAMETER;

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
    if (!VALID_ID(config_id, MAX_CONFIGS) || !data->configs[config_id].allocated) return VA_STATUS_ERROR_INVALID_CONFIG;
    data->configs[config_id].allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryConfigAttributes(VADriverContextP ctx, VAConfigID config_id, VAProfile *profile, VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list, int *num_attribs) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!VALID_ID(config_id, MAX_CONFIGS) || !data->configs[config_id].allocated) return VA_STATUS_ERROR_INVALID_CONFIG;

    if (profile) *profile = data->configs[config_id].profile;
    if (entrypoint) *entrypoint = data->configs[config_id].entrypoint;
    if (num_attribs) *num_attribs = data->configs[config_id].num_attribs;
    if (attrib_list && data->configs[config_id].num_attribs > 0) {
        memcpy(attrib_list, data->configs[config_id].attribs, data->configs[config_id].num_attribs * sizeof(VAConfigAttrib));
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QuerySurfaceAttributes(VADriverContextP ctx, VAConfigID config, VASurfaceAttrib *attrib_list, unsigned int *num_attribs) {
    (void)ctx; (void)config;
    if (!num_attribs) return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!attrib_list) {
        *num_attribs = 3;
        return VA_STATUS_SUCCESS;
    }

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
    if (!data || !surfaces) return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (width > data->max_width || height > data->max_height) return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;

    int allocated = 0;
    for (int i = 0; i < MAX_SURFACES && allocated < num_surfaces; i++) {
        if (!data->surfaces[i].allocated) {
            bc250_surface *surf = &data->surfaces[i];
            memset(surf, 0, sizeof(*surf));
            surf->allocated = 1;
            surf->width = width;
            surf->height = height;
            surf->format = format;
            surf->ref_count = 1;

            gpu_compute_create_image(&data->gpu, width, height, format, &surf->image, &surf->memory);
            surfaces[allocated++] = i;
        }
    }

    if (allocated < num_surfaces) {
        bc250_DestroySurfaces(ctx, surfaces, allocated);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateSurfaces2(VADriverContextP ctx, unsigned int format, unsigned int width, unsigned int height,
                              VASurfaceID *surfaces, unsigned int num_surfaces,
                              VASurfaceAttrib *attrib_list, unsigned int num_attribs) {
    (void)attrib_list; (void)num_attribs;
    return bc250_CreateSurfaces(ctx, width, height, format, num_surfaces, surfaces);
}

VAStatus bc250_DestroySurfaces(VADriverContextP ctx, VASurfaceID *surface_list, int num_surfaces) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !surface_list) return VA_STATUS_ERROR_INVALID_PARAMETER;

    for (int i = 0; i < num_surfaces; i++) {
        VASurfaceID id = surface_list[i];
        if (VALID_ID(id, MAX_SURFACES) && data->surfaces[id].allocated) {
            bc250_surface *surf = &data->surfaces[id];
            surf->ref_count--;
            if (surf->ref_count <= 0) {
                gpu_compute_destroy_image(&data->gpu, surf->image, surf->memory);
                surf->allocated = 0;
            }
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateContext(VADriverContextP ctx, VAConfigID config_id, int picture_width, int picture_height, int flag, VASurfaceID *render_targets, int num_render_targets, VAContextID *context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(config_id, MAX_CONFIGS) || !data->configs[config_id].allocated || !context) {
        return VA_STATUS_ERROR_INVALID_CONFIG;
    }

    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (!data->contexts[i].allocated) {
            bc250_context *c = &data->contexts[i];
            memset(c, 0, sizeof(*c));
            c->allocated = 1;
            c->config_id = config_id;
            c->width = picture_width;
            c->height = picture_height;
            c->flag = flag;
            c->num_render_targets = num_render_targets;
            c->coded_buf_id = VA_INVALID_ID;

            if (num_render_targets > 0 && render_targets) {
                c->render_targets = malloc(num_render_targets * sizeof(VASurfaceID));
                memcpy(c->render_targets, render_targets, num_render_targets * sizeof(VASurfaceID));
            }

            VAProfile prof = data->configs[config_id].profile;
            VAEntrypoint entry = data->configs[config_id].entrypoint;

            if (entry == VAEntrypointEncSlice) {
                if (prof == VAProfileHEVCMain) {
                    c->hevc_enc = hevc_encoder_create(&data->gpu, picture_width, picture_height, 30, 4000000);
                } else {
                    c->h264_enc = h264_encoder_create(&data->gpu, picture_width, picture_height, 30, 4000000, prof);
                }
            } else if (entry == VAEntrypointVLD) {
                c->h264_dec = h264_decoder_create(&data->gpu, picture_width, picture_height);
            }

            *context = i;
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_DestroyContext(VADriverContextP ctx, VAContextID context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    bc250_context *c = &data->contexts[context];
    if (c->h264_enc) {
        h264_encoder_destroy(c->h264_enc);
        c->h264_enc = NULL;
    }
    if (c->hevc_enc) {
        hevc_encoder_destroy(c->hevc_enc);
        c->hevc_enc = NULL;
    }
    if (c->h264_dec) {
        h264_decoder_destroy(c->h264_dec);
        c->h264_dec = NULL;
    }
    if (c->render_targets) {
        free(c->render_targets);
        c->render_targets = NULL;
    }
    c->allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateBuffer(VADriverContextP ctx, VAContextID context, VABufferType type, unsigned int size, unsigned int num_elements, void *data_ptr, VABufferID *buf_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !buf_id) return VA_STATUS_ERROR_INVALID_PARAMETER;
    (void)context;

    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (!data->buffers[i].allocated) {
            bc250_buffer *b = &data->buffers[i];
            b->allocated = 1;
            b->type = type;
            b->size = size;
            b->num_elements = num_elements;
            b->mapped = 0;

            size_t total_alloc = (size_t)size * num_elements;
            if (type == VAEncCodedBufferType) {
                total_alloc += sizeof(VACodedBufferSegment);
            }

            b->data = calloc(1, total_alloc);
            if (data_ptr) {
                memcpy(b->data, data_ptr, (size_t)size * num_elements);
            } else if (type == VAEncCodedBufferType) {
                VACodedBufferSegment *seg = (VACodedBufferSegment *)b->data;
                seg->size = 0;
                seg->bit_offset = 0;
                seg->status = 0;
                seg->reserved = 0;
                seg->buf = ((uint8_t *)b->data) + sizeof(VACodedBufferSegment);
                seg->next = NULL;
            }
            *buf_id = i;
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_BufferSetNumElements(VADriverContextP ctx, VABufferID buf_id, unsigned int num_elements) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    data->buffers[buf_id].num_elements = num_elements;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_MapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuf) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated || !pbuf) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    data->buffers[buf_id].mapped = 1;
    *pbuf = data->buffers[buf_id].data;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_UnmapBuffer(VADriverContextP ctx, VABufferID buf_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(buf_id, MAX_BUFFERS) || !data->buffers[buf_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    data->buffers[buf_id].mapped = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_DestroyBuffer(VADriverContextP ctx, VABufferID buffer_id) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(buffer_id, MAX_BUFFERS) || !data->buffers[buffer_id].allocated) return VA_STATUS_ERROR_INVALID_BUFFER;
    if (data->buffers[buffer_id].is_derived) {
        if (data->buffers[buffer_id].gpu_mem) {
            vkUnmapMemory(data->gpu.device, data->buffers[buffer_id].gpu_mem);
        }
    } else {
        free(data->buffers[buffer_id].data);
    }
    data->buffers[buffer_id].data = NULL;
    data->buffers[buffer_id].allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_BeginPicture(VADriverContextP ctx, VAContextID context, VASurfaceID render_target) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) return VA_STATUS_ERROR_INVALID_CONTEXT;
    if (!VALID_ID(render_target, MAX_SURFACES) || !data->surfaces[render_target].allocated) return VA_STATUS_ERROR_INVALID_SURFACE;

    bc250_context *c = &data->contexts[context];
    c->current_render_target = render_target;
    c->coded_buf_id = VA_INVALID_ID;

    c->h264_state.has_seq = 0;
    c->h264_state.has_pic = 0;
    c->h264_state.has_slice = 0;

    gpu_compute_begin_picture(&data->gpu, data->surfaces[render_target].image);
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_RenderPicture(VADriverContextP ctx, VAContextID context, VABufferID *buffers, int num_buffers) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated || !buffers) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
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
                    if (c->h264_enc) {
                        VAEncSequenceParameterBufferH264 *seq = &c->h264_state.seq_param;
                        if (seq->intra_period > 0) {
                            h264_encoder_set_gop_size(c->h264_enc, seq->intra_period);
                        }
                        if (seq->bits_per_second > 0) {
                            h264_encoder_set_bitrate(c->h264_enc, seq->bits_per_second);
                        }
                        if (seq->time_scale > 0 && seq->num_units_in_tick > 0) {
                            uint32_t fps = seq->time_scale / (2 * seq->num_units_in_tick);
                            if (fps > 0) h264_encoder_set_fps(c->h264_enc, fps);
                        }
                    }
                }
                break;
            case VAEncPictureParameterBufferType:
                if (b->size >= sizeof(VAEncPictureParameterBufferH264)) {
                    VAEncPictureParameterBufferH264 *pic = (VAEncPictureParameterBufferH264*)b->data;
                    memcpy(&c->h264_state.pic_param, pic, sizeof(VAEncPictureParameterBufferH264));
                    c->h264_state.has_pic = 1;
                    c->coded_buf_id = pic->coded_buf;
                    if (c->h264_enc) {
                        if (pic->pic_fields.bits.idr_pic_flag) {
                            h264_encoder_force_idr(c->h264_enc);
                        }
                        if (pic->pic_init_qp > 0) {
                            h264_encoder_set_qp(c->h264_enc, pic->pic_init_qp);
                        }
                    }
                }
                break;
            case VAEncMiscParameterBufferType:
                if (b->size >= sizeof(VAEncMiscParameterBuffer)) {
                    VAEncMiscParameterBuffer *misc = (VAEncMiscParameterBuffer*)b->data;
                    if (misc->type == VAEncMiscParameterTypeRateControl && c->h264_enc) {
                        VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl*)misc->data;
                        if (rc->bits_per_second > 0) {
                            h264_encoder_set_bitrate(c->h264_enc, rc->bits_per_second);
                        }
                    } else if (misc->type == VAEncMiscParameterTypeFrameRate && c->h264_enc) {
                        VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate*)misc->data;
                        if (fr->fps > 0) {
                            h264_encoder_set_fps(c->h264_enc, fr->fps);
                        }
                    }
                }
                break;
            case VAEncSliceParameterBufferType:
                if (b->size >= sizeof(VAEncSliceParameterBufferH264)) {
                    memcpy(&c->h264_state.slice_param, b->data, sizeof(VAEncSliceParameterBufferH264));
                    c->h264_state.has_slice = 1;
                }
                break;
            case VAEncCodedBufferType:
                c->coded_buf_id = buf_id;
                break;
            default:
                break;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_EndPicture(VADriverContextP ctx, VAContextID context) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(context, MAX_CONTEXTS) || !data->contexts[context].allocated) return VA_STATUS_ERROR_INVALID_CONTEXT;

    bc250_context *c = &data->contexts[context];
    bc250_surface *surf = &data->surfaces[c->current_render_target];

    if ((c->h264_enc || c->hevc_enc) && VALID_ID(c->coded_buf_id, MAX_BUFFERS) && data->buffers[c->coded_buf_id].allocated) {
        bc250_buffer *coded_buf = &data->buffers[c->coded_buf_id];
        uint8_t *dest = ((uint8_t *)coded_buf->data) + sizeof(VACodedBufferSegment);
        size_t total_buf_sz = (coded_buf->size * coded_buf->num_elements);
        size_t max_payload = total_buf_sz > sizeof(VACodedBufferSegment) ? (total_buf_sz - sizeof(VACodedBufferSegment)) : 0;

        int written = -1;
        if (c->h264_enc) {
            written = h264_encoder_encode_frame(c->h264_enc, &data->gpu, surf->image, dest, max_payload);
        } else if (c->hevc_enc) {
            written = hevc_encoder_encode_frame(c->hevc_enc, &data->gpu, surf->image, dest, max_payload);
        }

        if (written > 0) {
            VACodedBufferSegment *seg = (VACodedBufferSegment *)coded_buf->data;
            seg->size = (unsigned int)written;
            seg->bit_offset = 0;
            seg->status = 0;
            seg->reserved = 0;
            seg->buf = dest;
            seg->next = NULL;
        }
    } else {
        gpu_compute_dispatch_encode(&data->gpu, surf->image, c->width, c->height);
        gpu_compute_end_picture(&data->gpu);
    }

    return VA_STATUS_SUCCESS;
}

VAStatus bc250_SyncSurface(VADriverContextP ctx, VASurfaceID render_target) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(render_target, MAX_SURFACES) || !data->surfaces[render_target].allocated) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    gpu_compute_sync(&data->gpu);
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QuerySurfaceStatus(VADriverContextP ctx, VASurfaceID render_target, VASurfaceStatus *status) {
    (void)ctx; (void)render_target;
    if (!status) return VA_STATUS_ERROR_INVALID_PARAMETER;
    *status = VASurfaceReady;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryImageFormats(VADriverContextP ctx, VAImageFormat *format_list, int *num_formats) {
    (void)ctx;
    if (!num_formats) return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (!format_list) {
        *num_formats = 2;
        return VA_STATUS_SUCCESS;
    }

    int i = 0;
    format_list[i].fourcc = VA_FOURCC_NV12;
    format_list[i].byte_order = VA_LSB_FIRST;
    format_list[i].bits_per_pixel = 12;
    i++;

    format_list[i].fourcc = VA_FOURCC_RGBA;
    format_list[i].byte_order = VA_LSB_FIRST;
    format_list[i].bits_per_pixel = 32;
    i++;

    *num_formats = i;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_CreateImage(VADriverContextP ctx, VAImageFormat *format, int width, int height, VAImage *image) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !format || !image) return VA_STATUS_ERROR_INVALID_PARAMETER;

    for (int i = 0; i < MAX_IMAGES; i++) {
        if (!data->images[i].allocated) {
            bc250_image *img = &data->images[i];
            memset(img, 0, sizeof(*img));
            img->allocated = 1;

            image->image_id = i;
            image->format = *format;
            image->width = width;
            image->height = height;

            if (format->fourcc == VA_FOURCC_NV12) {
                image->num_planes = 2;
                image->pitches[0] = width;
                image->offsets[0] = 0;
                image->data_size = width * height * 3 / 2;
                image->pitches[1] = width;
                image->offsets[1] = width * height;
            } else {
                image->num_planes = 1;
                image->pitches[0] = width * 4;
                image->offsets[0] = 0;
                image->data_size = width * height * 4;
            }

            VABufferID buf_id;
            bc250_CreateBuffer(ctx, 0, VAImageBufferType, image->data_size, 1, NULL, &buf_id);
            image->buf = buf_id;
            img->image = *image;
            img->buffer_id = buf_id;

            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus bc250_DestroyImage(VADriverContextP ctx, VAImageID image) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(image, MAX_IMAGES) || !data->images[image].allocated) return VA_STATUS_ERROR_INVALID_IMAGE;

    bc250_DestroyBuffer(ctx, data->images[image].buffer_id);
    data->images[image].allocated = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_DeriveImage(VADriverContextP ctx, VASurfaceID surface, VAImage *image) {
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(surface, MAX_SURFACES) || !data->surfaces[surface].allocated || !image) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    bc250_surface *surf = &data->surfaces[surface];

    VAImageFormat fmt = {
        .fourcc = VA_FOURCC_NV12,
        .byte_order = VA_LSB_FIRST,
        .bits_per_pixel = 12
    };
    VAStatus status = bc250_CreateImage(ctx, &fmt, surf->width, surf->height, image);
    if (status != VA_STATUS_SUCCESS) return status;

    bc250_image *img = &data->images[image->image_id];
    bc250_buffer *buf = &data->buffers[img->buffer_id];
    if (buf && surf->memory.memory) {
        void *mapped = NULL;
        if (vkMapMemory(data->gpu.device, surf->memory.memory, 0, surf->memory.size, 0, &mapped) == VK_SUCCESS) {
            if (buf->data) free(buf->data);
            buf->data = mapped;
            buf->mapped = 1;
            buf->is_derived = 1;
            buf->gpu_mem = surf->memory.memory;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_GetImage(VADriverContextP ctx, VASurfaceID surface, int x, int y, unsigned int width, unsigned int height, VAImageID image) {
    (void)x; (void)y; (void)width; (void)height;
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(surface, MAX_SURFACES) || !data->surfaces[surface].allocated) return VA_STATUS_ERROR_INVALID_SURFACE;
    if (!VALID_ID(image, MAX_IMAGES) || !data->images[image].allocated) return VA_STATUS_ERROR_INVALID_IMAGE;

    bc250_surface *surf = &data->surfaces[surface];
    bc250_image *img = &data->images[image];
    bc250_buffer *buf = &data->buffers[img->buffer_id];

    if (buf && buf->data && surf->memory.memory) {
        uint8_t *dst_y = (uint8_t *)buf->data + img->image.offsets[0];
        uint8_t *dst_uv = (uint8_t *)buf->data + img->image.offsets[1];
        int y_pitch = img->image.pitches[0] > 0 ? (int)img->image.pitches[0] : surf->width;
        int uv_pitch = img->image.pitches[1] > 0 ? (int)img->image.pitches[1] : surf->width;

        gpu_compute_download_nv12(&data->gpu, &surf->image, surf->memory,
                                  dst_y, y_pitch,
                                  dst_uv, uv_pitch,
                                  surf->width, surf->height);
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_PutImage(VADriverContextP ctx, VASurfaceID surface, VAImageID image, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height) {
    (void)src_x; (void)src_y; (void)src_width; (void)src_height;
    (void)dest_x; (void)dest_y; (void)dest_width; (void)dest_height;
    bc250_driver_data *data = get_driver_data(ctx);
    if (!data || !VALID_ID(surface, MAX_SURFACES) || !data->surfaces[surface].allocated) return VA_STATUS_ERROR_INVALID_SURFACE;
    if (!VALID_ID(image, MAX_IMAGES) || !data->images[image].allocated) return VA_STATUS_ERROR_INVALID_IMAGE;

    bc250_surface *surf = &data->surfaces[surface];
    bc250_image *img = &data->images[image];
    bc250_buffer *buf = &data->buffers[img->buffer_id];

    if (buf && buf->data && surf->memory.memory) {
        const uint8_t *src_y = (const uint8_t *)buf->data + img->image.offsets[0];
        const uint8_t *src_uv = (const uint8_t *)buf->data + img->image.offsets[1];
        int y_pitch = img->image.pitches[0] > 0 ? (int)img->image.pitches[0] : surf->width;
        int uv_pitch = img->image.pitches[1] > 0 ? (int)img->image.pitches[1] : surf->width;

        gpu_compute_upload_nv12(&data->gpu, &surf->image, surf->memory,
                                src_y, y_pitch,
                                src_uv, uv_pitch,
                                surf->width, surf->height);
    }
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryVideoProcFilters(VADriverContextP ctx, VAContextID context, VAProcFilterType *filters, unsigned int *num_filters) {
    (void)ctx; (void)context;
    if (!num_filters) return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (!filters) {
        *num_filters = 0;
        return VA_STATUS_SUCCESS;
    }
    *num_filters = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryVideoProcFilterCaps(VADriverContextP ctx, VAContextID context, VAProcFilterType type, void *filter_caps, unsigned int *num_filter_caps) {
    (void)ctx; (void)context; (void)type; (void)filter_caps;
    if (!num_filter_caps) return VA_STATUS_ERROR_INVALID_PARAMETER;
    *num_filter_caps = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus bc250_QueryVideoProcPipelineCaps(VADriverContextP ctx, VAContextID context, VABufferID *filters, unsigned int num_filters, VAProcPipelineCaps *pipeline_caps) {
    (void)ctx; (void)context; (void)filters; (void)num_filters;
    if (!pipeline_caps) return VA_STATUS_ERROR_INVALID_PARAMETER;
    memset(pipeline_caps, 0, sizeof(*pipeline_caps));
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
        fprintf(stderr, "[bc250-drv] Failed to initialize Vulkan compute backend!\n");
        free(data);
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    data->max_width = BC250_MAX_WIDTH;
    data->max_height = BC250_MAX_HEIGHT;
    ctx->pDriverData = data;
    ctx->str_vendor = "AMD BC-250 RDNA2 Compute VA-API Driver";

    /* Wire complete vtable */
    ctx->vtable->vaTerminate = bc250_Terminate;
    ctx->vtable->vaQueryConfigProfiles = bc250_QueryConfigProfiles;
    ctx->vtable->vaQueryConfigEntrypoints = bc250_QueryConfigEntrypoints;
    ctx->vtable->vaGetConfigAttributes = bc250_GetConfigAttributes;
    ctx->vtable->vaCreateConfig = bc250_CreateConfig;
    ctx->vtable->vaDestroyConfig = bc250_DestroyConfig;
    ctx->vtable->vaQueryConfigAttributes = bc250_QueryConfigAttributes;
    ctx->vtable->vaCreateSurfaces = bc250_CreateSurfaces;
    ctx->vtable->vaCreateSurfaces2 = bc250_CreateSurfaces2;
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
    ctx->vtable->vaCreateImage = bc250_CreateImage;
    ctx->vtable->vaDestroyImage = bc250_DestroyImage;
    ctx->vtable->vaDeriveImage = bc250_DeriveImage;
    ctx->vtable->vaGetImage = bc250_GetImage;
    ctx->vtable->vaPutImage = bc250_PutImage;

    if (major_version) *major_version = VA_MAJOR_VERSION;
    if (minor_version) *minor_version = VA_MINOR_VERSION;

    return VA_STATUS_SUCCESS;
}

VAStatus __vaDriverInit_1_0(VADriverContextP ctx) {
    int major = VA_MAJOR_VERSION;
    int minor = VA_MINOR_VERSION;
    return bc250_Initialize(ctx, &major, &minor);
}

VAStatus __vaDriverInit_0_32(VADriverContextP ctx) {
    int major = VA_MAJOR_VERSION;
    int minor = VA_MINOR_VERSION;
    return bc250_Initialize(ctx, &major, &minor);
}
