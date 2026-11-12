// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// bk_split_flash -- module configuration.
//
// The module's keycodes are declared in qmk_module.json; the build generates
// the enum from that file, so no keycode range is defined here. The generated
// keycode (BK_SPLIT_FLASH_TRIGGER / BKSF_TRG) lives in the QK_COMMUNITY_MODULE
// range (0x77C0..0x77FF).

// Flash geometry (RP2040 QSPI flash).
#ifndef BKSF_FLASH_SECTOR_SIZE
#    define BKSF_FLASH_SECTOR_SIZE 4096
#endif
#ifndef BKSF_FLASH_PAGE_SIZE
#    define BKSF_FLASH_PAGE_SIZE 256
#endif

// The RP2040 second-stage bootloader is a 256-byte block at flash offset 0.
// Its last four bytes hold a bit-reversed CRC32 over the preceding 252 bytes;
// the ROM compares this on boot and drops to BOOTSEL on mismatch.
#define BKSF_BOOT2_SIZE 256
#define BKSF_BOOT2_CRC_OFFSET 0xFC
#define BKSF_BOOT2_CRC_SIZE 4

// XIP base address of the internal QSPI flash.
#ifndef BKSF_XIP_BASE
#    define BKSF_XIP_BASE 0x10000000u
#endif

// Bring-up test selector (Part 2). Override in the keymap's config.h, rebuild,
// flash, and run the one test. Default is the safe read-only dump.
#define BKSF_TEST_DUMP 0         // T1: log flash facts only (read-only)
#define BKSF_TEST_ARM 1          // T2.1: arm (zero boot2 CRC), keep running
#define BKSF_TEST_BOOTSEL 2      // T2.2: arm, then plain reset -> ROM CRC check -> RPI-RP2
#define BKSF_TEST_COMMIT 3       // T2.3: erase+reprogram sector 0 with identical bytes, reset
#define BKSF_TEST_UPDATE 4       // T2.5: stage the peer image, arm, apply (app+commit), reset
#define BKSF_TEST_ERASE0 5       // T2.4: leave sector 0 erased, reset (deterministic mid-COMMIT state)
#define BKSF_TEST_PARTIAL0 6     // T2.4: erase sector 0, program it with zeros, reset (mid-program state)
#define BKSF_TEST_RPC 7          // T3: master->slave ARM/ARMED/ABORT handshake
#define BKSF_TEST_UPDATE_SLAVE 8 // T4: master streams the peer image to the slave and commits
#define BKSF_TEST_UPDATE_BOTH 9  // T5: master updates the slave, then self-flashes last
#define BKSF_TEST_STAGING 10     // T8.1: compare the staged region against the embedded peer
#ifndef BKSF_TEST
#    define BKSF_TEST BKSF_TEST_DUMP
#endif

// T4.3 link-integrity test. Deliberately corrupt the payload of one frame on
// the master *after* its CRC32 is computed, so the slave drops that frame and
// the final VERIFY bitmap flags its sector:
//   BKSF_CORRUPT_OFF  : image offset of the frame to corrupt (0 = disabled).
//   BKSF_CORRUPT_MODE : 1 = corrupt only the first attempt (the re-stream then
//                       succeeds -> the update still completes and commits);
//                       2 = corrupt every attempt (re-stream retries exhaust
//                       -> the master aborts, the update must never commit).
#ifndef BKSF_CORRUPT_OFF
#    define BKSF_CORRUPT_OFF 0u
#endif
#ifndef BKSF_CORRUPT_MODE
#    define BKSF_CORRUPT_MODE 1
#endif

// Size of the slave's whole-image RAM receive buffer. The distributed peer
// image (the UF2 payload, padded to a 4096-byte sector) is ~90 KB for this
// board; 128 KB leaves headroom. It must fit ram0 (256 KB) alongside the rest
// of .bss/.data and the ChibiOS heap -- the linker refuses an oversize value,
// and the image descriptor rejects anything larger at runtime.
#ifndef BKSF_RX_BUFFER_SIZE
#    define BKSF_RX_BUFFER_SIZE (128u * 1024u)
#endif

// Max number of re-stream passes after a VERIFY reports bad sectors. A lost or
// corrupt frame is repaired by re-sending that sector's frames; exhausting this
// bound aborts the run without ever committing.
#ifndef BKSF_RESTREAM_MAX
#    define BKSF_RESTREAM_MAX 8
#endif

// T5.2 power-cut injection for the combined (both-halves) update. After the
// master arms its own boot2, hold this long before applying, so a power cut
// hits the master's armed window deterministically instead of racing the ~1 s
// copy. XIP stays on during the hold, so the app keeps running and the COM is
// observable. 0 = off (normal operation). The master is the USB half, so a cut
// here lands it in RPI-RP2 (recoverable).
#ifndef BKSF_SELF_HOLD_MS
#    define BKSF_SELF_HOLD_MS 0u
#endif

// Split RPC: the module owns one transaction id. The BastardKB QMK fork
// auto-defines SPLIT_TRANSACTION_RPC and registers RPC_ID_BK_SPLIT_FLASH_CMD in
// the split transaction enum via its generated community-module .inc. The stock
// QMK in this workspace has neither, so provide the equivalent from the module:
// enable the code path and add the id through QMK's standard
// SPLIT_TRANSACTION_IDS_USER hook (argos already uses the _KB hook, so the RPC
// machinery itself is present). On a tree that generates these, drop this block.
#ifndef SPLIT_TRANSACTION_RPC
#    define SPLIT_TRANSACTION_RPC
#endif
#ifndef SPLIT_TRANSACTION_IDS_USER
#    define SPLIT_TRANSACTION_IDS_USER RPC_ID_BK_SPLIT_FLASH_CMD
#endif

// v4 bulk data plane. The control-plane messages stay 32 B, but image frames
// travel in a dedicated length-prefixed frame (bk_split_flash_pack.h) delivered
// by a single split transaction, so the shared RPC buffer must hold a whole
// frame. The frame is 16 B header + BKSF_BULK_CHUNK payload; the transport's
// transaction size fields are uint8_t, so a frame must fit in 255 B (asserted
// in the header). The 32 B reply buffer must still hold a control message.
#ifndef BKSF_BULK_CHUNK
#    define BKSF_BULK_CHUNK 224
#endif
#ifndef RPC_M2S_BUFFER_SIZE
#    define RPC_M2S_BUFFER_SIZE (16 + BKSF_BULK_CHUNK)
#endif
#ifndef RPC_S2M_BUFFER_SIZE
#    define RPC_S2M_BUFFER_SIZE 32
#endif
