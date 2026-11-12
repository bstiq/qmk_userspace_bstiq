# Working notes

Standing rules for any task in this repository.

## Build

```bash
wsl -d Ubuntu -- bash -lc "cd /home/quinn/qmk_userspace_private && qmk compile -kb bastardkb/dilemma/4x6_4_procyon -km vendor -c"
```

The module `config.h` is force-included **before** the keymap's `config.h`, so any
keymap override of a module macro needs `#undef` first (otherwise `-Werror`
redefinition).

## Layout

- Module: `modules/bastardkb/bk_split_flash/` — sources named `bk_split_flash_*`,
  functions named `bksf_*`.
- Plans/design: `docs/superpowers/plans/`, `docs/superpowers/specs/`.
- The QMK firmware checkout at `/home/quinn/bastardkb-qmk` is **read-only** — never
  edit it.

## Hard constraints

- Flash operations run from RAM with interrupts off.
- Erase granularity 4096 B, program granularity 256 B.
- Staging bounds come from linker symbols.
- Fail-safe: a valid boot2 CRC is written only during COMMIT, and COMMIT is the last
  step before reset.

## Workflow

- After each task: log the result in the plan's "Task log" and update its
  "WHERE WE ARE" block.
- The agent does **not** do hardware verification; stop after each task and let the
  user verify on hardware.
- Any change that alters `split_shared_memory` (e.g. `RPC_M2S_BUFFER_SIZE`) requires
  rebuilding **both halves and the peer image** from the same code — a stale peer
  garbles the split link.

## Image generators

- `bash /home/quinn/bksf_gen_images.sh` — the bk_split_flash test image set (rebuilds
  the peer from current code first, then the carriers; restores `config.h`).
- `bash /home/quinn/bksf_mismatch_images.sh` — the argos red-mismatch test pair.

## QMK build system facts

Settled from the build system (no need to re-investigate):

- **Post-build hook.** A community module's `post_rules.mk` is included by QMK
  (`build_keyboard.mk:451-456`), so a module can add a per-build rule there. The
  userspace root `Makefile` only forwards to QMK and is **not** a post-build hook.
- **Artifacts.** The app `ELF`/`map`/`UF2` are in `$QMK_HOME/.build/<TARGET>.{elf,map,uf2}`;
  the `UF2` is also copied to the userspace root.
- **Staging start.** `__flash_binary_end` is a linker symbol available in the
  ELF/map; the staging start is that value rounded **up to a 4096 B sector** (e.g. the
  current dilemma build `__flash_binary_end=0x100177a4` → staging `0x18000`).
- **UF2 writing.** `util/uf2conv.py` can emit multiple UF2 images; concatenating the
  app (at `0x10000000`) and the peer (at `0x10000000 + staging_start`) is
  straightforward.

