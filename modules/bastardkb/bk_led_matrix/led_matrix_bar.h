// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

bool bklm_rgb_indicators_should_defer(void);
bool bklm_rgb_indicators_caps_lock_override(RGB *out);
void bklm_draw_left_bar_column(RGB *pixels, uint32_t now_ms);
