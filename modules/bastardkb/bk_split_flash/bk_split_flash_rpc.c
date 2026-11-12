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
#include "bk_split_flash_rx.h"

// v4 split control plane. The slave buffers the whole image in RAM
// (bk_split_flash_rx.c); the master streams every frame fire-and-forget, does
// one final VERIFY (a per-sector bad-bitmap + whole-image CRC), re-streams only
// flagged sectors, and COMMITs an apply-from-RAM. There are no deaf IRQ-off
// windows, per-sector flush/erase phases, SYNC polling or resync states.
//
// Both halves run the same build: a mismatched peer is refused by the image
// descriptor before anything is armed (bk_split_flash_image.h / BKSF_PROTO_VERSION).

#ifdef SPLIT_TRANSACTION_RPC

// ---------------------------------------------------------------------------
// Slave side
// ---------------------------------------------------------------------------

// Lock the RAM receive buffer for the announced image.
static void bksf_rxp_begin(bksf_msg_t *reply, const bksf_msg_t *msg) {
    uint32_t crc      = 0;
    uint32_t capacity = 0;
    memcpy(&crc, msg->data, sizeof(crc));

    if (bksf_rx_begin(msg->value, crc, &capacity)) {
        reply->cmd   = BKSF_CMD_ACK;
        reply->value = capacity;
    } else {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_ERR;
    }
}

// One final integrity check: build the per-sector bad-bitmap from the slot
// masks and compute the whole-image CRC backstop. The master decides clean
// (commit) vs. re-stream (bitmap non-empty) vs. abort (CRC mismatch).
static void bksf_rxp_verify(bksf_msg_t *reply, const bksf_msg_t *msg) {
    (void)msg; // the expected CRC is already held from BEGIN
    if (!bksf_rx_active()) {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_BUSY;
        return;
    }
    uint8_t  bitmap[BKSF_BITMAP_BYTES] = {0};
    uint32_t got                       = 0;
    uint8_t  bad                       = bksf_rx_verify(bitmap, sizeof(bitmap), &got);

    reply->cmd   = BKSF_CMD_VERIFIED;
    reply->value = bad;
    reply->crc   = got;
    reply->len   = bksf_rx_bitmap_bytes();
    memcpy(reply->data, bitmap, reply->len);
}

// v4: fire-and-forget frame. There is NO reply (out_len == 0): the slave writes
// the frame straight into rx_buf and validates its CRC. A lost or corrupt frame
// simply leaves that sector's slot unset and is recovered by the VERIFY bitmap.
static void bksf_bulk_handle(uint8_t in_len, const void *in_data) {
    if (in_data == NULL || in_len < BKSF_BULK_FRAME_SIZE) {
        return;
    }
    const bksf_bulk_frame_t *frame = (const bksf_bulk_frame_t *)in_data;
    if (frame->magic != BKSF_BULK_MAGIC) {
        return;
    }
    (void)bksf_rx_accept(frame->offset, frame->len, frame->data, frame->crc);
}

static void bksf_rpc_handle(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    if (in_data == NULL || in_len < sizeof(bksf_msg_t)) {
        return;
    }

    // The bulk data plane shares this transaction id but not the message format.
    // A bulk frame is exactly BKSF_BULK_FRAME_SIZE long; control messages are
    // exactly sizeof(bksf_msg_t). Dispatch on the length.
    if (in_len == BKSF_BULK_FRAME_SIZE) {
        bksf_bulk_handle(in_len, in_data);
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
            case BKSF_CMD_VERIFY:
                bksf_rxp_verify(&reply, &msg);
                break;
            case BKSF_CMD_COMMIT: {
                // Final fail-safe gate: never apply a corrupt or incomplete
                // image. bksf_rx_clean() re-checks every slot AND the CRC.
                uint32_t got = 0;
                if (!bksf_rx_clean(&got)) {
                    reply.cmd    = BKSF_CMD_NACK;
                    reply.status = BKSF_ST_BUSY;
                    break;
                }
                uprintf("bksf: slave commit %u bytes (crc=0x%08X) -> apply+reset\n", (unsigned)bksf_rx_len(), (unsigned)got);
                bksf_set_state(BKSF_STATE_COMMITTING);
                // Apply directly from the RAM receive buffer. bksf_apply_and_reset
                // copies each sector into its own RAM bounce before erase/program,
                // so a RAM source is safe while XIP is off. Never returns.
                bksf_apply_and_reset(bksf_rx_data(), bksf_rx_len());
                break;
            }
            case BKSF_CMD_ABORT:
                bksf_rx_abort();
                reply.cmd = BKSF_CMD_DONE;
                bksf_set_state(BKSF_STATE_IDLE);
                break;
            default:
                reply.status = BKSF_ST_BADCMD;
                break;
        }
    }

    if (msg.cmd != BKSF_CMD_NONE) { // NONE is the cheap rejoin liveness probe
        uprintf("bksf: slave rpc cmd=%u -> %u st=%u\n", (unsigned)msg.cmd, (unsigned)reply.cmd, (unsigned)reply.status);
    }
    if (out_data != NULL && out_len >= sizeof(reply)) {
        memcpy(out_data, &reply, sizeof(reply));
    }
}

void bksf_rpc_init(void) {
    transaction_register_rpc(RPC_ID_BK_SPLIT_FLASH_CMD, bksf_rpc_handle);
    // Size the module's transaction for the fire-and-forget bulk data plane.
    bksf_bulk_init();
}

// ---------------------------------------------------------------------------
// Master side
// ---------------------------------------------------------------------------

#    define BKSF_RPC_TIMEOUT_MS 250
#    define BKSF_RPC_MAX_TRIES 3
#    define BKSF_CHUNKS_PER_TICK 8   // frames per housekeeping pass (keyboard stays responsive)
#    define BKSF_CHUNK_PACE_MS 1     // small gap between streaming passes
#    define BKSF_REJOIN_TIMEOUT_MS 15000 // EEPROM wipe (~3 s) + app copy (~1 s) + reboot
#    define BKSF_REJOIN_POLL_MS 25       // Phase 4: trimmed rejoin presence cadence
#    define BKSF_PEER_PROBE_MS 1000
#    define BKSF_PEER_MISS_MAX 3

typedef enum {
    BKSF_SYNC_IDLE = 0,
    BKSF_SYNC_ARM,
    BKSF_SYNC_BEGIN,
    BKSF_SYNC_STREAM,
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
static uint32_t       up_crc;
static uint32_t       up_sectors;
static uint32_t       up_total_frames; // up_sectors * BKSF_SLOTS_PER_SECTOR
static uint32_t       up_frame;        // current frame index within a streaming pass
static uint8_t        up_todo[BKSF_BITMAP_BYTES]; // sectors to (re-)stream this pass
static uint8_t        up_restream_count;
static uint32_t       up_rejoin_deadline;
static bool           up_self_flash;     // T5.1: also self-flash the master
static bool           up_handshake_only; // T3: ARM/ARMED/ABORT only, no image

static bool bksf_rpc_send(bksf_cmd_t cmd, uint32_t value, const uint8_t *data, uint8_t len) {
    bksf_msg_t out = {.version = BKSF_PROTO_VERSION, .cmd = (uint8_t)cmd, .len = len, .value = value};
    if (data != NULL && len > 0) {
        memcpy(out.data, data, len);
    }

    bksf_msg_t in = {0};
    bool       ok = transaction_rpc_exec(RPC_ID_BK_SPLIT_FLASH_CMD, sizeof(out), &out, sizeof(in), &in);
    last_reply    = in;

    if (cmd != BKSF_CMD_NONE) { // NONE is the cheap rejoin liveness probe
        uprintf("bksf: master rpc cmd=%u ok=%u reply=%u st=%u v=0x%08X\n", (unsigned)cmd, (unsigned)ok, (unsigned)in.cmd, (unsigned)in.status, (unsigned)in.value);
    }
    return ok;
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
    up_handshake_only = true;
    up_self_flash     = false;
    sync_state        = BKSF_SYNC_ARM;
    sync_tries        = 0;
    sync_deadline     = timer_read32() + BKSF_RPC_TIMEOUT_MS;
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

    // T9.1: version/CRC/RAM-fit guard. Validate the image against its descriptor
    // and the running build's expectations *before* anything is armed; a bad or
    // incompatible image is refused here, so the slave is never armed for it.
    char why[48];
    if (!bksf_validate_image(img, len, bksf_peer_image_hdr_ptr(), why, sizeof(why))) {
        uprintf("bksf: peer image rejected: %s -> not armed\n", why);
        bksf_set_state(BKSF_STATE_ERROR);
        return false;
    }

    up_img            = img;
    up_len            = len;
    up_crc            = bksf_crc32(img, len);
    up_sectors        = up_len / BKSF_FLASH_SECTOR_SIZE;
    up_total_frames   = up_sectors * BKSF_SLOTS_PER_SECTOR;
    up_frame          = 0;
    up_restream_count = 0;
    up_handshake_only = false;

    // First streaming pass covers every sector; a VERIFY with bad sectors
    // replaces this with just those sectors' bits for the next pass.
    memset(up_todo, 0xFF, sizeof(up_todo));
    for (uint32_t s = up_sectors; s < BKSF_MAX_SECTORS; s++) {
        up_todo[s >> 3] &= (uint8_t)~(1u << (s & 7u));
    }

    bksf_bulk_reset(); // reset link-integrity injection for this run
    uprintf("bksf: slave update start len=%u crc=0x%08X sectors=%u frames=%u\n", (unsigned)up_len, (unsigned)up_crc, (unsigned)up_sectors, (unsigned)up_total_frames);
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

// T5.1: like bksf_start_slave_update(), but the master also self-flashes the
// peer image. Phase 4: the master's apply is kicked off at COMMIT (overlapped
// with the slave's apply on the other MCU), not after REJOIN.
void bksf_start_both_update(void) {
    up_self_flash = false;
    if (bksf_begin_update()) {
        up_self_flash = true;
        uprintf("bksf: both-halves update (master self-flashes at COMMIT)\n");
    }
}

void housekeeping_task_bk_split_flash(void) {
    housekeeping_task_bk_split_flash_kb();

    // T9.2: age out the transient ERROR indicator state.
    bksf_tick_state();

    // v4: the slave does all receive work inside the RPC callback (RAM writes +
    // per-frame CRC). There is NO flash write, erase phase, or deaf window during
    // streaming, so the slave needs no housekeeping work at all here.

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
                if (up_handshake_only) {
                    // T3: ARM/ARMED/ABORT handshake test -- disarm and stop.
                    bksf_rpc_send(BKSF_CMD_ABORT, 0, NULL, 0);
                    sync_state = BKSF_SYNC_IDLE;
                    bksf_set_state(BKSF_STATE_IDLE);
                    return;
                }
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
            if (bksf_rpc_send(BKSF_CMD_BEGIN, up_len, crcbuf, sizeof(crcbuf)) && last_reply.cmd == BKSF_CMD_ACK && last_reply.status == BKSF_ST_OK) {
                uprintf("bksf: slave BEGIN ack (RAM capacity=%u)\n", (unsigned)last_reply.value);
                bksf_bulk_quiesce();
                bksf_set_state(BKSF_STATE_COPYING);
                up_frame      = 0;
                sync_state    = BKSF_SYNC_STREAM;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;
        }

        case BKSF_SYNC_STREAM: {
            // Push frames back-to-back (fire-and-forget). Lost/corrupt frames are
            // recovered by the final VERIFY bitmap, so no per-frame retry here.
            for (int i = 0; i < BKSF_CHUNKS_PER_TICK && up_frame < up_total_frames;) {
                uint32_t sector = up_frame / BKSF_SLOTS_PER_SECTOR;
                if ((up_todo[sector >> 3] & (1u << (sector & 7u))) == 0u) {
                    up_frame++; // skip frames of sectors not flagged this pass
                    continue;
                }
                uint32_t slot = up_frame % BKSF_SLOTS_PER_SECTOR;
                uint32_t off  = sector * BKSF_FLASH_SECTOR_SIZE + slot * BKSF_BULK_CHUNK;
                uint8_t  n    = (uint8_t)bksf_slot_len(slot);
                bksf_bulk_send_chunk(off, up_img + off, n);
                up_frame++;
                i++;
            }
            if (up_frame >= up_total_frames) {
                sync_state    = BKSF_SYNC_VERIFY;
                sync_tries    = 0;
                sync_deadline = timer_read32();
            } else {
                sync_deadline = timer_read32() + BKSF_CHUNK_PACE_MS;
            }
            return;
        }

        case BKSF_SYNC_VERIFY: {
            if (sync_tries >= BKSF_RPC_MAX_TRIES) {
                bksf_abort("verify failed");
                return;
            }
            sync_tries++;
            if (bksf_rpc_send(BKSF_CMD_VERIFY, up_crc, NULL, 0) && last_reply.cmd == BKSF_CMD_VERIFIED && last_reply.status == BKSF_ST_OK) {
                uint8_t  bad = (uint8_t)last_reply.value;
                uint32_t got = last_reply.crc;
                if (bad == 0u && got == up_crc) {
                    uprintf("bksf: VERIFY clean (crc=0x%08X)\n", (unsigned)got);
                    bksf_bulk_resume();
                    sync_state    = BKSF_SYNC_COMMIT;
                    sync_deadline = timer_read32();
                    return;
                }
                if (bad == 0u) {
                    // All slots set but the CRC disagrees: never commit.
                    bksf_abort("verify CRC mismatch");
                    return;
                }
                if (up_restream_count >= BKSF_RESTREAM_MAX) {
                    bksf_abort("re-stream exhausted");
                    return;
                }
                up_restream_count++;
                memset(up_todo, 0, sizeof(up_todo));
                uint8_t n = last_reply.len < sizeof(up_todo) ? last_reply.len : (uint8_t)sizeof(up_todo);
                memcpy(up_todo, last_reply.data, n);
                up_frame = 0;
                uprintf("bksf: VERIFY %u bad sector(s) -> re-stream pass %u\n", (unsigned)bad, (unsigned)up_restream_count);
                sync_state    = BKSF_SYNC_STREAM;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;
        }

        case BKSF_SYNC_COMMIT:
            uprintf("bksf: slave COMMIT (slave will reboot)\n");
            bksf_rpc_send(BKSF_CMD_COMMIT, 0, NULL, 0);
            bksf_set_state(BKSF_STATE_COMMITTING);
            if (up_self_flash) {
                // Phase 4: the master and slave are separate MCUs, so start the
                // master's own apply NOW instead of waiting for REJOIN -- the two
                // applies run concurrently. After its reboot the normal peer
                // watchdog confirms the slave rejoined. Never returns on success.
                up_self_flash = false;
                uprintf("bksf: master self-flash (overlapped with slave apply)\n");
                bksf_self_update_from_peer_hold(BKSF_SELF_HOLD_MS);
                uprintf("bksf: master self-flash did not run\n");
                sync_state = BKSF_SYNC_IDLE;
                return;
            }
            sync_state         = BKSF_SYNC_REJOIN;
            up_rejoin_deadline = timer_read32() + BKSF_REJOIN_TIMEOUT_MS;
            sync_deadline      = timer_read32() + BKSF_REJOIN_POLL_MS;
            return;

        case BKSF_SYNC_REJOIN:
            // Slave-only update: confirm the slave rebooted before going idle.
            if (bksf_rpc_send(BKSF_CMD_NONE, 0, NULL, 0)) {
                uprintf("bksf: slave rejoined -> DONE\n");
                sync_state = BKSF_SYNC_IDLE;
                bksf_set_state(BKSF_STATE_IDLE);
                return;
            }
            if ((int32_t)(timer_read32() - up_rejoin_deadline) >= 0) {
                uprintf("bksf: slave did not rejoin within timeout\n");
                sync_state = BKSF_SYNC_IDLE;
                bksf_set_state(BKSF_STATE_IDLE);
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
