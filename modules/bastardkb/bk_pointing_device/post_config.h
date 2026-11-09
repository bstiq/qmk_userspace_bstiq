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

#pragma once

// The pointing device configuration is persisted in its own community module
// eeprom datablock, exposed through eeconfig_read_bk_pointing_device_datablock()
// and eeconfig_update_bk_pointing_device_datablock().
// Keep in sync with sizeof(bkpd_config_t); see the static assert in
// bk_pointing_device.c.
#ifdef BK_HAS_POINTING_DEVICE
#    define EECONFIG_MODULE_BK_POINTING_DEVICE_DATA_SIZE 142
#endif
