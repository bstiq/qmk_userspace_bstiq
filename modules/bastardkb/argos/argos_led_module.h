// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

void argos_led_module_init(void);
void argos_led_module_task(void);
void argos_led_module_set_brightness(uint8_t brightness);
void argos_led_module_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
