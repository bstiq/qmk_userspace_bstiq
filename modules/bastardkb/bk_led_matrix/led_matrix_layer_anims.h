// Copyright 2026 @ykz89
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* ykz89: an animation per layer, in place of the static layer stack. The keymap
 * picks one per layer (bklm_layer_anim_user); NONE keeps the layer stack. */

typedef enum {
    BKLM_LAYER_ANIM_NONE,
    BKLM_LAYER_ANIM_GEAR,      /* a spinning gear: settings, function keys */
    BKLM_LAYER_ANIM_NAVARROW,  /* a GPS navigation arrow turning to new headings: navigation */
    BKLM_LAYER_ANIM_EQUALIZER, /* bouncing equaliser bars: media */
    BKLM_LAYER_ANIM_HAND,      /* the pointing-hand cursor wandering and clicking: pointer */
    BKLM_LAYER_ANIM_DIGIT,     /* one big digit dancing, counting every half second: numbers */
    BKLM_LAYER_ANIM_MATH,      /* maths glyphs floating and fading, "math lady" style: symbols */
} bklm_layer_anim_t;

/* Defaults to NONE for all layers. */
uint8_t bklm_layer_anim_user(uint8_t layer);

/* Paints the held layer's animation. False on the base layer, or when the
 * layer has no animation (then the layer stack shows instead). */
bool bklm_draw_layer_anim(RGB *pixels);
