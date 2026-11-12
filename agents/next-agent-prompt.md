# bk_split_flash — handoff prompt for the next agent (2026-11-12)

Resume the split-flash work.

Read first: `docs/superpowers/plans/2026-11-11-split-flash-both-halves-parts.md`
(task breakdown + source of truth — read its "WHERE WE ARE" and "Local build
environment" blocks) and `docs/superpowers/plans/2026-11-10-split-flash-both-halves.md`
(the design).

State: Parts 1–5 done and HW-verified (T0–T4.3; T5.1/T5.2 on the dilemma 4x6
procyon). Part 6 (host flashing) is deferred and not started — a partial attempt was
reverted; leave it alone. **Part 7 (bulk USART transport) is done and HW-verified**
(T7.1 length-prefixed bulk framing with per-chunk CRC32 + NACK/retry; T7.2
quiesce/resume).

Do **Part 8 — Combined-UF2 packaging**:
- **T8.1** — verify the ROM mass-storage writer accepts a UF2 block targeting the
  staging address. Build a combined UF2 (app at `0x10000000`, peer image at the
  staging address), drag it to `RPI-RP2`, and confirm the staging region holds the
  peer image.
- **T8.2** — userspace post-build step using `util/uf2conv.py` that concatenates the
  app image and the peer image into one combined UF2 per build, with the peer placed
  at the staging address, so a single drag-and-drop updates both halves via the
  running module.

Constraints: module at `modules/bastardkb/bk_split_flash/` (`bk_split_flash_*`
sources, `bksf_*` functions); never edit `/home/quinn/bastardkb-qmk`. Flash ops run
from RAM with IRQs off; 4096 B erase / 256 B program granularity; staging bounds from
linker symbols. Fail-safe: a valid boot2 CRC is written only during COMMIT, and
COMMIT is the last step before reset.

Build: `wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c"`
(the module `config.h` is force-included before the keymap's, so overrides need
`#undef` first).

T7 note: the bulk transport enlarged `RPC_M2S_BUFFER_SIZE`, which changes
`split_shared_memory` — both halves **and the peer image must be rebuilt from the
same code** (a stale peer garbles the link). The peer blob is generated from the
current build with `modules/bastardkb/bk_split_flash/tools/gen_peer_image.py` (see
`bash /home/quinn/bksf_gen_images.sh`); Part 8 packages that same peer image into the
combined UF2.

You don't do hardware verification; the user does.

After each task: log results in the plan's "Task log" and update "WHERE WE ARE";
stop for the user's HW verification.
