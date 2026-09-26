// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

uint8_t bklm_get_active_layer_and_color(RGB *color);
void bklm_layers_paint(RGB *pixels);
