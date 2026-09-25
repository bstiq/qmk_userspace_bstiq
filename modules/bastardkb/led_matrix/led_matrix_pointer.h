// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "led_matrix_icons.h"

/* Logic only: which logo belongs to the current pointer mode.
 * Returns NULL when there is no pointing module, or the mode is Normal. */
const led_matrix_icon_t *led_matrix_pointer_active_icon(void);
