#!/usr/bin/env python3
# Copyright 2026 Quentin LEBASTARD <bstkbd@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the single bk_split_flash firmware UF2.

A plain `qmk compile` has no peer image compiled in, so the update path has
nothing to distribute. This script produces the ONE image to flash: a single
BOOTSEL-drag UF2 holding

  * the carrier app (with a peer image embedded in flash), at 0x10000000, and
  * that same peer image written at the staging address (the app's
    __flash_binary_end rounded up to a 4096-byte sector).

Both payloads are byte-identical to the embedded blob (gen_peer_image.py writes
the raw sidecar used here). Everything is derived from the build itself -- no
hardcoded addresses, no QMK core edits, no git.

The output is <TARGET>.uf2 in the userspace root -- the conventional QMK name,
overwritten with the complete image. The old <TARGET>_peer.uf2 /
<TARGET>_full.uf2 / <TARGET>_combined.uf2 set is gone: stale variants are
deleted after a successful build, so exactly one UF2 remains.

Pipeline (all module code lives under modules/bastardkb/bk_split_flash/):
  1. remove any stale generated peer blob, so build #1 is a clean "peer" build
  2. qmk compile  -> peer firmware UF2 in .build/ (fed straight to step 3)
  3. tools/gen_peer_image.py -> bk_split_flash_peer_image.{c,bin} + descriptor
  4. verify the descriptor (magic/proto/len/CRC/RAM-fit) against the sidecar
  5. qmk compile  -> carrier app (embeds the blob)
  6. tools/make_combined_uf2.py -> the single <TARGET>.uf2 (app + staged peer)
  7. verify the UF2 block structure (app at XIP base, peer at staging)
  8. delete stale <TARGET>_peer/_full/_combined.uf2 variants

Reusable usage (run inside WSL, where `qmk` is on PATH):
    python3 scripts/build_full_uf2.py
    python3 scripts/build_full_uf2.py -kb bastardkb/dilemma/4x6_4_procyon -km vendor
    python3 scripts/build_full_uf2.py --out /path/to/image.uf2
    python3 scripts/build_full_uf2.py --no-clean       # incremental, faster
    python3 scripts/build_full_uf2.py --bksf-test BKSF_TEST_UPDATE_BOTH

A stale bk_split_flash_peer_image.c garbles the split link; step 1 always removes
it before the peer build, so re-running this script is always safe.
"""

import argparse
import atexit
import os
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
USERSPACE = SCRIPT_DIR.parent
MODULE = USERSPACE / "modules" / "bastardkb" / "bk_split_flash"
TOOLS = MODULE / "tools"

# Reuse the module's own tooling (single source of truth for CRC32 / UF2 parsing).
sys.path.insert(0, str(TOOLS))
import gen_peer_image as gpi  # noqa: E402
import make_combined_uf2 as mcu  # noqa: E402

XIP_BASE = 0x10000000
SECTOR = 4096


def log(msg):
    print("[full-uf2] " + msg, flush=True)


def fail(msg):
    sys.exit("[full-uf2] ERROR: " + msg)


def run(cmd, cwd):
    log("$ " + " ".join(str(c) for c in cmd))
    try:
        subprocess.run([str(c) for c in cmd], cwd=str(cwd), check=True)
    except subprocess.CalledProcessError as exc:
        fail("command failed (exit %d): %s" % (exc.returncode, " ".join(str(c) for c in cmd)))


def resolve_qmk_home(arg):
    candidates = []
    if arg:
        candidates.append(Path(arg))
    env = os.environ.get("QMK_HOME")
    if env:
        candidates.append(Path(env))
    try:
        out = subprocess.check_output(["qmk", "config", "user.qmk_home"], stderr=subprocess.DEVNULL)
        for line in out.decode(errors="replace").splitlines():
            if "user.qmk_home" in line and "=" in line:
                candidates.append(Path(line.split("=", 1)[1].strip()))
    except (OSError, subprocess.CalledProcessError):
        pass
    candidates.append(USERSPACE.parent / "bastardkb-qmk")
    for c in candidates:
        if (c / "quantum").is_dir() and (c / "builddefs").is_dir():
            return c
    fail("could not locate the QMK firmware tree (tried %s); pass --qmk-home" %
         ", ".join(str(c) for c in candidates))


def config_expr(text, name):
    """Evaluate a simple #define numeric expression from module config.h."""
    m = re.search(r"#\s*define\s+%s\s+(.+)" % re.escape(name), text)
    if not m:
        fail("%s not found in %s" % (name, MODULE / "config.h"))
    expr = re.sub(r"[uU](?![a-zA-Z_])", "", m.group(1).strip())
    try:
        return int(eval(expr, {"__builtins__": {}}, {}))
    except Exception:
        fail("cannot parse %s value %r" % (name, expr))


def keymap_config_path(keyboard, keymap):
    return USERSPACE / "keyboards" / keyboard / "keymaps" / keymap / "config.h"


def patch_bksf_test(path, test_name):
    """Temporarily override the compile-time BKSF_TEST selector. Returns the
    original text so the caller can always restore it (finally)."""
    original = path.read_text()
    patched, n = re.subn(r"(^\s*#\s*define\s+BKSF_TEST\s+)\S+",
                         lambda m: m.group(1) + test_name,
                         original, flags=re.MULTILINE)
    if n == 0:
        path.write_text(original)
        fail("no '#define BKSF_TEST' line in %s" % path)
    path.write_text(patched)
    log("temporarily set BKSF_TEST=%s in %s (restored on exit)" % (test_name, path.name))
    return original


def verify_blob(peer_c, peer_bin):
    """Cross-check the generated descriptor against the raw sidecar."""
    if not peer_c.is_file() or not peer_bin.is_file():
        fail("gen_peer_image.py did not produce both the .c blob and .bin sidecar")
    text = peer_c.read_text()

    def field(pattern):
        m = re.search(pattern, text)
        if not m:
            fail("descriptor field missing (regex %r)" % pattern)
        return m

    magic = int(field(r"\.magic\s*=\s*0x([0-9A-Fa-f]+)u").group(1), 16)
    proto = int(field(r"\.proto_version\s*=\s*(\d+)").group(1))
    dlen = int(field(r"\.len\s*=\s*(\d+)u").group(1))
    dcrc = int(field(r"\.crc\s*=\s*0x([0-9A-Fa-f]+)u").group(1), 16)
    blob_len = int(field(r"bksf_peer_image_len\s*=\s*(\d+)u").group(1))

    data = peer_bin.read_bytes()
    if magic != gpi.IMAGE_MAGIC:
        fail("descriptor magic 0x%08X != 0x%08X" % (magic, gpi.IMAGE_MAGIC))
    expected_proto = gpi.read_proto_version(str(MODULE))
    if proto != expected_proto:
        fail("descriptor proto v%d != module proto v%d" % (proto, expected_proto))
    if not (len(data) == dlen == blob_len):
        fail("descriptor len %d / blob len %d / sidecar %d disagree" % (dlen, blob_len, len(data)))
    if len(data) % SECTOR != 0:
        fail("peer image %d bytes is not sector-aligned" % len(data))
    crc = gpi.bksf_crc32(data)
    if crc != dcrc:
        fail("descriptor CRC 0x%08X != computed 0x%08X" % (dcrc, crc))

    rx_size = config_expr((MODULE / "config.h").read_text(), "BKSF_RX_BUFFER_SIZE")
    if len(data) > rx_size:
        fail("peer image %d bytes exceeds the slave RAM buffer %d bytes" % (len(data), rx_size))

    log("descriptor OK: magic=0x%08X proto=v%d len=%d crc=0x%08X "
        "(%d sectors, RAM buffer %d bytes)" %
        (magic, proto, dlen, crc, dlen // SECTOR, rx_size))


def verify_uf2(path, elf, peer_bin):
    """Check the combined UF2: app at XIP base, peer at the staging address."""
    blocks = mcu.parse_uf2(str(path))
    if not blocks:
        fail("combined UF2 has no blocks")
    addrs = [b[0] for b in blocks]
    if min(addrs) != XIP_BASE:
        fail("app does not start at XIP base 0x%08X (min 0x%08X)" % (XIP_BASE, min(addrs)))

    app_end = mcu.elf_symbol(str(elf), "__flash_binary_end")
    staging_off = ((app_end - XIP_BASE + SECTOR - 1) // SECTOR) * SECTOR
    staging_addr = XIP_BASE + staging_off
    peer_addrs = [a for a in addrs if a >= staging_addr]
    if not peer_addrs:
        fail("no peer blocks found at/after staging 0x%08X" % staging_addr)
    if min(peer_addrs) != staging_addr:
        fail("peer does not start exactly at staging 0x%08X" % staging_addr)

    peer_len = len(peer_bin.read_bytes())
    peer_len_padded = peer_len + ((mcu.UF2_PAYLOAD - peer_len % mcu.UF2_PAYLOAD) % mcu.UF2_PAYLOAD)
    expected = peer_len_padded // mcu.UF2_PAYLOAD
    if len(peer_addrs) != expected:
        fail("peer block count %d != expected %d" % (len(peer_addrs), expected))

    log("combined UF2 OK: %d blocks, app_end=0x%08X, peer @0x%08X "
        "(%d blocks / %d bytes), total %d bytes" %
        (len(blocks), app_end, staging_addr, len(peer_addrs), peer_len, path.stat().st_size))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-kb", "--keyboard", default="bastardkb/dilemma/4x6_4_procyon",
                    help="QMK keyboard (default: bastardkb/dilemma/4x6_4_procyon)")
    ap.add_argument("-km", "--keymap", default="vendor", help="keymap (default: vendor)")
    ap.add_argument("--qmk-home", default=None, help="QMK firmware tree (default: autodetect)")
    ap.add_argument("--out", default=None, help="output UF2 (default: <TARGET>.uf2 in userspace root)")
    ap.add_argument("--no-clean", action="store_true", help="skip qmk compile -c (incremental, faster)")
    ap.add_argument("--bksf-test", default=None,
                    help="temporarily override the keymap's BKSF_TEST selector (e.g. "
                         "BKSF_TEST_UPDATE_BOTH); the file is always restored on exit")
    args = ap.parse_args()

    if not (TOOLS / "gen_peer_image.py").is_file() or not (TOOLS / "make_combined_uf2.py").is_file():
        fail("module tooling not found under %s" % TOOLS)

    # Optional compile-time test selector: patch for both builds, restore on any
    # exit path (atexit also runs when fail() calls sys.exit).
    if args.bksf_test:
        km_cfg = keymap_config_path(args.keyboard, args.keymap)
        if not km_cfg.is_file():
            fail("keymap config.h not found: %s" % km_cfg)
        original = patch_bksf_test(km_cfg, args.bksf_test)
        atexit.register(km_cfg.write_text, original)

    qmk_home = resolve_qmk_home(args.qmk_home)

    target = args.keyboard.replace("/", "_") + "_" + args.keymap
    build = qmk_home / ".build"
    app_uf2 = build / (target + ".uf2")
    app_elf = build / (target + ".elf")
    peer_c = MODULE / "bk_split_flash_peer_image.c"
    peer_bin = MODULE / "bk_split_flash_peer_image.bin"
    out = Path(args.out) if args.out else USERSPACE / (target + ".uf2")
    clean_flag = [] if args.no_clean else ["-c"]

    log("userspace    : %s" % USERSPACE)
    log("qmk firmware : %s" % qmk_home)
    log("target       : %s" % target)

    # 1. A stale embedded peer would garble the peer build's split link.
    for p in (peer_c, peer_bin):
        if p.exists():
            log("removing stale %s" % p.name)
            p.unlink()

    # 2. Clean "peer" build (no blob compiled in). Its UF2 is fed straight to
    #    gen_peer_image.py in step 3; there is no separate "_peer.uf2" deliverable.
    run(["qmk", "compile", "-kb", args.keyboard, "-km", args.keymap] + clean_flag, cwd=USERSPACE)
    if not app_uf2.is_file():
        fail("peer build produced no %s" % app_uf2)
    log("peer firmware: %s (%d bytes)" % (app_uf2, app_uf2.stat().st_size))

    # 3. Generate the embedded blob + raw sidecar + image descriptor.
    run([sys.executable, TOOLS / "gen_peer_image.py", app_uf2], cwd=USERSPACE)

    # 4. Descriptor must be one the running firmware will accept before ARM.
    verify_blob(peer_c, peer_bin)

    # 5. Carrier build: rules.mk now compiles the blob in. The combined image is
    #    built explicitly in step 6; post_rules.mk does not emit a second file.
    run(["qmk", "compile", "-kb", args.keyboard, "-km", args.keymap] + clean_flag, cwd=USERSPACE)
    if not app_uf2.is_file() or not app_elf.is_file():
        fail("carrier build produced no %s / %s" % (app_uf2, app_elf))

    # 6. Build the single output UF2 (app + staged peer) at the conventional name.
    run([sys.executable, TOOLS / "make_combined_uf2.py",
         "--app", app_uf2, "--elf", app_elf,
         "--peer", peer_bin, "--out", out], cwd=USERSPACE)
    if not out.is_file():
        fail("make_combined_uf2.py produced no %s" % out)

    # 7. Structural verification of the produced image.
    verify_uf2(out, app_elf, peer_bin)

    # 8. Exactly one image: remove the old redundant variants (and QMK's carrier
    #    copy when --out points elsewhere) so only <out> remains.
    for name in (target + ".uf2", target + "_peer.uf2",
                 target + "_full.uf2", target + "_combined.uf2"):
        stale = USERSPACE / name
        if stale.exists() and stale.resolve() != out.resolve():
            stale.unlink()
            log("removed stale %s" % name)

    log("DONE. Image    : %s" % out)
    log("      Generated: %s , %s" % (peer_c, peer_bin))


if __name__ == "__main__":
    main()
