/*
 * Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
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
 
 enum {
    MODE_NORMAL = 0,
    MODE_SNIPING = 1,
    MODE_DRAGSCROLL = 2,
    MODE_CURSOR = 3
};

void bkpd_mode_set_active(uint8_t id);
void bkpd_mode_toggle_active(uint8_t id);
report_mouse_t bkpd_process_active_mode(report_mouse_t mouse_report);
void bkpd_mode_current_cycle_dpi(bool forward);
void bkpd_mode_current_invert_axis(bool axis);
void bkpd_mode_legacy_sniping_cycle_dpi(bool forward);
void bkpd_mode_normal_set_active(bool active);
void bkpd_mode_sniping_set_active(bool active);
report_mouse_t bkpd_mode_dragscroll_process(report_mouse_t mouse_report);
report_mouse_t bkpd_mode_cursor_process(report_mouse_t mouse_report);
void bkpd_modes_init(void);
void bkpd_mode_cycle_dpi(uint8_t mode_id, bool forward);
void bkpd_mode_affect_dpi(uint8_t mode_id);
void bkpd_mode_current_affect_dpi(void);
uint8_t bkpd_mode_get_active_id(void);