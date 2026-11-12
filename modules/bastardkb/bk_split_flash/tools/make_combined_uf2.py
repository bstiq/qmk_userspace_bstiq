#!/usr/bin/env python3
# Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a combined UF2 for bk_split_flash (T8.2).

Emits one UF2 holding both halves' payloads, so a single BOOTSEL drag delivers:

  * the app image, at its normal XIP base (0x10000000), and
  * the peer image, at the staging address = __flash_binary_end rounded up to a
    4096-byte sector (the same address the running module computes at runtime via
    bksf_get_staging_window()).

The peer's load address is derived from the *built* app's `__flash_binary_end`
linker symbol, which is read straight from the ELF symbol table (no `nm` needed).
The peer bytes come from the raw sidecar that tools/gen_peer_image.py writes next
to the generated C blob, so the peer dragged at the staging address is
byte-identical to the one embedded in the app.

The app's UF2 block header fields (flags + family id) are preserved, so the file
stays a normal RP2040 UF2. The output path is chosen by the caller; the build
script passes `--out <TARGET>.uf2`, overwriting QMK's plain carrier copy with
the complete image so that exactly one UF2 remains for the target.

Usage:
    python3 make_combined_uf2.py \
        --app .build/<TARGET>.uf2 --elf .build/<TARGET>.elf \
        --peer bk_split_flash_peer_image.bin --out <TARGET>.uf2
"""

import argparse
import os
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK = 512
UF2_PAYLOAD = 256
XIP_BASE = 0x10000000
SECTOR = 4096


def parse_uf2(path):
    """Return a list of (addr, payload, flags, family) for a 512-byte-block UF2."""
    data = open(path, "rb").read()
    if len(data) == 0 or len(data) % UF2_BLOCK != 0:
        sys.exit(f"{path}: not a UF2 (size {len(data)} is not a multiple of {UF2_BLOCK})")
    out = []
    for i in range(len(data) // UF2_BLOCK):
        blk = data[i * UF2_BLOCK : (i + 1) * UF2_BLOCK]
        magic0, magic1, flags, addr, size, _no, _tot, family = struct.unpack_from("<8I", blk, 0)
        end_magic = struct.unpack_from("<I", blk, 508)[0]
        if magic0 != UF2_MAGIC_START0 or magic1 != UF2_MAGIC_START1 or end_magic != UF2_MAGIC_END:
            sys.exit(f"{path}: block {i} is not a valid UF2 block")
        if size == 0 or size > 476:
            sys.exit(f"{path}: block {i} has an invalid payload size ({size})")
        out.append((addr, blk[32 : 32 + size], flags, family))
    return out


def elf_symbol(path, name):
    """Read a symbol's value from a 32-bit little-endian ELF's .symtab."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        sys.exit(f"{path}: not an ELF file")
    if d[4] != 1 or d[5] != 1:
        sys.exit(f"{path}: expected a 32-bit little-endian ELF")
    e_shoff = struct.unpack_from("<I", d, 0x20)[0]
    e_shentsize, e_shnum, _e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    shdrs = [struct.unpack_from("<10I", d, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    for sh in shdrs:
        # (name, type, flags, addr, offset, size, link, info, addralign, entsize)
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = shdrs[sh[6]]
        str_off = strtab[4]
        sym_off, sym_size, sym_entsize = sh[4], sh[5], sh[9]
        if sym_entsize == 0:
            sym_entsize = 16
        for j in range(sym_size // sym_entsize):
            o = sym_off + j * sym_entsize
            st_name, st_value, _st_size = struct.unpack_from("<III", d, o)
            if st_name == 0:
                continue
            end = d.find(b"\x00", str_off + st_name)
            if d[str_off + st_name : end].decode(errors="replace") == name:
                return st_value
    sys.exit(f"{path}: symbol {name!r} not found in .symtab")


def encode_block(addr, payload, flags, family, blockno, numblocks):
    if len(payload) == 0 or len(payload) > 476:
        sys.exit(f"invalid payload size at 0x{addr:08X} ({len(payload)} bytes)")
    hd = struct.pack("<8I", UF2_MAGIC_START0, UF2_MAGIC_START1, flags, addr, len(payload), blockno, numblocks, family)
    block = hd + payload + b"\x00" * (UF2_BLOCK - 32 - len(payload) - 4) + struct.pack("<I", UF2_MAGIC_END)
    assert len(block) == UF2_BLOCK
    return block


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="app UF2 (from the normal build)")
    ap.add_argument("--elf", required=True, help="app ELF (source of __flash_binary_end)")
    ap.add_argument("--peer", required=True, help="raw peer image sidecar (.bin)")
    ap.add_argument("--out", required=True, help="combined UF2 to write")
    ap.add_argument("--base", default=hex(XIP_BASE), help="XIP base address (default 0x10000000)")
    ap.add_argument("--align", type=int, default=SECTOR, help="staging sector alignment (default 4096)")
    args = ap.parse_args()

    base = int(args.base, 0)

    app_blocks = parse_uf2(args.app)
    if not app_blocks:
        sys.exit(f"{args.app}: no payload blocks")
    flags = app_blocks[0][2]
    family = app_blocks[0][3]

    app_end = elf_symbol(args.elf, "__flash_binary_end")
    if app_end < base:
        sys.exit(f"__flash_binary_end (0x{app_end:08X}) is below the XIP base (0x{base:08X})")
    # Match bksf_get_staging_window(): end of app rounded up to a sector.
    staging_off = ((app_end - base + args.align - 1) // args.align) * args.align
    staging_addr = base + staging_off

    peer = open(args.peer, "rb").read()
    if not peer:
        sys.exit(f"{args.peer}: peer image is empty")
    if len(peer) % UF2_PAYLOAD != 0:
        peer += b"\x00" * (UF2_PAYLOAD - (len(peer) % UF2_PAYLOAD))

    peer_blocks = [(staging_addr + i, peer[i : i + UF2_PAYLOAD]) for i in range(0, len(peer), UF2_PAYLOAD)]

    # Guard: the peer must not overlap the app image.
    app_hi = max(addr + len(p) for addr, p, _f, _fam in app_blocks)
    if staging_addr < app_hi:
        sys.exit(f"staging 0x{staging_addr:08X} overlaps the app (ends 0x{app_hi:08X})")

    total = len(app_blocks) + len(peer_blocks)
    out = []
    for blockno, (addr, payload, _f, _fam) in enumerate(app_blocks):
        out.append(encode_block(addr, payload, flags, family, blockno, total))
    for k, (addr, payload) in enumerate(peer_blocks):
        out.append(encode_block(addr, payload, flags, family, len(app_blocks) + k, total))

    with open(args.out, "wb") as f:
        f.write(b"".join(out))

    print(f"combined UF2: {args.out}")
    print(f"  app   : {len(app_blocks)} blocks, __flash_binary_end=0x{app_end:08X}")
    print(f"  peer  : {len(peer_blocks)} blocks, {len(peer)} bytes at 0x{staging_addr:08X} (staging offset 0x{staging_off:05X})")
    print(f"  total : {total} blocks, {os.path.getsize(args.out)} bytes")


if __name__ == "__main__":
    main()
