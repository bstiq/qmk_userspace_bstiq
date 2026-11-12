// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// bk_split_flash -- post-config hooks.
//
// Build-environment compatibility shim. The BastardKB modules argos,
// bk_led_matrix and bk_pointing_device persist config through the QMK
// "community module eeconfig datablock" APIs (eeconfig_read_<name>_datablock /
// eeconfig_update_<name>_datablock). The QMK checkout used for this build does
// not ship the generator that declares them, so GCC 15 (implicit declarations
// are hard errors) refuses to compile those modules. Declare the APIs here
// (this post_config.h is force-included into every translation unit); the weak
// definitions live in bk_split_flash_compat.c. On a QMK tree that ships the
// generator, its strong declarations/definitions take precedence -- remove this
// block then. bk_split_flash itself never calls these functions.
//
// post_config.h is force-included into EVERY translation unit, including
// preprocessed assembly (.S, which defines __ASSEMBLER__). Keep the C-only
// declarations out of the assembler.
#ifndef __ASSEMBLER__
#    include <stdint.h>

uint32_t eeconfig_read_argos_datablock(void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_update_argos_datablock(const void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_read_bk_pointing_device_datablock(void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_update_bk_pointing_device_datablock(const void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_read_bk_led_matrix_datablock(void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_update_bk_led_matrix_datablock(const void *data, uint32_t offset, uint32_t length);
#endif // !__ASSEMBLER__
