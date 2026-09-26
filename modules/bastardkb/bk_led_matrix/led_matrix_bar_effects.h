// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* bar_index 0 = top row of the side column (underglow index 0). */
RGB bklm_bar_effect_rgb(uint8_t bar_index, uint8_t effect_mode, uint32_t now_ms);

const char *bklm_bar_effect_mode_name(uint8_t effect_mode);
