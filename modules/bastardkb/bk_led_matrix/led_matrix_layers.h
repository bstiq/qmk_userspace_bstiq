// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/* ykz89: override in the keymap to name layers; return NULL to fall back. */
const char *bklm_layer_name_user(uint8_t layer);

#include "quantum.h"

bool bklm_draw_layer_stack(RGB *pixels);

RGB bklm_get_active_layer_color(void);
