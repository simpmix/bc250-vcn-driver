/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * rate_control.c - Proportional-Integral CBR/VBR/Low-Latency rate controller
 */
#include "rate_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void rc_init(rate_control_t *rc, rc_mode_t mode, uint32_t bitrate, double fps) {
    if (!rc) return;
    rc->mode = mode;
    rc->target_bitrate = bitrate > 0 ? bitrate : 5000000;
    rc->max_bitrate = rc->target_bitrate * 3 / 2;
    rc->qp_min = 12;
    rc->qp_max = 51;
    rc->framerate = fps > 0 ? fps : 60.0;

    rc->target_bits_per_frame = (uint32_t)(rc->target_bitrate / rc->framerate);

    if (mode == RC_LOW_LATENCY) {
        /* 2-frame buffer for instant game streaming feedback */
        rc->buffer_size = rc->target_bits_per_frame * 2;
    } else {
        /* Standard 1-second leaky bucket buffer */
        rc->buffer_size = rc->target_bitrate;
    }

    rc->buffer_fullness = rc->buffer_size / 2;
    rc->base_qp = 26;
    rc->current_qp = 26;
    rc->prev_frame_sad = 0;
    rc->error_integral = 0;
}

int rc_get_frame_qp(rate_control_t *rc, uint64_t est_sad) {
    if (!rc) return 26;

    /* Compute buffer fullness deviation from 50% target */
    int64_t target_level = rc->buffer_size / 2;
    int64_t error = rc->buffer_fullness - target_level;

    /* Proportional feedback: map buffer error to QP adjustments */
    double p_term = (double)error / (double)target_level * 6.0;

    int qp_adjust = (int)round(p_term);

    /* VBR: Adjust for temporal complexity */
    if (rc->mode == RC_VBR && rc->prev_frame_sad > 0) {
        double complexity_ratio = (double)est_sad / (double)rc->prev_frame_sad;
        if (complexity_ratio > 1.3) qp_adjust += 2;
        else if (complexity_ratio < 0.7) qp_adjust -= 2;
    }

    /* Clamp maximum single-frame QP delta to prevent visual pulsation */
    int max_step = (rc->mode == RC_LOW_LATENCY) ? 3 : 2;
    int delta = (rc->base_qp + qp_adjust) - rc->current_qp;
    if (delta > max_step) delta = max_step;
    if (delta < -max_step) delta = -max_step;

    rc->current_qp += delta;

    if (rc->current_qp < rc->qp_min) rc->current_qp = rc->qp_min;
    if (rc->current_qp > rc->qp_max) rc->current_qp = rc->qp_max;

    rc->prev_frame_sad = est_sad;
    return rc->current_qp;
}

void rc_update_stats(rate_control_t *rc, int bits_used) {
    if (!rc) return;

    rc->buffer_fullness += bits_used;
    rc->buffer_fullness -= rc->target_bits_per_frame;

    if (rc->buffer_fullness < 0) {
        rc->buffer_fullness = 0;
    } else if (rc->buffer_fullness > rc->buffer_size) {
        rc->buffer_fullness = rc->buffer_size;
    }
}
