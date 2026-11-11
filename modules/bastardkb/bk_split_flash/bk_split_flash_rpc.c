// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <string.h>

#include "print.h"
#include "transactions.h"

#include "bk_split_flash.h"
#include "bk_split_flash_flash.h"
#include "bk_split_flash_proto.h"

// Parts 3 & 4: the split control plane. Part 3 is the ARM/ARMED/ABORT
// handshake; Part 4 streams the image into the slave's staging region, verifies
// it, and commits (the slave then reboots into the new build).

#ifdef SPLIT_TRANSACTION_RPC

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
static bool     rxp_pending;     // a full sector is buffered, awaiting flush
static uint32_t rxp_pending_off; // staging offset of the buffered sector

static void bksf_rxp_begin(bksf_msg_t *reply, const bksf_msg_t *msg) {
    uint32_t staging = 0;
    uint32_t bytes   = 0;
    uint32_t size    = msg->value;
    uint32_t crc     = 0;
    memcpy(&crc, msg->data, sizeof(crc));

    rxp_active  = false;
    rxp_pending = false;
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
    rxp_active  = true;

    reply->cmd   = BKSF_CMD_ACK;
    reply->value = staging;
}

// Buffers a chunk only; the sector flush happens in housekeeping, never inside
// the RPC callback (an erase takes tens of ms and would blow the reply timeout).
static void bksf_rxp_chunk(bksf_msg_t *reply, const bksf_msg_t *msg) {
    uint32_t off = msg->value;
    uint8_t  n   = msg->len;

    if (!rxp_active || n == 0 || off != rxp_off || rxp_off + n > rxp_len || rxp_pending) {
        reply->cmd    = BKSF_CMD_NACK;
        reply->status = BKSF_ST_ERR;
        return;
    }

    uint8_t *buf       = bksf_scratch();
    uint32_t in_sector = off & (BKSF_FLASH_SECTOR_SIZE - 1u);
    memcpy(buf + in_sector, msg->data, n);
    rxp_crc_run = bksf_crc32_update(rxp_crc_run, msg->data, n);
    rxp_off += n;

    if (in_sector + n == BKSF_FLASH_SECTOR_SIZE) {
        rxp_pending     = true;
        rxp_pending_off = rxp_staging + (off - in_sector);
    }

    reply->cmd   = BKSF_CMD_ACK;
    reply->value = rxp_flushed;
}

static void bksf_rxp_verify(bksf_msg_t *reply, const bksf_msg_t *msg) {
    if (!rxp_active || rxp_pending || rxp_off != rxp_len || rxp_flushed != rxp_len) {
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
                break;
            case BKSF_CMD_BEGIN:
                bksf_rxp_begin(&reply, &msg);
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
                break;
            case BKSF_CMD_COMMIT:
                if (!rxp_active || rxp_pending || rxp_off != rxp_len || rxp_flushed != rxp_len) {
                    reply.cmd    = BKSF_CMD_NACK;
                    reply.status = BKSF_ST_BUSY;
                    break;
                }
                // Never returns: applies the staged image and resets into it.
                uprintf("bksf: slave commit %u bytes -> apply+reset\n", (unsigned)rxp_len);
                bksf_apply_and_reset((const uint8_t *)(uintptr_t)(BKSF_XIP_BASE + rxp_staging), rxp_len);
                break;
            case BKSF_CMD_ABORT:
                rxp_active = false;
                reply.cmd  = BKSF_CMD_DONE;
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
}

// ---------------------------------------------------------------------------
// Master side
// ---------------------------------------------------------------------------

#    define BKSF_RPC_TIMEOUT_MS 250
#    define BKSF_RPC_MAX_TRIES 3
#    define BKSF_CHUNKS_PER_TICK 4
#    define BKSF_CHUNK_PACE_MS 2         // give the slave's loop time between chunks
#    define BKSF_SECTOR_PACE_MS 150      // let the slave flush a sector (erase+program) before the next one
#    define BKSF_FLUSH_MAX_TRIES 30      // ~4.5 s of slack per sector
#    define BKSF_REJOIN_TIMEOUT_MS 15000 // EEPROM wipe (~3 s) + app copy (~1 s) + reboot

typedef enum {
    BKSF_SYNC_IDLE = 0,
    BKSF_SYNC_ARM,
    BKSF_SYNC_BEGIN,
    BKSF_SYNC_CHUNKS,
    BKSF_SYNC_FLUSH,
    BKSF_SYNC_VERIFY,
    BKSF_SYNC_COMMIT,
    BKSF_SYNC_REJOIN,
} bksf_sync_t;

static bksf_sync_t sync_state    = BKSF_SYNC_IDLE;
static uint8_t     sync_tries    = 0;
static uint32_t    sync_deadline = 0;
static bksf_msg_t  last_reply;

static const uint8_t *up_img;
static uint32_t       up_len;
static uint32_t       up_off;
static uint32_t       up_crc;
static uint32_t       up_flush_target;
static uint32_t       up_rejoin_deadline;

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

static void bksf_abort(const char *why) {
    uprintf("bksf: %s -> ABORT\n", why);
    bksf_rpc_send(BKSF_CMD_ABORT, 0, NULL, 0);
    sync_state = BKSF_SYNC_IDLE;
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

void bksf_start_slave_update(void) {
    if (!is_keyboard_master()) {
        uprintf("bksf: update requested on slave (ignored)\n");
        return;
    }
    const uint8_t *img = bksf_peer_image_ptr();
    uint32_t       len = bksf_peer_image_size();
    if (len == 0) {
        uprintf("bksf: no peer image compiled in\n");
        return;
    }

    up_img = img;
    up_len = len;
    up_off = 0;
    up_crc = bksf_crc32(img, len);

    uprintf("bksf: slave update start len=%u crc=0x%08X\n", (unsigned)up_len, (unsigned)up_crc);
    sync_state    = BKSF_SYNC_ARM;
    sync_tries    = 0;
    sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
}

void housekeeping_task_bk_split_flash(void) {
    housekeeping_task_bk_split_flash_kb();

    // Slave side: flush a buffered sector here, never inside the RPC callback
    // (an erase/program takes tens of ms and would blow the reply timeout).
    if (!is_keyboard_master() && rxp_active && rxp_pending) {
        rxp_pending = false;
        if (bksf_staging_flush_sector(rxp_pending_off)) {
            rxp_flushed += BKSF_FLASH_SECTOR_SIZE;
        }
    }

    if (sync_state == BKSF_SYNC_IDLE) {
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
                sync_state    = BKSF_SYNC_CHUNKS;
                up_off        = 0;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_RPC_TIMEOUT_MS;
            return;
        }

        case BKSF_SYNC_CHUNKS:
            for (int i = 0; i < BKSF_CHUNKS_PER_TICK && up_off < up_len; i++) {
                // Never let a chunk straddle a sector boundary: the slave flushes
                // on an exact boundary and buffers into a 4 KiB sector scratch.
                uint32_t in_sector = up_off & (BKSF_FLASH_SECTOR_SIZE - 1u);
                uint32_t n         = up_len - up_off;
                if (n > BKSF_CHUNK_DATA_SIZE) {
                    n = BKSF_CHUNK_DATA_SIZE;
                }
                if (n > BKSF_FLASH_SECTOR_SIZE - in_sector) {
                    n = BKSF_FLASH_SECTOR_SIZE - in_sector;
                }
                if (!bksf_rpc_send(BKSF_CMD_CHUNK, up_off, up_img + up_off, (uint8_t)n) || last_reply.cmd != BKSF_CMD_ACK) {
                    uprintf("bksf: chunk failed at off=%u (reply=%u st=%u)\n", (unsigned)up_off, (unsigned)last_reply.cmd, (unsigned)last_reply.status);
                    bksf_abort("chunk failed");
                    return;
                }
                up_off += n;
                if ((up_off & (BKSF_FLASH_SECTOR_SIZE - 1u)) == 0u) {
                    // A sector is buffered on the slave; give it time to flush
                    // (its erase+program is far longer than an RPC reply timeout).
                    up_flush_target = up_off;
                    sync_tries      = 0;
                    sync_state      = BKSF_SYNC_FLUSH;
                    sync_deadline   = timer_read32() + BKSF_SECTOR_PACE_MS;
                    return;
                }
            }
            sync_deadline = timer_read32() + BKSF_CHUNK_PACE_MS;
            return;

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
                    sync_state    = BKSF_SYNC_VERIFY;
                    sync_tries    = 0;
                    sync_deadline = timer_read32();
                    return;
                }
                sync_state    = BKSF_SYNC_CHUNKS;
                sync_deadline = timer_read32();
                return;
            }
            sync_deadline = timer_read32() + BKSF_SECTOR_PACE_MS;
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
            sync_state         = BKSF_SYNC_REJOIN;
            up_rejoin_deadline = timer_read32() + BKSF_REJOIN_TIMEOUT_MS;
            sync_deadline      = timer_read32() + 500;
            return;

        case BKSF_SYNC_REJOIN:
            if (bksf_rpc_send(BKSF_CMD_NONE, 0, NULL, 0)) {
                uprintf("bksf: slave rejoined -> DONE\n");
                sync_state = BKSF_SYNC_IDLE;
                return;
            }
            if ((int32_t)(timer_read32() - up_rejoin_deadline) >= 0) {
                uprintf("bksf: slave did not rejoin within timeout\n");
                sync_state = BKSF_SYNC_IDLE;
                return;
            }
            sync_deadline = timer_read32() + 500;
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
void housekeeping_task_bk_split_flash(void) {}

#endif // SPLIT_TRANSACTION_RPC
