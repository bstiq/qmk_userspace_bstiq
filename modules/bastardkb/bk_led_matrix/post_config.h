// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * Second WS2812 chain. QMK's driver is limited to 256 LEDs and already drives
 * the per-key matrix, so this strip is bit-banged on its own pin.
 * Define LED_MATRIX_MODULE_PIN in the keyboard or keymap config.
 */
#ifndef LED_MATRIX_MODULE_PIN
#    error "led_matrix module requires LED_MATRIX_MODULE_PIN"
#endif
#ifndef LED_MATRIX_MODULE_LED_COUNT
#    define LED_MATRIX_MODULE_LED_COUNT 192
#endif
/* A frame holds interrupts for ~6 ms, so it is paced instead of sent every loop. */
#ifndef LED_MATRIX_MODULE_REFRESH_MS
#    define LED_MATRIX_MODULE_REFRESH_MS 100
#endif
#ifndef LED_MATRIX_MODULE_DIM_MS
#    define LED_MATRIX_MODULE_DIM_MS 30000 // 30 seconds
#endif
#ifndef LED_MATRIX_MODULE_OFF_MS
#    define LED_MATRIX_MODULE_OFF_MS 120000 // 2 minutes
#endif
/* ykz89: the panel is on the left half only. Drawing on the other half would
 * just hold its interrupts off for ~6 ms a frame. Define as 0 for a right panel. */
#ifndef LED_MATRIX_MODULE_ON_LEFT
#    define LED_MATRIX_MODULE_ON_LEFT 1
#endif
/* ykz89: how often the USB half re-sends the pointer mode even if unchanged,
 * so a half that reconnects or missed a sync catches up. */
#ifndef LED_MATRIX_MODULE_SYNC_MS
#    define LED_MATRIX_MODULE_SYNC_MS 500
#endif
/* ykz89: trackball motion is batched and sent to the panel half this often. */
#ifndef LED_MATRIX_MODULE_MOTION_SYNC_MS
#    define LED_MATRIX_MODULE_MOTION_SYNC_MS 20
#endif

/* ykz89: trackball animations (led_matrix_motion*.c). Frames come faster while
 * one shows: each still holds interrupts ~6 ms, so 50 ms is ~12% of the time. */
/* The styles LED_MATRIX_ANIMATION_NEXT cycles through, in order (values of
 * bklm_motion_style_t in led_matrix_motion.h: 0 wave, 1 chevron, 2 comet,
 * 3 sparks, 4 stars, 5 ball, 6 warp, 7 fireworks, 8 fireflies, 9 sand,
 * 10 ocean, 11 water, 12 car, 13 asteroids, 14 matrix, 15 tetris, 16 badapple). */
#ifndef LED_MATRIX_MODULE_MOTION_CYCLE
#    define LED_MATRIX_MODULE_MOTION_CYCLE 3, 7, 10, 13, 14, 15, 16 // sparks, fireworks, ocean, asteroids, matrix, tetris, bad apple
#endif
#ifndef LED_MATRIX_MODULE_MOTION_STYLE // starting style, until one is picked and saved
#    define LED_MATRIX_MODULE_MOTION_STYLE 3 // sparks
#endif
/* 1: the animation keeps running, calmly, while the ball is still, in place of
 * the duck (the layer stack still shows over it while a layer is held).
 * 0: it fades out after the ball stops and the duck comes back. */
#ifndef LED_MATRIX_MODULE_MOTION_IDLE
#    define LED_MATRIX_MODULE_MOTION_IDLE 1
#endif
#ifndef LED_MATRIX_MODULE_MOTION_IDLE_SPEED // the drift while still, counts/s along the last direction
#    define LED_MATRIX_MODULE_MOTION_IDLE_SPEED 400
#endif
#ifndef LED_MATRIX_MODULE_MOTION_REFRESH_MS
#    define LED_MATRIX_MODULE_MOTION_REFRESH_MS 50
#endif
#ifndef LED_MATRIX_MODULE_MOTION_HOLD_MS // full strength this long after the ball stops
#    define LED_MATRIX_MODULE_MOTION_HOLD_MS 150
#endif
#ifndef LED_MATRIX_MODULE_MOTION_FADE_MS // then fades out over this long
#    define LED_MATRIX_MODULE_MOTION_FADE_MS 400
#endif
#ifndef LED_MATRIX_MODULE_MOTION_MIN_SPEED // counts/s below which the direction is kept
#    define LED_MATRIX_MODULE_MOTION_MIN_SPEED 40
#endif
#ifndef LED_MATRIX_MODULE_MOTION_PREVIEW_MS // after LED_MATRIX_ANIMATION_NEXT, play the new style this long
#    define LED_MATRIX_MODULE_MOTION_PREVIEW_MS 1000
#endif
#ifndef LED_MATRIX_MODULE_MOTION_FULL_SPEED // counts/s for full brightness and speed
#    define LED_MATRIX_MODULE_MOTION_FULL_SPEED 4000
#endif

/* Wave and chevron. */
#ifndef LED_MATRIX_MODULE_WAVE_LENGTH // crest to crest, in cells
#    define LED_MATRIX_MODULE_WAVE_LENGTH 6
#endif
#ifndef LED_MATRIX_MODULE_WAVE_RAINBOW // 1: rainbow gradient that travels with the bands; 0: active layer's colour
#    define LED_MATRIX_MODULE_WAVE_RAINBOW 1
#endif
#ifndef LED_MATRIX_MODULE_WAVE_RAINBOW_SPAN // wavelengths for one trip round the colour wheel
#    define LED_MATRIX_MODULE_WAVE_RAINBOW_SPAN 3
#endif

/* How far each style moves per trackball count: higher is slower. */
#ifndef LED_MATRIX_MODULE_COMET_COUNTS_PER_CELL
#    define LED_MATRIX_MODULE_COMET_COUNTS_PER_CELL 180
#endif
#ifndef LED_MATRIX_MODULE_COMET_DECAY // trail loses DECAY/256 of its brightness per ms
#    define LED_MATRIX_MODULE_COMET_DECAY 1
#endif
#ifndef LED_MATRIX_MODULE_STARS_COUNTS_PER_CELL // for the farthest stars; nearer ones go 2x, 3x
#    define LED_MATRIX_MODULE_STARS_COUNTS_PER_CELL 400
#endif
#ifndef LED_MATRIX_MODULE_BALL_COUNTS_PER_CELL
#    define LED_MATRIX_MODULE_BALL_COUNTS_PER_CELL 250
#endif
#ifndef LED_MATRIX_MODULE_FIREFLIES_COUNTS_PER_CELL // how far the swarm's target moves
#    define LED_MATRIX_MODULE_FIREFLIES_COUNTS_PER_CELL 200
#endif
#ifndef LED_MATRIX_MODULE_FIREFLIES_LINGER_MS // wander this long after the ball stops
#    define LED_MATRIX_MODULE_FIREFLIES_LINGER_MS 2500
#endif
#ifndef LED_MATRIX_MODULE_SAND_ROWS // rows of sand to start with
#    define LED_MATRIX_MODULE_SAND_ROWS 6
#endif
#ifndef LED_MATRIX_MODULE_SAND_STEP_MS // one fall step per this many ms (faster when rolling hard)
#    define LED_MATRIX_MODULE_SAND_STEP_MS 40
#endif
#ifndef LED_MATRIX_MODULE_OCEAN_LENGTH // main swell, crest to crest, in cells
#    define LED_MATRIX_MODULE_OCEAN_LENGTH 8
#endif
#ifndef LED_MATRIX_MODULE_WATER_LEVEL // resting water depth, in rows
#    define LED_MATRIX_MODULE_WATER_LEVEL 7
#endif
#ifndef LED_MATRIX_MODULE_WATER_LINGER_MS // keep sloshing this long after the ball stops
#    define LED_MATRIX_MODULE_WATER_LINGER_MS 3000
#endif
#ifndef LED_MATRIX_MODULE_CAR_SPEED // road cells scrolled per second
#    define LED_MATRIX_MODULE_CAR_SPEED 8
#endif
#ifndef LED_MATRIX_MODULE_CAR_TURN_RATE // steering, in 1/256 turns per second
#    define LED_MATRIX_MODULE_CAR_TURN_RATE 280
#endif
#ifndef LED_MATRIX_MODULE_ASTEROIDS_TURN_RATE // ship turning, in 1/256 turns per second
#    define LED_MATRIX_MODULE_ASTEROIDS_TURN_RATE 320
#endif
#ifndef LED_MATRIX_MODULE_ASTEROIDS_FIRE_MS // average time between shots (random, half to 1.5x this)
#    define LED_MATRIX_MODULE_ASTEROIDS_FIRE_MS 550
#endif
#ifndef LED_MATRIX_MODULE_SAND_LINGER_MS // keep showing the sand settle this long after the ball stops
#    define LED_MATRIX_MODULE_SAND_LINGER_MS 1500
#endif
