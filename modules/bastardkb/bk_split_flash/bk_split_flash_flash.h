// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "bk_split_flash_image.h"

// bk_split_flash -- flash introspection and (later) the RAM-resident flasher.
//
// Part 1 is read-only: nothing here writes to flash. The erase/program/commit
// routines that make up the fail-safe land in Part 2.

// Compute the RP2040 ROM's boot2 checksum over `len` bytes.
//
// This reproduces lib/pico-sdk/src/rp2_common/boot_stage2/pad_checksum with the
// seed the build uses (-s 0xffffffff): a bit-reversed CRC32 whose output is
// itself bit-reversed. For a valid boot2, this equals the little-endian word at
// offset 0xFC.
uint32_t bksf_boot2_crc32(const uint8_t *data, size_t len);

// Read the 32-bit little-endian boot2 CRC word stored at flash offset 0xFC.
uint32_t bksf_boot2_crc_stored(void);

// Compute the staging window in *flash-offset* space, i.e. the same space used
// by `WEAR_LEVELING_RP2040_FLASH_BASE` and `flash_range_erase`/`program`.
//
//   *start_off  : first sector-aligned offset at/above the end of the app.
//   *size_bytes : bytes available before the EEPROM region (0 if none).
//
// `__flash_binary_end` is an XIP address, so it is converted to an offset
// internally. Either output pointer may be NULL. Returns true if size > 0.
bool bksf_get_staging_window(uint32_t *start_off, uint32_t *size_bytes);

// Log the flash facts the fail-safe depends on: the stored vs. computed boot2
// CRC, `__flash_binary_end`, the wear-leveling (EEPROM) base, and the derived
// staging window. Read-only.
void bksf_dump_flash_facts(void);

// T8.1: read the staging region and compare it byte-for-byte (size + CRC32)
// against the compiled-in peer image, logging MATCH or MISMATCH. Used to confirm
// the ROM mass-storage writer accepted a combined UF2's peer block at the staging
// address (the packaged peer is the raw sidecar, identical to the embedded blob).
// Read-only.
void bksf_verify_staging_peer(void);

// ---------------------------------------------------------------------------
// Part 2: fail-safe primitives. These WRITE to flash; use only on hardware you
// can recover (BOOTSEL). See the plan's Part 2 for the ordering rationale.
// ---------------------------------------------------------------------------

// ARM (T2.1): invalidate the boot2 CRC in place by programming page 0 with the
// CRC word zeroed. No erase, so it only clears bits and the running firmware is
// unaffected; the next reset will drop to RPI-RP2. Returns false if read-back
// does not show the CRC word as zero.
bool bksf_arm(void);

// True if the boot2 CRC word currently reads as zero (armed).
bool bksf_is_armed(void);

// COMMIT, identical (T2.3): erase and reprogram flash sector 0 with the exact
// bytes already there, then reset. Proves the erase/program/re-enable-XIP
// ordering. RAM-resident and does not return.
void bksf_commit_identical(void);

// ---------------------------------------------------------------------------
// Part 2 (T2.5): self-flash from a staged image.
// ---------------------------------------------------------------------------

// Copy a full image (`len` bytes, sector-aligned, beginning with the boot2) from
// `src` (XIP) into the staging region. `src` must not overlap the staging
// region. Per-sector read-back verify. Safe to call from flash code (it never
// erases the running app).
bool bksf_stage_image(const uint8_t *src, uint32_t len);

// RAM-resident: copy the staged image at `src` (in the staging region) over the
// running app, sectors 1..N first, then sector 0 (boot2 + vector table) last.
// Per-sector read-back verify. NEVER RETURNS: it always resets, so it is safe
// even though it overwrites the code that called it. If the boot2 CRC is already
// zeroed (armed), an interrupted copy lands in RPI-RP2.
void bksf_apply_and_reset(const uint8_t *src, uint32_t len);

// Orchestrate the T2.5 test: stage the compiled-in peer image, ARM, then apply
// and reset. Does not return on success. Requires a peer image to be compiled
// in (see tools/gen_peer_image.py); otherwise it only logs and returns.
void bksf_self_update_from_peer(void);

// Part 5 (T5.1/T5.2): like bksf_self_update_from_peer(), but after ARM hold for
// `hold_ms` before applying (XIP stays on, so the app keeps running). `hold_ms`
// is a power-cut test injection: it widens the master's armed window so a cut
// lands there deterministically instead of racing the ~1 s apply. 0 = normal.
void bksf_self_update_from_peer_hold(uint32_t hold_ms);

// T2.4 helper: deterministically leave flash sector 0 in the state a COMMIT
// interruption would produce, then reset. `zeroed` true programs the sector with
// all-zero bytes (a garbage boot2), false leaves it erased. Either way the boot2
// CRC is invalid, so the ROM drops to RPI-RP2. RAM-resident; does not return.
void bksf_leave_sector0_invalid(bool zeroed);

// Generic 32-bit integrity hash over a flash/RAM buffer (the RP2040 boot2 CRC
// algorithm, reused as the image checksum). bksf_boot2_crc32() is an alias.
// The running form (update/final) lets the RPC receiver hash chunks as they
// arrive, so VERIFY never has to read the whole image inside a callback.
uint32_t bksf_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t bksf_crc32_final(uint32_t crc);
uint32_t bksf_crc32(const uint8_t *data, uint32_t len);

// The module's 4 KiB RAM sector scratch buffer, for the RPC image receiver.
uint8_t *bksf_scratch(void);

// Erase one 4 KiB staging sector for the up-front erase phase, taking interrupts
// off only for the (~45 ms typical) erase itself. A sector that already reads as
// erased (0xFF) is skipped: the staging window is untouched until an update
// writes it, so a first-time update into a never-written window pays no erase at
// all. Used one sector per housekeeping pass so the slave stays answerable.
bool bksf_staging_erase_sector(uint32_t dst_off);

// Program-only + read-back verify one *already-erased* staging sector, taking the
// 4 KiB payload from bksf_scratch(). This is the per-sector flush of the streaming
// phase (~11 ms program, no erase) and is only valid after the erase phase has
// erased that sector. Used by the slave-side RPC receiver to flush a completed
// sector.
bool bksf_staging_program_sector(uint32_t dst_off);

// Legacy (BKSF_LEGACY_FLUSH) per-sector flush: erase+program+read-back, used only
// by the A/B benchmark path (no up-front erase phase). Not used by the default
// build.
bool bksf_staging_flush_sector(uint32_t dst_off);

// Accessors for the (weakly defined) compiled-in peer image.
const uint8_t *bksf_peer_image_ptr(void);
uint32_t       bksf_peer_image_size(void);

// T9.1: accessor for the compiled-in peer image descriptor (the "header").
// Returns NULL when no peer image is compiled in.
const bksf_image_hdr_t *bksf_peer_image_hdr_ptr(void);

// T9.1: validate a distributable image against its descriptor and the running
// firmware's expectations (descriptor magic/format, protocol version, length,
// CRC32, and staging fit). Returns true if the image may be distributed; on
// false, writes a short human-readable reason into `why` (if non-NULL). Pure
// computation -- nothing is written and nothing is armed.
bool bksf_validate_image(const uint8_t *img, uint32_t len, const bksf_image_hdr_t *hdr, char *why, size_t why_len);
