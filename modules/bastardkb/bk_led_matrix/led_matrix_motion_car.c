// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "led_matrix_motion.h"

/*
 * Car: a top-down car fixed at the centre, always driving: the road scrolls
 * past underneath and the ball only steers, at a limited turn rate.
 * Car and road are shapes in car coordinates (forward f, sideways s, Q8 cells),
 * not sprites, so they can face any angle; each cell is sampled four times.
 * Angles are 0..255 for a full turn, 0 = right, 64 = up (panel y is up).
 */

#define BKLM_CAR_HALF_L 704 /* 2.75 cells: half the length */
#define BKLM_CAR_HALF_W 448 /* 1.75 cells: half the width */
#define BKLM_ROAD_HALF 832  /* 3.25 cells: half the road width */
#define BKLM_ROAD_DASH 768  /* 3 cells: centre line period (dash + gap) */

/* The car's colour at (f, s) in its own coordinates, or false outside it. */
static bool bklm_car_shape(int32_t f, int32_t s, RGB *c) {
    const int32_t as = s < 0 ? -s : s;
    if (f > BKLM_CAR_HALF_L || f < -BKLM_CAR_HALF_L || as > BKLM_CAR_HALF_W) return false;
    if ((f > BKLM_CAR_HALF_L - 128 || f < -BKLM_CAR_HALF_L + 128) && as > BKLM_CAR_HALF_W - 128) return false; /* round the corners */

    if (f > BKLM_CAR_HALF_L - 230 && as > BKLM_CAR_HALF_W - 230) {
        *c = (RGB){.r = 255, .g = 235, .b = 160}; /* headlights */
    } else if (f < -BKLM_CAR_HALF_L + 200 && as > BKLM_CAR_HALF_W - 230) {
        *c = (RGB){.r = 255, .g = 0, .b = 20}; /* tail lights */
    } else if (f > 0 && f < 420 && as < BKLM_CAR_HALF_W - 100) {
        *c = (RGB){.r = 90, .g = 180, .b = 240}; /* windscreen */
    } else if (f > -560 && f < -300 && as < BKLM_CAR_HALF_W - 110) {
        *c = (RGB){.r = 45, .g = 100, .b = 160}; /* rear window */
    } else {
        *c = (RGB){.r = 225, .g = 25, .b = 30}; /* body */
    }
    return true;
}

/* The world under the car at (f, s), car coordinates, with the road scrolled
 * by `dist` (Q8 cells travelled). */
static RGB bklm_road(int32_t f, int32_t s, int32_t dist) {
    const int32_t as = s < 0 ? -s : s;
    const int32_t w  = f + dist; /* along the road, in world terms */
    if (as < BKLM_ROAD_HALF) {
        if (as > BKLM_ROAD_HALF - 150) {
            return (RGB){.r = 200, .g = 200, .b = 195}; /* edge line */
        }
        const int32_t along = ((w % BKLM_ROAD_DASH) + BKLM_ROAD_DASH) % BKLM_ROAD_DASH;
        if (as < 70 && along < BKLM_ROAD_DASH / 2) {
            return (RGB){.r = 235, .g = 190, .b = 40}; /* centre dash */
        }
        return (RGB){.r = 40, .g = 40, .b = 46}; /* asphalt */
    }
    /* Grass: a hash of the world cell picks lighter tufts and darker patches. */
    const uint32_t cell = (uint32_t)((w >> 8) * 73856093) ^ (uint32_t)((s >> 8) * 19349663);
    const uint32_t hash = (cell ^ (cell >> 13)) * 0x5bd1e995;
    switch ((hash >> 24) & 7) {
        case 0:
            return (RGB){.r = 40, .g = 120, .b = 35}; /* tuft */
        case 1:
            return (RGB){.r = 5, .g = 35, .b = 10}; /* shadow */
        default:
            return (RGB){.r = 12, .g = 70, .b = 20};
    }
}

bool bklm_motion_car(RGB *pixels, const bklm_motion_t *m) {
    static uint8_t ang;
    static int32_t dist; /* Q8 cells driven, for the road's scroll */

    const uint8_t target = bklm_atan2_8(m->dir_y, m->dir_x);
    if (m->fresh) {
        ang = target;
    }

    const int8_t  diff = (int8_t)(uint8_t)(target - ang);
    const int32_t most = MAX((int32_t)(LED_MATRIX_MODULE_CAR_TURN_RATE * m->dt / 1000), 1);
    ang                = (uint8_t)(ang + CONSTRAIN(diff, -most, most));

    /* Wrap well before overflow, at a multiple of the dash period. */
    dist = (dist + (int32_t)(LED_MATRIX_MODULE_CAR_SPEED * 256 * m->dt / 1000)) % (BKLM_ROAD_DASH * 4096);

    const int32_t hx = bklm_cos8(ang), hy = bklm_sin8(ang); /* heading * 127 */
    const int32_t cx = BKLM_COLS * 128, cy = BKLM_ROWS * 128;      /* the car sits here */

    for (uint8_t py = 0; py < BKLM_ROWS; py++) {
        for (uint8_t px = 0; px < BKLM_COLS; px++) {
            /* Samples at the quarter points: road averaged, car coverage counted. */
            uint32_t r = 0, g = 0, b = 0, hits = 0;
            int32_t  beam_f = 0, beam_s = 0;
            for (uint8_t q = 0; q < 4; q++) {
                const int32_t dx = px * 256 + 64 + (q & 1) * 128 - cx, dy = py * 256 + 64 + (q >> 1) * 128 - cy;
                const int32_t f = (dx * hx + dy * hy) / 127, s = (-dx * hy + dy * hx) / 127;
                const RGB     w = bklm_road(f, s, dist);
                r += w.r, g += w.g, b += w.b;
                RGB c;
                if (bklm_car_shape(f, s, &c)) hits++;
                if (q == 0) beam_f = f, beam_s = s;
            }
            RGB *dst = bklm_px(pixels, px, py);
            *dst     = (RGB){.r = (uint8_t)(r / 4), .g = (uint8_t)(g / 4), .b = (uint8_t)(b / 4)};

            const int32_t ahead = beam_f - BKLM_CAR_HALF_L;
            if (ahead > 0 && ahead < 3 * 256 && (beam_s < 0 ? -beam_s : beam_s) < 200 + ahead / 2) {
                bklm_add(dst, (RGB){.r = 255, .g = 200, .b = 110}, (uint8_t)(110 * (3 * 256 - ahead) / (3 * 256)));
            }
            /* The car on top, blended by how much of the cell it covers; its
             * colour from the cell centre, so the small windows stay crisp. */
            if (hits) {
                const int32_t dx = px * 256 + 128 - cx, dy = py * 256 + 128 - cy;
                RGB           c  = {.r = 225, .g = 25, .b = 30}; /* body, if the centre just misses */
                bklm_car_shape((dx * hx + dy * hy) / 127, (-dx * hy + dy * hx) / 127, &c);
                dst->r = (uint8_t)((dst->r * (4 - hits) + c.r * hits) / 4);
                dst->g = (uint8_t)((dst->g * (4 - hits) + c.g * hits) / 4);
                dst->b = (uint8_t)((dst->b * (4 - hits) + c.b * hits) / 4);
            }
            dst->r = (uint8_t)(dst->r * m->fade / 255);
            dst->g = (uint8_t)(dst->g * m->fade / 255);
            dst->b = (uint8_t)(dst->b * m->fade / 255);
        }
    }
    return true;
}
