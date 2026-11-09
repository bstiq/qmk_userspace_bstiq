/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
*  Copyright 2026 Henrik Pejer <mrpejer@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Publicw License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H

#include "led_matrix.h"
#include "led_matrix_config.h"
#include "led_matrix_motion.h"

uint8_t bklm_off_s, bklm_dim_s;
uint8_t bklm_idle_cfg;
uint16_t bklm_cycle_mask;
uint8_t bklm_save_seq;

static bool bklm_timeout_valid(uint8_t s) {
    return s >= 1 && s <= BKLM_TIMEOUT_MAX_S;
}

void bklm_config_load(void) {
    uint8_t saved[5] = {0, 0, 0, 0, 0};
    eeconfig_read_bk_led_matrix_datablock(saved, 1, sizeof(saved));
    bklm_off_s      = bklm_timeout_valid(saved[0]) ? saved[0] : 0;
    bklm_dim_s      = bklm_timeout_valid(saved[1]) ? saved[1] : 0;
    bklm_idle_cfg   = saved[2];
    bklm_cycle_mask = saved[3] | saved[4] << 8;
}

bool bklm_timeouts_set_off(uint8_t s) {
    if (!bklm_timeout_valid(s)) {
        return false;
    }
    bklm_off_s = s;
    return true;
}

bool bklm_timeouts_set_dim(uint8_t s) {
    if (!bklm_timeout_valid(s)) {
        return false;
    }
    bklm_dim_s = s;
    return true;
}

void bklm_config_adopt(uint8_t off_s, uint8_t dim_s, uint8_t idle_cfg, uint16_t cycle_mask) {
    if (off_s == 0 || bklm_timeout_valid(off_s)) {
        bklm_off_s = off_s;
    }
    if (dim_s == 0 || bklm_timeout_valid(dim_s)) {
        bklm_dim_s = dim_s;
    }
    bklm_idle_cfg   = idle_cfg;
    bklm_cycle_mask = cycle_mask;
}

void bklm_config_save(void) {
    const uint8_t saved[5] = {bklm_off_s, bklm_dim_s, bklm_idle_cfg, bklm_cycle_mask & 0xFF, bklm_cycle_mask >> 8};
    eeconfig_update_bk_led_matrix_datablock(saved, 1, sizeof(saved));
    bklm_save_seq++;
}

uint32_t bklm_off_ms(void) {
    return bklm_off_s ? bklm_off_s * 1000UL : LED_MATRIX_MODULE_OFF_MS;
}

uint32_t bklm_dim_ms(void) {
    return bklm_dim_s ? bklm_dim_s * 1000UL : LED_MATRIX_MODULE_DIM_MS;
}

uint8_t bklm_off_report_s(void) {
    return MIN(bklm_off_ms() / 1000, 255);
}

uint8_t bklm_dim_report_s(void) {
    return MIN(bklm_dim_ms() / 1000, 255);
}

/* The idle value: from the setting, or the compiled default mapped onto it. */
static uint8_t bklm_idle_value(void) {
    if (bklm_idle_cfg) {
        return bklm_idle_cfg == 255 ? 255 : bklm_idle_cfg - 1;
    }
    if (!LED_MATRIX_MODULE_MOTION_IDLE) {
        return 0;
    }
    return LED_MATRIX_MODULE_MOTION_IDLE_MS == 0 ? 255 : MIN(MAX(LED_MATRIX_MODULE_MOTION_IDLE_MS / 1000, 1), 253);
}

bool bklm_idle_enabled(void) {
    return bklm_idle_cfg ? bklm_idle_cfg - 1 != 0 : LED_MATRIX_MODULE_MOTION_IDLE;
}

uint32_t bklm_idle_ms(void) {
    if (!bklm_idle_cfg) {
        return LED_MATRIX_MODULE_MOTION_IDLE_MS;
    }
    return bklm_idle_cfg == 255 ? 0 : (bklm_idle_cfg - 1) * 1000UL;
}

uint8_t bklm_idle_report(void) {
    return bklm_idle_value();
}

bool bklm_dispatch_command(uint8_t *command_data) {
    const uint8_t child = command_data[0];
    const uint8_t value = command_data[1];
    switch (child) {
        case bklm_cmd_get:
            command_data[1] = bklm_motion_style;
            command_data[2] = bklm_off_report_s();
            command_data[3] = bklm_dim_report_s();
            command_data[4] = bklm_idle_report();
            command_data[5] = bklm_cycle_report() & 0xFF;
            command_data[6] = bklm_cycle_report() >> 8;
            command_data[7] = bklm_cycle_existing() & 0xFF;
            command_data[8] = bklm_cycle_existing() >> 8;
            return true;
        case bklm_cmd_set_style:
            bklm_motion_style_set(value, true);
            return true;
        case bklm_cmd_set_off_timeout:
            bklm_timeouts_set_off(value);
            return true;
        case bklm_cmd_set_dim_timeout:
            bklm_timeouts_set_dim(value);
            return true;
        case bklm_cmd_save:
            bklm_config_save();
            return true;
        case bklm_cmd_set_idle:
            bklm_idle_cfg = value == 255 ? 255 : MIN(value, 253) + 1;
            return true;
        case bklm_cmd_set_cycle:
            bklm_cycle_mask = value | command_data[2] << 8;
            return true;
        default:
            return false;
    }
}
