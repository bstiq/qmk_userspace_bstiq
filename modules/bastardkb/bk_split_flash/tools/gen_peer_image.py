#!/usr/bin/env python3
# Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the bk_split_flash peer image blob for the self-update tests.

The module weakly defines `bksf_peer_image[]` / `bksf_peer_image_len` /
`bksf_peer_image_hdr`; this script writes a file with strong definitions so the
update can distribute a *distinct* build. Build the peer firmware, then:

    qmk compile -kb <keyboard> -km <keymap>          # produces <name>.uf2
    python3 tools/gen_peer_image.py <name>.uf2       # writes bk_split_flash_peer_image.c
    qmk compile -kb <keyboard> -km <keymap>          # this build carries the blob

Then flash it and run the BKSF_TEST_UPDATE / UPDATE_SLAVE / UPDATE_BOTH test.

Three outputs are written next to each other:
  * `bk_split_flash_peer_image.c` -- the `const uint8_t[]` embedded in the build
    (for the T2.5 self-flash and the T4-T7 slave stream) plus the T9.1 image
    descriptor (`bksf_peer_image_hdr`) the master validates before ARM.
  * `bk_split_flash_peer_image.bin` -- the exact same image as a raw sidecar. The
    T8.2 post-build step packages this sidecar into a combined UF2, so the peer
    dragged at the staging address is byte-identical to the embedded one.

The descriptor records the protocol version of the peer's own code (read from
bk_split_flash_proto.h unless --proto-version overrides it). A peer built from a
different protocol version is refused by the master before it arms anything --
pass --proto-version to reproduce that rejection.

Accepts a `.uf2` (RP2040 absolute addrs, 0x10000000-based) or a raw `.bin`
(flash image starting at offset 0). The image is padded to a 4096-byte sector.
"""

import argparse
import os
import re
import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGICE = 0x0AB16F30
XIP_BASE = 0x10000000
SECTOR = 4096

# Image descriptor constants -- must match bk_split_flash_image.h.
IMAGE_MAGIC = 0xB5F71A6E
IMAGE_HDR_VERSION = 1


def load_uf2(path):
    data = open(path, "rb").read()
    if len(data) % 512 != 0:
        sys.exit(f"{path}: not a UF2 (size not a multiple of 512)")
    blocks = {}
    for i in range(len(data) // 512):
        blk = data[i * 512 : (i + 1) * 512]
        m0, m1, _flags, addr, size, _no, _tot, _fam = struct.unpack("<8I", blk[:32])
        me = struct.unpack("<I", blk[508:512])[0]
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or me != UF2_MAGICE:
            continue
        base = addr - XIP_BASE
        if base < 0:
            sys.exit(f"{path}: block target 0x{addr:08X} is below XIP base")
        blocks[base] = blk[32 : 32 + size]
    if not blocks:
        sys.exit(f"{path}: no valid UF2 payload blocks")
    lo = min(blocks)
    hi = max(k + len(v) for k, v in blocks.items())
    img = bytearray(b"\x00" * (hi - lo))
    for k, v in blocks.items():
        img[k - lo : k - lo + len(v)] = v
    if lo != 0:
        sys.exit(f"{path}: image starts at flash offset 0x{lo:X}, expected 0")
    return bytes(img)


def load_bin(path):
    return open(path, "rb").read()


def _bitrev8(b):
    b = ((b >> 4) | (b << 4)) & 0xFF
    b = (((b & 0xCC) >> 2) | ((b & 0x33) << 2)) & 0xFF
    b = (((b & 0xAA) >> 1) | ((b & 0x55) << 1)) & 0xFF
    return b


def _bitrev32(x):
    x = ((x & 0x55555555) << 1 | (x >> 1) & 0x55555555) & 0xFFFFFFFF
    x = ((x & 0x33333333) << 2 | (x >> 2) & 0x33333333) & 0xFFFFFFFF
    x = ((x & 0x0F0F0F0F) << 4 | (x >> 4) & 0x0F0F0F0F) & 0xFFFFFFFF
    x = ((x & 0x00FF00FF) << 8 | (x >> 8) & 0x00FF00FF) & 0xFFFFFFFF
    x = ((x << 16) | (x >> 16)) & 0xFFFFFFFF
    return x


def bksf_crc32(data):
    """The firmware's integrity hash (bksf_crc32): a bit-reversed CRC32 with the
    RP2040 boot2 seed, finalised by a bit-reversal. Mirrors bk_split_flash_flash.c."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= _bitrev8(byte)
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if (crc & 1) else 0)
        crc &= 0xFFFFFFFF
    return _bitrev32(crc)


def read_proto_version(module_dir):
    path = os.path.join(module_dir, "bk_split_flash_proto.h")
    try:
        text = open(path).read()
    except OSError as exc:
        sys.exit(f"{path}: {exc}")
    m = re.search(r"#define\s+BKSF_PROTO_VERSION\s+(\d+)", text)
    if not m:
        sys.exit(f"{path}: BKSF_PROTO_VERSION not found")
    return int(m.group(1))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    module_dir = os.path.dirname(here)
    default_out = os.path.join(module_dir, "bk_split_flash_peer_image.c")

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="peer .uf2 or raw .bin")
    ap.add_argument("-o", "--out", default=default_out, help="output .c path")
    ap.add_argument(
        "--proto-version",
        type=int,
        default=None,
        help="override the descriptor's protocol version (default: parsed from bk_split_flash_proto.h)",
    )
    args = ap.parse_args()

    if args.input.lower().endswith(".uf2"):
        img = load_uf2(args.input)
    else:
        img = load_bin(args.input)

    if (len(img) & (SECTOR - 1)) != 0:
        img = img + b"\x00" * (SECTOR - (len(img) & (SECTOR - 1)))

    proto_version = args.proto_version if args.proto_version is not None else read_proto_version(module_dir)

    # Sanity: the image's own boot2 checksum at offset 0xFC must match our CRC
    # port (a valid RP2040 firmware image). Warn only -- an armed/garbage image
    # would legitimately differ, and this is a check on our tooling, not the peer.
    if len(img) >= 0x100:
        stored = int.from_bytes(img[0xFC:0x100], "little")
        if stored != bksf_crc32(img[:0xFC]):
            print(
                f"warning: boot2 CRC at 0xFC (0x{stored:08X}) != computed "
                f"(0x{bksf_crc32(img[:0xFC]):08X}); image is not a normal firmware image?",
                file=sys.stderr,
            )

    image_crc = bksf_crc32(img)

    lines = []
    for i in range(0, len(img), 16):
        chunk = img[i : i + 16]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    body = "\n".join(lines)

    with open(args.out, "w") as f:
        f.write(
            "// Generated by tools/gen_peer_image.py from %s -- do not edit.\n"
            "// SPDX-License-Identifier: GPL-2.0-or-later\n\n"
            "#include <stdint.h>\n"
            '#include "bk_split_flash_image.h"\n\n'
            "const uint32_t bksf_peer_image_len = %du;\n\n"
            "const uint8_t bksf_peer_image[] = {\n%s\n};\n\n"
            "// T9.1 image descriptor: the master validates this before ARM.\n"
            "const bksf_image_hdr_t bksf_peer_image_hdr = {\n"
            "    .magic = 0x%08Xu,\n"
            "    .hdr_version = %d,\n"
            "    .proto_version = %d,\n"
            "    .len = %du,\n"
            "    .crc = 0x%08Xu,\n"
            "};\n"
            % (
                os.path.basename(args.input),
                len(img),
                body,
                IMAGE_MAGIC,
                IMAGE_HDR_VERSION,
                proto_version,
                len(img),
                image_crc,
            )
        )

    # Raw sidecar: the exact same bytes as the embedded blob, for the T8.2
    # combined-UF2 post-build step (so the packaged peer == the embedded peer).
    out_bin = os.path.splitext(args.out)[0] + ".bin"
    with open(out_bin, "wb") as f:
        f.write(img)

    print(f"wrote {args.out}: {len(img)} bytes ({len(img) // SECTOR} sectors)")
    print(f"wrote {out_bin}: {len(img)} bytes (raw sidecar for combined UF2)")
    print(f"descriptor: magic=0x{IMAGE_MAGIC:08X} hdr={IMAGE_HDR_VERSION} proto={proto_version} len={len(img)} crc=0x{image_crc:08X}")


if __name__ == "__main__":
    main()
