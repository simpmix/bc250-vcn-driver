/* bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver */
/*
 * Copyright (c) 2026 BC-250 Project
 * SPDX-License-Identifier: MIT
 *
 * rate_control.h - CBR/VBR rate control header
 */
#ifndef RATE_CONTROL_H
#define RATE_CONTROL_H

#include <stdint.h>

typedef enum {
    RC_CBR,
    RC_VBR
} rc_mode_t;

typedef struct {
    rc_mode_t mode;
    uint32_t target_bitrate;
    uint32_t max_bitrate;
    int qp_min;
    int qp_max;
    
    // Buffer model variables
    double framerate;
    uint32_t target_bits_per_frame;
    int64_t buffer_fullness;
    int64_t buffer_size;
    int current_qp;
    uint64_t prev_frame_sad;
} rate_control_t;

void rc_init(rate_control_t *rc, rc_mode_t mode, uint32_t bitrate, double fps);
int rc_get_frame_qp(rate_control_t *rc, uint64_t est_sad);
void rc_update_stats(rate_control_t *rc, int bits_used);

#endif
