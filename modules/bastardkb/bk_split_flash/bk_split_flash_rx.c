// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bk_split_flash_rx.h"
#include "bk_split_flash_flash.h"
#include "bk_split_flash_proto.h"

#include <string.h>

// The whole-image RAM receive buffer. Both halves compile it in, but only the
// slave uses it. It lives in .bss (ram0, 256 KB): the existing ~78 KB of
// .bss/.data plus this buffer leave a comfortable heap, and the bootloader
// bounce buffer is a separate 4 KiB object in the same file set.
static uint8_t  rx_buf[BKSF_RX_BUFFER_SIZE];
static uint32_t sector_slots[BKSF_MAX_SECTORS];

static bool     rx_active;
static uint32_t rx_len;
static uint32_t rx_expected_crc;
static uint8_t  rx_sectors;
static uint8_t  rx_bitmap_bytes;

bool bksf_rx_begin(uint32_t len, uint32_t expected_crc, uint32_t *capacity_out) {
    rx_active = false;
    if (capacity_out != NULL) {
        *capacity_out = (uint32_t)BKSF_RX_BUFFER_SIZE;
    }
    if (len == 0u || (len & (BKSF_FLASH_SECTOR_SIZE - 1u)) != 0u || len > BKSF_RX_BUFFER_SIZE) {
        return false;
    }
    uint32_t sectors = len / BKSF_FLASH_SECTOR_SIZE;
    if (sectors == 0u || sectors > BKSF_MAX_SECTORS) {
        return false;
    }
    if ((sectors + 7u) / 8u > BKSF_MSG_DATA_SIZE) {
        return false;
    }

    // Deterministic gaps: unwritten slots read as zero. A clean bitmap means
    // every byte was written by a CRC-validated frame, so gaps never reach an
    // applied image.
    memset(rx_buf, 0, len);
    memset(sector_slots, 0, sizeof(sector_slots));
    rx_len           = len;
    rx_expected_crc  = expected_crc;
    rx_sectors       = (uint8_t)sectors;
    rx_bitmap_bytes  = (uint8_t)((sectors + 7u) / 8u);
    rx_active        = true;
    return true;
}

void bksf_rx_abort(void) {
    rx_active = false;
}

bool bksf_rx_active(void) {
    return rx_active;
}

const uint8_t *bksf_rx_data(void) {
    return rx_buf;
}

uint32_t bksf_rx_len(void) {
    return rx_len;
}

uint8_t bksf_rx_bitmap_bytes(void) {
    return rx_bitmap_bytes;
}

bool bksf_rx_accept(uint32_t off, uint32_t n, const uint8_t *data, uint32_t crc) {
    if (!rx_active || data == NULL) {
        return false;
    }
    if (n == 0u || n > BKSF_BULK_CHUNK) {
        return false;
    }
    if (off >= rx_len || off + n > rx_len) {
        return false;
    }

    uint32_t sector    = off / BKSF_FLASH_SECTOR_SIZE;
    uint32_t in_sector = off % BKSF_FLASH_SECTOR_SIZE;
    uint32_t slot      = in_sector / BKSF_BULK_CHUNK;
    if (sector >= rx_sectors || slot >= BKSF_SLOTS_PER_SECTOR) {
        return false;
    }
    // Reject a corrupted offset/len header: a frame must land exactly on its
    // deterministic slot boundary and carry that slot's exact length.
    if (off != sector * BKSF_FLASH_SECTOR_SIZE + slot * BKSF_BULK_CHUNK) {
        return false;
    }
    if (n != bksf_slot_len(slot)) {
        return false;
    }
    // Per-frame CRC before accepting: a corrupt payload never reaches rx_buf
    // and never sets its slot, so it surfaces in the VERIFY bitmap and is
    // re-streamed.
    if (bksf_crc32(data, n) != crc) {
        return false;
    }

    memcpy(rx_buf + off, data, n);
    sector_slots[sector] |= (1u << slot);
    return true;
}

uint8_t bksf_rx_verify(uint8_t *bitmap, uint8_t bitmap_cap, uint32_t *crc_out) {
    uint32_t computed = bksf_crc32(rx_buf, rx_len);
    if (crc_out != NULL) {
        *crc_out = computed;
    }

    uint8_t nbytes = rx_bitmap_bytes;
    if (nbytes > bitmap_cap) {
        nbytes = bitmap_cap;
    }
    if (bitmap != NULL) {
        memset(bitmap, 0, nbytes);
    }

    uint8_t bad = 0u;
    for (uint8_t s = 0; s < rx_sectors; s++) {
        if (sector_slots[s] != BKSF_ALL_SLOTS_MASK) {
            bad++;
            if (bitmap != NULL && (s >> 3) < nbytes) {
                bitmap[s >> 3] |= (uint8_t)(1u << (s & 7u));
            }
        }
    }
    return bad;
}

bool bksf_rx_clean(uint32_t *crc_out) {
    uint8_t  bitmap[BKSF_BITMAP_BYTES];
    uint32_t computed = 0u;
    uint8_t  bad      = bksf_rx_verify(bitmap, sizeof(bitmap), &computed);
    if (crc_out != NULL) {
        *crc_out = computed;
    }
    return rx_active && bad == 0u && computed == rx_expected_crc;
}
