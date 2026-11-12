# BK Split Flash — streaming speed-up plan

> **Status:** DRAFT — plan only, no code changed. Awaiting Phase 0 measurement / user hardware verification before any edits.
> **Date:** 2026-11-12
> **Module:** `modules/bastardkb/bk_split_flash/`
> **Fresh analysis:** produced from the module source and the board serial config only; prior plan documents were intentionally not consulted.

## 1. Goal

Speed up the master→slave firmware image transfer **significantly**, measured as **total wall-clock** (trigger press → slave rejoined), **without changing the split-link baud rate** (`SERIAL_USART_SPEED 625000`, `keyboards/bastardkb/charybdis/4x6/keymaps/vendor/config.h:26`; also `dilemma/4x6_4_procyon/keymaps/vendor/config.h:26`).

The intended lever is **removing back-and-forth**: drop the per-chunk ACK and the per-sector polling, do **one final integrity check**, and **re-stream only the missing/corrupted sectors** before committing.

## 2. Constraints & decisions

| # | Decision | Consequence for the design |
|---|----------|----------------------------|
| 1 | **"Anything goes as long as all code stays in the module."** | No QMK core edits. Use only public transport APIs (`transport_execute_transaction`, `transaction_rpc_exec`, `transaction_register_rpc`) and module-owned buffers/state. The `uint8_t` transaction/RPC length (≤255 B) cannot be widened from the module, so frames stay at 240 B. |
| 2 | **Slave RAM ≈ 256 KB general-purpose pool** (`RP2040_FLASH_TIMECRIT.ld`: ram0 0x20000000, 256 KB → `.data`/`.bss`/heap; last 8 KB are stacks/USB). | A whole-image RAM receive buffer is plausible → primary architecture. Confirm free headroom from the map in Phase 0; a multi-sector ring buffer is the fallback. |
| 3 | **Per-sector bitmap** for localization. | One 32-byte VERIFY reply can carry up to a 160-sector bitmap (`bksf_msg_t.data[20]`, `bk_split_flash_proto.h:56`); a 256 KB image needs only 8 bytes. Chosen over per-chunk (too much metadata) and whole-image-CRC-only (cannot localize). |
| 4 | **Success = total wall-clock.** | Optimize the whole run (including the up-front erase phase and final apply), not just the streaming phase. |

Additional standing constraints:
- Baud stays at 625000 (10 bits/byte ⇒ ~62.5 kB/s wire ceiling).
- Both halves run the same build; a protocol mismatch must be refused before arming.
- No code modifications in this task — this document is the deliverable.

## 3. How communication works today

**One master-initiated, half-duplex split link** (single-wire PIO USART), two logical planes:

- **Control plane** — 32-byte versioned messages (`bksf_msg_t`, `bk_split_flash_proto.h:49`) via `transaction_rpc_exec` (`bk_split_flash_rpc.c:337`). Commands: `ARM/ARMED`, `BEGIN/ACK`, `VERIFY/VERIFIED`, `SYNC/ACK`, `COMMIT`, `ABORT` (enum at `bk_split_flash_proto.h:27`).
- **Bulk data plane (T7)** — each chunk is a 240-byte frame (16 B header `magic/offset/len/crc` + `BKSF_BULK_CHUNK` 224 B payload, `bk_split_flash_pack.h:40`) sent with `transport_execute_transaction` (`bk_split_flash_pack.c:63`) and replied with a 12-byte `bksf_bulk_ack_t` (`bk_split_flash_pack.h:52`). Frame ≤255 B is asserted (`bk_split_flash_pack.h:50`).

**Every transaction is request/reply.** For each 224 B of image the master blocks for a full master→slave→master turnaround, then checks `ack.magic/status/rxp_off` (`bk_split_flash_pack.c:68`). `rxp_off` in the ack is the resync/idempotency mechanism (a lost ack is recovered by comparing the slave's position).

**Nine-state handshake** (`bk_split_flash_rpc.c:301`, driven by `housekeeping_task_bk_split_flash`, `:476`):
`ARM → BEGIN → ERASING → CHUNKS ↔ RESYNC / FLUSH → VERIFY → COMMIT → REJOIN`.

**Streaming is serialized per 4 KiB sector** because the slave has exactly **one 4 KiB RAM scratch** (`bksf_bounce`, `bk_split_flash_flash.c:316`):
- Up to `BKSF_CHUNKS_PER_TICK 8` chunk transactions per housekeeping pass, 1 ms pace, stopping at each sector boundary (`bk_split_flash_rpc.c:603`). 4096/224 ⇒ 18 full chunks + a 64-byte tail = ~19 frames per sector.
- Slave buffers chunks, checks per-chunk CRC *before* accepting (`bk_split_flash_rpc.c:97`), and on a sector boundary defers the flash program by `BKSF_FLUSH_DELAY_MS 4` so the in-flight reply is not dropped while IRQs are off (`bk_split_flash_rpc.c:108`).
- `bksf_staging_program_sector` programs 4 KiB with **interrupts off ~11 ms** (`bk_split_flash_flash.c:387`); during that the slave is **deaf** (ISRs live in flash, XIP off).
- Master then waits `BKSF_SECTOR_PACE_MS 20` and polls `SYNC` until `flushed >= target` (`bk_split_flash_rpc.c:690`). A poll racing the program costs a **250 ms RPC timeout** and a retry (`bk_split_flash_rpc.c:270`, note at `:283`).
- Up-front erase phase: slave erases one sector per pass (~45 ms each, skipped if already 0xFF), master polls `SYNC` every `BKSF_ERASE_POLL_MS 50` (`bk_split_flash_rpc.c:579`).
- VERIFY sends the whole-image CRC; slave compares against a running CRC (`bk_split_flash_rpc.c:160`). COMMIT applies the staged image from flash (`bk_split_flash_rpc.c:236`).

## 4. Where the time goes (why streaming is ~1.6–2.6× the baud floor)

At 625000 baud, 10 bits/byte ⇒ **62.5 kB/s** wire ceiling. Per 4 KiB sector, pure payload wire time is **~65.5 ms** (40960 bits / 625000). On top, per sector:
- ~19 chunk frames → 304 B headers + 228 B acks (~8.5 ms wire) **plus ~19 line turnarounds / slave-handler / master-blocking round trips** (estimated 0.1–0.5 ms each).
- **20 ms `SECTOR_PACE` wait + 1 `SYNC` poll** before the next sector, and ~2 ms of `CHUNK_PACE`.
- Occasional **250 ms timeout spikes** when a poll races a program, plus `RESYNC` backoffs (60 ms).
- The up-front **erase phase** is cheap on a first flash (~50–100 ms: staging already reads 0xFF, the slave skips each sector in fast housekeeping passes and the master sees completion within one 50 ms poll) but expensive on a re-flash that must really erase (~45 ms/sector with IRQs off; the master's 50 ms polls mostly race the deaf erases and time out) — roughly **1.7–2.5 s** for a ~150 KB image.

The **hard serialization** is the single scratch + the deaf ~11 ms program per sector: the master must idle every sector because the slave can neither receive nor buffer the next sector. The dominant cost is therefore **stop-and-wait per sector and the polling/timeout machinery built around the deaf window** — not the ack bytes themselves.

Illustrative budget for a ~150 KB image (S = 37 sectors, F ≈ 703 frames), **confirm in Phase 0** — the 2.4 s pure-data figure is the hard floor:

| Component | Current | New |
|---|---|---|
| Wire data | 2.40 s | 2.40 s |
| Frame headers + per-chunk acks | 0.31 s | 0.18 s (headers only) |
| Per-frame turnarounds (est.) | 0.07–0.35 s | 0.07–0.35 s (≈0 with fire-and-forget) |
| `CHUNK_PACE` + per-sector `SYNC` polls | 0.11 s | 0 |
| `SECTOR_PACE` (20 ms × S) | 0.74 s | 0 |
| 250 ms timeout spikes | 0–0.5 s (tail) | 0 |
| Up-front erase phase | 0.1 s first / 1.7–2.5 s re-flash | 0 |
| ARM/BEGIN + VERIFY | ~0.03 s | ~0.03 s (+ bad-sector re-stream) |
| **Streaming subtotal** | **~3.9 s first / ~5.6–6.4 s re-flash** | **~2.8 s** |
| COMMIT + apply + reboot + rejoin (fixed, unchanged) | ~3–4.5 s | ~3–4.5 s |
| **Total wall-clock** | **~7–8.5 s first / ~8.6–11 s re-flash** | **~5.8–7.3 s** |

The fixed COMMIT/apply/rejoin cost (EEPROM wipe + sector-by-sector erase+program of the app + reboot; the `BKSF_REJOIN_TIMEOUT_MS` comment at `bk_split_flash_rpc.c:294` estimates ~3 s wipe + ~1 s copy) is **unchanged by this plan** and caps the total-wall-clock gain even though streaming nearly halves. For a both-halves update there is a second, serialized apply (master self-flash) after REJOIN, which Phase 4 overlaps.

## 5. Chosen architecture

**Buffer the whole image in the slave's RAM, verify once, then apply directly from RAM.** This collapses the entire erase phase, the per-sector staging program, the deaf IRQ-off windows, and every wait/poll built around them. Because the slave never writes flash while streaming, it stays continuously answerable, so the master pushes frames back-to-back with no 20 ms pace, no per-sector `SYNC`, no 250 ms timeouts, and no `RESYNC` state.

Two facts make it fit cleanly:
- `bksf_apply_and_reset(const uint8_t *src, uint32_t len)` (`bk_split_flash_flash.c:441`) copies from `src` into the RAM bounce *before* each erase/program, so a **RAM source is valid** — and safer than the current XIP staging source. No staging-flash program is needed at all.
- The per-sector bitmap for a 256 KB image is 64 bits = 8 bytes, well inside `bksf_msg_t.data[20]` → **one 32-byte VERIFY reply carries the whole bitmap**.

**"Remove ack" = remove the per-chunk reply/turnaround, KEEP the in-frame CRC32.** Per-chunk CRC already guarantees byte correctness, so the final whole-image CRC is a cheap backstop, not the primary check. The bad-sector bitmap is derived from per-chunk CRC results and missing slots.

Constraint kept honest: frames stay at 240 B (the `uint8_t` length cap cannot be widened from inside the module). The speedup comes from removing **wait states and round-trips**, not from bigger frames or a faster baud. True fire-and-forget (no reply wait at all) is a conditional Phase 2 stretch; the core win does not depend on it.

## 6. Target protocol (bump `BKSF_PROTO_VERSION` 3 → 4)

1. **`ARM → ARMED`** — unchanged, 1 round trip.
2. **`BEGIN`** — `value = image_len`, `data[0..3] = whole-image CRC32`. Validate against the **RAM budget** (not the staging window). Slave allocates/locks `rx_buf[image_len]`, initializes per-sector slot bitmasks. Replies `ACK` with `value = rx_buf capacity`/OK status.
3. **`STREAM`** — master sends every 240-byte frame (same `bksf_bulk_frame_t` layout, `bk_split_flash_pack.h:40`). **No per-chunk ack.** Slave writes `frame.data → rx_buf[frame.offset]`, checks `frame.crc` before accepting, and sets that sector's chunk slot (deterministic layout from `BKSF_BULK_CHUNK`: 18 × 224 B + 64 B tail per 4 KiB sector). No flash writes, no deaf windows, no pacing.
4. **`VERIFY`** — one round trip. Slave computes the whole-image CRC over `rx_buf` and returns `BKSF_CMD_VERIFIED` with `value = computed CRC` (or bad-sector count) and `data[0..N] = bad-sector bitmap` (bit `s` of `data[s/8]`, bit `s%8` set ⇒ sector `s` incomplete/corrupt), `len = bitmap byte count`. A sector is bad if any of its chunk slots is unset (missing frame) or a frame failed CRC.
5. **`RE-STREAM`** — master re-sends only the flagged sectors' frames; slave clears those sectors' slots first and refills them; re-VERIFY until the bitmap is clean. Bounded by `BKSF_RESTREAM_MAX`; on exhaustion `ABORT` (never commit a corrupt image).
6. **`COMMIT`** — slave calls `bksf_apply_and_reset(rx_buf, len)` (sector 0 last, boot2 already armed), resets. **`REJOIN`** as today.

Master state machine becomes: `ARM → BEGIN → STREAM → VERIFY → RE-STREAM → COMMIT → REJOIN`. `ERASING`, `FLUSH`, `RESYNC` and their pacing constants are deleted.

## 7. Implementation phases (all under `modules/bastardkb/bk_split_flash/`)

- **Phase 0 — measure & de-risk (hardware; user verification).**
  - (a) A/B end-to-end timing with `BKSF_LEGACY_FLUSH` (`config.h:71`); attribute time to wire vs. turnaround vs. pace vs. erase vs. timeouts.
  - (b) From the map/`size`, confirm `image_len + bksf_bounce(4K) + existing .bss/heap + stack headroom ≤ 256 KB`. If it does not fit, switch to the N-sector ring-buffer fallback (short blind pacing around per-sector programs kept).
  - (c) Confirm on this PIO serial driver whether `transport_execute_transaction` with `target2initiator_buffer_size = 0` (or a public raw-send symbol) **skips the reply wait**. Decides true fire-and-forget. If not, accept one tiny turnaround per frame (still ~1.1× floor).
- **Phase 1 — RAM receive path** (`bk_split_flash_rpc.c`, new `bk_split_flash_rx.*` if it keeps files small). Add module-owned `rx_buf` + per-sector slot/bitmap tracking, replacing `rxp_*` sector-flush state. New states per §6. Delete `BKSF_SECTOR_PACE_MS`, `BKSF_ERASE_POLL_MS`, `BKSF_FLUSH_*`, `BKSF_RESYNC_BACKOFF_MS`, `BKSF_FLUSH_DELAY_MS` (`bk_split_flash_rpc.c:270`, `:24`).
- **Phase 2 — fire-and-forget sender** (`bk_split_flash_pack.c/.h`). Add a no-reply chunk send (one-way if Phase 0(c) permits, else minimal reply). Keep the frame CRC. Repurpose `BKSF_CMD_VERIFY` to return the bitmap in `data[]`; bump `BKSF_PROTO_VERSION` (`bk_split_flash_proto.h:20`) and the image descriptor version if its layout changes (`bk_split_flash_image.h`). Both halves must be reflashed together (version guard refuses a mismatched peer before arming).
- **Phase 3 — apply from RAM** (`bk_split_flash_flash.c`). Pass `rx_buf` to `bksf_apply_and_reset`; retire `bksf_staging_program_sector` / `bksf_staging_erase_sector` / `bksf_staging_flush_sector` (keep behind `BKSF_LEGACY_FLUSH` for A/B). Update `bksf_validate_image` (`bk_split_flash_flash.c:234`) size check from staging-fit to RAM-fit.
- **Phase 4 — total-wall-clock extras.** For both-halves updates, overlap the master's self-flash with the slave's apply instead of waiting for `REJOIN` (`bk_split_flash_rpc.c:747`); trim `REJOIN` poll cadence. Optional: RLE of the 0xFF-padded tail to cut bytes-on-wire (no baud change).

### Files touched (expected)

| File | Change |
|------|--------|
| `bk_split_flash_proto.h` | Version 4; VERIFY/RE-STREAM commands; bitmap layout in `data[]` |
| `bk_split_flash_pack.h/.c` | No-reply chunk send; frame unchanged (240 B, CRC kept) |
| `bk_split_flash_rpc.c` | Whole-image `rx_buf` + per-sector bitmap; new state machine; slave handler |
| `bk_split_flash_flash.c/.h` | Apply from RAM; retire staging helpers; RAM-fit validation |
| `bk_split_flash_image.h` | Descriptor/version guard alignment |
| `config.h` | `BKSF_RX_BUFFER_SIZE`, `BKSF_RESTREAM_MAX`; keep `BKSF_LEGACY_FLUSH` A/B |

## 8. Expected gain (estimate — confirm in Phase 0)

New streaming ≈ **2.8 s** (≈2.6 s with true fire-and-forget), near the practical floor for 240-byte frames: 2.4 s pure data + ~0.18 s headers + a small per-frame turnaround. Versus ~3.9 s first-flash / ~5.6–6.4 s re-flash today, that is **~1.4× on streaming (first flash) to ~2–2.3× (re-flash)**.

Adding the unchanged ~3–4.5 s apply/reboot/rejoin, **total wall-clock improves ~1.2–1.5× (≈15–35%: save ~1.2 s first-flash, ~3 s re-flash)**. Streaming drops from ~1.6–2.6× the baud floor to ~1.17×. Gains scale with image size (the 20 ms/sector pace and the re-flash erase are the main removed costs); the ceiling is the 62.5 kB/s wire plus the fixed apply. Phase 0 measures L, the per-frame turnaround, and the fixed cost to turn this range into a single number.

## 9. Invariants, risks, mitigations

- **Fail-safe unchanged.** ARM invalidates boot2, sector 0 applied last, never COMMIT a corrupt image (clean VERIFY bitmap first), power-cut → RPI-RP2. RAM staging is safe because boot2 is armed and the only destructive step is the final apply — equivalent to today's guarantee (the current design has no resume-from-staging anyway).
- **Power cut before COMMIT** loses `rx_buf`; the half is armed → RPI-RP2 → reflash. Acceptable and documented.
- **RAM fit** (risk): whole-image buffer may not fit alongside the board's other modules (argos, led_matrix, lcd, pointing). Mitigation: Phase 0(b) measurement; ring-buffer fallback.
- **No deaf windows during streaming** ⇒ no dropped frames, no 250 ms timeout spikes, keyboard stays responsive; all IRQ-off time is consolidated into the single final apply (as today).
- **Version skew:** bump `BKSF_PROTO_VERSION` and the image descriptor; refuse mixed-version peers before arming (`bk_split_flash_image.h`, `bk_split_flash_flash.c:259`).
- **Duplicate/out-of-order frames:** the per-sector slot bitmask (set only on a valid-CRC frame at a known offset) makes duplicates harmless.
- **Re-stream correctness:** bad sectors are never programmed into staging flash (they were never in flash at all under the RAM design), so re-streaming needs no re-erase.
- **No QMK core edits** — module remains a self-contained community module.

## 10. Rejected / deferred alternatives

- **Bigger frames (>255 B, e.g. 4 KiB/transaction):** would cut frame count ~19×, but the `uint8_t` transaction/RPC length lives in QMK core — excluded by the "all code in the module" constraint. Deferred.
- **True fire-and-forget via raw low-level send:** conditional on Phase 0(c); if the driver forces a turnaround, keep a trivial per-frame reply (the core win survives).
- **Whole-image CRC only:** cannot localize damage → would force re-streaming the entire image on failure. Rejected in favour of the per-sector bitmap.
- **Compression / delta updates (RLE of padding, changed-sector manifest):** optional secondary levers; not required to hit the target.
- **Keeping staging-flash program per sector:** slower (deaf windows); replaced by RAM buffering + direct apply.

## 11. Verification & tests

- **A/B benchmark:** `BKSF_LEGACY_FLUSH=1` vs. new path, same board + image, measure trigger→rejoin wall-clock (the success metric).
- **Corruption injection:** keep `BKSF_CORRUPT_OFF` / `BKSF_CORRUPT_MODE` (`config.h:58`) working. Mode 1 (corrupt one attempt) ⇒ the sector appears in the bitmap, is re-streamed, update completes and commits. Mode 2 (corrupt every attempt) ⇒ re-stream retries exhaust, `ABORT`, **never commits**.
- **Power-cut tests (T5.2 / `BKSF_SELF_HOLD_MS`)**: confirm an interrupted run still lands in RPI-RP2.
- **Both-halves update (T5.1)**: confirm master self-flash + slave update still end on the same build.
- **Version guard**: confirm a mismatched peer is refused before ARM.
- **Fail-safe:** never COMMIT unless the VERIFY bitmap is clean.

## 12. Phase-0 open checks (blocking, need hardware / QMK driver confirmation)

1. Free `.bss`/heap headroom vs. `image_len` (whole-image RAM buffer feasible?).
2. Does a zero-length `target2initiator` transaction skip the reply wait on this PIO serial driver (true fire-and-forget)?
3. Baseline time attribution (which of pace / erase polling / turnarounds / timeouts actually dominates on this board).
4. Re-read `bksf_apply_and_reset` to reconfirm a RAM source is safe before committing to it.

## WHERE WE ARE

Plan complete (this document). **No code modified.** Deliberately a fresh document — prior plan docs were not read or edited, per the "fresh start / ignore all previous plans" instruction. Next step is **Phase 0 measurement and driver-capability confirmation on hardware**, then stop for the user's hardware verification before any code changes. Decisions locked from Q&A: all code in the module, ~256 KB RAM pool, per-sector bitmap, total-wall-clock metric.

## Task log

| Date | Task | Result |
|------|------|--------|
| 2026-11-12 | Fresh analysis of `bk_split_flash` communication (source only) | Mapped control + bulk planes, 9-state handshake, per-sector serialization, deaf ~11 ms program, 20 ms pace, 250 ms timeout races, erase polling. |
| 2026-11-12 | Bottleneck / time-budget estimate | Streaming ~1.6–2.6× the baud floor; dominant cost = stop-and-wait per sector + polling/timeout machinery, not ack bytes. |
| 2026-11-12 | Design Q&A (scope, RAM, localization, metric) | Locked: module-only code, 256 KB RAM pool, per-sector bitmap, total wall-clock. |
| 2026-11-12 | Architecture decision | Whole-image RAM buffer → one final VERIFY (CRC + sector bitmap) → re-stream bad sectors → apply directly from RAM. |
| 2026-11-12 | Write this plan document | Done. No code changes. Awaiting Phase 0 / hardware verification. |
| 2026-11-12 | Recompute the time-gain estimate (§4/§8) | Corrected: skipped-erase polling is ~0.1 s (not 2.5 s — that applies only to a real re-flash erase); the fixed apply/reboot (~3–4.5 s) caps total-wall-clock gain at ~1.2–1.5× (≈15–35%), while streaming itself gains ~1.4–2.3×. Added a full component budget table. |
