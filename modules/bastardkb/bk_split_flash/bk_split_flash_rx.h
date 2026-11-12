// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "bk_split_flash_proto.h"
#include "bk_split_flash_pack.h"

// bk_split_flash -- slave-side RAM receive path (v4).
//
// The slave buffers the entire image in a module-owned RAM buffer instead of
// programming staging flash a sector at a time. Frames are checked for CRC and
// written to their fixed offset; a per-sector slot bitmask records which slots
// arrived. One VERIFY builds a compact bad-sector bitmap from those masks and
// computes a whole-image CRC backstop; the master re-streams only flagged
// sectors. Because the slave never writes flash while streaming, there are no
// deaf IRQ-off windows and no per-sector pacing/polling.

// Bitmap sizing for the RAM buffer. Config macros come from the module's
// force-included config.h.
#define BKSF_MAX_SECTORS  (BKSF_RX_BUFFER_SIZE / BKSF_FLASH_SECTOR_SIZE)
#define BKSF_BITMAP_BYTES ((BKSF_MAX_SECTORS + 7u) / 8u)
_Static_assert(BKSF_BITMAP_BYTES <= BKSF_MSG_DATA_SIZE, "per-sector bitmap must fit bksf_msg_t.data[]");

// Lock a receive session for a sector-aligned image. `expected_crc` is the
// whole-image CRC the master will expect at VERIFY/COMMIT. Returns false if the
// image does not fit the RAM buffer or the sector bitmap. `capacity_out`, if
// non-NULL, receives the RAM buffer capacity.
bool bksf_rx_begin(uint32_t len, uint32_t expected_crc, uint32_t *capacity_out);

void bksf_rx_abort(void);
bool bksf_rx_active(void);

// Accept one frame: validate header/offset/len and the per-frame CRC, copy to
// rx_buf[off], and set the sector's slot bit. Duplicates are harmless.
bool bksf_rx_accept(uint32_t off, uint32_t n, const uint8_t *data, uint32_t crc);

// Build the bad-sector bitmap (`data[s/8] & 1<<(s%8)` set => sector s bad),
// return the number of bad sectors, and compute the whole-image CRC into
// `crc_out`. A sector is bad if any of its slots is unset (missing frame or a
// frame that failed CRC). `bitmap_cap` bounds the bytes written.
uint8_t bksf_rx_verify(uint8_t *bitmap, uint8_t bitmap_cap, uint32_t *crc_out);

// Final fail-safe gate used at COMMIT: true only if every slot is set AND the
// whole-image CRC matches the CRC supplied at BEGIN. Never commit otherwise.
bool bksf_rx_clean(uint32_t *crc_out);

const uint8_t *bksf_rx_data(void);
uint32_t       bksf_rx_len(void);
uint8_t        bksf_rx_bitmap_bytes(void);
