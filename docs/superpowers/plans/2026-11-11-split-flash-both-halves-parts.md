# Split Flash-From-One-Side — Multi-Part Implementation Plan

> **For agentic workers:** This plan is a *task breakdown*, not the design. The design and mechanism live in `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md` (read it first; this document assumes it). Work one task at a time, top to bottom. Each task is small enough to finish and verify in a single sitting. **Never start a task whose `Depends on` is unfinished.** A mistake here can brick a half, so every task ends with an explicit, on-hardware verification before the next begins.

**Source design:** `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md`
**Goal:** Update **both** halves of an RP2040 split keyboard from the USB master half, with a boot2-CRC fail-safe that guarantees an interrupted update lands each half in `RPI-RP2` BOOTSEL instead of a brick.
**Status:** In progress. **Parts 1–7 complete and HW-verified** (T0–T4.3 on the
charybdis 4x6 and dilemma 4x6 procyon; T5.1/T5.2 on the dilemma 4x6 procyon, with a
few non-timed master cases assumed; T7.1/T7.2 bulk transport on the dilemma 4x6
procyon). **Part 6 (host flashing) DEFERRED and NOT STARTED** (user, 2026-11-11; a
partial implementation was reverted). **Part 8 (combined-UF2 packaging) is code
complete and passes offline checks; HW verification pending.** **Next: Part 9**
(guard rails and recovery) after the Part 8 HW check. See the "WHERE WE ARE" resume
point below and the "Local build environment" section.

---

## How to use this plan

1. **Parts are milestones.** A part is a coherent, demonstrable capability. Do not start the next part until the current part's exit criteria pass on hardware.
2. **Tasks are the unit of work.** Each task has: `Depends on`, `Goal`, `Files`, `Steps`, `Verify`, `Done when`. Keep each task small; if a step grows, split it into a new task rather than expanding one.
3. **Hardware verification is mandatory.** "Compiles" is not verification for anything that touches flash.
4. **Dev board first.** Tasks that can brick (Part 2 onward) are exercised on a spare RP2040 board before any daily-driver half.
5. **The fail-safe invariant is absolute:** a *valid* boot2 CRC is written **only** during COMMIT, and COMMIT is the last action before reset. Every task that could reset a half must be checked against this.
6. **Record results.** After each task, append the observed values/results to the "Task log" at the end of this document.

## Global constraints (apply to every task)

- Module lives at `modules/bastardkb/bk_split_flash/`. **No edits to `bastardkb-qmk`.**
- Source files are `bk_split_flash_*`; functions are `bksf_*`. QMK-mandated names are the only exceptions (`qmk_module.json`, `rules.mk`, `post_config.h`, and derived hooks like `keyboard_post_init_bk_split_flash`).
- All erase/program calls run from RAM (`__no_inline_not_in_flash_func`) with interrupts disabled; wrap with `connect_internal_flash`/`flash_exit_xip`/`flash_flush_cache`/`flash_enable_xip_via_boot2` as the wear-leveling flasher does.
- Respect granularity: erase a multiple of 4096 B, program a multiple of 256 B, offsets aligned.
- Compute staging bounds at runtime from linker symbols; bounds-check against `WEAR_LEVELING_RP2040_FLASH_BASE`. Never hard-code.
- The module is enabled per target by adding `"bastardkb/bk_split_flash"` to that keymap's `keymap.json` `modules` list.

---

## Local build environment (this machine)

The agent's shell is Windows PowerShell; `qmk` lives in WSL, not on the Windows PATH.
Fixed paths (verified 2026-11-11):

| what | path |
|---|---|
| userspace / overlay — **this repo** | `/home/quinn/qmk_userspace_private` |
| QMK firmware tree (`qmk_home`) | `/home/quinn/bastardkb-qmk` |
| `qmk` CLI (v1.2.0, uv tool) | `/home/quinn/.local/bin/qmk` |
| qmk config (`qmk_home` / `overlay_dir`) | `/home/quinn/.config/qmk/qmk.ini` |

Run builds through WSL with a login shell (`bash -lc` sets `HOME`/`PATH`; do **not**
write `$HOME` in the PowerShell string — PowerShell expands it to a Windows path):

```powershell
wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb <keyboard> -km <keymap> -c"
```

`-c` = compile only (no flash). For the T4.3 target that is
`-kb bastardkb/dilemma/4x6_4_procyon -km vendor`. The read-only firmware tree
`/home/quinn/bastardkb-qmk` must never be edited; all module work lives in this repo
under `modules/`.

---

## Part 0 — Decisions and recon

> No code that can brick anything may start until Part 0 is complete. Tasks here are read-only or config-only.

### T0.1 — Answer Open Decision 1: does the slave expose USB?
- **Depends on:** —
- **Goal:** Determine whether the slave half has a reachable USB data connection. This gates whether the slave may ever be armed.
- **Steps:**
  1. Inspect the board/PCB for a USB connector on the non-master half for each target (skeletyl, scylla, tbkmini, charybdis, dilemma).
  2. Confirm by plugging USB into the slave alone and checking for `RPI-RP2`/enumeration in BOOTSEL.
- **Verify:** Written answer per target: "slave USB reachable: yes/no".
- **Done when:** Decision recorded in the "Task log" and copied into the design doc's Open Decisions.
- **Blocks:** T3.1, T4.1 (any slave-side arming). If "no", the slave path must switch to the app-region-only A/B fallback described in the design doc §11.

### T0.2 — Confirm flash size, EEPROM base, and `__flash_binary_end` per target
- **Depends on:** —
- **Goal:** Know the real staging budget for each target.
- **Steps:**
  1. Build one target (`qmk compile -kb <kb> -km <km>`) and open the `.map` file.
  2. Record `__flash_binary_end`, `WEAR_LEVELING_RP2040_FLASH_BASE`, and total flash size.
  3. Note whether the build includes `argos` (which reserves the top 256 KiB per `modules/bastardkb/argos/config.h:43-44`).
- **Verify:** Values match the linker defaults referenced in the design doc §2 (`RP2040_FLASH_TIMECRIT.ld:23`, `wear_leveling_rp2040_flash_config.h:16,31`).
- **Done when:** A table of `target → app_end → eeprom_base → staging_bytes` is recorded.

### T0.3 — Decide first milestone scope (Open Decision 2)
- **Depends on:** T0.1, T0.2
- **Goal:** Pick the first demonstrable milestone.
- **Options:** (a) single-half master self-flash only (safest, proves ARM/COMMIT); (b) both halves immediately.
- **Verify:** Decision recorded.
- **Done when:** Part ordering confirmed (default in this plan is (a): Parts 1–2 before Parts 3–5).

### T0.4 — Decide data plane for v1 (Open Decision 3) and staging budget (Open Decision 4)
- **Depends on:** T0.2
- **Goal:** Confirm Option A (const blob) for v1, Option B (raw HID) as end state; confirm a peer image fits in staging for each target.
- **Verify:** For each target, `staging_bytes >= peer_image_size` (or the target is excluded).
- **Done when:** Decision + per-target fit table recorded.

### T0.5 — Decide trigger UX (Open Decision 6) and versioning guard (Open Decision 7)
- **Depends on:** —
- **Goal:** Fix the trigger (module keycode and/or host command) and the "flash pending" indicator (LED/OLED), and confirm the version/CRC guard in the image header.
- **Verify:** Decision recorded.
- **Done when:** Recorded in Task log.

**Part 0 exit criteria:** All seven Open Decisions resolved and written down; per-target staging table exists.

---

## Part 1 — Module skeleton and read-only introspection

> Goal: the module builds and can read flash facts, with zero risk. No writes.

### T1.1 — Create the module directory and `qmk_module.json`
- **Depends on:** Part 0
- **Goal:** A loadable community module with no behavior.
- **Files:** `modules/bastardkb/bk_split_flash/qmk_module.json`
- **Steps:**
  1. Create the directory.
  2. Author `qmk_module.json` with `module_name: bk_split_flash`, maintainer, license, and a `features` block (keycodes added later).
- **Verify:** `qmk compile` for one target does not error on module discovery.
- **Done when:** Module is discovered.

### T1.2 — Add `config.h`, `post_config.h`, `rules.mk`, and a no-op `bk_split_flash.c`
- **Depends on:** T1.1
- **Goal:** Compilable skeleton with a lifecycle hook and no-op.
- **Files:** `config.h`, `post_config.h`, `rules.mk`, `bk_split_flash.c`
- **Steps:**
  1. `rules.mk`: `SRC += bk_split_flash.c`.
  2. `bk_split_flash.c`: add `keyboard_post_init_bk_split_flash()` that does nothing (or logs a version string).
  3. `post_config.h`: leave empty for now.
- **Verify:** `qmk compile` succeeds; firmware boots.
- **Done when:** Target builds and runs with the module linked.

### T1.3 — Add a module keycode (placeholder trigger)
- **Depends on:** T1.2
- **Goal:** A key that will later trigger an update; for now it logs.
- **Files:** `config.h` (keycode range), `bk_split_flash.c` (`process_record_bk_split_flash`)
- **Steps:**
  1. Define the module keycode range.
  2. Add `process_record_bk_split_flash()` handling the key with a debug print.
- **Verify:** Pressing the key prints to the console; normal typing unaffected.
- **Done when:** Keycode observed working on hardware.

### T1.4 — Read-only introspection: page-0 CRC word, `__flash_binary_end`, EEPROM base
- **Depends on:** T1.2
- **Goal:** Log the flash facts the fail-safe depends on, read-only.
- **Files:** `bk_split_flash_flash.c` (new), `rules.mk`
- **Steps:**
  1. Add `bksf_dump_flash_facts()` that reads the 32-bit LE word at `0x100000FC` and prints it, plus `__flash_binary_end` and `WEAR_LEVELING_RP2040_FLASH_BASE`.
  2. Call it from `keyboard_post_init_bk_split_flash()`.
- **Verify:** Printed values match T0.2's map-file values; CRC word is non-zero for a normal build.
- **Done when:** Values confirmed on hardware for at least one target.
- **Notes:** This file becomes the home of the RAM-resident flasher in Part 2.

### T1.5 — Reproduce the boot2 CRC32 algorithm (unit-level)
- **Depends on:** T1.4
- **Goal:** Prove we can compute the ROM's expected CRC.
- **Files:** `bk_split_flash_flash.c`
- **Steps:**
  1. Implement the bit-reversed CRC32 (seed `0xffffffff`, little-endian output) matching `lib/pico-sdk/.../boot_stage2/pad_checksum`.
  2. Compute it over bytes `[0..251]` of a known boot2 blob (`platforms/chibios/vendors/RP/stage2_bootloaders.c`) and compare to the word at `0xFC`.
- **Verify:** Computed CRC equals the stored word for the real boot2 and for a known-good sample.
- **Done when:** Match confirmed (logged or asserted).

**Part 1 exit criteria:** Module builds, keycode works, and flash facts + CRC reproduction are verified on hardware. Nothing has been written to flash.

---

## Part 2 — Master-only fail-safe bring-up (the risky core)

> Goal: prove ARM and COMMIT on a spare RP2040 dev board, master only. This is the part that can brick; do it on hardware you can recover.

### T2.1 — Implement `bksf_arm()` (zero CRC, no erase)
- **Depends on:** Part 1
- **Goal:** Invalidate boot2 CRC in place without erasing.
- **Files:** `bk_split_flash_flash.c`
- **Steps:**
  1. Read the 256-byte page 0 into a RAM buffer.
  2. Zero bytes 252..255.
  3. `flash_range_program(0, page, 256)` (bit-subset, no erase).
- **Verify:** After arming (without reset), firmware keeps running; re-reading `0xFC` yields `0x00000000`.
- **Done when:** Arm is observed to take effect live with no side effects.
- **Notes:** Do **not** reset in this task.

### T2.2 — Test path: arm then reset → confirm BOOTSEL
- **Depends on:** T2.1
- **Goal:** Prove the fail-safe actually drops to `RPI-RP2`.
- **Files:** `bk_split_flash.c` (keycode → arm → reset)
- **Steps:**
  1. Wire the placeholder keycode to `bksf_arm()` then `reset_usb_boot(0,0)` (or NVIC reset).
  2. Recover by reflashing a normal UF2.
- **Verify:** After arm+reset the board enumerates as `RPI-RP2`; reflashing restores normal boot.
- **Done when:** Round-trip (arm → BOOTSEL → reflash → normal) confirmed on the dev board.

### T2.3 — Implement RAM-resident `bksf_commit_identical()` for the current image
- **Depends on:** T2.2
- **Goal:** Prove the erase/program/re-enable-XIP ordering by rewriting sector 0 with an identical copy.
- **Files:** `bk_split_flash_flash.c`
- **Steps:**
  1. Buffer the first 4 KiB of the *current* image into RAM while XIP is on.
  2. `save_and_disable_interrupts()`; `connect_internal_flash()`; `flash_exit_xip()`.
  3. `flash_range_erase(0, 4096)`; `flash_range_program(0, buf4k, 4096)`.
  4. `flash_flush_cache()`; `flash_enable_xip_via_boot2()`; restore interrupts; reset.
  5. Mark the whole routine `__no_inline_not_in_flash_func`.
- **Verify:** After reset the half boots the same firmware with no data loss (settings intact).
- **Done when:** Identical-image commit confirmed on the dev board.

### T2.4 — Power-cut matrix for ARM/COMMIT
- **Depends on:** T2.3
- **Goal:** Prove no interruption window leaves a brick.
- **Steps:** At each point — armed, mid-erase, mid-program of sector 0 — cut power and reapply.
- **Verify:** Every cut lands in `RPI-RP2` (if pre-valid-boot2) or boots the correct app (if post-COMMIT). Never unresponsive.
- **Done when:** Matrix results recorded.

### T2.5 — RAM-resident self-flash of a *changed* const-blob image (single half)
- **Depends on:** T2.3
- **Goal:** Update the running half from a distinct build compiled in as a const blob.
- **Files:** `bk_split_flash.c`, `bk_split_flash_flash.c`, `rules.mk`
- **Steps:**
  1. Add a `const uint8_t[]` peer image (a second build) to the module.
  2. Implement the minimal RAM-resident routine taking `(src, dst, first-4KiB buffer)` that erases/programs/commits/resets without returning.
  3. Trigger: ARM → COPY (app region only, page 0 excluded, per-sector read-back verify) → COMMIT.
- **Verify:** Firmware visibly changes and boots; interrupt at every step (pull power) always lands in BOOTSEL.
- **Done when:** Changed-image self-flash + power-cut matrix pass on the dev board.

**Part 2 exit criteria:** Master-only ARM/COPY/COMMIT and changed-image self-flash are proven on a spare board, with the full power-cut matrix passing. No slave involved yet.

---

## Part 3 — Control-plane RPC (no data)

> Goal: master and slave can talk the update handshake safely, without moving an image. Gate: T0.1 must permit slave arming.

### T3.1 — Register the module RPC id and buffers
- **Depends on:** T0.1, Part 2
- **Goal:** Module owns a split RPC transaction id.
- **Files:** `config.h`, `post_config.h`
- **Steps:**
  1. Define `SPLIT_TRANSACTION_IDS_MODULE_BK_SPLIT_FLASH` and `RPC_ID_*` command bytes.
  2. Set `RPC_M2S_BUFFER_SIZE`/`RPC_S2M_BUFFER_SIZE` to the small control size (≥16).
- **Verify:** Build succeeds; `SPLIT_TRANSACTION_RPC` is auto-defined (generated `community_post_config.h`).
- **Done when:** Both halves build with the RPC id present.

### T3.2 — Define the control protocol structs
- **Depends on:** T3.1
- **Goal:** Shared, versioned message definitions.
- **Files:** `bk_split_flash_proto.h` (new)
- **Steps:**
  1. Define command enum (`ARM`, `ARMED`, `BEGIN`, `CHUNK`, `ACK`, `NACK`, `VERIFY`, `VERIFIED`, `COMMIT`, `DONE`, `ABORT`) and payload structs.
  2. Add a protocol version byte.
- **Verify:** Compiles on both halves; sizes fit the RPC buffer.
- **Done when:** Structs usable by both sides.

### T3.3 — Slave-side RPC handler
- **Depends on:** T3.2
- **Goal:** Slave responds to control commands (log-only for now, except ARM/ABORT).
- **Files:** `bk_split_flash_rpc.c` (new), `rules.mk`
- **Steps:**
  1. Implement `bksf_rpc_handle()` and register via `transaction_register_rpc()`.
  2. On `ARM`: call `bksf_arm()` and reply `ARMED` (only if T0.1 permits). On `ABORT`: reply and stay recoverable.
- **Verify:** Both halves log the handshake.
- **Done when:** ARM/ARMED/ABORT observed end-to-end on hardware.

### T3.4 — Master-side send helpers + retry/timeout
- **Depends on:** T3.3
- **Goal:** Master drives the handshake with bounded retries.
- **Files:** `bk_split_flash_rpc.c`, `bk_split_flash.c`
- **Steps:**
  1. Add `bksf_send_cmd()` helpers using `transaction_rpc_send()`/`transaction_rpc_exec()`.
  2. Add per-step timeout/retry in `housekeeping_task_bk_split_flash()`.
- **Verify:** A dropped/unanswered command retries and eventually ABORTs cleanly.
- **Done when:** Timeout/retry behavior confirmed.

**Part 3 exit criteria:** Control handshake works between halves; slave arm/abort is safe and recoverable (or the A/B fallback is chosen if T0.1 was "no").

---

## Part 4 — Slave update via const blob

> Goal: master updates the slave first, then verifies it rejoins. Still no host upload.

### T4.1 — Implement slave ARM→COPY→COMMIT path
- **Depends on:** Part 3
- **Goal:** Slave runs the full update from the const-blob image.
- **Files:** `bk_split_flash_rpc.c`, `bk_split_flash_flash.c`
- **Steps:**
  1. Slave handles `BEGIN` (validate size ≤ staging), `VERIFY` (whole-image CRC), `COMMIT`.
  2. Reuse the Part 2 RAM-resident routines on the slave side.
- **Verify:** Slave accepts commands and, on COMMIT, reboots into the new build.
- **Done when:** Slave self-flash from blob works.

### T4.2 — Master orchestration: update slave first, verify rejoin
- **Depends on:** T4.1
- **Goal:** Master sequences the slave update and confirms the slave is back.
- **Files:** `bk_split_flash.c`
- **Steps:**
  1. State machine: ARM(slave) → BEGIN → (blob copied/staged) → VERIFY → COMMIT → wait for `DONE`/rejoin within timeout.
  2. On timeout: ABORT and report.
- **Verify:** Slave updates and reappears as slave (matrix works) within the timeout.
- **Done when:** Rejoin confirmed; power-cut matrix repeated for the slave path.

### T4.3 — Slave-path power-cut matrix and link-integrity test
- **Depends on:** T4.2
- **Goal:** Prove the slave path is safe under interruption and wire corruption.
- **Steps:**
  1. Power-cut at armed / mid-COPY / mid-COMMIT on the slave.
  2. Corrupt a chunk on the wire; slave must NACK and master must retry, never commit.
- **Verify:** Every cut lands in `RPI-RP2` or the correct app; corruption never commits.
- **Done when:** Results recorded.

**Part 4 exit criteria:** One trigger updates the slave; it rejoins; interruption and corruption are safe.

> **Implementation note (2026-11-11):** the update now **erases the EEPROM of the
> half being flashed** (`bksf_erase_eeprom()` at the start of
> `bksf_apply_and_reset()`), so settings and stored state — including argos's
> persisted `build_id` — are regenerated from the new image. This is what makes
> the two halves agree again after an update. Cost: ~3 s (256 KiB / 4 KiB sectors)
> per flashed half, which is why the rejoin timeout is 15 s.
>
> **IRQ scope (2026-11-11):** `bksf_apply_and_reset()` must hold interrupts off
> for the *entire* app copy, not just around each flash op — the vector table and
> ISRs live in the flash being erased, so an IRQ between sector writes faults into
> erased code (the half hangs and never rejoins). The EEPROM wipe still toggles
> per sector, which is safe because the app is intact then. Long-running work
> (sector flush, CRC) must also stay out of the RPC callback: the flush runs in
> slave housekeeping with master-side pacing (`SYNC` confirms durability), and the
> image CRC is accumulated incrementally per chunk so VERIFY is instant.
>
> **Peer image must be built from the same code (2026-11-11):** a peer built
> before the module registered its split transaction id had a different
> `NUM_TOTAL_TRANSACTIONS`, so `split_shared_memory` differed between the halves
> and the link garbled — the freshly-updated half looked dead (no keys, no RGB) in
> either master/slave role. The blob must be regenerated from a build of the
> *current* code so only `BKSF_TEST`/`BKSF_VERSION` differ.

---

## Part 5 — Master self-flash last (both halves, one trigger)

### T5.1 — Wire master self-flash as the final step
- **Depends on:** Part 4, T2.5
- **Goal:** After the slave is done, the master self-flashes and resets.
- **Files:** `bk_split_flash.c`
- **Steps:**
  1. After slave `DONE`, run the master ARM → COPY → COMMIT path from Part 2.
  2. Ensure the trigger's state survives until the final reset.
- **Verify:** Both halves updated from a single trigger.
- **Done when:** End-to-end both-halves update confirmed.

### T5.2 — Full both-halves power-cut matrix
- **Depends on:** T5.1
- **Goal:** Prove the combined path has no brick window.
- **Steps:** Cut power at every state of both halves' ARM/COPY/COMMIT.
- **Verify:** Every cut lands in a recoverable state.
- **Done when:** Matrix recorded.

**Part 5 exit criteria:** The core feature works: one trigger, both halves, fail-safe. This is the v1 milestone.

---

## Part 6 — Host → master upload (Option B, no per-target rebuild)

### T6.1 — Add the `via_command_kb()` host channel
- **Depends on:** Part 5
- **Goal:** Host can send an image to the master, as `argos` owns its host channel.
- **Files:** `bk_split_flash.c`
- **Steps:**
  1. Handle a host command id for "begin upload".
  2. Prefer `via_command_kb()` over defining the user `raw_hid_receive` symbol.
- **Verify:** Host command observed; no conflict with `argos`.
- **Done when:** Channel established.

### T6.2 — Receive + stage the image, then distribute
- **Depends on:** T6.1
- **Goal:** Master buffers the uploaded image in staging, then runs the Part 4/5 flow from staging.
- **Files:** `bk_split_flash_pack.c` (new), `rules.mk`, `bk_split_flash.c`
- **Steps:**
  1. Decode incoming packets into the staging region with per-block CRC.
  2. Bounds-check staging before ARM.
- **Verify:** A host-uploaded image updates both halves with no rebuild of the module.
- **Done when:** No per-target rebuild needed to update.

**Part 6 exit criteria:** Update driven entirely from the host; module is generic across targets.

> **Implementation note (2026-11-11) — PART 6 DEFERRED AND NOT STARTED.** A partial
> implementation was attempted and then **reverted** (working tree is back to the
> Part 5 commit); resume at Part 7 first. The finding below is kept so the work can
> be redone quickly later. A module-owned
> `via_command_kb()` is not possible — argos already defines it (strong symbol), and
> two definitions would collide at link time. Instead the module registers a sub-command in argos's
> existing router (`argos_id_bk_split_flash = 0x1C`), exactly like `bk_led_matrix`:
> `argos.c` includes `bk_split_flash_pack.h` and calls `bksf_dispatch_command()` from
> its `switch` (guarded by `#ifdef COMMUNITY_MODULE_BK_SPLIT_FLASH_ENABLE`). This is
> the design doc's "as argos owns its host channel" taken literally. A raw-HID report
> is `data[0]=0x90` (ARGOS_CMD_PREFIX), `data[1]=0x1C`, `data[2]=bksf_host_cmd`,
> `data[3..]`=payload (~29 bytes/report). T6.2 therefore needs a **host-side sender**
> (none exists in-repo) and a **staging-based update start** (today
> `bksf_self_update_from_peer()` stages the const blob first; a host upload is
> already in staging, so the distribution must read from staging directly).

---

## Part 7 — Bulk USART transport (throughput)

### T7.1 — Dedicated length-prefixed framing with per-chunk CRC32
- **Depends on:** Part 6
- **Goal:** Move image bytes master→slave efficiently, bypassing the 32 B transaction buffer.
- **Files:** `bk_split_flash_pack.c`, `bk_split_flash_rpc.c`
- **Steps:**
  1. Add the bespoke frame on the existing USART peripheral.
  2. Per-chunk CRC32 + NACK/retry.
- **Verify:** Chunks transfer and verify; corruption triggers retry.
- **Done when:** Reliable chunk transport demonstrated.

### T7.2 — Quiesce normal split traffic during the bulk phase
- **Depends on:** T7.1
- **Goal:** Prevent frames interleaving with matrix/RPC traffic.
- **Steps:**
  1. Suspend normal split transactions for the bulk phase; resume deterministically after.
  2. Scope the suspension tightly.
- **Verify:** Throughput target met; normal split traffic resumes cleanly; matrix scanning unaffected.
- **Done when:** Quiesce/resume proven stable.

**Part 7 exit criteria:** Bulk transport is fast and reliable, and the keyboard returns to normal operation after an update.

---

## Part 8 — Combined-UF2 packaging

### T8.1 — Verify the ROM mass-storage writer accepts staging addresses
- **Depends on:** T0.2
- **Goal:** Confirm a UF2 block targeting the staging address is written by the ROM writer.
- **Steps:**
  1. Build a combined UF2 (app at `0x10000000`, peer image at the staging address).
  2. Drag to `RPI-RP2`; confirm staging bytes land where expected.
- **Verify:** Staging region contains the peer image after flashing.
- **Done when:** Address acceptance confirmed.

### T8.2 — Userspace post-build script using `util/uf2conv.py`
- **Depends on:** T8.1
- **Goal:** Produce one combined UF2 per build.
- **Steps:**
  1. Add a userspace post-build step that concatenates the app image and the peer image.
  2. Place the peer at the staging address.
- **Verify:** A single drag-and-drop updates both halves via the running module.
- **Done when:** One-file delivery works.

**Part 8 exit criteria:** Single-file, single-drag update of both halves.

### Part 8 recon — resolved decisions (2026-11-12)

Settled so T8.1/T8.2 don't re-open them. Build-system facts (post-build hook,
artifacts, staging start, `uf2conv.py`) live in `agents/working-notes.md`.

1. **Peer input for the per-build step — sidecar `.bin`.** `gen_peer_image.py` also
   writes a raw sidecar of exactly the embedded image (`bk_split_flash_peer_image.bin`
   next to the generated `.c`); the post-build step packages that sidecar, so the
   packaged peer is byte-identical to the one embedded in the build. (Not a make
   variable, not parsing the `.c`.)
2. **Runtime use of the staged peer — keep the embedded blob.** A raw peer at
   staging has no length/CRC header, so the running module learns size/CRC from the
   embedded const blob. The staged bytes are identical, so one-file delivery works.
   A staging header/footer + a staging-based update path is a possible later
   enhancement, not required now.
3. **Combined output naming — separate `_combined.uf2`.** Write
   `<TARGET>_combined.uf2` in the userspace root (do **not** overwrite the normal
   `<TARGET>.uf2`); optionally also copy it to `bksf_images/`.
4. **Bounds check — `__flash_binary_end` only.** The post-build script needs just
   `__flash_binary_end` (sector-aligned up) for the peer's load address; it cannot
   read `WEAR_LEVELING_RP2040_FLASH_BASE` (a compile-time macro, not an ELF symbol).
   The upper bound is guarded at runtime by `bksf_get_staging_window()`.
5. **T8.1 verification — new `BKSF_TEST_STAGING` selector.** It reads the staging
   region and compares it to the embedded peer (size + CRC), logging match/mismatch;
   that is how T8.1 is verified after dragging the combined UF2.
6. **When the step runs — only when a peer is present.** The post-build step runs
   only for carrier builds (peer sidecar/blob present); otherwise it is skipped.

### Part 8 bring-up: procedure — dilemma 4x6 procyon (2026-11-12)

Target: **`bastardkb/dilemma/4x6_4_procyon:vendor`** (RP2040, default 2 MB layout,
same image on both halves). T8.1 confirms the ROM mass-storage writer accepts a UF2
block at the staging address; T8.2 produces the combined image.

**Images (`bksf_images/`, built 2026-11-12 from the Part 8 code):**

| image | build | what it is |
|---|---|---|
| `bksf_dilemma_T8_peer.uf2` | `DUMP` + `BKSF_VERSION "0.1.0-peer"` | peer regenerated from the Part 8 code (192512 B) |
| `bksf_dilemma_T8_staging.uf2` | `BKSF_TEST_STAGING` + peer embedded | app-only; a normal drag writes the app and **nothing** to staging (391168 B) |
| `bksf_dilemma_T8_staging_combined.uf2` | same build, packaged by T8.2 | combined: app **+** peer staged at `0x10030000` (587776 B) |

**T8.1 — ROM writer accepts the staging address.** Stage a combined image and let
the running app verify it:

1. Drag `bksf_dilemma_T8_staging_combined.uf2` onto `RPI-RP2` (double-tap reset →
   the drive). The ROM writes the app **and** the peer block in one pass.
2. Link the halves, USB into the master, `qmk console`, press `BKSFTRG`.
3. Expected **master** log:
   ```
   bksf: staging verify: window start=0x30000 bytes=1638400 (XIP 0x10030000)
   bksf: staging peer len=98304 crc staged=0xD41659BA peer=0xD41659BA diff=0 -> MATCH
   ```
   A `MISMATCH` (with `diff != 0`) means the ROM did **not** write the staging
   block — the T8.1 result to investigate.

**Control (staging not written).** On a half whose staging region is still erased,
drag `bksf_dilemma_T8_staging.uf2` (app-only) and press `BKSFTRG`: the same line
reports `-> MISMATCH` with a large `diff`, confirming the check is really reading
the staged bytes. (Reflashing the combined afterwards restores `MATCH`.)

**T8.2 — one-file delivery.** The combined image *is* the T8.2 output: one drag
leaves the master running the carrier build with the peer already in staging, so the
normal trigger (Part 5/7 flow) updates both halves from the embedded blob. The
staged copy is currently redundant (recon decision 2 keeps the embedded blob as the
size/CRC source); T8.2 verifies the packaging and address, not a new runtime path.

**Rebuild recipe** (regenerate the peer from current code, then the carrier):
```bash
# 1. peer from current code: remove the blob, set DUMP + BKSF_VERSION "0.1.0-peer"
#    in keyboards/bastardkb/dilemma/4x6_4_procyon/keymaps/vendor/config.h, then:
rm -f modules/bastardkb/bk_split_flash/bk_split_flash_peer_image.{c,bin}
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
cp -f bastardkb_dilemma_4x6_4_procyon_vendor.uf2 bksf_images/bksf_dilemma_T8_peer.uf2

# 2. blob + sidecar from that peer:
python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py bksf_images/bksf_dilemma_T8_peer.uf2

# 3. carrier: set #undef/#define BKSF_TEST BKSF_TEST_STAGING in the same config.h, then:
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
#    -> <userspace root>/bastardkb_dilemma_4x6_4_procyon_vendor_combined.uf2
```
Remove the generated `bk_split_flash_peer_image.{c,bin}` and restore the keymap
`config.h` afterwards so normal builds carry no 98304 B blob and emit no combined
file. The module `config.h` is included *before* the keymap `config.h`, so every
override must `#undef` first (`-Werror` redefinition).

---

## Part 9 — Guard rails and recovery

### T9.1 — Version/CRC guard on the image header
- **Depends on:** Part 5
- **Goal:** Refuse to distribute an image that doesn't match the master's expectations.
- **Steps:** Add a version/CRC header; validate before ARM.
- **Verify:** A mismatched image is rejected before ARM.
- **Done when:** Mismatch rejection confirmed.

### T9.2 — "Flash pending" indicator
- **Depends on:** Part 5, T0.5
- **Goal:** Show update state on LED/OLED.
- **Verify:** Indicator reflects IDLE/ARMED/COPY/COMMIT/error.
- **Done when:** Observable on hardware.

### T9.3 — Documented recovery procedure
- **Depends on:** Part 0 (T0.1)
- **Goal:** A written, tested procedure to recover a bricked half.
- **Steps:** Document BOOTSEL access (and the A/B fallback if the slave is USB-less); test it.
- **Verify:** Procedure successfully recovers a deliberately armed half.
- **Done when:** Procedure written and validated.

**Part 9 exit criteria:** The feature fails safe and has a tested recovery path.

---

## Part 10 — Bootmagic trigger (start the update at power-on)

> Goal: start the update by holding a key at boot instead of pressing a runtime
> keycode. This works even when argos blocks keys (`Not synced, ignoring
> keypresses`, `argos.c:602`, because `process_record_argos` returns false and
> `process_record_modules` short-circuits) and needs no `BKSFTRG` in the keymap.
> Downstream (ARM → chunks → flush → VERIFY → COMMIT, EEPROM wipe) is
> trigger-agnostic and needs no change.

### T10.1 — Boot-time trigger hook + transport gate
- **Depends on:** Part 5
- **Goal:** A held key at power-on drives the same state machine, once the split link is up.
- **Files:** `bk_split_flash.c`, `config.h`
- **Steps:**
  1. Define the trigger key (`BKSF_BOOTMAGIC_ROW` / `BKSF_BOOTMAGIC_COL`, defaulting to the same physical key as the `BKSFTRG` alias), overridable per keymap.
  2. In `keyboard_post_init_bk_split_flash` (the matrix is initialised by then) `matrix_scan()` and, if the key is held, set a pending flag and log it. This runs before argos's `process_record`, so argos cannot suppress it.
  3. Gate the first ARM on `is_transport_connected()`: at boot the link may not be up yet, and `transaction_rpc_exec` returns false, which would burn the 3 retries into an ABORT. Wait instead. Apply the same gate to the runtime keycode path.
  4. Optionally also override QMK's weak `bootmagic_lite()` / define `bootmagic_user()` so the key can be set through QMK's own bootmagic config.
- **Verify:** Holding the key at power-on starts the update (the master log shows the full ARM sequence); a normal boot with the key not held does nothing.
- **Done when:** A bootmagic-started update reaches `slave rejoined -> DONE`.

### T10.2 — One-shot guard (no re-trigger after the master self-flash reboot)
- **Depends on:** T10.1
- **Goal:** A still-held key across the master's final self-flash must not start a second update.
- **Steps:**
  1. Track a key *edge*, not a level: require release-then-rehold, or
  2. Set a one-shot marker before the master self-flash and ignore the held key once on the next boot, then clear it. Note the update wipes the module EEPROM datablock, so use CPU backup/RTC RAM, or set the marker *after* the wipe.
- **Verify:** With the key held across the master's self-flash reboot, exactly one update runs.
- **Done when:** No runaway re-trigger loop.

### T10.3 — Trigger parity and docs
- **Depends on:** T10.1, T10.2
- **Goal:** Both triggers behave identically.
- **Steps:** Confirm the keycode and the bootmagic path drive the same state machine and produce the same logs; document the trigger key and the bootmagic option.
- **Verify:** One run per trigger, same result.
- **Done when:** Documented.

### T10.4 — Retire the bring-up escape hatch and the runtime key
- **Depends on:** T10.1–T10.3
- **Goal:** Ship the real behavior: the boot-time key is the only trigger, and a
  mismatched half is not silently usable.
- **Steps:**
  1. **Brick typing on mismatch.** Remove `ARGOS_DISABLE_BUILD_ID_GATE` from the
     keymap `config.h` so argos's `process_record_argos` blocks keys when the two
     halves' `build_id`s differ (`Not synced, ignoring keypresses`). The red
     mismatch indication already runs independently of that flag, so no RGB change
     is needed.
  2. **Get rid of the special key.** Remove the `BKSFTRG` keycode from the keymap
     (the base-layer top-right key returns to its normal function); the module no
     longer needs a runtime key.
  3. **Only support bootmagic flashing.** The update is started solely by holding
     the bootmagic key at power-on (T10.1). Drop the keycode path from the docs and
     any test procedure that relies on pressing a key at runtime.
- **Interim state (before T10.4):** `ARGOS_DISABLE_BUILD_ID_GATE` stays defined so
  typing keeps working during bring-up, but the red mismatch indication is already
  live — a reflashed-only-one-side board flashes red without bricking. This is the
  current behavior as of 2026-11-11.
- **Verify:** With the two halves on different builds, keys are blocked and the RGB
  flashes red; a matching pair types normally. With the key not held at power-on,
  nothing happens; held at power-on, exactly one update runs.
- **Done when:** The escape hatch and `BKSFTRG` are gone and the bootmagic trigger
  is the only way to start an update.

**Part 10 exit criteria:** An update can be started by holding a key at power-on,
safely and exactly once; the runtime keycode and the `ARGOS_DISABLE_BUILD_ID_GATE`
escape hatch are retired, so a mismatched half is blocked and flashes red.

---

## Definition of done (whole plan)

- One trigger (keycode or host command) updates both halves.
- Interrupting any update step leaves every half in `RPI-RP2` or the correct app — never a brick.
- No changes to `bastardkb-qmk`.
- Mismatched images are rejected before arming.
- A tested recovery procedure exists for a half with no reachable USB.

## Test matrix (reused across parts)

| Test | Applies to | Assertion |
|---|---|---|
| Power-cut matrix | T2.4, T4.3, T5.2 | Every cut → `RPI-RP2` or correct app |
| CRC unit test | T1.5 | Computed CRC == ROM expectation for a known boot2 |
| Bounds test | T6.2 | Image too large for staging rejected before ARM |
| Link-integrity test | T4.3, T7.1 | Corrupt chunk → NACK/retry, never commit |
| Rejoin test | T4.2 | Slave reappears as slave within timeout |
| Mismatch test | T9.1 | Wrong version/CRC rejected before ARM |

## Risk register (maps to design doc §11)

| Risk | Mitigated by |
|---|---|
| USB-less slave (blocking) | T0.1; A/B fallback if "no" |
| Commit erases sector 0 | T2.3, T2.4, T5.2 |
| Arming window exposure | T2.2, T2.4 |
| Quiescing split traffic | T7.2 |
| Power during slave write | T4.3 |
| Watchdog/double-tap race | T9.1, T9.2 |
| Bootmagic re-trigger after master reboot | T10.2 (one-shot / edge guard) |
| Staging bounds (argos 256 KiB) | T0.2, T6.2 |
| `raw_hid_receive` ownership | T6.1 (use `via_command_kb()`) |

---

## Part 0 results (recon + decisions)

> Recorded 2026-11-11. Values below come from a real build of
> `bastardkb/skeletyl/splinktegrated_rev1:vendor`; the module computes all of
> them at runtime from linker symbols and never hard-codes them.

**T0.2 — flash layout (skeletyl / splinktegrated_rev1):**

| Quantity | Value | Source |
|---|---|---|
| Flash region (`flash1`) | `0x200000` (2 MiB) | `RP2040_FLASH_TIMECRIT.ld:23` (linker default) |
| `__flash_binary_end` | `0x10011888` (offset `0x11888`) | build `.map` |
| `WEAR_LEVELING_BACKING_SIZE` | `262144` (256 KiB) | `argos/config.h:43-44` (confirmed in `wear_leveling_rp2040_flash.o`) |
| `WEAR_LEVELING_RP2040_FLASH_BASE` | `0x1C0000` | derived (`0x200000 - 0x40000`); confirmed in object disassembly |
| Staging start (sector-aligned) | `0x12000` | `align_up(__flash_binary_end, 4096)` |
| **Staging bytes** | **`0x1A0000` = 1,703,936 (≈1.62 MiB)** | `eeprom_base - staging_start` |

*(skeletyl row predates the 4 MB / 131072 fixes below; skeletyl needs the same
per-target overrides to reach ~3.7 MiB staging.)*

**T0.2 — charybdis / 4x6 / splinktegrated_rev1** (build target
`bastardkb/charybdis/4x6/splinktegrated_rev1:vendor`), **after the 4 MB fix
below**:

| Quantity | Value |
|---|---|
| Flash region (`flash1`) | `0x400000` (4 MiB) |
| `__flash_binary_end` | `0x100141CC` (offset `0x141CC`) |
| `WEAR_LEVELING_BACKING_SIZE` | `0x40000` (262144 = logical × 4) |
| `WEAR_LEVELING_RP2040_FLASH_BASE` | `0x3C0000` |
| Staging start | `0x15000` |
| **Staging bytes** | **`0x3AB000` = 3,846,144 (≈3.67 MiB)** |

**EEPROM backing size: logical × 4 (262144).** `argos/config.h` is the single
source of truth — `WEAR_LEVELING_LOGICAL_SIZE 65536`,
`WEAR_LEVELING_BACKING_SIZE (WEAR_LEVELING_LOGICAL_SIZE * 4)`. The `eeprom`
block `argos/qmk_module.json` used to carry was inert (QMK only maps
`eeprom.wear_leveling.backing_size` from a keyboard `info.json`) and has been
removed, so the two can no longer disagree. The 4× ratio (vs. the driver's
default 2×) spreads writes over more flash for longer life at the same logical
size.

**Found: the build assumed 2 MB flash — and fixing it wrong bricked a half.**
`PICO_FLASH_SIZE_BYTES` comes from the ChibiOS pico bindings →
`boards/pico.h` (`2 * 1024 * 1024`); nothing overrides it, so wear-leveling sat
at `0x1C0000` and the linker region defaulted to 2 MB, wasting the top half of
the 4 MB chip. `EXTERNAL_FLASH_SIZE` is unrelated (it only feeds the external
SPI `flash` driver).

Correct fix (per-target, userspace) — all three knobs must agree:
- keymap `config.h`: `#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)` (the
  pico-sdk board header is `#ifndef`-guarded, so this wins). Wear-leveling
  derives `WEAR_LEVELING_RP2040_FLASH_SIZE`/`_BASE` from it.
- keymap `rules.mk`: `LDFLAGS += -Wl,--defsym=FLASH_LEN=0x400000`.

**Brick incident (2026-11-11):** an earlier attempt set only
`WEAR_LEVELING_RP2040_FLASH_SIZE` (4 MB) while `PICO_FLASH_SIZE_BYTES` stayed
2 MB. Wear-leveling's first erase, `flash_range_erase(0x3C0000, 0x40000)`, fails
pico-sdk's `hard_assert(offs + count <= PICO_FLASH_SIZE_BYTES)` (active even
with `-DNDEBUG`; `pico/assert.h:38-46`) → `hard_assertion_failure()` → `panic()`
→ `chSysHalt()` at boot. Symptom: no keypresses, `hid_listen` never attaches,
"USB device not recognized". **Recovery:** hold BOOTSEL while plugging USB →
`RPI-RP2` → reflash.

**Anti-brick guard:** `bk_split_flash_flash.c` now carries a `_Static_assert`
that `WEAR_LEVELING_RP2040_FLASH_BASE + WEAR_LEVELING_BACKING_SIZE <=
PICO_FLASH_SIZE_BYTES` (guarded by `#if defined(...)`), so a mismatched
flash-size config fails the **build** instead of the board. Verified both ways.

Verified: `__flash1_size__ = 0x400000`, erase base `= 0x3C0000`, guard rejects
the 2 MB/4 MB mismatch. Side effect: the EEPROM region moves, so stored settings
(incl. VIA dynamic keymap) reset once.

⚠️ Residual assumption: the QSPI chip really is 4 MB. If it is not, this config
must be reverted (a 2 MB part would alias addresses ≥ `0x200000`). Confirm the
chip marking (e.g. `W25Q32` = 4 MB, `W25Q16` = 2 MB) before trusting the 4 MB
config.

**Gotcha found on hardware (T1.4):** `__flash_binary_end` is an XIP *address*
(`0x10014190`) while `WEAR_LEVELING_RP2040_FLASH_BASE` is a flash *offset*
(`0x1C0000`). The first cut compared them directly, yielding `staging start
=0x10015000 bytes=0`. Fixed: `bksf_app_end_offset()` converts the XIP address to
an offset before the bounds math, and `bksf_get_staging_window()` returns the
window in offset space (the space `flash_range_erase`/`program` use). Part 2 must
keep every staging bound in offset space.

Observation: the physical flash chip is 4 MiB (`EXTERNAL_FLASH_SIZE`), but QMK's
RP2040 platform leaves `PICO_FLASH_SIZE_BYTES` at 2 MiB, so wear-leveling sat at
`0x1C0000` and the upper 2 MiB was unused. Addressed by the per-target 4 MB
override below (charybdis verified at `0x400000`); the skeletyl row above is the
pre-fix baseline.

**T0.1 — slave USB reachable: YES (2026-11-11).** The slave (left) half exposes
USB and enters `RPI-RP2` via double-tap reset, using the stock build (no extra
flashing needed — the double-tap hook is keyboard-level). Direct slave arming is
therefore viable, and the app-region-only A/B fallback (design doc §11) is **not**
needed. This unblocks T3.1/T4.1.

**T0.3 — first milestone:** (a) single-half master self-flash first (Parts 1–2),
then both halves (Parts 3–5). Confirmed.

**T0.4 — data plane:** v1 = Option A (const blob compiled into the module);
end state = Option B (host→master over raw HID via `via_command_kb()`), then
master→slave over a dedicated USART frame. Staging fits per T0.2. Confirmed.

**T0.5 — trigger UX + guard:** trigger = module keycode `BKSFTRG` for bring-up
(host command added in Part 6); "flash pending" indicator on LED/OLED (Part 9);
version/CRC guard in the image header (Part 9). Confirmed.

## Task log

> Append one row per completed task. Keep raw observed values; they feed later tasks.

| Task | Date | Result / observed values | Verified on |
|---|---|---|---|
| T0.1 | 2026-11-11 | **RESOLVED: yes.** The slave (left) half exposes USB and enters `RPI-RP2` via double-tap reset — no extra flashing needed (double-tap is a keyboard-level feature already in the stock build). Direct slave arming is viable; the §11 A/B fallback is not required. | HW (user) |
| T0.2 | 2026-11-11 | skeletyl/splinktegrated_rev1: flash=0x200000, app_end=0x10011888, eeprom_base=0x1C0000, staging_start=0x12000, staging_bytes=0x1A0000 (1,703,936). BACKING=262144. | build + `.map` + object disasm |
| T0.2b | 2026-11-11 | 4 MB flash fix: `PICO_FLASH_SIZE_BYTES (4 MiB)` + `FLASH_LEN=0x400000`; charybdis flash=0x400000, eeprom_base=0x3C0000, staging_bytes=0x3AB000 (3,846,144). Setting only `WEAR_LEVELING_RP2040_FLASH_SIZE` bricked a half (`hard_assert` in `flash_range_erase` → `chSysHalt`); added a `_Static_assert` guard. | build + `.map` + object disasm; **HW confirmed** (charybdis 4x6 right half, no brick) |
| T0.3 | 2026-11-11 | milestone (a): single-half master self-flash first. | n/a |
| T0.4 | 2026-11-11 | v1 = Option A (const blob); end state = Option B (host→master raw HID). Peer image fits staging. | T0.2 |
| T0.5 | 2026-11-11 | trigger = keycode `BKSFTRG`; pending indicator = LED/OLED (P9); version/CRC guard in header (P9). | n/a |
| T1.1 | 2026-11-11 | `modules/bastardkb/bk_split_flash/qmk_module.json` discovered; schema-valid. | compile |
| T1.2 | 2026-11-11 | skeleton (`config.h`/`post_config.h`/`rules.mk`/`bk_split_flash.c`) builds and links; UF2 produced. | compile |
| T1.3 | 2026-11-11 | `BK_SPLIT_FLASH_TRIGGER`/`BKSFTRG` generated in `QK_COMMUNITY_MODULE` range; `process_record_bk_split_flash` handles it and logs. | **HW confirmed** (charybdis 4x6 right half): `trigger pressed (state=0)` |
| T1.4 | 2026-11-11 | `bksf_dump_flash_facts()` logs boot2 CRC word, `__flash_binary_end` (XIP + offset), eeprom base, staging window. HW run exposed the XIP-vs-offset bug (see Part 0 results); fixed via `bksf_app_end_offset()` + `bksf_get_staging_window()`. | **HW confirmed** (charybdis 4x6 right half): `__flash_binary_end` offset `0x141CC`, `eeprom_base=0x3C0000`, `staging bytes=3846144`. Boot-time dump is lost (races the HID console); trigger re-dumps. |
| T1.5 | 2026-11-11 | bit-reversed CRC32 reproduced; computed == stored for all six `BOOT2_ROM` blobs and for the live page-0 word. | offline (Python); HW `stored == computed == rom word == 0xD58F0B07` |
| T2.1 | 2026-11-11 | `bksf_arm()` (snapshot page 0, zero CRC word, `flash_range_program` — bit-subset, no erase) + `bksf_is_armed()`. | **HW confirmed** (charybdis 4x6 right half): `arm OK, crc now=0x00000000 (armed=1)`, firmware stayed alive |
| T2.2 | 2026-11-11 | `BKSF_TEST_BOOTSEL`: arm then **plain reset** (`mcu_reset()`), so the ROM CRC check (not `reset_usb_boot`) decides → expect `RPI-RP2`. | **HW confirmed** (charybdis 4x6 right half): logged `arm OK; plain reset`, then disconnected and re-enumerated as the USB bootloader |
| T2.3 | 2026-11-11 | `bksf_commit_identical()`: RAM-resident (`.time_critical`), erase+reprogram sector 0 with identical bytes, `mcu_reset()`. | **HW confirmed** (charybdis 4x6 right half): `commit identical sector 0` → reset → same firmware booted (a subsequent press ran the test again) |
| T2.4 | 2026-11-11 | Interruption matrix: `BKSF_TEST_ARM` (armed), `BKSF_TEST_ERASE0` / `BKSF_TEST_PARTIAL0` (`bksf_leave_sector0_invalid()`, RAM-resident) for the deterministic mid-erase/mid-program sector-0 states, and the long-window `BKSF_TEST_UPDATE` path. | **HW confirmed** (charybdis 4x6 right half): every cut landed in `RPI-RP2` or booted the correct build; never unresponsive |
| T2.5 | 2026-11-11 | Self-flash from a distinct const blob: `bksf_stage_image()` (blob→staging), `bksf_arm()`, then RAM-resident `bksf_apply_and_reset()` (staging→app, sector 0 last) which never returns. Peer image auto-included by `rules.mk` (`-DBKSF_PEER_IMAGE_PRESENT`) + `tools/gen_peer_image.py`. **Gotcha:** first cut used weak fallback symbols in the same TU as the accessor, so GCC bound the pointer to the local 1-byte weak array and the strong blob was GC'd; fixed by making the accessor `extern` under `#ifdef BKSF_PEER_IMAGE_PRESENT`. | **HW confirmed** (charybdis 4x6 right half): clean run booted the peer build (`0.1.0-peer`); interrupted runs landed in `RPI-RP2` |
| T3.1 | 2026-11-11 | Registered `SPLIT_TRANSACTION_IDS_MODULE_BK_SPLIT_FLASH RPC_ID_BK_SPLIT_FLASH_CMD` in `config.h`. RPC buffers: QMK defaults (32) already fit `bksf_msg_t`. | compile: `SPLIT_TRANSACTION_RPC` auto-defined in `community_post_config.h`; id present in the generated `.inc` |
| T3.2 | 2026-11-11 | `bk_split_flash_proto.h`: versioned `bksf_msg_t` (32 B, `_Static_assert`ed) + command/status enums. | compile |
| T3.3 | 2026-11-11 | `bk_split_flash_rpc.c`: `bksf_rpc_handle()` registered via `transaction_register_rpc()`; handles ARM (→ `bksf_arm()`, reply ARMED + crc) and ABORT; `bksf_rpc_init()` called from `keyboard_post_init_bk_split_flash()`. | **HW confirmed:** slave logged `slave rpc cmd=1 -> 2 st=0`; master `slave ARMED (crc=0x00000000)` |
| T3.4 | 2026-11-11 | Master send helper `bksf_rpc_send()` + `bksf_start_handshake()` and a retry/timeout state machine in `housekeeping_task_bk_split_flash()` (3 tries, 250 ms each → clean ABORT). | **HW confirmed:** with the link down, master retried then logged `no ARMED after 3 tries -> ABORT` |
| T4.1 | 2026-11-11 | Slave handles BEGIN (size ≤ staging, CRC), CHUNK (buffer into the 4 KiB scratch, hash incrementally with `bksf_crc32_update`, flush via housekeeping), VERIFY (finalise the running CRC — no 84 KB read in the callback), COMMIT (→ `bksf_apply_and_reset`). Chunks never straddle a sector boundary. | **HW confirmed** (end-to-end with T4.2) |
| T4.2 | 2026-11-11 | Master state machine: ARM → BEGIN → chunks (paced) → per-sector flush wait (SYNC confirm) → VERIFY → COMMIT → wait for rejoin (15 s), else ABORT. `bksf_start_slave_update()`. | **HW confirmed:** full run reached `slave rejoined -> DONE`; the slave rebooted into the peer build |
| T4.2b | 2026-11-11 | **EEPROM wipe on update.** `bksf_apply_and_reset()` now calls `bksf_erase_eeprom()` first (RAM-resident, per-sector, IRQs toggled): erases the full wear-leveling region (`0x3C0000..0x400000`) so the freshly-flashed half regenerates settings *and its argos build id* from the new build. Applies to whichever half applies an image (slave in T4, master in T5). Verified in disassembly. | compile |
| T4.3 | 2026-11-11 | **DONE + HW-verified** (target: dilemma 4x6 procyon). **Link integrity:** protocol v2 per-chunk CRC32 (`bksf_msg_t.crc`); the slave checks it before buffering and NACKs on mismatch. Chunk ACKs are **idempotent** (report `rxp_off`); on a non-ACK the master **resyncs** (60 ms backoff + `rxp_off` probe) and either advances (reply lost) or re-sends (real NACK), bounded by `BKSF_CHUNK_ATTEMPT_MAX` (30) so a persistent fault aborts — a corrupt link never commits. The slave **defers the sector flush** a few ms so it can't race the master's reply read. **Power-cut matrix** (no new code, `BKSF_TEST_UPDATE_SLAVE`): A1 armed, A2 mid-COPY, A3 mid-COMMIT all land in `RPI-RP2`. Test injection `BKSF_CORRUPT_OFF`/`BKSF_CORRUPT_MODE`. Also: `bksf_abort()` disconnect hint, idle **peer watchdog**, argos `ARGOS_DISABLE_BUILD_ID_GATE` escape hatch. See "Part 4 bring-up" and "T4.3 guard rails". | **HW confirmed:** A1–A3 → `RPI-RP2`; B1 recovers to `slave rejoined -> DONE`; B2 aborts with no commit after 30 attempts |
| T5.1 | 2026-11-11 | Master self-flashes last: new `bksf_start_both_update()` + `BKSF_TEST_UPDATE_BOTH`; on `slave rejoined -> DONE` the REJOIN handler runs `bksf_self_update_from_peer_hold(BKSF_SELF_HOLD_MS)` (stage peer → ARM → COPY → COMMIT → reset), so one trigger updates both halves. `bksf_self_update_from_peer()` now delegates to the hold variant. | **HW confirmed** (dilemma 4x6 procyon): the combined flow runs end-to-end to the master's self-flash — M1 reached `master self-flash last` → stage → arm. Clean-run completion of the master's own apply (and M0/M2/M3) not separately logged (timing-hard; the master's apply is the already-proven T2.5 path) |
| T5.2 | 2026-11-11 | Combined power-cut injection `BKSF_SELF_HOLD_MS`: after the master arms its own boot2 (slave already done), hold (XIP on, app running) before applying, so a cut lands in the master's armed window deterministically. Mid-COPY/mid-COMMIT sampled by real cuts (sector-0 states proven deterministic in T2.4). Partial build `bksf_dilemma_J_both_hold.uf2`. | **HW confirmed** (dilemma 4x6 procyon): S2 mid-COPY and S3 mid-COMMIT slave cuts → `RPI-RP2`; the master aborted cleanly (`resync failed -> ABORT`, disconnect hint, `cmd=12` ABORT, peer-lost watchdog). M1 master armed-window cut (image J) → `RPI-RP2`. M0/M2/M3 not timed (assumed) |
| T6.1 | 2026-11-11 | Host channel (argos-routed: `argos_id_bk_split_flash` in `argos.h` + guarded dispatch in `argos_handle_command`, plus `bk_split_flash_pack.{c,h}`). **Reverted per user** — Part 6 is deferred and not started. See the "WHERE WE ARE" note for the design finding to reuse. | — (reverted; working tree back to the Part 5 commit) |
| T6.2 | — | **Deferred** with Part 6 (host flashing to be implemented later): staging decode + distribution from staging + host sender. | — |
| T7.1 | 2026-11-11 | **DONE + HW-verified** (target: dilemma 4x6 procyon). New `bk_split_flash_pack.{c,h}` carry a length-prefixed bulk frame (`magic`+`offset`+`len`+`crc32`+payload) delivered by **one** split transaction on the module's own id via `transport_execute_transaction()` — replacing the 4-transaction RPC path for chunks. The 32 B limit is lifted by enlarging `RPC_M2S_BUFFER_SIZE` to `16 + BKSF_BULK_CHUNK` (`=240`, the transaction size fields are `uint8_t` so a frame must fit 255 B; `_Static_assert`ed in `bk_split_flash_pack.c`). Chunk 20 B → 224 B (bounded by the 20 ms `SERIAL_USART_TIMEOUT`: 240 B ≈ 11.5 ms at 230400 8E2). Per-chunk CRC32 checked before buffering; NACK → master `resync` (unchanged T4.3 logic, now over bulk acks that also report `rxp_off`); the T4.3 injection now targets the image offset (chunk-size independent). Slave-side receive reuses the shared `bksf_rxp_accept()` core (also used by the legacy RPC CHUNK path); `bksf_rpc_handle` dispatches bulk vs control on `in_len` (240 vs 32). Images: `bksf_dilemma_T7_{slave_update,corrupt_recover,corrupt_persist,both_update}.uf2` + regenerated `bksf_dilemma_peer.uf2` (`bash /home/quinn/bksf_gen_images.sh`). | **HW confirmed** (dilemma 4x6 procyon): clean stream works; corruption cases not separately reported |
| T7.2 | 2026-11-11 | **DONE + HW-verified** (target: dilemma 4x6 procyon). Bulk phase quiesced/resumed at the module boundary: `bksf_bulk_quiesce()` on the BEGIN ack (data phase start) and `bksf_bulk_resume()` when the last sector is durably written (or on ABORT); while quiesced the master's idle peer watchdog is suspended and only the bulk data plane runs. Rationale (recorded): QMK's own matrix/RPC split transactions cannot be globally suspended from a module (no weak hook; `matrix_post_scan`/`transport_master_if_connected` are strong), and they cannot interleave with bulk frames anyway — the transport is master-initiated and single-threaded, and every bulk chunk is itself one atomic transaction. So the quiesce is the module-level gate plus the existing per-sector flush pacing; normal traffic resumes deterministically. | **HW confirmed** (dilemma 4x6 procyon): `bulk phase begin/end` logged, normal typing resumes |
| T10.4a | 2026-11-12 | **Interim argos change (prep for T10.4).** Decoupled the build-id gate: `ARGOS_DISABLE_BUILD_ID_GATE` now only suppresses the key block (`process_record_argos`); the red mismatch indication (`RGB_MATRIX_BREATHING` + `HSV_RED`) always runs (master `housekeeping_task_argos`, slave `argos_build_id_sync_handler`). Vendor keymap keeps the flag → reflashed-one-side board flashes red but still types. Red test pair `bksf_dilemma_mismatch_{A,B}.uf2` via `bash /home/quinn/bksf_mismatch_images.sh`. | compile; **HW confirmed** typing works on a mismatched pair (red indication not separately confirmed) |
| T8.1 | 2026-11-12 | `BKSF_TEST_STAGING` (10) + `bksf_verify_staging_peer()` (`bk_split_flash_flash.c`): reads the staging region at `bksf_get_staging_window()` and compares it byte-for-byte to the embedded peer (length + CRC32 + a differing-byte count), logging `MATCH`/`MISMATCH`. Read-only. Offline: staged sidecar == embedded blob, peer CRC32 (firmware algo) = `0xD41659BA`. | offline; **HW verification pending** (drag the combined UF2 → `RPI-RP2`, press `BKSFTRG`) |
| T8.2 | 2026-11-12 | Per-build combined UF2. New module **`post_rules.mk`** (the QMK post-build hook) adds target `bksf-combined-uf2` (prereq of `all`), and **`tools/make_combined_uf2.py`** emits `<TARGET>_combined.uf2` = app at `0x10000000` + peer at `0x10000000 + staging_start`, where `staging_start` = `__flash_binary_end` (read from the ELF `.symtab`) rounded up to 4096 B. Peer comes from the new raw sidecar `bk_split_flash_peer_image.bin` (written by `gen_peer_image.py` alongside the `.c`, byte-identical to the embedded blob). Runs only when blob+sidecar are present; no-op otherwise; the normal `<TARGET>.uf2` is never touched. Offline: combined 1148 blocks / 587776 B; app region == normal UF2 (195584 B); staged peer == sidecar; family 0xE48BFF56 preserved. `__flash_binary_end=0x1002FBE4` → staging offset `0x30000`. | offline (combined UF2 parsed + regions compared); **HW verification pending** |
| T9.1 | | | |
| T9.2 | | | |
| T9.3 | | | |
| T10.1 | | | |
| T10.2 | | | |
| T10.3 | | | |
| T10.4 | | | |

> **T8.2 note — the sidecar `.bin` is not a make prerequisite.** Listing it as one
> makes QMK's generic `%.bin: %.elf` pattern rule try to *build* it (linking a bogus
> `bk_split_flash_peer_image.elf`), which clobbers the sidecar and fails the build.
> It is intentionally not a prerequisite — the `$(wildcard ...)` gate ensures it
> exists, and any peer change relinks the app (the blob is compiled in), so the
> uf2/elf prerequisites already trigger a regeneration.
>
> **T8.2 note — a failed post-build step still exits 0.** When the recipe fails,
> `gmake` reports `Error 2` and the traceback is in the build log, but `qmk compile`
> still exits `0` (QMK/milc behaviour, not specific to this module — reproduced by
> hiding the tool). A missing `*_combined.uf2` is therefore the signal, not the exit
> code.
>
> **T8 note — rebuilds are never byte-identical.** QMK embeds a compile date/time
> string (`__DATE__`/`__TIME__`), so two builds of the same source differ by a few
> bytes in `.rodata` (observed: only the timestamp differed). Compare staged vs.
> embedded at runtime rather than byte-comparing two build outputs.

## Part 1 hardware verification checklist (gate before Part 2)

> Part 2 writes flash, so it must not start until Part 1 passes on hardware.
> Build/flash: `qmk compile -kb bastardkb/charybdis/4x6/splinktegrated_rev1 -km vendor -c`,
> flash via double-tap reset, then `qmk console`.

**If a flash bricks the half** (no keypresses / "USB device not recognized"): the
app is halted in `chSysHalt`, so QMK's double-tap-reset won't help. Hold the
**BOOTSEL** button while plugging in USB (or BOOTSEL + tap RESET) so it mounts as
`RPI-RP2`, then copy the UF2 across.

**WHERE WE ARE (2026-11-12, resume point):**
- **Parts 1–3 complete and HW-verified** (T1.1–T1.5, T2.1–T2.5, T3.1–T3.4).
- **Part 4: T4.1–T4.3 complete and HW-verified** — master streams the peer image to the
  slave (ARM → BEGIN → chunks → per-sector flush → VERIFY → COMMIT), the slave wipes its
  EEPROM, reboots into the peer, and rejoins (`slave rejoined -> DONE`). T4.3 added the
  link integrity + guard rails: protocol v2 per-chunk CRC32; chunk ACKs report `rxp_off`
  so a lost reply is **resynced** (backoff + probe) instead of stalling, bounded to 30
  attempts so a corrupt chunk `ABORT`s without committing; a slave **flush hold-off**;
  a `bksf_abort()` disconnect hint + idle **peer watchdog**; and an argos
  `ARGOS_DISABLE_BUILD_ID_GATE` escape hatch. HW verified on the **dilemma 4x6 procyon**:
  A1–A3 (slave power-cut matrix) → `RPI-RP2`; B1 recovers to `slave rejoined -> DONE`;
  B2 aborts with no commit. See "Part 4 bring-up" and "T4.3 guard rails".
- **Part 5 — HW-VERIFIED (2026-11-11, with noted gaps).** T5.1: after the slave
  rejoins, the master self-flashes the same peer image last, so **one trigger
  updates both halves**. New selector `BKSF_TEST_UPDATE_BOTH`; the REJOIN handler
  runs the Part 2 path (`bksf_self_update_from_peer_hold(BKSF_SELF_HOLD_MS)`: stage
  peer → ARM → COPY → COMMIT → reset). T5.2: injection `BKSF_SELF_HOLD_MS` holds the
  master armed (XIP on) after the slave rejoins so a cut lands in the armed window
  deterministically. **Results (dilemma 4x6 procyon):** S2 mid-COPY and S3 mid-COMMIT
  slave cuts → `RPI-RP2`; the master aborted cleanly (`resync failed -> ABORT` +
  disconnect hint + peer-lost watchdog); M1 master armed-window cut (image J) →
  `RPI-RP2`. **Gaps:** the clean-run completion of the master's own apply and
  M0/M2/M3 were not separately logged (timing-hard; the master's apply is the
  already-proven T2.5 path). Images: `bksf_dilemma_I_both_update.uf2`,
  `bksf_dilemma_J_both_hold.uf2`, regenerated `bksf_dilemma_peer.uf2`. See "Part 5
  bring-up".
- **Part 6 (host flashing) — DEFERRED and NOT STARTED (user, 2026-11-11).** The
  intent is unchanged ("update from the computer, no per-target rebuild"), but it is
  postponed; a partial implementation (argos-routed host channel + `bk_split_flash_pack.{c,h}`
  + a host sender) was **reverted** — the working tree is back to the Part 5 state.
  Work resumes at **Part 7** first. **Design finding to reuse when Part 6 is picked
  up:** the channel cannot be a module-owned `via_command_kb()` — argos already
  defines that symbol (strong), so the module must be a sub-command of argos's
  raw-HID router (like `bk_led_matrix`), e.g. a new `argos_id_bk_split_flash` in
  `argos.h` + a guarded dispatch case in `argos_handle_command`, with report layout
  `data[0]=0x90`, `data[1]=<id>`, `data[2]=bksf_host_cmd`, `data[3..]`=payload.
- **Part 7 — DONE + HW-VERIFIED (2026-11-11, dilemma 4x6 procyon).** T7.1:
  image chunks now travel over a dedicated **length-prefixed bulk frame**
  (`bk_split_flash_pack.{c,h}`: `magic`+`offset`+`len`+`crc32`+payload) delivered by
  **one** split transaction (`transport_execute_transaction` on the module's own id)
  instead of the four-transaction RPC path. The 32 B limit is lifted by enlarging
  `RPC_M2S_BUFFER_SIZE` to `16 + BKSF_BULK_CHUNK` (`=240`); the transaction size
  fields are `uint8_t`, so a frame must fit 255 B (`_Static_assert`ed). Chunk size
  rose 20 B → **224 B** (bounded by the 20 ms `SERIAL_USART_TIMEOUT`: 240 B ≈ 11.5 ms
  at 230400 8E2). Per-chunk CRC32 is still checked before buffering; a NACK drives the
  **unchanged T4.3 resync** (bulk acks also report `rxp_off`); the corruption
  injection moved into `bksf_bulk_send_chunk`. Slave-side receive shares the
  `bksf_rxp_accept()` core with the legacy RPC CHUNK path; `bksf_rpc_handle` dispatches
  on `in_len` (bulk 240 vs control 32). T7.2: the bulk phase is quiesced at the module
  boundary (`bksf_bulk_quiesce()`/`bksf_bulk_resume()`; the idle peer watchdog is
  suspended while the data plane runs) — QMK's matrix/RPC transactions cannot be
  globally suspended from a module and cannot interleave with bulk frames anyway (the
  transport is master-initiated, single-threaded, and each bulk chunk is one atomic
  transaction), so the quiesce is the module-level gate plus the existing per-sector
  flush pacing. **Both halves and the peer image must be rebuilt from this code** (the
  enlarged `RPC_M2S_BUFFER_SIZE` changes `split_shared_memory`). See "Part 7 bring-up".
- **Argos build-id gate — DECOUPLED (2026-11-12).** The key block and the red
  mismatch indication are now separate concerns. `ARGOS_DISABLE_BUILD_ID_GATE`
  only suppresses the **key block** (`process_record_argos`); the red indication
  (`RGB_MATRIX_BREATHING` + `HSV_RED`) always runs on a `build_id` mismatch — on the
  master in `housekeeping_task_argos`, on the slave in
  `argos_build_id_sync_handler`. The vendor keymap keeps the flag defined, so a
  reflashed-one-side board **flashes red but still types**. T10.4 retires the flag
  (brick typing), the `BKSFTRG` key, and makes bootmagic the only trigger.
- **Part 8 — CODE COMPLETE, HW VERIFICATION PENDING (2026-11-12).** T8.1
  (`BKSF_TEST_STAGING` + `bksf_verify_staging_peer()`) and T8.2 (module
  `post_rules.mk` + `tools/make_combined_uf2.py` → `<TARGET>_combined.uf2`, peer
  from the new raw sidecar `.bin`) are implemented and pass offline checks (the
  combined UF2's app region equals the normal UF2; the staged peer equals the
  sidecar/blob; family preserved). **Next:** the user drags
  `bksf_images/bksf_dilemma_T8_staging_combined.uf2` to `RPI-RP2` and presses
  `BKSFTRG` — expect `staging peer len=98304 crc staged=0xD41659BA peer=0xD41659BA
  diff=0 -> MATCH`. See "Part 8 bring-up" for the images and the control case.
  Part 7 is HW-verified (clean `UPDATE_SLAVE` stream works; the B1/B2 corruption
  cases and the `UPDATE_BOTH` convergence image are built but not separately
  reported). Part 6 (host flashing) remains deferred.
- **NEXT TASK — Part 9** (guard rails and recovery: version/CRC guard, "flash
  pending" indicator, documented recovery) once Part 8 is HW-verified.
- **Protocol v2 (2026-11-11):** proto bumped 1 → 2 and `BKSF_MSG_DATA_SIZE` shrank
  24 → 20 to make room for the `crc` field (struct stays 32 B). Both halves must be
  rebuilt from this code, and the peer image **must be regenerated** from it before
  rebuilding the F carrying build — a v1 peer has a different `split_shared_memory`
  layout and garbles the link.
- **Known/expected:** after a T4 run the halves run *different* builds by design
  (master F, slave peer), so argos reports a mismatch until Part 5 updates the
  master too. The transfer was slow (~1 min) with 20 B RPC chunks; **Part 7 (T7.1)
  replaces that with the 224 B bulk frame — one transaction per chunk instead of
  four — so expect a large speed-up (≈50× fewer wire exchanges).**
- **Non-blocking loose ends:** post-4 MB regression check
  (typing/layers/trackball/RGB/VIA) and confirming the flash chip really is 4 MB.
- **Build/test images:** `bksf_images/`. The T4.3 set is built for the **dilemma
  4x6 procyon**: `bksf_dilemma_{E_rpc,F_slave_update,G_corrupt_recover,
  H_corrupt_persist,peer}.uf2`. The earlier charybdis v2 set (`bksf_{E_rpc,
  F_slave_update,G_corrupt_recover,H_corrupt_persist,peer}.uf2`) also remains.
  The Part 5 set adds `bksf_dilemma_I_both_update.uf2` (one trigger, both halves)
  and `bksf_dilemma_J_both_hold.uf2` (same + `BKSF_SELF_HOLD_MS 5000`), with
  `bksf_dilemma_peer.uf2` regenerated from the current code (191488 B).
  Regenerate the peer from the *current* code before rebuilding the carrier — a
  stale peer has a different `split_shared_memory` layout and breaks the split link.
- **Part 7 images (`bksf_images/`, built 2026-11-11 from the bulk-transport code):**
  `bksf_dilemma_T7_slave_update.uf2` (clean stream + commit), 
  `bksf_dilemma_T7_corrupt_recover.uf2` (B1 transient), 
  `bksf_dilemma_T7_corrupt_persist.uf2` (B2 persistent), 
  `bksf_dilemma_T7_both_update.uf2` (one trigger, both halves), plus a regenerated
  `bksf_dilemma_peer.uf2`. Rebuild the whole set with
  `bash /home/quinn/bksf_gen_images.sh` (regenerates the peer from current code
  first, restores `config.h`, drops the blob). Argos-red test pair:
  `bksf_dilemma_mismatch_{A,B}.uf2` — identical except the argos `build_id` — from
  `bash /home/quinn/bksf_mismatch_images.sh`; flash A to both halves (match, no
  red), then B to one half (mismatch → red). **The `_A/_B` pair are `DUMP` builds,
  so their trigger only dumps facts — use the T7 update images to run an update.**
- **Test trigger:** the T5 run uses the **dilemma 4x6 procyon**. Set
  `#undef BKSF_TEST` + `#define BKSF_TEST BKSF_TEST_UPDATE_BOTH` (plus, for the
  armed-window case, `#undef BKSF_SELF_HOLD_MS` + `#define BKSF_SELF_HOLD_MS 5000`)
  in `keyboards/bastardkb/dilemma/4x6_4_procyon/keymaps/vendor/config.h`, then build
  `bastardkb/dilemma/4x6_4_procyon:vendor` and flash both halves.

**Status detail (2026-11-11):** T1.3–T1.5 and **all of Part 2 (T2.1–T2.5)** pass on
hardware (charybdis 4x6). **Part 2 and Part 3 are complete** (T3.1–T3.4: module
RPC id, versioned protocol, slave ARM/ABORT handler, master retry/timeout
handshake — verified across both halves). **Part 4 complete (T4.1–T4.3)** — slave
BEGIN/CHUNK/VERIFY/COMMIT receiver, master streaming state machine, EEPROM wipe on
update, and the T4.3 link-integrity + power-cut matrix — verified end to end on the
**dilemma 4x6 procyon**. **Part 5 complete (T5.1–T5.2)** — one trigger updates both
halves (master self-flashes last) and the combined power-cut matrix passes (slave
S2/S3 → `RPI-RP2`, master M1 → `RPI-RP2`; a few master cases assumed). **Part 7
(T7.1–T7.2) complete** — the bulk USART transport replaces the 20 B RPC chunk with
a 224 B single-transaction frame; verified on the dilemma 4x6 procyon. The argos
build-id gate is now decoupled (red indication independent of the key block).
Next: Part 8. Non-blocking loose
ends: regression (typing/layers/trackball/RGB/VIA after the 4 MB change) and the
chip-is-really-4 MB sanity check.

**Argos note (encountered during Part 3):** argos blocks all keys until the two
halves' `build_id`s match, and it caches that id in EEPROM on first init only
(`argos.c:99-115`), so reflashing only one half leaves a stale id and dead keys
(`process_record_argos` returns false → `process_record_modules` short-circuits,
so `BKSFTRG` never reaches the module). Both halves' argos datablocks had to be
re-initialised (same firmware on both) before the handshake could be observed.

**Argos build-id gate — interim (2026-11-11):** the key-block and the red mismatch
indication are now decoupled. `ARGOS_DISABLE_BUILD_ID_GATE` only suppresses the
*key block* (`process_record_argos`); the red indication
(`RGB_MATRIX_BREATHING` + `HSV_RED`) always runs on a mismatch (`argos.c`). The
vendor keymap keeps the flag defined for bring-up, so a reflashed-only-one-side
board **flashes red but still types**. T10.4 retires the flag (brick typing), the
`BKSFTRG` key, and makes bootmagic the only trigger.

### Part 3 bring-up: test procedure (verified 2026-11-11)

Both halves must run the same build (the slave-side handler lives in the module).
The handshake arms the **slave**, so an interrupted/aborted run leaves the slave
needing a reflash.

1. `#define BKSF_TEST BKSF_TEST_RPC` in the keymap `config.h`, rebuild, flash
   **both** halves.
2. Reconnect the link, plug USB into the master, press `BKSFTRG`. Expect:
   - master: `bksf: handshake start`, then `master rpc cmd=1 ok=1 reply=2 st=0 v=0x00000000`, then `slave ARMED (crc=0x00000000)`
   - slave (if a console is attached to it): `slave rpc cmd=1 -> 2 st=0`
3. **Timeout/retry:** disconnect the link (or power the slave off) and press
   `BKSFTRG`. Expect the master to try 3× then `no ARMED after 3 tries -> ABORT`.
4. **Recover the slave:** it is armed now, so its next reset lands in `RPI-RP2`;
   reflash it (or `BKSF_TEST_DUMP`).

### Part 4 bring-up: T4.3 procedure — dilemma 4x6 procyon

Target board: **`bastardkb/dilemma/4x6_4_procyon:vendor`** (RP2040). Handedness is by
`SPLIT_HAND_PIN GP29` and master is the USB half (`split_util.c`), so flash the
**same image to both halves** — no separate left build. Protocol v2 (per-chunk
CRC32). All images below are prebuilt in `bksf_images/`.

**Board wiring (done 2026-11-11):** the dilemma vendor keymap now lists
`bastardkb/bk_split_flash` in `keymap.json`, has `BKSFTRG` on the base-layer
top-right key (replacing `KC_MINS`), and carries the test selector in `config.h`
(`#undef BKSF_TEST` + `#define BKSF_TEST BKSF_TEST_DUMP`). The dilemma runs the
module on the default **2 MB** layout (`PICO_FLASH_SIZE_BYTES` is not overridden),
so staging is ~`app_end..0x1C0000`, plenty for the 98304 B peer.

**Images → test cases:**

| image (`bksf_images/`) | `BKSF_TEST` (+ corruption) | case |
|---|---|---|
| `bksf_dilemma_E_rpc.uf2` | `BKSF_TEST_RPC` | **A1** arm-only; arms the slave then aborts |
| `bksf_dilemma_F_slave_update.uf2` | `BKSF_TEST_UPDATE_SLAVE` | **A2 / A3** stream + commit the peer |
| `bksf_dilemma_G_corrupt_recover.uf2` | `UPDATE_SLAVE`, `BKSF_CORRUPT_OFF 20`, `MODE 1` | **B1** corruption, recovers |
| `bksf_dilemma_H_corrupt_persist.uf2` | `UPDATE_SLAVE`, `BKSF_CORRUPT_OFF 20`, `MODE 2` | **B2** corruption, never commits |
| `bksf_dilemma_peer.uf2` | `DUMP` + `BKSF_VERSION "0.1.0-peer"` | source for the 98304 B blob embedded in F/G/H |

**Per-run setup (every case):**
1. Flash that case's image to **both** halves (double-tap reset → drag the UF2 onto `RPI-RP2`).
2. Link the halves, plug USB into the master, run `qmk console`.
3. Press `BKSFTRG` (base-layer top-right) and read the **master** log.

> The slave has no USB host while the master is plugged, so only the master's log is
> visible; every expected line below is emitted by the master.

**A. Slave power-cut matrix.** Cut the *slave's* power (unplug its USB if it is
independently powered, otherwise the link cable), then replug and note where it
comes up. Reflash both halves between runs.

| case | image | when to cut | expected result |
|---|---|---|---|
| **A1** armed | E | after master logs `slave ARMED (crc=0x00000000)` — it then logs `no BEGIN ack -> ABORT`; the slave stays armed | **`RPI-RP2`** |
| **A2** mid-COPY | F | during the ~1 min stream: after `slave BEGIN ack`, before `sent … bytes` / `slave VERIFIED` | **`RPI-RP2`** |
| **A3** mid-COMMIT | F | right after `slave COMMIT (slave will reboot)`, during the EEPROM wipe / app copy (~4 s) | **`RPI-RP2`** (or the peer build if cut at the very end) |

Repeat A2/A3 3–5×. Every result must be `RPI-RP2` or the correct build — **never
unresponsive**. (Deterministic sector-0 states were proven by T2.4; the slave uses
the same flash + boot2 path, so A3 samples the real window.)

**B. Link-integrity (NACK + resync, never commit).** The master sends each chunk
once; if the reply is not an ACK it does not hammer blind retries (a sector flush can
make the slave briefly deaf) — it backs off and asks the slave where it is
(`resync`), then either advances (reply was lost) or re-sends (real NACK).

| case | image | expected master log | required outcome |
|---|---|---|---|
| **B1** transient | G | `chunk off=20 not acked reply=6 st=1 …`, then `resync: retry off=20`, then the run continues | `slave VERIFIED` → `slave COMMIT` → `slave rejoined -> DONE` |
| **B2** persistent | H | `chunk off=20 not acked …` + repeated `resync: retry off=20 (attempt N)` (N climbs to 30), then `chunk off=20 refused 30 times -> ABORT` / `resync gave up at off=20 … -> ABORT` | **no** `slave COMMIT`, no reboot into the peer; reset slave → `RPI-RP2` |

(A lost reply instead logs `chunk off=… already applied (reply lost)` or
`resync: off=… already applied`, and the run continues. The slave also logs
`slave chunk crc mismatch off=20 … -> NACK` on its own console if one is attached.)

**Rebuild recipe** (change offset/mode, or rebuild the peer):
```bash
python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py bksf_images/bksf_dilemma_peer.uf2
# set #undef/#define BKSF_TEST (+ BKSF_CORRUPT_OFF / BKSF_CORRUPT_MODE) in
#   keyboards/bastardkb/dilemma/4x6_4_procyon/keymaps/vendor/config.h
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
```
The module `config.h` is included *before* the keymap `config.h`, so an override must
`#undef` first or it is a `-Werror` redefinition. Remove the generated
`bk_split_flash_peer_image.c` afterwards so normal builds do not carry the 98304 B blob.

**Recovery** is always BOOTSEL: hold/plug the cut half so it enumerates `RPI-RP2`,
then drag the desired image. The master tells you when the peer is gone — on a failed
handshake it logs `split peer not responding; if that half is in RPI-RP2, reflash it`,
and the idle watchdog logs `split peer lost (slave off or in RPI-RP2) - reflash that
half to restore`. Keys stay usable even when the halves differ because the test build
sets `ARGOS_DISABLE_BUILD_ID_GATE` (see the guard-rails section below); to be safe,
reflash both halves with the same image between cases.

- [ ] **Flash-size sanity:** confirm the QSPI chip really is 4 MB (marking
      `W25Q32`/similar). If it is 2 MB, revert the `PICO_FLASH_SIZE_BYTES` /
      `FLASH_LEN` overrides before flashing.
- [x] **T1.3 keycode:** pressing `BKSFTRG` (top-right key of the base layer) logs
      `bksf: trigger pressed (state=0)`.
- [x] **T1.4 facts:** the log shows the boot2 CRC pair, `__flash_binary_end`
      (XIP + offset), `eeprom_base`, and a **non-zero staging window**. Observed
      on charybdis 4x6 (trigger press; the boot-time dump races the HID console):
      ```
      bksf: 0.1.0
      bksf: boot2 crc stored=0xD58F0B07 computed=0xD58F0B07 (OK)
      bksf: boot2 rom  crc=0xD58F0B07 word=0xD58F0B07 (OK)
      bksf: __flash_binary_end=0x10014300 (offset 0x14300)
      bksf: eeprom_base=0x3C0000
      bksf: staging offset start=0x15000 bytes=3846144 (XIP 0x10015000)
      ```
      (`__flash_binary_end` grows with the image — `0x14300` in the current
      build; `eeprom_base` and `staging bytes` are stable.)
- [x] **T0.2 cross-check:** logged `__flash_binary_end` offset and
      `eeprom_base 0x3C0000` match the build `.map`/object.
- [ ] **Regression:** normal typing, layers, trackball, RGB, and (VIA) still work
      after the 4 MB change. Expect a one-time settings/VIA reset from the EEPROM
      move.
- [ ] **T0.1 (blocks Parts 3–5, not Part 2):** plug USB into the **slave** alone
      and confirm whether it enumerates / enters `RPI-RP2` (double-tap reset), to
      answer whether the slave half exposes USB.

### Part 5 bring-up: T5.1/T5.2 procedure — dilemma 4x6 procyon

Target board: **`bastardkb/dilemma/4x6_4_procyon:vendor`** (RP2040, same image on
both halves). Protocol v2. The combined ("carrier") build runs
`BKSF_TEST_UPDATE_BOTH` and carries the peer blob; on the trigger it updates the
slave first, then **self-flashes the same peer image last**, so one trigger leaves
both halves on the peer build.

**Images:**
`bksf_dilemma_I_both_update.uf2` (normal, `BKSF_SELF_HOLD_MS 0`) and
`bksf_dilemma_J_both_hold.uf2` (`BKSF_SELF_HOLD_MS 5000`, for the deterministic
armed-window cut). Both embed a freshly regenerated 98304 B
`bksf_dilemma_peer.uf2`.

**Per-run setup:**
1. Flash the case image to **both** halves (double-tap reset → drag onto `RPI-RP2`).
2. Link the halves, plug USB into the master, run `qmk console`.
3. Press `BKSFTRG` (base-layer top-right) and read the **master** log.

**T5.1 — clean run (image I).** Expected master log:
```
bksf: both-halves update (master self-flashes last)
bksf: slave update start len=98304 crc=0x........
bksf: slave ARMED (crc=0x00000000)
bksf: slave BEGIN ack (staging=0x.....)
... (chunks / per-sector flush) ...
bksf: sent 98304 bytes
bksf: slave VERIFIED
bksf: slave COMMIT (slave will reboot)
bksf: slave rejoined -> DONE
bksf: master self-flash last
bksf: stage 98304 bytes -> 0x.....
bksf: arm
bksf: apply (never returns)
```
The master then reboots into the peer build; **both** halves now run the peer
(`BKSFTRG` on either logs `bksf: 0.1.0-peer test=0`). Done when a single trigger
updates both halves and the halves agree (no argos mismatch).

**T5.2 — combined power-cut matrix.** Cut power at every state of **both** halves'
ARM/COPY/COMMIT. Every result must be `RPI-RP2` or the correct build — **never
unresponsive**. Repeat 3–5×. Reflash both halves with the case image between runs.

Slave half (carrier/master stays alive throughout; the ~1 min stream gives wide
windows — these are the T4.3 A1–A3 cases re-run inside the combined flow):

| case | when to cut slave power | expected |
|---|---|---|
| S1 armed | after `slave ARMED (crc=0x00000000)` | **`RPI-RP2`** |
| S2 mid-COPY | during the stream, after `slave BEGIN ack` | **`RPI-RP2`** |
| S3 mid-COMMIT | after `slave COMMIT (slave will reboot)` (~4 s EEPROM wipe + copy) | **`RPI-RP2`** (or the peer build if cut at the very end) |

Master half (the USB half; cut its USB/power after the slave has rejoined):

| case | image / when to cut master power | expected |
|---|---|---|
| M0 pre-arm | I or J, during master staging (before `bksf: arm`) | boot2 still valid → **boots the carrier (I) build** (recoverable; re-run or reflash) |
| M1 armed | **J**, during `armed; holding 5000 ms for the power-cut test` | **`RPI-RP2`** |
| M2 mid-COPY | I or J, during the ~1 s apply (after `bksf: apply (never returns)`) | **`RPI-RP2`** |
| M3 mid-COMMIT | I or J, at the very end of apply (EEPROM wipe / sector-0 rewrite) | **`RPI-RP2`** (or the peer build if cut after commit) |

The two sector-0 COMMIT states are proven **deterministically** by the T2.4
standalone builds (`BKSF_TEST_ERASE0` / `BKSF_TEST_PARTIAL0`) — same flash + boot2
path as the master uses here, so M2/M3 sample the real window without racing it.
M1 is made deterministic by `BKSF_SELF_HOLD_MS` (image J): the master stays armed
with XIP on, so the app keeps running and `qmk console` stays attached during the
hold.

> Cutting the *master's* USB also cuts the slave if the slave is powered only
> through the link; both then come up needing a reflash. Either way the fail-safe
> holds (each half lands in `RPI-RP2` or its valid build), so it is recoverable.

**T5.2 observed (2026-11-11, dilemma 4x6 procyon).** All outcomes matched the
matrix; the fail-safe and the guard rails both held:
- **S2 (slave cut mid-COPY) → `RPI-RP2`.** The master aborted cleanly:
  `bksf: resync failed -> ABORT`,
  `bksf: split peer not responding; if that half is in RPI-RP2, reflash it`,
  `bksf: master rpc cmd=12 ok=0 reply=0 st=0 v=0x00000000` (`cmd 12 = ABORT`, no
  reply — the slave is gone), then the idle watchdog:
  `bksf: split peer lost (slave off or in RPI-RP2) - reflash that half to restore`.
- **S3 (slave cut mid-COMMIT) → `RPI-RP2`.**
- **M1 (image J, master cut in the armed hold) → `RPI-RP2`.**
- **M0/M2/M3 not timed** (microsecond/window-race cuts); assumed safe — the master's
  apply is the already-proven T2.5 path and the two sector-0 states are proven
  deterministic in T2.4. The clean-run completion of the master's own self-flash was
  likewise not separately logged.

**Rebuild recipe** (change the selectors, or rebuild the peer):
```bash
python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py bksf_images/bksf_dilemma_peer.uf2
# set #undef/#define BKSF_TEST BKSF_TEST_UPDATE_BOTH (+ #undef/#define BKSF_SELF_HOLD_MS [0|5000])
#   in keyboards/bastardkb/dilemma/4x6_4_procyon/keymaps/vendor/config.h
qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c
```
The module `config.h` is included *before* the keymap `config.h`, so **every**
override must `#undef` first or it is a `-Werror` redefinition (this applies to
`BKSF_SELF_HOLD_MS` too). Remove the generated `bk_split_flash_peer_image.c`
afterwards so normal builds do not carry the 98304 B blob.

**Recovery** is always BOOTSEL: hold/plug the cut half so it enumerates `RPI-RP2`,
then drag the desired image. A master that rebooted into the peer (post-commit) is
already correct; a slave that rebooted into the peer likewise.

### Part 7 bring-up: T7.1/T7.2 procedure — dilemma 4x6 procyon

Target board: **`bastardkb/dilemma/4x6_4_procyon:vendor`** (same image on both
halves). T7 changes the *data plane only*: the update flow is still
ARM → BEGIN → chunks → per-sector flush → VERIFY → COMMIT → rejoin, but each chunk
now rides one bulk frame (224 B) instead of a 20 B RPC. **The shared-memory layout
changed** (enlarged `RPC_M2S_BUFFER_SIZE`), so both halves **and the peer image must
be rebuilt from this code** before any run — a peer built from the old code has a
different `split_shared_memory` and garbles the link.

**Why it should be faster.** Per chunk the old path did four transactions (RPC info,
request data, execute, response) for 20 B; the new path does **one** transaction for
224 B. Wire exchanges drop ≈50×; expect the stream to finish in seconds, not ~1 min.

**Images (generated 2026-11-11, `bksf_images/`):**

| image | build | case |
|---|---|---|
| `bksf_dilemma_peer.uf2` | `DUMP` + `BKSF_VERSION "0.1.0-peer"` | the peer the carriers embed/update to (192512 B) |
| `bksf_dilemma_T7_slave_update.uf2` | `UPDATE_SLAVE` | **T7.1/T7.2** clean stream + commit + rejoin (390656 B) |
| `bksf_dilemma_T7_corrupt_recover.uf2` | `UPDATE_SLAVE`, `CORRUPT_OFF 20`, `MODE 1` | **B1** transient corruption, recovers |
| `bksf_dilemma_T7_corrupt_persist.uf2` | `UPDATE_SLAVE`, `CORRUPT_OFF 20`, `MODE 2` | **B2** persistent corruption, never commits |
| `bksf_dilemma_T7_both_update.uf2` | `UPDATE_BOTH` | one trigger updates both halves (391168 B) |

Regenerate with `bash /home/quinn/bksf_gen_images.sh` (rebuilds the peer from the
current code first, then the carriers; restores `config.h` and drops the blob after).

**Per-run setup (reuse the T4/T5 flow):**
1. Regenerate the peer: `python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py bksf_images/bksf_dilemma_peer.uf2`.
2. Set the selector (`#undef BKSF_TEST` + `#define BKSF_TEST BKSF_TEST_UPDATE_SLAVE`
   for the slave-only case, or `..._UPDATE_BOTH` for the combined case) in
   `keyboards/bastardkb/dilemma/4x6_4_procyon/keymaps/vendor/config.h`, then
   `qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c`.
3. Flash the case image to **both** halves; link them; USB into the master; `qmk console`;
   press `BKSFTRG`.

**T7.1 — bulk framing + per-chunk CRC32 + NACK/retry.** Clean run: expect the same
master log lines as T4/T5 (`slave BEGIN ack`, `sent 98304 bytes`, `slave VERIFIED`,
`slave COMMIT`, `slave rejoined -> DONE`) but the stream completes **much** faster.
The bulk phase logs `bksf: bulk phase begin (split traffic quiesced)` on entry and
`bksf: bulk phase end (split traffic resumed)` once the last sector is written.
Corruption (`BKSF_CORRUPT_OFF` / `BKSF_CORRUPT_MODE`) must still recover (B1) or
abort without commit (B2), and the master's failure line is now
`bksf: bulk off=<n> not acked ok=… magic=… st=… rxp_off=…`. `BKSF_CORRUPT_OFF` is an
**image offset** (the chunk containing it is corrupted), so `20` still works with the
larger chunk size.

**T7.2 — quiesce/resume.** While the bulk phase runs, the master's idle peer
watchdog is suspended (`bksf_bulk_is_quiesced()`), and normal matrix/RPC traffic
continues between chunks (it cannot interleave — the transport is master-initiated
and each chunk is one atomic transaction). Confirm the keyboard keeps scanning during
the transfer and that normal split operation resumes cleanly after `slave rejoined`
or after an `ABORT` (the quiesce is always released by `bksf_bulk_resume()`).

**Tuning.** `BKSF_BULK_CHUNK` (default 224) is the only knob. It must keep the frame
(`16 + BKSF_BULK_CHUNK`) ≤ 255 B (transaction size fields are `uint8_t`; a
`_Static_assert` enforces it) and complete inside `SERIAL_USART_TIMEOUT` (20 ms):
240 B ≈ 11.5 ms at 230400 8E2, so 224 is near the ceiling.

## Part 2 scope — COMPLETE (2026-11-11)

Master-only fail-safe bring-up: `bksf_arm()` → `bksf_stage_image()` →
RAM-resident `bksf_apply_and_reset()` (app sectors, then sector 0/boot2 last) →
reset, with the full interruption matrix and a changed-image self-flash proven on
the right half alone. See the T2.1–T2.5 tasks above and the procedure below.

### Part 2 bring-up: test procedure (verified for T2.1–T2.5)

The module carries a compile-time selector; set one, rebuild, flash, press
`BKSFTRG`, and observe. Put the override in the keymap `config.h` (the module's
own `config.h` uses `#ifndef`):

```c
#define BKSF_TEST BKSF_TEST_ARM       // or BKSF_TEST_BOOTSEL / BKSF_TEST_COMMIT
```

- **T2.1 `BKSF_TEST_ARM`** — press once: logs `arm OK, crc now=0x00000000
  (armed=1)`; the keyboard keeps working (no reset). Confirm a second press still
  works. A power cycle now lands in `RPI-RP2` (that is the fail-safe firing) —
  recover with a normal UF2.
- **T2.2 `BKSF_TEST_BOOTSEL`** — press once: logs `arm OK; plain reset -> expect
  RPI-RP2`, then the half enumerates as `RPI-RP2` **because the ROM's boot2 CRC
  check failed**, not because we asked for BOOTSEL. Reflash a normal UF2 to
  recover. (Uses `mcu_reset()`, a plain NVIC reset, on purpose — `reset_usb_boot`
  would force BOOTSEL and prove nothing.)
- **T2.3 `BKSF_TEST_COMMIT`** — press once: logs `commit identical sector 0`,
  resets, and boots the **same** firmware with settings intact. This proves the
  erase/program/re-enable-XIP ordering on sector 0 (which holds boot2 + vectors).

**T2.4** (power-cut matrix) and **T2.5** (self-flash from a distinct const blob)
follow once T2.3 passes on hardware; T2.5 also needs the peer-image blob plumbing
(a generated `const uint8_t[]`), which is not built yet.

- **T2.4 — interruption matrix.** Deterministic states (no timing race) plus the
  realistic long-window path. Each row is one build (override `BKSF_TEST` in the
  keymap `config.h`), flash, press `BKSFTRG`, observe:

  | build | `BKSF_TEST` | what it does | expected |
  |---|---|---|---|
  | A | `BKSF_TEST_ARM` | arms (boot2 CRC zeroed), keeps running | pull USB → replug → `RPI-RP2` |
  | B | `BKSF_TEST_ERASE0` | erase sector 0, then reset ("mid-erase" state) | `RPI-RP2` |
  | C | `BKSF_TEST_PARTIAL0` | erase sector 0, program it all-zero, reset ("mid-program" state) | `RPI-RP2` |
  | D | `BKSF_TEST_UPDATE` (+peer blob) | stage→arm→apply, ~1–2 s | cut mid-apply → `RPI-RP2`; cut after → peer build boots |

  B and C reproduce the two dangerous COMMIT-interruption states exactly, without
  racing a ~50 ms window. D gives a multi-second window for a real power cut at
  arbitrary points, and is the only path that also risks a torn write. Repeat
  each 3–5× and record outcomes; every result must be `RPI-RP2` (pre-COMMIT) or
  the correct app (post-COMMIT) — never unresponsive. Recover by reflashing.

- **T2.5 — self-flash from a distinct build.** `bksf_stage_image()` +
  `bksf_arm()` + RAM-resident `bksf_apply_and_reset()` (writes sectors 1..N, then
  sector 0 last; never returns). The peer image is a *separate* build, compiled
  into the running one as a `const uint8_t[]`:
  1. Build the peer: `qmk compile -kb <kb> -km <km>` (optionally give it a
     distinguishing `#define`, e.g. a different `BKSF_VERSION`, so the change is
     visible in the log).
  2. `python3 modules/bastardkb/bk_split_flash/tools/gen_peer_image.py <peer>.uf2`
     → writes `bk_split_flash_peer_image.c`, auto-included by `rules.mk`.
  3. Rebuild the *running* build (now carrying the blob) and flash it.
  4. `#define BKSF_TEST BKSF_TEST_UPDATE`, rebuild, flash, press `BKSFTRG`.
     Expect `stage …`, `arm`, `apply (never returns)`, then a reboot into the
     **peer** build (its log/behaviour differs). Cut power during `apply` to
     confirm it lands in `RPI-RP2` instead.

⚠️ Use a **spare/dev board**: T2.1–T2.3 deliberately leave the half needing a
reflash. Recovery is always BOOTSEL (hold BOOTSEL while plugging USB).

### Part 2 test images (built 2026-11-11)

Standalone right-half builds under `bksf_images/` (all single-half; left half USB
disconnected):

| file | `BKSF_TEST` | notes |
|---|---|---|
| `bksf_A_arm.uf2` | `BKSF_TEST_ARM` | armed-state cut test |
| `bksf_B_erase0.uf2` | `BKSF_TEST_ERASE0` | mid-erase sector-0 state |
| `bksf_C_partial0.uf2` | `BKSF_TEST_PARTIAL0` | mid-program sector-0 state |
| `bksf_peer.uf2` | `BKSF_TEST_DUMP` + `BKSF_VERSION "0.1.0-peer"` | distinct build embedded in D |
| `bksf_D_update.uf2` | `BKSF_TEST_UPDATE` | carries the 86016 B peer blob |

The generated `bk_split_flash_peer_image.c` is removed after building D so it
does not bloat normal builds by 86 KB; regenerate with
`tools/gen_peer_image.py bksf_images/bksf_peer.uf2` before rebuilding D.

### Part 4 test images (protocol v2, 2026-11-11)

The proto bump (v1 → v2, per-chunk CRC) changes `split_shared_memory`, so all images
are rebuilt from the current code and a matching pair must be flashed. The T4.3 run
uses the **dilemma 4x6 procyon** (same image on both halves):

| file | `BKSF_TEST` (+ corruption) | case | size |
|---|---|---|---|
| `bksf_dilemma_peer.uf2` | `DUMP` + `BKSF_VERSION "0.1.0-peer"` | blob source | 190976 B |
| `bksf_dilemma_E_rpc.uf2` | `BKSF_TEST_RPC` | A1 | 190976 B |
| `bksf_dilemma_F_slave_update.uf2` | `BKSF_TEST_UPDATE_SLAVE` | A2/A3 | 388096 B (98304 B blob) |
| `bksf_dilemma_G_corrupt_recover.uf2` | `UPDATE_SLAVE` + `CORRUPT_OFF 20`, `MODE 1` | B1 | 388096 B |
| `bksf_dilemma_H_corrupt_persist.uf2` | `UPDATE_SLAVE` + `CORRUPT_OFF 20`, `MODE 2` | B2 | 388608 B |

(The earlier v2 charybdis set — `bksf_{peer,E_rpc,F_slave_update,G_corrupt_recover,
H_corrupt_persist}.uf2` — also remains in `bksf_images/`.) Full run procedure and the
one-command rebuild recipe are in the "Part 4 bring-up" section above.

### T4.3 guard rails: disconnect detection, lost-reply resync (added 2026-11-11)

Observed on the first T4.3 run: a power-cut case ends with the slave in `RPI-RP2`
(expected), but the link is then dead with no indication why; a half-flashed /
half-updated pair looks to argos like a build-id mismatch, which **blocks every key**;
and the chunk stream stalled at a sector boundary with a zeroed RPC reply
(`chunk off=8176 NACK … reply=0 st=0`). Three fixes:

**1. Detection (module, `bk_split_flash_rpc.c`).**
- `bksf_abort()` now appends `split peer not responding; if that half is in RPI-RP2,
  reflash it` when `is_transport_connected()` is false, so every failed
  handshake/flush/rejoin says what to do.
- A master **idle peer watchdog** (1 s probe, 3-miss debounce) logs `split peer lost
  (slave off or in RPI-RP2) - reflash that half to restore` when the peer drops and
  `split peer back` when it returns. Purely informational: it never blocks the
  keyboard or drives the transport beyond the cheap connection check.

**2. Lost-reply resync (module).** Root cause of `reply=0`: the slave's RPC handler
runs on the transport thread but the sector **flush runs in the main loop with
interrupts off**, so the master's reply read could race it, and the transport's
`crc8` guard drops a garbled RPC silently — a false `ok` with a zeroed reply. The
old code then hammered blind retries whose reply was also lost, and because the
slave had already advanced `rxp_off`, a retry could never succeed (abort).
- Chunk ACKs now carry **`rxp_off`**, and a chunk is only ever sent once per attempt;
  on a non-ACK the master backs off and probes `rxp_off` (SYNC also returns it in
  `data[0..3]`), then either **advances** (reply lost, data safe) or **re-sends**
  (real NACK). Attempts at one offset are bounded by `BKSF_CHUNK_ATTEMPT_MAX` (30),
  counted on every failed send/probe, so a persistently corrupt chunk aborts
  (`chunk refused` / `resync gave up`) instead of looping; forward progress resets the
  count. A corrupt link therefore still never commits.
- The slave **defers the sector flush** by a few ms so it cannot start while the
  master is reading the boundary chunk's reply.

**3. Keep the keyboard typable (argos, `argos.c`).** New opt-in
`ARGOS_DISABLE_BUILD_ID_GATE`: with it defined, `process_record_argos` allows keys
regardless of the peer's build id and the red mismatch indicator is suppressed. The
dilemma vendor `config.h` defines it, so a half-flashed dev board stays usable and
B1/B2 can be observed. Remove the `#define` to restore normal gating.

**Recovery flow:**
1. If the cut half is in `RPI-RP2`, reflash it with the same image (normal BOOTSEL).
2. If keys are dead after an update (halves run different builds), rely on
   `ARGOS_DISABLE_BUILD_ID_GATE`, or reflash **both** halves with the same image.
3. Between cases, always reflash both halves with that case's image before the trigger.

This is groundwork for Part 8 (guard rails); a full slave-recovery UX lands with
Part 5 (one trigger updates both halves) plus Part 8.

