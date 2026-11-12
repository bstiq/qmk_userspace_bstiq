// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Build-environment compatibility shim (see post_config.h).
//
// The QMK checkout used for this build does not generate the community-module
// eeconfig datablock functions that argos, bk_led_matrix and bk_pointing_device
// call. Provide weak fallbacks so the firmware links. On a QMK tree that ships
// the generator, its (strong) definitions override these weak ones. Here the
// fallbacks simply return zeroed defaults on read and discard writes, so the
// three unrelated modules use their compiled-in defaults; bk_split_flash (the
// subject of this work) does not use them at all.

#include <stdint.h>
#include <string.h>

#define BKSF_COMPAT_DATABLOCK(name)                                         \
    __attribute__((weak)) uint32_t eeconfig_read_##name##_datablock(        \
        void *data, uint32_t offset, uint32_t length) {                      \
        (void)offset;                                                         \
        if (data != NULL && length > 0u) {                                    \
            memset(data, 0, length);                                          \
        }                                                                      \
        return length;                                                         \
    }                                                                          \
    __attribute__((weak)) uint32_t eeconfig_update_##name##_datablock(      \
        const void *data, uint32_t offset, uint32_t length) {                 \
        (void)data;                                                            \
        (void)offset;                                                          \
        return length;                                                         \
    }

BKSF_COMPAT_DATABLOCK(argos)
BKSF_COMPAT_DATABLOCK(bk_pointing_device)
BKSF_COMPAT_DATABLOCK(bk_led_matrix)
