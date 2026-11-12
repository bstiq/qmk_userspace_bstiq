// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bk_split_flash_flash.h"
#include "bk_split_flash.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "print.h"
#include "wait.h" // wait_ms, for the T5.2 armed-window hold injection

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

// QMK platform hook (weak, defined by the RP2040 bootloader driver): plain
// reset via NVIC_SystemReset(), as opposed to jumping to the USB bootloader.
extern void mcu_reset(void);

// Part 1 is read-only. Nothing in this file writes to flash.

// Wear-leveling (EEPROM) base. Prefer the driver's own definition when the
// header is reachable; otherwise fall back to a zero base.
#if __has_include("wear_leveling_rp2040_flash_config.h")
#    include "wear_leveling_rp2040_flash_config.h"
#endif

#ifndef WEAR_LEVELING_RP2040_FLASH_BASE
#    define BKSF_EEPROM_BASE 0u
#else
#    define BKSF_EEPROM_BASE (WEAR_LEVELING_RP2040_FLASH_BASE)
#endif

// Compile-time guard for the invariant the RP2040 platform relies on: the
// wear-leveling region must lie within the declared flash size, otherwise
// pico-sdk's `flash_range_erase`/`program` `hard_assert` trips on the first
// EEPROM erase and `chSysHalt()`s the half at boot (a "USB device not
// recognized" / dead-keyboard brick). A mismatched flash-size config now fails
// the build instead of the board.
#if defined(PICO_FLASH_SIZE_BYTES) && defined(WEAR_LEVELING_RP2040_FLASH_BASE) && defined(WEAR_LEVELING_BACKING_SIZE)
_Static_assert((WEAR_LEVELING_RP2040_FLASH_BASE) + (WEAR_LEVELING_BACKING_SIZE) <= (PICO_FLASH_SIZE_BYTES), "wear-leveling region exceeds PICO_FLASH_SIZE_BYTES: set PICO_FLASH_SIZE_BYTES to the real flash size");
#endif

// End of the linked image (defined by the RP2040 linker script).
extern uint8_t __flash_binary_end[];

// The boot2 blob linked into the firmware, for the known-good CRC sample.
extern const uint8_t BOOT2_ROM[256];

static inline uint8_t bksf_bitrev8(uint8_t b) {
    b = (uint8_t)((uint8_t)(b >> 4) | (uint8_t)(b << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

static inline uint32_t bksf_bitrev32(uint32_t x) {
    x = ((x & 0x55555555u) << 1) | ((x >> 1) & 0x55555555u);
    x = ((x & 0x33333333u) << 2) | ((x >> 2) & 0x33333333u);
    x = ((x & 0x0F0F0F0Fu) << 4) | ((x >> 4) & 0x0F0F0F0Fu);
    x = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);
    x = (x << 16) | (x >> 16);
    return x;
}

uint32_t bksf_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc ^= bksf_bitrev8(data[i]);
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc;
}

uint32_t bksf_crc32_final(uint32_t crc) {
    return bksf_bitrev32(crc);
}

uint32_t bksf_crc32(const uint8_t *data, uint32_t len) {
    return bksf_crc32_final(bksf_crc32_update(0xFFFFFFFFu, data, len));
}

uint32_t bksf_boot2_crc32(const uint8_t *data, size_t len) {
    return bksf_crc32(data, (uint32_t)len);
}

uint32_t bksf_boot2_crc_stored(void) {
    const uint8_t *boot2 = (const uint8_t *)(uintptr_t)BKSF_XIP_BASE;
    uint32_t       stored;
    memcpy(&stored, boot2 + BKSF_BOOT2_CRC_OFFSET, sizeof(stored));
    return stored;
}

static uint32_t bksf_align_up(uint32_t value, uint32_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

// Flash offset of the end of the linked image. `__flash_binary_end` is an XIP
// address (0x10000000-based) whereas flash_range_erase/program and
// WEAR_LEVELING_RP2040_FLASH_BASE use offsets from the start of flash, so the
// two must not be mixed.
static uint32_t bksf_app_end_offset(void) {
    uint32_t app_end = (uint32_t)(uintptr_t)__flash_binary_end;
    return (app_end >= BKSF_XIP_BASE) ? (app_end - BKSF_XIP_BASE) : app_end;
}

bool bksf_get_staging_window(uint32_t *start_off, uint32_t *size_bytes) {
    uint32_t start  = bksf_align_up(bksf_app_end_offset(), BKSF_FLASH_SECTOR_SIZE);
    uint32_t eeprom = (uint32_t)BKSF_EEPROM_BASE;
    uint32_t size   = (eeprom > start) ? (eeprom - start) : 0u;

    if (start_off != NULL) {
        *start_off = start;
    }
    if (size_bytes != NULL) {
        *size_bytes = size;
    }
    return size > 0u;
}

void bksf_dump_flash_facts(void) {
    const uint8_t *boot2    = (const uint8_t *)(uintptr_t)BKSF_XIP_BASE;
    uint32_t       stored   = bksf_boot2_crc_stored();
    uint32_t       computed = bksf_boot2_crc32(boot2, BKSF_BOOT2_CRC_OFFSET);
    uint32_t       rom_crc  = bksf_boot2_crc32(BOOT2_ROM, BKSF_BOOT2_CRC_OFFSET);
    uint32_t       rom_word = 0;
    memcpy(&rom_word, BOOT2_ROM + BKSF_BOOT2_CRC_OFFSET, sizeof(rom_word));
    uint32_t app_end       = (uint32_t)(uintptr_t)__flash_binary_end;
    uint32_t app_off       = bksf_app_end_offset();
    uint32_t eeprom        = (uint32_t)BKSF_EEPROM_BASE;
    uint32_t staging       = 0;
    uint32_t staging_bytes = 0;
    bksf_get_staging_window(&staging, &staging_bytes);

    uprintf("bksf: %s\n", BKSF_VERSION);
    uprintf("bksf: boot2 crc stored=0x%08X computed=0x%08X (%s)\n", (unsigned)stored, (unsigned)computed, (stored == computed) ? "OK" : "MISMATCH");
    uprintf("bksf: boot2 rom  crc=0x%08X word=0x%08X (%s)\n", (unsigned)rom_crc, (unsigned)rom_word, (rom_crc == rom_word) ? "OK" : "MISMATCH");
    uprintf("bksf: __flash_binary_end=0x%08X (offset 0x%05X)\n", (unsigned)app_end, (unsigned)app_off);
    uprintf("bksf: eeprom_base=0x%05X\n", (unsigned)eeprom);
    uprintf("bksf: staging offset start=0x%05X bytes=%u (XIP 0x%08X)\n", (unsigned)staging, (unsigned)staging_bytes, (unsigned)(BKSF_XIP_BASE + staging));
}

// ---------------------------------------------------------------------------
// Part 2: fail-safe primitives.
// ---------------------------------------------------------------------------

bool bksf_is_armed(void) {
    return bksf_boot2_crc_stored() == 0u;
}

bool bksf_arm(void) {
    // Snapshot page 0 (the boot2) while XIP is on, zero the CRC word, and write
    // it back. Programming can only clear bits, so writing the identical 252
    // bytes is a no-op for them and the CRC word becomes 0. No erase, so the
    // running firmware keeps executing; the next reset will fail the ROM's CRC
    // check and drop to RPI-RP2.
    static uint8_t page[BKSF_FLASH_PAGE_SIZE];

    memcpy(page, (const void *)(uintptr_t)BKSF_XIP_BASE, sizeof(page));
    memset(page + BKSF_BOOT2_CRC_OFFSET, 0, BKSF_BOOT2_CRC_SIZE);
    __compiler_memory_barrier();

    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(0, page, sizeof(page));
    restore_interrupts(irq);

    return bksf_is_armed();
}

void __no_inline_not_in_flash_func(bksf_commit_identical)(void) {
    // Rewrite flash sector 0 with the bytes already in it. This sector holds the
    // boot2, the vector table, and early code, so the routine must run from RAM
    // (it does, via __no_inline_not_in_flash_func -> .time_critical) with
    // interrupts disabled, since no flash fetch may occur while XIP is off.
    static uint8_t sector0[BKSF_FLASH_SECTOR_SIZE];

    memcpy(sector0, (const void *)(uintptr_t)BKSF_XIP_BASE, sizeof(sector0));
    __compiler_memory_barrier();

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(0, BKSF_FLASH_SECTOR_SIZE);
    flash_range_program(0, sector0, BKSF_FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    mcu_reset();
}

// ---------------------------------------------------------------------------
// Part 2 (T2.5): self-flash from a staged image.
// ---------------------------------------------------------------------------

// Peer image, supplied by a generated file (see tools/gen_peer_image.py) that
// defines `bksf_peer_image` / `bksf_peer_image_len`. rules.mk sets
// BKSF_PEER_IMAGE_PRESENT when that file is compiled in. Do NOT define a weak
// fallback in this TU: the compiler would bind the accessor to the local object
// and the strong definition would be garbage-collected.
#ifdef BKSF_PEER_IMAGE_PRESENT
extern const uint8_t  bksf_peer_image[];
extern const uint32_t bksf_peer_image_len;

const uint8_t *bksf_peer_image_ptr(void) {
    return bksf_peer_image;
}

uint32_t bksf_peer_image_size(void) {
    return bksf_peer_image_len;
}
#else
const uint8_t *bksf_peer_image_ptr(void) {
    return NULL;
}

uint32_t bksf_peer_image_size(void) {
    return 0u;
}
#endif

// T8.1: verify that the staging region holds exactly the compiled-in peer image.
// A combined UF2 writes the peer's raw sidecar to the staging address in the same
// BOOTSEL pass as the app; this confirms the ROM mass-storage writer accepted that
// block by comparing the staged bytes (size + CRC32, and a byte count) against the
// embedded blob, which is byte-identical to the sidecar. Read-only.
void bksf_verify_staging_peer(void) {
    uint32_t start = 0;
    uint32_t bytes = 0;
    bksf_get_staging_window(&start, &bytes);
    uprintf("bksf: staging verify: window start=0x%05X bytes=%u (XIP 0x%08X)\n", (unsigned)start, (unsigned)bytes, (unsigned)(BKSF_XIP_BASE + start));

    const uint8_t *peer = bksf_peer_image_ptr();
    uint32_t       plen = bksf_peer_image_size();
    if (plen == 0u) {
        uprintf("bksf: staging verify: no peer image compiled in (see tools/gen_peer_image.py)\n");
        return;
    }
    if (plen > bytes) {
        uprintf("bksf: staging verify: peer (%u) larger than staging (%u) -> INCONCLUSIVE\n", (unsigned)plen, (unsigned)bytes);
        return;
    }

    const uint8_t *staged     = (const uint8_t *)(uintptr_t)(BKSF_XIP_BASE + start);
    uint32_t       staged_crc = bksf_crc32(staged, plen);
    uint32_t       peer_crc   = bksf_crc32(peer, plen);

    uint32_t diff = 0;
    for (uint32_t i = 0; i < plen; i++) {
        if (staged[i] != peer[i]) {
            diff++;
        }
    }

    bool match = (staged_crc == peer_crc) && (diff == 0u);
    uprintf("bksf: staging peer len=%u crc staged=0x%08X peer=0x%08X diff=%u -> %s\n", (unsigned)plen, (unsigned)staged_crc, (unsigned)peer_crc, (unsigned)diff, match ? "MATCH" : "MISMATCH");
}

// Scratch for one sector. In .bss (RAM), so it survives the app being erased.
static uint8_t bksf_bounce[BKSF_FLASH_SECTOR_SIZE];

uint8_t *bksf_scratch(void) {
    return bksf_bounce;
}

// RAM-resident byte copy: does not call memcpy, which lives in flash and may be
// overwritten mid-update. volatile defeats the memcpy idiom.
static void __no_inline_not_in_flash_func(bksf_copy4k)(volatile uint8_t *dst, const volatile uint8_t *src) {
    for (uint32_t i = 0; i < BKSF_FLASH_SECTOR_SIZE; i++) {
        dst[i] = src[i];
    }
}

// RAM-resident: erase+program one sector from bksf_bounce, then read it back.
// Caller must hold interrupts disabled: no IRQ may run while the app (or the
// sector holding the vector table) is being overwritten, because the ISRs live
// in the flash itself.
static bool __no_inline_not_in_flash_func(bksf_write_sector_ram)(uint32_t dst_off, bool verify) {
    flash_range_erase(dst_off, BKSF_FLASH_SECTOR_SIZE);
    flash_range_program(dst_off, bksf_bounce, BKSF_FLASH_SECTOR_SIZE);

    if (!verify) {
        return true;
    }
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)(BKSF_XIP_BASE + dst_off);
    for (uint32_t i = 0; i < BKSF_FLASH_SECTOR_SIZE; i++) {
        if (p[i] != bksf_bounce[i]) {
            return false;
        }
    }
    return true;
}

// The slave-side RPC receiver accumulates one sector in bksf_scratch() and
// flushes it here. This only touches staging (never the running app), so
// interrupts are toggled per sector rather than held off.
bool bksf_staging_flush_sector(uint32_t dst_off) {
    uint32_t irq = save_and_disable_interrupts();
    bool     ok  = bksf_write_sector_ram(dst_off, true);
    restore_interrupts(irq);
    return ok;
}

// RAM-resident immediate reset (AIRCR.SYSRESETREQ). Inlined, so no flash fetch.
static inline void bksf_system_reset(void) {
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u; // VECTKEY | SYSRESETREQ
    __asm volatile("dsb");
    for (;;) {
    }
}

// T2.4 helper: reproduce a COMMIT-interruption state deterministically and reset.
void __no_inline_not_in_flash_func(bksf_leave_sector0_invalid)(bool zeroed) {
    uint32_t irq = save_and_disable_interrupts();

    flash_range_erase(0, BKSF_FLASH_SECTOR_SIZE);
    if (zeroed) {
        for (uint32_t i = 0; i < BKSF_FLASH_SECTOR_SIZE; i++) {
            bksf_bounce[i] = 0;
        }
        __compiler_memory_barrier();
        flash_range_program(0, bksf_bounce, BKSF_FLASH_SECTOR_SIZE);
    }

    restore_interrupts(irq);
    bksf_system_reset();
}

// RAM-resident: wipe the wear-leveling region so the freshly-flashed firmware
// starts from a clean EEPROM. Done per sector with interrupts re-enabled between
// them so the system stays alive; the app is still intact at this point.
static void __no_inline_not_in_flash_func(bksf_erase_eeprom)(void) {
#if defined(WEAR_LEVELING_BACKING_SIZE) && (BKSF_EEPROM_BASE) > 0
    for (uint32_t off = 0; off < (WEAR_LEVELING_BACKING_SIZE); off += BKSF_FLASH_SECTOR_SIZE) {
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase((BKSF_EEPROM_BASE) + off, BKSF_FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }
#endif
}

void __no_inline_not_in_flash_func(bksf_apply_and_reset)(const uint8_t *src, uint32_t len) {
    bool ok = true;

    // Clean EEPROM first: settings and stored state (notably argos's persisted
    // firmware build id) are regenerated from the new build, so both halves end
    // up matching. Safe before the copy: the app is untouched and boot2 is
    // already invalid (armed).
    bksf_erase_eeprom();

    // From here the app is being destroyed, so interrupts must stay OFF for the
    // whole copy: the vector table and ISRs live in the flash we are erasing, so
    // an interrupt firing between sector writes would fault into erased code.
    // Never restored: this routine ends in a reset.
    (void)save_and_disable_interrupts();

    // App sectors (1..N) first.
    for (uint32_t off = BKSF_FLASH_SECTOR_SIZE; ok && off < len; off += BKSF_FLASH_SECTOR_SIZE) {
        bksf_copy4k(bksf_bounce, src + off);
        __compiler_memory_barrier();
        ok = bksf_write_sector_ram(off, true);
    }

    // COMMIT last: sector 0 (boot2 + vector table) with a valid CRC.
    if (ok) {
        bksf_copy4k(bksf_bounce, src);
        __compiler_memory_barrier();
        ok = bksf_write_sector_ram(0, true);
    }

    // Never return: the running app has been overwritten, so falling back into
    // flash is unsafe. Reset unconditionally; if `ok` is false the boot2 CRC is
    // still zeroed (armed) and the ROM will drop to RPI-RP2.
    (void)ok;
    bksf_system_reset();
}

bool bksf_stage_image(const uint8_t *src, uint32_t len) {
    uint32_t start = 0;
    uint32_t bytes = 0;
    if (!bksf_get_staging_window(&start, &bytes)) {
        return false;
    }
    if (len == 0u || (len & (BKSF_FLASH_SECTOR_SIZE - 1u)) != 0u || len > bytes) {
        return false;
    }
    // The source must not live inside the staging region we are about to erase.
    uint32_t src_off = (uint32_t)((uintptr_t)src - BKSF_XIP_BASE);
    if (src_off < start + len && src_off + len > start) {
        return false;
    }

    for (uint32_t off = 0; off < len; off += BKSF_FLASH_SECTOR_SIZE) {
        memcpy(bksf_bounce, src + off, BKSF_FLASH_SECTOR_SIZE);
        __compiler_memory_barrier();
        uint32_t irq = save_and_disable_interrupts();
        bool     ok  = bksf_write_sector_ram(start + off, true);
        restore_interrupts(irq);
        if (!ok) {
            return false;
        }
    }
    return true;
}

void bksf_self_update_from_peer(void) {
    bksf_self_update_from_peer_hold(0u);
}

void bksf_self_update_from_peer_hold(uint32_t hold_ms) {
    const uint8_t *img = bksf_peer_image_ptr();
    uint32_t       len = bksf_peer_image_size();

    if (len == 0u) {
        uprintf("bksf: no peer image compiled in (see tools/gen_peer_image.py)\n");
        return;
    }

    uint32_t start = 0;
    uint32_t bytes = 0;
    if (!bksf_get_staging_window(&start, &bytes) || len > bytes) {
        uprintf("bksf: peer image (%u) does not fit staging (%u)\n", (unsigned)len, (unsigned)bytes);
        return;
    }

    uprintf("bksf: stage %u bytes -> 0x%05X\n", (unsigned)len, (unsigned)start);
    if (!bksf_stage_image(img, len)) {
        uprintf("bksf: stage FAILED (nothing changed)\n");
        return;
    }

    uprintf("bksf: arm\n");
    if (!bksf_arm()) {
        uprintf("bksf: arm FAILED\n");
        return;
    }

    // T5.2 injection: keep the master armed (XIP still on, app still running) so
    // a power cut here lands in RPI-RP2 deterministically. Normal builds pass 0.
    if (hold_ms != 0u) {
        uprintf("bksf: armed; holding %u ms for the power-cut test\n", (unsigned)hold_ms);
        wait_ms(hold_ms);
    }

    uprintf("bksf: apply (never returns)\n");
    bksf_apply_and_reset((const uint8_t *)(uintptr_t)(BKSF_XIP_BASE + start), len);
}
