/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * rate_control.c - CBR/VBR rate control implementation
 */
#include "rate_control.h"
#include <stdio.h>

void rc_init(rate_control_t *rc, rc_mode_t mode, uint32_t bitrate, double fps) {
    rc->mode = mode;
    rc->target_bitrate = bitrate;
    rc->max_bitrate = bitrate * 2;
    rc->qp_min = 10;
    rc->qp_max = 51;
    rc->framerate = fps;
    
    rc->target_bits_per_frame = (uint32_t)(bitrate / fps);
    rc->buffer_size = rc->target_bitrate; // 1 second buffer
    rc->buffer_fullness = rc->buffer_size / 2; // start half full
    rc->current_qp = 26; // Default QP
    rc->prev_frame_sad = 0;
}

int rc_get_frame_qp(rate_control_t *rc, uint64_t est_sad) {
    if (rc->mode == RC_VBR) {
        if (est_sad > rc->prev_frame_sad * 1.5) {
            rc->current_qp += 2;
        } else if (est_sad < rc->prev_frame_sad * 0.5) {
            rc->current_qp -= 2;
        }
    }
    
    // Adjust based on buffer
    double buffer_ratio = (double)rc->buffer_fullness / rc->buffer_size;
    if (buffer_ratio > 0.8) {
        rc->current_qp += 3; // Buffer filling up, increase QP
    } else if (buffer_ratio > 0.6) {
        rc->current_qp += 1;
    } else if (buffer_ratio < 0.2) {
        rc->current_qp -= 3; // Buffer empty, decrease QP
    } else if (buffer_ratio < 0.4) {
        rc->current_qp -= 1;
    }
    
    if (rc->current_qp < rc->qp_min) rc->current_qp = rc->qp_min;
    if (rc->current_qp > rc->qp_max) rc->current_qp = rc->qp_max;
    
    rc->prev_frame_sad = est_sad;
    return rc->current_qp;
}

void rc_update_stats(rate_control_t *rc, int bits_used) {
    rc->buffer_fullness += bits_used;
    rc->buffer_fullness -= rc->target_bits_per_frame;
    
    if (rc->buffer_fullness < 0) {
        rc->buffer_fullness = 0;
    } else if (rc->buffer_fullness > rc->buffer_size) {
        rc->buffer_fullness = rc->buffer_size; // drop frames ideally, but clamp here
    }
}
