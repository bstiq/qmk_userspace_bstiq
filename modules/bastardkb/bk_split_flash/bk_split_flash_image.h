// Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// bk_split_flash -- T9.1 image descriptor (the "version/CRC header").
//
// Every distributable image carries a small descriptor describing what the
// master expects of it. Before a run is armed, the master validates the
// descriptor: a bad magic or descriptor version (wrong tool), a protocol
// version that differs from the running firmware (an image built from
// incompatible code -- the documented "stale peer garbles the link" failure), a
// length that does not match the image, a CRC that does not match the image
// bytes, or an image that does not fit staging all refuse the run *before* the
// slave is armed.
//
// For the const-blob path (Parts 2-8) the image and its descriptor are both
// compiled in, so `tools/gen_peer_image.py` emits the descriptor next to the
// blob; the format is the same one a future host upload (Part 6) would prefix
// to a staged image.

#define BKSF_IMAGE_MAGIC 0xB5F71A6Eu
#define BKSF_IMAGE_HDR_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;         // BKSF_IMAGE_MAGIC
    uint16_t hdr_version;   // BKSF_IMAGE_HDR_VERSION (descriptor format)
    uint16_t proto_version; // BKSF_PROTO_VERSION the image expects
    uint32_t len;           // image length in bytes (sector-padded, == blob size)
    uint32_t crc;           // bksf_crc32() over the image bytes
} bksf_image_hdr_t;

_Static_assert(sizeof(bksf_image_hdr_t) == 16, "bksf_image_hdr_t layout changed");
