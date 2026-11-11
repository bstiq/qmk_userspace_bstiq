// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

// bk_split_flash -- master <-> slave control protocol.
//
// Small, versioned messages carried in one split RPC transaction (the default
// 32-byte M2S/S2M buffers). This is the control plane only; image bytes move in
// later parts.

#define BKSF_PROTO_VERSION 2

// Payload bytes in one message. Sized so `bksf_msg_t` is exactly 32 bytes (the
// default split RPC buffer): version+cmd+status+len (4) + value (4) + crc (4) +
// data (20). The CRC field (added in v2 for T4.3) costs 4 payload bytes.
#define BKSF_MSG_DATA_SIZE 20

typedef enum {
    BKSF_CMD_NONE = 0,
    BKSF_CMD_ARM,      // master -> slave: arm (invalidate boot2), reply ARMED
    BKSF_CMD_ARMED,    // slave -> master
    BKSF_CMD_BEGIN,    // master -> slave: image size in `value`, CRC in `data`
    BKSF_CMD_CHUNK,    // master -> slave: offset in `value`, `len` bytes in `data`, CRC32 of those bytes in `crc`
    BKSF_CMD_ACK,      // slave -> master: ready / bytes received in `value`
    BKSF_CMD_NACK,     // slave -> master: refused/again
    BKSF_CMD_VERIFY,   // master -> slave: whole-image CRC in `value`
    BKSF_CMD_VERIFIED, // slave -> master
    BKSF_CMD_SYNC,     // master -> slave: report durable bytes in `value` (ACK)
    BKSF_CMD_COMMIT,   // master -> slave: apply + reset
    BKSF_CMD_DONE,     // slave -> master (or abort ack)
    BKSF_CMD_ABORT,    // either: stop, stay recoverable
} bksf_cmd_t;

#define BKSF_ST_OK 0
#define BKSF_ST_ERR 1
#define BKSF_ST_BADCMD 2
#define BKSF_ST_BADVER 3
#define BKSF_ST_BUSY 4

typedef struct __attribute__((packed)) {
    uint8_t  version; // BKSF_PROTO_VERSION
    uint8_t  cmd;     // bksf_cmd_t
    uint8_t  status;  // BKSF_ST_*
    uint8_t  len;     // bytes used in `data`
    uint32_t value;   // generic field (size, offset, CRC, ...)
    uint32_t crc;     // BKSF_CMD_CHUNK: CRC32 over `data[0..len-1]`; else unused
    uint8_t  data[BKSF_MSG_DATA_SIZE];
} bksf_msg_t;

_Static_assert(sizeof(bksf_msg_t) == 32, "bksf_msg_t must be 32 bytes to fit the RPC buffer");

// Image bytes carried by one BKSF_CMD_CHUNK message.
#define BKSF_CHUNK_DATA_SIZE BKSF_MSG_DATA_SIZE
