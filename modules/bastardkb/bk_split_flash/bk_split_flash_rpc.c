// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <string.h>

#include "print.h"
#include "transactions.h"
#include "split_util.h" // is_transport_connected()

#include "bk_split_flash.h"
#include "bk_split_flash_flash.h"
#include "bk_split_flash_proto.h"
#include "bk_split_flash_pack.h"

// Parts 3 & 4: the split control plane. Part 3 is the ARM/ARMED/ABORT
// handshake; Part 4 streams the image into the slave's staging region, verifies
// it, and commits (the slave then reboots into the new build).

#ifdef SPLIT_TRANSACTION_RPC

// Let an in-flight chunk reply finish before the slave starts a sector flush.
#define BKSF_FLUSH_DELAY_MS 4

// ---------------------------------------------------------------------------
// Slave side
// ---------------------------------------------------------------------------

// Receive state, valid on the slave while an image is arriving.
static bool     rxp_active;
static uint32_t rxp_staging;     // staging region start (flash offset)
static uint32_t rxp_len;         // image length
static uint32_t rxp_off;         // bytes received into the sector buffer
static uint32_t rxp_crc;         // expected whole-image CRC
static uint32_t rxp_crc_run;     // running CRC over received chunks (finalised at VERIFY)
static uint32_t rxp_flushed;     // bytes durably written to staging
static uint32_t rxp_erased;      // bytes erased up front (erase phase progress)
static bool     rxp_erasing;     // erase phase in progress (no chunks accepted yet)
static bool     rxp_pending;     // a full sector is buffered, awaiting flush
static uint32_t rxp_pending_off; // staging offset of the buffered sector
static uint32_t rxp_flush_not_before; // don't start the flush before this time

static void bksf_rxp_begin(bksf_msg_t *reply, const bksf_msg_t *msg) {
    uint32_t staging = 0;
    uint32_t bytes   = 0;
    uint32_t size    = msg->value;
    uint32_t crc     = 0;
    memcpy(&crc, msg->data, sizeof(crc));

    rxp_active  = false;
    rxp_pending = false;
    rxp_erasing = false;
    if (!bksf_get_staging_window(&staging, &bytes)) {
        reply->status = BKSF_ST_ERR;
        return;
    }
    if (size == 0 || (size & (BKSF_FLASH_SECTOR_SIZE - 1u)) != 0u || size > bytes) {
        reply->status = BKSF_ST_ERR;
        return;
    }

    rxp_staging = staging;
    rxp_len     = size;
    rxp_off     = 0;
    rxp_crc     = crc;
    rxp_crc_run = 0xFFFFFFFFu;
    rxp_flushed = 0;
    // Start the erase phase: the slave erases the whole staging window one sector
    // per housekeeping pass *before* any image byte moves, so the streaming
    // phase's per-sector flush is program-only. Reported through SYNC (rxp_erased).
    rxp_erased  = 0;
    rxp_erasing = !BKSF_LEGACY_FLUSH;
    rxp_active  = true;

    reply->cmd   = BKSF_CMD_ACK;
    reply->value = staging;
}

// Buffers one chunk into the 4 KiB sector scratch, hashing it incrementally and
// flagging a pending flush on an exact sector boundary. Shared by the control
// RPC chunk path and the T7.1 bulk-frame path so both enforce the same rules.
// Returns true if accepted; the caller always reports the resulting rxp_off.
static bool bksf_rxp_accept(uint32_t off, uint8_t n, const uint8_t *data, uint32_t crc, uint8_t max_chunk) {
    if (!rxp_active || rxp_erasing || n == 0 || n > max_chunk || off != rxp_off || rxp_off + n > rxp_len || rxp_pending) {
        return false;
    }

    uint32_t in_sector = off & (BKSF_FLASH_SECTOR_SIZE - 1u);
    if (n > BKSF_FLASH_SECTOR_SIZE - in_sector) {
        return false;
    }

    // Per-chunk integrity: check the CRC *before* buffering or hashing, so a
    // corrupted payload is never written to staging and rxp_off does not
    // advance; the master re-sends this exact offset.
    uint32_t got = bksf_crc32(data, n);
    if (got != crc) {
        uprintf("bksf: slave chunk crc mismatch off=%u len=%u want=0x%08X got=0x%08X -> NACK\n", (unsigned)off, (unsigned)n, (unsigned)crc, (unsigned)got);
        return false;
    }

    uint8_t *buf = bksf_scratch();
    memcpy(buf + in_sector, data, n);
    rxp_crc_run = bksf_crc32_update(rxp_crc_run, data, n);
    rxp_off += n;

    if (in_sector + n == BKSF_FLASH_SECTOR_SIZE) {
        rxp_pending          = true;
        rxp_pending_off      = rxp_staging + (off - in_sector);
        // Defer the flush so it does not start while the master is still reading
        // this chunk's reply (the flush holds interrupts off and would drop the
        // in-flight transport transaction -> a zeroed "false" reply).
        rxp_flush_not_before = timer_read32() + BKSF_FLUSH_DELAY_MS;
    }
    return true;
}

// Buffers a chunk only; the sector flush happens in housekeeping, never inside
// the RPC callback (an erase takes tens of ms and would blow the reply timeout).
static void bksf_rxp_chunk(bksf_msg_t *reply, const bksf_msg_t *msg) {
    // Always report rxp_off: the master uses it to resync when a reply is lost.
    // On ACK the slave has advanced, so value becomes off+n; on NACK it is the
    // current position, so the master can tell "already applied" from "retry".
    reply->value = rxp_off;

    if (!bksf_rxp_accept(msg->value, msg->len, msg->data, msg->crc, BKSF_MSG_DATA_SIZE)) {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_ERR;
        return;
    }

    reply->cmd   = BKSF_CMD_ACK;
    reply->value = rxp_off;
}

// T7.1: receive one bulk frame and reply with an ack. The frame is delivered by
// a single split transaction on the module's own id, so `in_len` is exactly the
// frame size; the control plane keeps `in_len == sizeof(bksf_msg_t)`.
static void bksf_bulk_handle(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    bksf_bulk_ack_t ack = {.magic = BKSF_BULK_ACK_MAGIC, .status = BKSF_ST_OK, .rxp_off = rxp_off};

    if (in_len < BKSF_BULK_FRAME_SIZE) {
        ack.status = BKSF_ST_ERR;
    } else {
        const bksf_bulk_frame_t *frame = (const bksf_bulk_frame_t *)in_data;
        if (frame->magic != BKSF_BULK_MAGIC) {
            ack.status = BKSF_ST_BADCMD;
        } else if (!bksf_rxp_accept(frame->offset, (uint8_t)frame->len, frame->data, frame->crc, BKSF_BULK_CHUNK)) {
            ack.status = BKSF_ST_ERR;
        }
        ack.rxp_off = rxp_off;
    }

    if (out_data != NULL && out_len >= sizeof(ack)) {
        memcpy(out_data, &ack, sizeof(ack));
    }
}

static void bksf_rxp_verify(bksf_msg_t *reply, const bksf_msg_t *msg) {
    if (!rxp_active || rxp_erasing || rxp_pending || rxp_off != rxp_len || rxp_flushed != rxp_len) {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_BUSY;
        return;
    }
    uint32_t got = bksf_crc32_final(rxp_crc_run);
    if (got == msg->value) {
        reply->cmd = BKSF_CMD_VERIFIED;
    } else {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_ERR;
        reply->value  = got;
    }
}

static void bksf_rpc_handle(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_data == NULL || in_len < sizeof(bksf_msg_t)) {
        return;
    }

    // T7.1: the bulk data plane shares this transaction id but not the message
    // format. A bulk frame is exactly BKSF_BULK_FRAME_SIZE long; control-plane
    // messages are exactly sizeof(bksf_msg_t). Dispatch on the length.
    if (in_len == BKSF_BULK_FRAME_SIZE) {
        bksf_bulk_handle(in_len, in_data, out_len, out_data);
        return;
    }

    bksf_msg_t msg = {0};
    memcpy(&msg, in_data, sizeof(msg));

    bksf_msg_t reply = {.version = BKSF_PROTO_VERSION, .cmd = msg.cmd, .status = BKSF_ST_OK};

    if (msg.version != BKSF_PROTO_VERSION) {
        reply.status = BKSF_ST_BADVER;
    } else {
        switch (msg.cmd) {
            case BKSF_CMD_ARM:
                reply.cmd    = BKSF_CMD_ARMED;
                reply.status = bksf_arm() ? BKSF_ST_OK : BKSF_ST_ERR;
                reply.value  = bksf_boot2_crc_stored();
                bksf_set_state(reply.status == BKSF_ST_OK ? BKSF_STATE_ARMED : BKSF_STATE_ERROR);
                break;
            case BKSF_CMD_BEGIN:
                bksf_rxp_begin(&reply, &msg);
                if (reply.status == BKSF_ST_OK) {
                    bksf_set_state(BKSF_STATE_COPYING);
                }
                break;
            case BKSF_CMD_CHUNK:
                bksf_rxp_chunk(&reply, &msg);
                break;
            case BKSF_CMD_VERIFY:
                bksf_rxp_verify(&reply, &msg);
                break;
            case BKSF_CMD_SYNC:
                reply.cmd   = BKSF_CMD_ACK;
                reply.value = rxp_flushed;
                // data[0..3] = rxp_off    (RESYNC compares it to tell "already
                //                           applied" from "retry")
                // data[4..7] = rxp_erased (ERASING polls it: this reaches rxp_len
                //                           when the whole window is erased and the
                //                           slave is ready for chunks)
                memcpy(reply.data, &rxp_off, sizeof(rxp_off));
                memcpy(reply.data + sizeof(rxp_off), &rxp_erased, sizeof(rxp_erased));
                break;
            case BKSF_CMD_COMMIT:
                if (!rxp_active || rxp_erasing || rxp_pending || rxp_off != rxp_len || rxp_flushed != rxp_len) {
                    reply.cmd    = BKSF_CMD_NACK;
                    reply.status = BKSF_ST_BUSY;
                    break;
                }
                // Never returns: applies the staged image and resets into it.
                uprintf("bksf: slave commit %u bytes -> apply+reset\n", (unsigned)rxp_len);
                bksf_set_state(BKSF_STATE_COMMITTING);
                bksf_apply_and_reset((const uint8_t *)(uintptr_t)(BKSF_XIP_BASE + rxp_staging), rxp_len);
                break;
            case BKSF_CMD_ABORT:
                rxp_active  = false;
                rxp_pending = false;
                rxp_erasing = false;
                reply.cmd  = BKSF_CMD_DONE;
                bksf_set_state(BKSF_STATE_IDLE);
                break;
            default:
                reply.status = BKSF_ST_BADCMD;
                break;
        }
    }

    if (msg.cmd != BKSF_CMD_CHUNK) { // chunks are too numerous to log
        uprintf("bksf: slave rpc cmd=%u -> %u st=%u\n", (unsigned)msg.cmd, (unsigned)reply.cmd, (unsigned)reply.status);
    }

    if (out_data != NULL && out_len >= sizeof(reply)) {
        memcpy(out_data, &reply, sizeof(reply));
    }
}

void bksf_rpc_init(void) {
    transaction_register_rpc(RPC_ID_BK_SPLIT_FLASH_CMD, bksf_rpc_handle);
    // T7.1: size the module's transaction for the bulk data plane.
    bksf_bulk_init();
}

// ---------------------------------------------------------------------------
// Master side
// ---------------------------------------------------------------------------

#    define BKSF_RPC_TIMEOUT_MS 250
#    define BKSF_RPC_MAX_TRIES 3
#    if BKSF_LEGACY_FLUSH
#        define BKSF_CHUNKS_PER_TICK 4   // (legacy baseline) bulk frames per housekeeping pass
#        define BKSF_CHUNK_PACE_MS 2     // (legacy baseline) gap between passes
#        define BKSF_SECTOR_PACE_MS 60   // (legacy baseline) first poll after an erase+program flush
#    else
#        define BKSF_CHUNKS_PER_TICK 8   // bulk frames per housekeeping pass (keep the link busy)
#        define BKSF_CHUNK_PACE_MS 1     // small gap between passes so the slave's loop runs
// The erase was moved *out* of the streaming phase (BKSF_SYNC_ERASING), so a
// 4 KiB staging flush is now a program-only write (~11 ms typ) with no erase. The
// slave has a single 4 KiB scratch, so the master still serializes one sector at
// a time: it waits about one program before the first SYNC poll, then polls
// quickly. The slave is deaf (IRQs off) while programming, so an early poll costs
// one RPC timeout and is retried.
#        define BKSF_SECTOR_PACE_MS 20   // first flush poll: ~ one typical 4 KiB program
#    endif
#    define BKSF_FLUSH_RETRY_MS 10       // subsequent polls while the flush is still running
#    define BKSF_FLUSH_MAX_TRIES 30      // plenty of slack per sector
// Up-front erase phase. The slave erases one 4 KiB sector per housekeeping pass
// (a sector erase holds interrupts off ~45 ms), so the master polls slowly and
// tolerates the polls that race an erase (they time out and are retried).
#    define BKSF_ERASE_POLL_MS 50        // ERASING poll cadence
#    define BKSF_ERASE_MAX_TRIES 600     // slack for many sectors and aged (slow) erases
#    define BKSF_REJOIN_TIMEOUT_MS 15000 // EEPROM wipe (~3 s) + app copy (~1 s) + reboot
#    define BKSF_REJOIN_POLL_MS 100      // rejoin presence cadence (was a flat 500 ms)
#    define BKSF_PEER_PROBE_MS 1000     // peer-presence watchdog cadence
#    define BKSF_PEER_MISS_MAX 3        // consecutive misses before declaring it lost
#    define BKSF_RESYNC_BACKOFF_MS 60   // wait out a sector flush before probing rxp_off
#    define BKSF_CHUNK_ATTEMPT_MAX 30   // total attempts at one offset before giving up

typedef enum {
    BKSF_SYNC_IDLE = 0,
    BKSF_SYNC_ARM,
    BKSF_SYNC_BEGIN,
    BKSF_SYNC_ERASING,
    BKSF_SYNC_CHUNKS,
    BKSF_SYNC_RESYNC,
    BKSF_SYNC_FLUSH,
    BKSF_SYNC_VERIFY,
    BKSF_SYNC_COMMIT,
    BKSF_SYNC_REJOIN,
} bksf_sync_t;

static bksf_sync_t sync_state    = BKSF_SYNC_IDLE;
static uint16_t    sync_tries    = 0;
static uint32_t    sync_deadline = 0;
static bksf_msg_t  last_reply;

static const uint8_t *up_img;
static uint32_t       up_len;
static uint32_t       up_off;
static uint32_t       up_crc;
static uint32_t       up_flush_target;
static uint32_t       up_rejoin_deadline;
static uint32_t       up_fail_off; // offset of the chunk whose reply was lost
static uint8_t        up_fail_len; // its length, so RESYNC knows the target rxp_off
static uint8_t        up_fail_tries; // attempts spent on the current offset
static bool           up_self_flash; // T5.1: self-flash the master after REJOIN

static bool bksf_rpc_send(bksf_cmd_t cmd, uint32_t value, const uint8_t *data, uint8_t len) {
    bksf_msg_t out = {.version = BKSF_PROTO_VERSION, .cmd = (uint8_t)cmd, .len = len, .value = value};
    if (data != NULL && len > 0) {
        memcpy(out.data, data, len);
    }

    bksf_msg_t in = {0};
    bool       ok = transaction_rpc_exec(RPC_ID_BK_SPLIT_FLASH_CMD, sizeof(out), &out, sizeof(in), &in);
    last_reply    = in;

    if (cmd != BKSF_CMD_CHUNK) { // chunks are too numerous to log
        uprintf("bksf: master rpc cmd=%u ok=%u reply=%u st=%u v=0x%08X\n", (unsigned)cmd, (unsigned)ok, (unsigned)in.cmd, (unsigned)in.status, (unsigned)in.value);
    }
    return ok;
}

// T7.1: image chunks now travel over the dedicated bulk frame
// (bksf_bulk_send_chunk in bk_split_flash_pack.c), not the control RPC. The
// semantics are unchanged: a corrupted payload is caught by the receiver's CRC
// check (it NACKs without buffering), and the reply can be lost or arrive
// zeroed when it races the slave's sector flush (the flush runs with interrupts
// off and the transport's crc8 guard silently drops a garbled RPC). So a non-ACK
// is *not* fatal: the caller resyncs against the receiver's rxp_off (always
// reported), which distinguishes "already applied" from "retry" and keeps a
// corrupt link from ever reaching COMMIT.

// Probe the slave's receive position (SYNC also reports rxp_off in data[0..3]).
static bool bksf_rpc_status(uint32_t *off, uint32_t *flushed) {
    if (!bksf_rpc_send(BKSF_CMD_SYNC, 0, NULL, 0) || last_reply.cmd != BKSF_CMD_ACK) {
        return false;
    }
    if (off != NULL) {
        memcpy(off, last_reply.data, sizeof(*off));
    }
    if (flushed != NULL) {
        *flushed = last_reply.value;
    }
    return true;
}

static void bksf_abort(const char *why) {
    uprintf("bksf: %s -> ABORT\n", why);
    // Detection aid: a dead link usually means the slave is in RPI-RP2 (e.g.
    // after a power-cut test) or absent. Say so, because the fix is a reflash.
    if (!is_transport_connected()) {
        uprintf("bksf: split peer not responding; if that half is in RPI-RP2, reflash it\n");
    }
    bksf_rpc_send(BKSF_CMD_ABORT, 0, NULL, 0);
    bksf_bulk_resume();
    bksf_set_state(BKSF_STATE_ERROR);
    sync_state = BKSF_SYNC_IDLE;
}

// Peer-presence watchdog (master, idle only). Purely informational: it surfaces a
// lost slave (powered off, or sitting in RPI-RP2 after a power-cut test) so the
// user is told to reflash, rather than being left with a silently dead link. It
// never blocks the keyboard and never touches the transport beyond the cheap
// connection check. A few consecutive misses are required to debounce.
static bool     peer_present    = true;
static uint8_t  peer_miss_count = 0;
static uint32_t peer_probe_next = 0;

static void bksf_peer_watchdog(void) {
    if (sync_state != BKSF_SYNC_IDLE) {
        return; // a test is driving the link; its own timeouts report failures
    }
    if ((int32_t)(timer_read32() - peer_probe_next) < 0) {
        return;
    }
    peer_probe_next = timer_read32() + BKSF_PEER_PROBE_MS;

    if (is_transport_connected()) {
        peer_miss_count = 0;
        if (!peer_present) {
            uprintf("bksf: split peer back\n");
            peer_present = true;
        }
    } else if (peer_present && ++peer_miss_count >= BKSF_PEER_MISS_MAX) {
        peer_miss_count = 0;
        peer_present    = false;
        uprintf("bksf: split peer lost (slave off or in RPI-RP2) - reflash that half to restore\n");
    }
}

void bksf_start_handshake(void) {
    if (!is_keyboard_master()) {
        uprintf("bksf: handshake requested on slave (ignored)\n");
        return;
    }
    uprintf("bksf: handshake start\n");
    sync_state    = BKSF_SYNC_ARM;
    sync_tries    = 0;
    sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
}

// Common setup for a peer-image update. Returns true if the run has started.
static bool bksf_begin_update(void) {
    if (!is_keyboard_master()) {
        uprintf("bksf: update requested on slave (ignored)\n");
        return false;
    }
    const uint8_t *img = bksf_peer_image_ptr();
    uint32_t       len = bksf_peer_image_size();
    if (len == 0) {
        uprintf("bksf: no peer image compiled in\n");
        return false;
    }

    // T9.1: version/CRC guard. Validate the image against its descriptor and the
    // running build's expectations *before* anything is armed or staged; a bad
    // or incompatible image is refused here, so the slave is never armed for it.
    char why[48];
    if (!bksf_validate_image(img, len, bksf_peer_image_hdr_ptr(), why, sizeof(why))) {
        uprintf("bksf: peer image rejected: %s -> not armed\n", why);
        bksf_set_state(BKSF_STATE_ERROR);
        return false;
    }

    up_img = img;
    up_len = len;
    up_off = 0;
    up_crc = bksf_crc32(img, len);

    uprintf("bksf: slave update start len=%u crc=0x%08X\n", (unsigned)up_len, (unsigned)up_crc);
    bksf_set_state(BKSF_STATE_PREPARE);
    sync_state    = BKSF_SYNC_ARM;
    sync_tries    = 0;
    sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
    return true;
}

void bksf_start_slave_update(void) {
    up_self_flash = false;
    (void)bksf_begin_update();
}

// T5.1: same as bksf_start_slave_update(), but the master self-flashes the peer
// image last (after the slave has rejoined), so one trigger updates both halves.
void bksf_start_both_update(void) {
    up_self_flash = false;
    if (bksf_begin_update()) {
        up_self_flash = true;
        uprintf("bksf: both-halves update (master self-flashes last)\n");
    }
}

void housekeeping_task_bk_split_flash(void) {
    housekeeping_task_bk_split_flash_kb();

    // T9.2: age out the transient ERROR indicator state.
    bksf_tick_state();

    // Slave side. Two phases, both driven from housekeeping (never inside the RPC
    // callback: an erase/program takes tens of ms and would blow the reply
    // timeout):
    //   1. ERASE  -- after BEGIN, erase the whole staging window one sector per
    //      pass, reporting progress in rxp_erased (the master's ERASING state
    //      reads it via SYNC). Interrupts are off only for the sector being
    //      erased, so the slave stays answerable between sectors.
    //   2. STREAM -- once erased, buffer incoming chunks; on a sector boundary
    //      flush the buffered sector with a program-only write (the erase is
    //      already done). The short hold-off lets the master read the chunk's
    //      reply first, so the flush cannot make the transport drop it.
    if (!is_keyboard_master() && rxp_active) {
        if (rxp_erasing) {
            if (rxp_erased < rxp_len && bksf_staging_erase_sector(rxp_staging + rxp_erased)) {
                rxp_erased += BKSF_FLASH_SECTOR_SIZE;
                if (rxp_erased >= rxp_len) {
                    rxp_erasing = false;
                    uprintf("bksf: slave erase done (%u bytes)\n", (unsigned)rxp_erased);
                }
            }
        } else if (rxp_pending && (int32_t)(timer_read32() - rxp_flush_not_before) >= 0) {
            rxp_pending = false;
#if BKSF_LEGACY_FLUSH
            bool ok = bksf_staging_flush_sector(rxp_pending_off);
#else
            bool ok = bksf_staging_program_sector(rxp_pending_off);
#endif
            if (ok) {
                rxp_flushed += BKSF_FLASH_SECTOR_SIZE;
            }
        }
    }

    if (sync_state == BKSF_SYNC_IDLE) {
        if (is_keyboard_master() && !bksf_bulk_is_quiesced()) {
            bksf_peer_watchdog();
        }
        return;
    }
    if (!is_keyboard_master()) {
        sync_state = BKSF_SYNC_IDLE;
        return;
    }
    if ((int32_t)(timer_read32() - sync_deadline) < 0) {
        return;
    }

    switch (sync_state) {
        case BKSF_SYNC_ARM:
            if (sync_tries >= BKSF_RPC_MAX_TRIES) {
                bksf_abort("no ARMED");
                return;
            }
            sync_tries++;
            if (bksf_rpc_send(BKSF_CMD_ARM, 0, NULL, 0) && last_reply.cmd == BKSF_CMD_ARMED && last_reply.status == BKSF_ST_OK) {
                uprintf("bksf: slave ARMED (crc=0x%08X)\n", (unsigned)last_reply.value);
                bksf_set_state(BKSF_STATE_ARMED);
                sync_state    = BKSF_SYNC_BEGIN;
                sync_tries    = 0;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;

        case BKSF_SYNC_BEGIN: {
            if (sync_tries >= BKSF_RPC_MAX_TRIES) {
                bksf_abort("no BEGIN ack");
                return;
            }
            sync_tries++;
            uint8_t crcbuf[4];
            memcpy(crcbuf, &up_crc, sizeof(crcbuf));
            if (bksf_rpc_send(BKSF_CMD_BEGIN, up_len, crcbuf, sizeof(crcbuf)) && last_reply.cmd == BKSF_CMD_ACK) {
                uprintf("bksf: slave BEGIN ack (staging=0x%05X)\n", (unsigned)last_reply.value);
                // T7.2: the bulk data phase starts now; quiesce normal module
                // split activity until the last sector is durably written.
                bksf_bulk_quiesce();
                bksf_set_state(BKSF_STATE_COPYING);
#if BKSF_LEGACY_FLUSH
                sync_state    = BKSF_SYNC_CHUNKS;
                up_off        = 0;
                sync_deadline = timer_read32();
#else
                // The slave now erases the whole staging window (one sector per
                // housekeeping pass) before accepting any chunk; wait it out.
                sync_state    = BKSF_SYNC_ERASING;
                up_off        = 0;
                sync_tries    = 0;
                sync_deadline = timer_read32() + BKSF_ERASE_POLL_MS;
#endif
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;
        }

        case BKSF_SYNC_ERASING: {
            // Poll until the slave reports the whole window erased. A poll that
            // races a sector erase (interrupts off ~45 ms) times out and is
            // retried on the next deadline.
            if (sync_tries >= BKSF_ERASE_MAX_TRIES) {
                bksf_abort("erase timeout");
                return;
            }
            sync_tries++;
            if (bksf_rpc_send(BKSF_CMD_SYNC, 0, NULL, 0) && last_reply.cmd == BKSF_CMD_ACK) {
                uint32_t erased = 0;
                memcpy(&erased, last_reply.data + sizeof(uint32_t), sizeof(erased));
                if (erased >= up_len) {
                    uprintf("bksf: slave erased %u bytes -> stream\n", (unsigned)erased);
                    sync_state    = BKSF_SYNC_CHUNKS;
                    up_off        = 0;
                    sync_deadline = timer_read32();
                    return;
                }
            }
            sync_deadline = timer_read32() + BKSF_ERASE_POLL_MS;
            return;
        }

        case BKSF_SYNC_CHUNKS:
            for (int i = 0; i < BKSF_CHUNKS_PER_TICK && up_off < up_len; i++) {
                // Never let a chunk straddle a sector boundary: the slave flushes
                // on an exact boundary and buffers into a 4 KiB sector scratch.
                uint32_t in_sector = up_off & (BKSF_FLASH_SECTOR_SIZE - 1u);
                uint32_t n         = up_len - up_off;
                if (n > BKSF_BULK_CHUNK) {
                    n = BKSF_BULK_CHUNK;
                }
                if (n > BKSF_FLASH_SECTOR_SIZE - in_sector) {
                    n = BKSF_FLASH_SECTOR_SIZE - in_sector;
                }
                if (!bksf_bulk_send_chunk(up_off, up_img + up_off, (uint8_t)n, NULL)) {
                    // Reply lost or refused: don't hammer retries blind (a sector
                    // flush is about to make the slave deaf). Back off and resync.
                    // Count attempts at this offset so a persistently bad chunk
                    // (e.g. mode-2 corruption) gives up instead of looping forever.
                    if (up_fail_off != up_off) {
                        up_fail_off   = up_off;
                        up_fail_tries = 0;
                    }
                    up_fail_len = (uint8_t)n;
                    if (++up_fail_tries >= BKSF_CHUNK_ATTEMPT_MAX) {
                        uprintf("bksf: chunk off=%u refused %u times -> ABORT\n", (unsigned)up_off, (unsigned)up_fail_tries);
                        bksf_abort("chunk refused");
                        return;
                    }
                    sync_state    = BKSF_SYNC_RESYNC;
                    sync_deadline = timer_read32() + BKSF_RESYNC_BACKOFF_MS;
                    return;
                }
                up_fail_tries = 0;
                up_off += n;
                if ((up_off & (BKSF_FLASH_SECTOR_SIZE - 1u)) == 0u) {
                    // A sector is buffered on the slave; give it time to program
                    // it (the program-only flush is far longer than an RPC reply
                    // timeout) before confirming, then sending more.
                    up_flush_target = up_off;
                    sync_tries      = 0;
                    sync_state      = BKSF_SYNC_FLUSH;
                    sync_deadline   = timer_read32() + BKSF_SECTOR_PACE_MS;
                    return;
                }
            }
            sync_deadline = timer_read32() + BKSF_CHUNK_PACE_MS;
            return;

        case BKSF_SYNC_RESYNC: {
            // A chunk reply was lost or refused. Ask the slave where it is: a
            // lost ACK leaves the slave at off+n (chunk applied) while a real
            // NACK leaves it at off (retry). Back off first so a sector flush is
            // not still holding the transport. Attempts are counted on
            // up_fail_tries (shared with CHUNKS) so retries cannot loop forever.
            uint32_t soff = 0;
            if (!bksf_rpc_status(&soff, NULL)) {
                if (++up_fail_tries >= BKSF_CHUNK_ATTEMPT_MAX) {
                    uprintf("bksf: resync gave up at off=%u after %u attempts -> ABORT\n", (unsigned)up_fail_off, (unsigned)up_fail_tries);
                    bksf_abort("resync failed");
                    return;
                }
                sync_deadline = timer_read32() + BKSF_RESYNC_BACKOFF_MS;
                return;
            }
            if (soff == up_fail_off + up_fail_len) {
                uprintf("bksf: resync: off=%u already applied (slave off=%u)\n", (unsigned)up_fail_off, (unsigned)soff);
                up_off        = soff;
                up_fail_tries = 0;
                if ((up_off & (BKSF_FLASH_SECTOR_SIZE - 1u)) == 0u) {
                    up_flush_target = up_off;
                    sync_tries      = 0;
                    sync_state      = BKSF_SYNC_FLUSH;
                    sync_deadline   = timer_read32();
                } else {
                    sync_state    = BKSF_SYNC_CHUNKS;
                    sync_deadline = timer_read32();
                }
            } else if (soff == up_fail_off) {
                uprintf("bksf: resync: retry off=%u (attempt %u)\n", (unsigned)up_fail_off, (unsigned)up_fail_tries);
                sync_state    = BKSF_SYNC_CHUNKS;
                sync_deadline = timer_read32() + BKSF_CHUNK_PACE_MS;
            } else {
                uprintf("bksf: resync: slave off=%u not %u nor %u -> ABORT\n", (unsigned)soff, (unsigned)up_fail_off, (unsigned)(up_fail_off + up_fail_len));
                bksf_abort("desync");
            }
            return;
        }

        case BKSF_SYNC_FLUSH:
            // Confirm the slave has durably written the sector before sending more.
            if (sync_tries >= BKSF_FLUSH_MAX_TRIES) {
                bksf_abort("flush timeout");
                return;
            }
            sync_tries++;
            if (bksf_rpc_send(BKSF_CMD_SYNC, 0, NULL, 0) && last_reply.cmd == BKSF_CMD_ACK && last_reply.value >= up_flush_target) {
                if (up_off >= up_len) {
                    uprintf("bksf: sent %u bytes\n", (unsigned)up_off);
                    // T7.2: the data phase is over; resume normal module traffic.
                    bksf_bulk_resume();
                    sync_state    = BKSF_SYNC_VERIFY;
                    sync_tries    = 0;
                    sync_deadline = timer_read32();
                    return;
                }
                sync_state    = BKSF_SYNC_CHUNKS;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_FLUSH_RETRY_MS;
            return;

        case BKSF_SYNC_VERIFY:
            if (sync_tries >= BKSF_RPC_MAX_TRIES) {
                bksf_abort("verify failed");
                return;
            }
            sync_tries++;
            if (bksf_rpc_send(BKSF_CMD_VERIFY, up_crc, NULL, 0) && last_reply.cmd == BKSF_CMD_VERIFIED) {
                uprintf("bksf: slave VERIFIED\n");
                sync_state    = BKSF_SYNC_COMMIT;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;

        case BKSF_SYNC_COMMIT:
            uprintf("bksf: slave COMMIT (slave will reboot)\n");
            bksf_rpc_send(BKSF_CMD_COMMIT, 0, NULL, 0);
            bksf_set_state(BKSF_STATE_COMMITTING);
            sync_state         = BKSF_SYNC_REJOIN;
            up_rejoin_deadline = timer_read32() + BKSF_REJOIN_TIMEOUT_MS;
            sync_deadline      = timer_read32() + BKSF_REJOIN_POLL_MS;
            return;

        case BKSF_SYNC_REJOIN:
            if (bksf_rpc_send(BKSF_CMD_NONE, 0, NULL, 0)) {
                uprintf("bksf: slave rejoined -> DONE\n");
                sync_state = BKSF_SYNC_IDLE;
                bksf_set_state(BKSF_STATE_IDLE);
                // T5.1: with the slave done and back, update the master last so
                // both halves end on the same build. Runs from here (the master's
                // main loop) and never returns once the apply begins; if it fails
                // early it only logs, leaving the master usable.
                if (up_self_flash) {
                    up_self_flash = false;
                    uprintf("bksf: master self-flash last\n");
                    bksf_self_update_from_peer_hold(BKSF_SELF_HOLD_MS);
                    uprintf("bksf: master self-flash did not run\n");
                }
                return;
            }
            if ((int32_t)(timer_read32() - up_rejoin_deadline) >= 0) {
                uprintf("bksf: slave did not rejoin within timeout\n");
                sync_state = BKSF_SYNC_IDLE;
                return;
            }
            sync_deadline = timer_read32() + BKSF_REJOIN_POLL_MS;
            return;

        default:
            sync_state = BKSF_SYNC_IDLE;
            return;
    }
}

#else // !SPLIT_TRANSACTION_RPC

void bksf_rpc_init(void) {}
void bksf_start_handshake(void) {}
void bksf_start_slave_update(void) {}
void bksf_start_both_update(void) {}
void housekeeping_task_bk_split_flash(void) {}

#endif // SPLIT_TRANSACTION_RPC
