// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

// bk_split_flash -- master <-> slave control protocol (v4).
//
// Small, versioned 32-byte messages carried in the split RPC buffer. This is
// the control plane only; image bytes move in dedicated 240-byte bulk frames
// (bk_split_flash_pack.h) with no per-chunk acknowledgement.
//
// v4: the slave buffers the whole image in RAM, so the per-sector pacing,
// erase/flush polling and resync states are gone. BEGIN locks a RAM receive
// buffer; STREAM pushes every frame back-to-back; one VERIFY returns a
// per-sector bad-bitmap; the master re-streams only the flagged sectors;
// COMMIT applies directly from RAM. Both halves run the same build -- the
// image descriptor (bk_split_flash_image.h) stamps this version, so a peer
// built from v3 code is refused before anything is armed.
#define BKSF_PROTO_VERSION 4

// Payload bytes in one message. Sized so `bksf_msg_t` is exactly 32 bytes (the
// split RPC buffer): version+cmd+status+len (4) + value (4) + crc (4) +
// data (20). The `crc` field carries the VERIFY whole-image CRC.
#define BKSF_MSG_DATA_SIZE 20

typedef enum {
    BKSF_CMD_NONE = 0,
    BKSF_CMD_ARM,      // master -> slave: arm (invalidate boot2), reply ARMED
    BKSF_CMD_ARMED,    // slave -> master
    BKSF_CMD_BEGIN,    // master -> slave: value=image_len, data[0..3]=whole-image CRC
    BKSF_CMD_ACK,      // slave -> master: generic OK
    BKSF_CMD_NACK,     // slave -> master: refused/again
    BKSF_CMD_VERIFY,   // master -> slave: value=expected whole-image CRC
    BKSF_CMD_VERIFIED, // slave -> master: value=bad-sector count, crc=computed CRC,
                       //                  data[]=bad-sector bitmap, len=bitmap bytes
    BKSF_CMD_COMMIT,   // master -> slave: apply from RAM + reset
    BKSF_CMD_DONE,     // slave -> master (abort ack)
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
    uint32_t value;   // generic field (size, count, CRC, ...)
    uint32_t crc;     // BKSF_CMD_VERIFIED: computed whole-image CRC; else unused
    uint8_t  data[BKSF_MSG_DATA_SIZE];
} bksf_msg_t;

_Static_assert(sizeof(bksf_msg_t) == 32, "bksf_msg_t must be 32 bytes to fit the RPC buffer");
