# Plan: bk_split_flash — master auto-flashes its slave over the split link

Status: **plan, being rewritten for this repo. M0 scaffold not yet built.**
Target module: `modules/bastardkb/bk_split_flash/` — the master (USB) half streams a
full firmware image to its slave over the split link, the slave programs itself safely,
and (optionally) the master self-flashes last, so **one trigger updates both halves**.
Optionally the whole thing is driven with no host at all (boot-record / combined UF2).

Source of this plan: an extraction plan (`split_update`) authored for a different
userspace (svalboard), which this document re-targets to a BastardKB RP2040 split
module. The svalboard-specific file paths in that plan do **not** exist here; every
reference below points at this repository.

Design references (in this repo, tracked in git):

- `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md` — the original
  design/decisions (D1–D46 equivalent) and the boot2-CRC fail-safe rationale.
- `docs/superpowers/plans/2026-11-11-split-flash-both-halves-parts.md` — the task
  breakdown and bring-up log (the "source of truth" for what has been proven on HW).
- `agents/working-notes.md` — standing build/test commands, flash/EEPROM math,
  QMK build-system facts.

---

## 0. What is being built (reference behaviour)

The feature has three layers on a base self-updater:

1. **Self-update (single half).** Stage a raw image into a spare flash **window**
   above the app (`__flash_binary_end` → EEPROM base, computed at runtime from linker
   symbols, never hard-coded). The write path **arms** the ROM fail-safe first
   (zeroes the boot2 CRC word at `0xFC`, a bit-subset program with no erase), copies
   the app sectors, then **commits** last (erases sector 0 and reprograms it with a
   valid boot2 + vectors + first app page, from a RAM copy) and resets. Because a
   valid boot2 is written *only* during COMMIT and COMMIT is the final act before
   reset, an interruption anywhere between ARM and COMMIT lands the half in the ROM
   `RPI-RP2` BOOTSEL mass-storage, **sticky across power cycles** — never a brick.

2. **Split relay (master → slave).** The master streams the staged image to the slave
   over the split link. Control is a split-transaction RPC on the module's own id
   (`ARM` / `BEGIN` / `VERIFY` / `COMMIT` / `ABORT` / `SYNC`); the bulk payload rides
   one **length-prefixed bulk frame** per chunk on that same id (one transaction per
   chunk, not four), sized to the split transaction limit. The slave buffers into its
   staging window, per-chunk CRC32 checked before buffering; a NACK drives a
   resync/probe (never a blind retry storm), bounded so a corrupt link aborts without
   committing. The bulk phase is quiesced at the module boundary and resumed
   deterministically.

3. **Boot record / combined UF2 (automatic).** A per-build **combined UF2** carries the
   app at `0x10000000` **and** the peer image at `0x10000000 + staging_start`, so one
   BOOTSEL drag stages the peer; a boot-time record/trigger then auto-propagates it to
   the slave and self-flashes the master — no host session, no chord.

The module owns *how* an image crosses the link and is committed on the far side. The
keyboard owns *whether* it may (consent: a keycode/bootmagic trigger), *how* it looks
(RGB indicator), and the board's matrix/pointing/hand in the image descriptor.

---

## 1. Overall work principles

Adapted from the extracted svalboard plan; keep these invariants at every step.

1. **Move, don't rewrite, once code exists.** Wire formats and the image descriptor
   must stay byte-for-byte stable while they are being relocated/refactored. Renames
   and format changes never ride the same step.

2. **Mechanism vs. policy.** The module owns transport + fail-safe. The keyboard owns
   consent (trigger), indicator (RGB), and board identity (hand, pointing, image
   bounds). No policy leaks into the module.

3. **Ports, not includes.** The module must include **no keyboard header**. Every
   platform/policy dependency goes through `bk_split_flash_port.h`. The keyboard (or a
   platform file) implements it.

4. **Reversible, staged diffs.** Every milestone ends with the plain build compiling
   and the default behaviour unchanged (the module is inert unless a keymap opts in).

5. **Both halves, same commit.** The relay changes the split message table and
   `split_shared_memory`. The module must be enabled on **both** halves at the same
   commit; document and assert it. A stale peer garbles the link.

6. **Never widen the security reach.** A valid boot2 is written in exactly one place
   (COMMIT, RAM-resident, last). The record write-side stays boot-ROM-only. This is a
   test, not a comment.

7. **Platform stays platform.** The RAM-resident flash/commit code is register-level
   RP2040 (ROM calls, XIP, watchdog, SRAM); it ships as the RP2040 platform layer and
   is only parameterized by the image bound.

8. **Runtime bounds, never constants.** Staging bounds come from linker symbols
   (`bksf_get_staging_window()`), the EEPROM base from `WEAR_LEVELING_RP2040_FLASH_*`.
   Confirm per-build from the `.map`.

9. **Names.** Files `bk_split_flash_*`, functions `bksf_*`; only QMK-mandated names
   (`qmk_module.json`, `rules.mk`, `post_config.h`, `post_rules.mk`, and derived hooks
   like `keyboard_post_init_bk_split_flash`) are exceptions.

10. **Small, verifiable steps.** One milestone item per change, each with build + test
    evidence recorded here and in the parts plan.

---

## 2. Detailed plan

### M0 — inventory and seams (no behaviour change)

Goal: define the module surface and the port before moving source.

Steps:

1. Keep this file as the plan; fold the reference behaviour in §0 into it.
2. Create `modules/bastardkb/bk_split_flash/` with `qmk_module.json`, `config.h`,
   `post_config.h`, `rules.mk`, and `bk_split_flash.c` (hooks only). Wire it into the
   three vendor keymaps that already list `"bastardkb/bk_split_flash"` (skeletyl,
   charybdis/4x6, dilemma/4x6_4_procyon).
3. Add `bk_split_flash_port.h`: the only contract the module uses (see §3.4).
4. Add `bk_split_flash.h` (public API, §3.3) as declarations only.
5. Add `bk_split_flash_image.h` (descriptor) and `bk_split_flash_proto.h` (wire/op
   codes) as declarations only.
6. Add a module-isolation test (grep-based) that no module `.c`/`.h` includes a
   keyboard header.

Exit: module builds empty; the three vendor keymaps compile; the isolation test passes
vacuously.

### M1 — self-update on one half (fail-safe proof)

Steps:

1. `bk_split_flash_flash.c`: runtime bounds (`bksf_get_staging_window()`), `bksf_arm()`
   (zero boot2 CRC, no erase), RAM-resident `bksf_apply_and_reset()` (app sectors, then
   sector 0 last), EEPROM wipe on apply.
2. `bk_split_flash.c`: hooks + compile-time test selector + `BKSFTRG` keycode; trigger
   logs the facts (boot2 CRC pair, `__flash_binary_end`, EEPROM base, staging window).
3. `tools/gen_peer_image.py`: turn a peer `.uf2` into an embedded `const uint8_t[]`
   blob + descriptor + raw sidecar `.bin`.

Exit: ARM → power cycle lands in `RPI-RP2`; COMMIT of an identical sector 0 boots the
same firmware; a changed peer self-flashes; the interruption matrix (deterministic
mid-erase/mid-program states + a real cut mid-apply) is all recoverable.

### M2 — split relay (master streams to the slave)

Steps:

1. `bk_split_flash_proto.h`: versioned protocol + control command enum.
2. `bk_split_flash_rpc.c`: master state machine (ARM → BEGIN → chunks (paced) →
   per-sector flush → VERIFY → COMMIT → wait for rejoin) and the slave receiver.
3. `bk_split_flash_pack.c`: the length-prefixed bulk frame; enlarge
   `RPC_M2S_BUFFER_SIZE` accordingly (asserted).
4. Link integrity: per-chunk CRC32, idempotent ACKs reporting `rxp_off`, resync on a
   non-ACK, bounded attempts; disconnect hint + idle peer watchdog.

Exit: slave updates and rejoins (`slave rejoined -> DONE`); corrupt link never commits
(B2 aborts, B1 recovers); power-cut matrix on the slave → `RPI-RP2`.

### M3 — both halves, one trigger

Steps:

1. `bksf_start_both_update()`: after `slave rejoined -> DONE`, the master self-flashes
   the same peer last (`bksf_self_update_from_peer_hold()`).
2. Combined power-cut matrix on both halves.

Exit: one trigger leaves both halves on the peer build; every cut → `RPI-RP2` or the
correct build, never unresponsive.

### M4 — packaging and no-host trigger

Steps:

1. `post_rules.mk` + `tools/make_combined_uf2.py`: per-build
   `<TARGET>_combined.uf2` = app + peer at `staging_start` (from the ELF `.symtab`,
   rounded to 4096 B); gated on the blob **and** its sidecar; never touches the normal
   UF2.
2. `BKSF_TEST_STAGING` + `bksf_verify_staging_peer()`: on boot, confirm the staged peer
   equals the embedded blob (len + CRC32 + diff); `MATCH`/`MISMATCH`.
3. Boot trigger (bootmagic: start the update by holding a key at power-on), retiring the
   bring-up keycode.

Exit: a single BOOTSEL drag of the combined UF2, then the boot trigger, updates both
halves with no host session.

### M5 — guard rails, indicator, docs, release

Steps:

1. Version/CRC image guard (`bksf_validate_image()` before ARM): magic / descriptor
   version / proto / len / CRC / staging-fit; a mismatch is refused and the indicator
   goes red.
2. "Flash pending" RGB indicator (`bksf_set_state()` / `bksf_tick_state()`,
   `rgb_matrix_indicators_advanced_bk_split_flash()`), ERROR auto-clearing.
3. Recovery procedure documented; module README (config surface, port contract,
   security invariants, both-halves/same-commit rule).
4. If upstreaming: neutral vendor path + a `qmk_module.json` feature list.

Exit: mismatched image refused before ARM; indicator states observed; recovery
procedure validated.

### M6 — prove reuse on a second RP2040 split board (optional)

Steps: enable the module on a second BastardKB RP2040 split board with its own staging
window, provide consent + a simple indicator, and confirm the master flashes its slave
with no per-board module source.

---

## 3. Code snippets / interfaces

### 3.1 `modules/bastardkb/bk_split_flash/qmk_module.json`

```json
{
    "module_name": "BK split flash",
    "maintainer": "bastardkb",
    "license": "GPL-2.0-or-later",
    "features": {
        "split_keyboard": true
    }
}
```

### 3.2 `rules.mk` / `post_config.h` (shape)

```make
# bk_split_flash: master auto-flashes its slave over the split link.
BK_SPLIT_FLASH_PATH := $(dir $(lastword $(MAKEFILE_LIST)))

# Slide in the compile-time test selector / tunables only when opted in by a keymap.
SRC += $(BK_SPLIT_FLASH_PATH)bk_split_flash.c
SRC += $(BK_SPLIT_FLASH_PATH)bk_split_flash_flash.c
SRC += $(BK_SPLIT_FLASH_PATH)bk_split_flash_rpc.c
SRC += $(BK_SPLIT_FLASH_PATH)bk_split_flash_pack.c
```

```c
/* post_config.h — only needed if the module owns an EEPROM datablock */
/* #define EECONFIG_MODULE_BK_SPLIT_FLASH_DATA_SIZE <n> */
```

### 3.3 `bk_split_flash.h` — public API (both halves)

```c
// modules/bastardkb/bk_split_flash/bk_split_flash.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bk_split_flash_proto.h"   // control commands, descriptor

// ---- init / housekeeping ---------------------------------------------------------
void bksf_init(void);            // keyboard_post_init: probe flash, register RPC
void bksf_task(void);            // housekeeping: relay + boot relay + slave slice

// ---- master relay ----------------------------------------------------------------
bool bksf_begin_update(const uint8_t *image, uint32_t len);   // validate → ARM → stream
bool bksf_begin_both_update(const uint8_t *image, uint32_t len); // + master self-flash
void bksf_abort(void);
bool bksf_busy(void);

// ---- slave session status --------------------------------------------------------
uint8_t bksf_slave_state(void);
bool    bksf_slave_active(void);

// ---- boot record / combined UF2 (present when a record is configured) ------------
void bksf_boot_task(void);
bool bksf_verify_staging_peer(void);   // staged peer == embedded peer?
```

### 3.4 `bk_split_flash_port.h` — the only contract

Formalizes the platform/policy seam. The keyboard (or a platform file) implements every
function; the module includes no keyboard header.

```c
// modules/bastardkb/bk_split_flash/bk_split_flash_port.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bk_split_flash_proto.h"

// ---- time / role -----------------------------------------------------------------
uint32_t bksf_port_now_ms(void);
bool     bksf_port_is_master(void);   // this half has USB

// ---- split link ------------------------------------------------------------------
bool bksf_port_rpc(const uint8_t *req, uint8_t req_len, uint8_t *rsp, uint8_t rsp_len);
bool bksf_port_bulk(const uint8_t *frame, uint8_t len, uint8_t *rsp, uint8_t rsp_len);
void bksf_port_lock(void);            // split shared-memory lock
void bksf_port_unlock(void);
bool bksf_port_connected(void);       // is_transport_connected()

// ---- flash (master reads, slave programs) ----------------------------------------
bool     bksf_port_flash_available(void);
void     bksf_port_jedec(uint8_t id[3]);
bool     bksf_port_slot_read(uint32_t off, uint8_t *dst, uint32_t n);  // staged, uncached
bool     bksf_port_self_read(uint32_t off, uint8_t *dst, uint32_t n); // flash [0, end)
uint32_t bksf_port_staging_start(void);  // __flash_binary_end rounded up to 4096
uint32_t bksf_port_staging_bytes(void);  // EEPROM base - staging start
// commit (platform): copies the staged image over the running firmware and resets
bool     bksf_port_commit_available(void);
void     bksf_port_reboot(void);
uint32_t bksf_port_crc32_mpeg2(const uint8_t *p, uint32_t n);

// ---- policy ----------------------------------------------------------------------
void    bksf_port_build_id_touch(void);      // keep ids referenced where needed
uint8_t bksf_port_info_flags(void);
bool    bksf_port_updating(void);
```

### 3.5 `config.h` — config surface (defaults via `#ifndef`)

```c
// modules/bastardkb/bk_split_flash/config.h
#pragma once

#ifndef BKSF_PROTO_VERSION
#    define BKSF_PROTO_VERSION 2        // bump when the wire/descriptor changes
#endif
#ifndef BKSF_BULK_CHUNK
#    define BKSF_BULK_CHUNK 224         // 16 + chunk <= 255 (uint8_t sizes)
#endif
#ifndef BKSF_SECTOR_PACE_MS
#    define BKSF_SECTOR_PACE_MS 60
#endif
#ifndef BKSF_FLUSH_RETRY_MS
#    define BKSF_FLUSH_RETRY_MS 20
#endif
#ifndef BKSF_REJOIN_POLL_MS
#    define BKSF_REJOIN_POLL_MS 100
#endif
#ifndef BKSF_CHUNK_ATTEMPT_MAX
#    define BKSF_CHUNK_ATTEMPT_MAX 30
#endif
#ifndef BKSF_ERROR_HOLD_MS
#    define BKSF_ERROR_HOLD_MS 3000
#endif
```

### 3.6 Staging bounds from linker symbols (never hard-coded)

```c
// bk_split_flash_flash.c
extern uint8_t __flash_binary_end;
extern uint8_t __flash_binary_start;

// start = __flash_binary_end rounded UP to a 4096 B sector; end = EEPROM base.
bool bksf_get_staging_window(uint32_t *start, uint32_t *bytes) {
    uint32_t app = (uint32_t)&__flash_binary_end - XIP_BASE;
    uint32_t s   = (app + 0xFFFu) & ~0xFFFu;
    uint32_t end = WEAR_LEVELING_RP2040_FLASH_BASE - XIP_BASE;
    if (s >= end) return false;
    *start = s; *bytes = end - s; return true;
}
```

### 3.7 Boot record — write-side stays boot-ROM-only

The invariant is enforced by construction: the module has no function that programs
page 0 of the record sector, and the record offset is used once.

```c
// bk_split_flash_flash.h
#define BKSF_RECORD_MAGIC 0x465342u
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint8_t  manifest[BKSF_MANIFEST_BYTES];
    uint32_t crc;
} bksf_record_t;

// The only two writers, both address-constant; neither can program page 0.
bool bksf_port_record_erase(void);                       // whole 4 KiB sector
bool bksf_port_record_state_program(const uint8_t page[256]); // page 1 only
```

### 3.8 Module-isolation test (new)

Keeps principle 3 honest; runs from the module's host-test entry point.

```python
# modules/bastardkb/bk_split_flash/tools/check_isolation.py
import pathlib, re, sys
MOD = pathlib.Path(__file__).resolve().parents[1]
FORBIDDEN = re.compile(r'#\s*include\s+"(keyboards/|.*keymap|argos|bk_led_matrix|bk_pointing_device|lcd)')
bad = [f"{f}:{i}: {l.strip()}"
       for f in list(MOD.glob("*.c")) + list(MOD.glob("*.h"))
       for i, l in enumerate(f.read_text().splitlines(), 1) if FORBIDDEN.search(l)]
if bad:
    sys.exit("module includes keyboard headers:\n" + "\n".join(bad))
print("module isolation OK")
```

---

## 4. Image generation: compile vs. script

`qmk compile` produces only the plain firmware `.uf2` (blocks from `0x10000000`). It
does **not** produce the update artifacts; both are computed from the linked image
after the fact:

- **Peer blob** (`gen_peer_image.py`): a peer `.uf2` → embedded `const uint8_t[]` +
  `bksf_image_hdr_t` descriptor + a byte-identical raw sidecar `.bin`. The generated
  `bk_split_flash_peer_image.c` must **not** be committed.
- **Combined UF2** (`make_combined_uf2.py`, via module `post_rules.mk`): app at
  `0x10000000` + peer at `0x10000000 + staging_start` (`__flash_binary_end` rounded up
  to 4096 B). Gated on blob + sidecar; a no-op otherwise; never touches the normal UF2.

```sh
# 1. peer build
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
# 2. embed it in the carrier build
python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py <peer>.uf2
# 3. rebuild the carrier (now carrying the blob); post_rules emits <TARGET>_combined.uf2
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
```

Decisions this forces:

1. The generator is a required module deliverable, generalized with the firmware
   (read bounds from the build, not hard-coded).
2. The plain firmware `.uf2` remains directly flashable and fully functional for one
   half; only the cross-half artifacts need the script.
3. `qmk compile` can exit `0` even when the post-build step fails — a missing
   `*_combined.uf2` is the signal, not the exit code.

---

## 5. Acceptance / definition of done

1. `modules/bastardkb/bk_split_flash/` builds and links with the BastardKB vendor
   keymaps that list it (skeletyl, charybdis/4x6, dilemma/4x6_4_procyon).
2. Default builds are unchanged; the module is inert unless opted in.
3. A relay updates the slave and it rejoins; a corrupt link never commits; the
   power-cut matrix (ARM/COPY/COMMIT, both halves) always lands recoverable.
4. One trigger (keycode during bring-up, bootmagic at release) updates both halves.
5. The combined UF2 stages the peer; `bksf_verify_staging_peer()` reports `MATCH`.
6. The isolation test and the security-invariant test pass.
7. A second RP2040 split board can enable the module with its own window and no
   per-board module source.

---

## 6. Open decisions (resolve before coding the dependent milestone)

1. **Trigger UX (M4/M5):** bootmagic-only at release (retiring `BKSFTRG`) vs. keeping
   the keycode too. *Leaning: bootmagic-only, keycode for bring-up.*
2. **Second board (M6):** which board proves reuse (candidate: charybdis
   splinktegrated, which already lists the module).
3. **Record vs. pure combined-UF2 (M4):** whether a 4 KiB boot record is needed at all,
   or whether the combined UF2 + boot trigger is sufficient.
