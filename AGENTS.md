# AGENTS.md — twang project conventions

Durable architectural conventions for the twang synthesizer. These apply to
all C++ work in this repo and outlive any single task or epic.

## State and globals

- No mutable globals. State lives in structs/objects owned by a caller and
  passed by reference.
- The only surviving globals are externally imposed, and each carries a
  "Justified exception, not a pattern -- <reason>" comment: the fixed SDRAM IPC
  address constant (`kSharedIpcAddr`), and the Zephyr kernel/callback objects
  (`tx_slab`, `g_touch`).

## Control side vs. audio side

- Control side (`EngineControl`, `Interaction`): a class with private state and
  public methods. It encapsulates what callers must not touch (e.g. the
  cross-core transport).
- Audio side (`EngineAudio`): a plain public POD struct + free functions.
  Data-oriented; nothing to hide (single consumer).

## Functions

- A pure helper (operates only on its parameters) may stay file-static in an
  anonymous namespace — it keeps the header minimal.
- A function that needs an object's private state is a private method; the
  method names the operation on that state.
- Public entry points: methods on the control side, free functions on the audio
  side.

## Const data

- Header-declared, `k`-prefixed (`inline constexpr` or `extern const`), never
  TU-local. Tuning parameters and data tables stay visible so they can later
  move to configuration and become data-driven.

## Transport (cross-core IPC)

- One `SharedIpc` instance, referenced (not copied) by both cores via a pointer
  set once at construction. No scattered accessor — the block is reached
  through the owning object.
- Control-side reads of the shared block (`GetParam`, `GetRoute`) take no
  engine/control argument; they read the transport, not any core's state.

## Audio path

- No malloc in the render path. `Voice` and `Part` are plain old data
  (memcpy-able, no heap, no vtable). Fixed block sizes.
- The audio engine state lives in static storage in DTCM (`.dtcm_bss`) — never
  a stack local, never SDRAM.

## Naming

- Constants are `k`-prefixed. Mutable state never uses the `g_` prefix.

## Target builds (Zephyr, EK-RA8D2)

Two Zephyr applications under `target/zephyr/` — `cm85/` (audio core) and
`cm33/` (control core, the UI). Both cross-compile for the EK-RA8D2 with the
Zephyr SDK; no desktop deps. Run from the app dir, `-d` is an out-of-tree build
dir under `/tmp`:

    export ZEPHYR_BASE=/workspace/zephyrproject/zephyr
    export ZEPHYR_SDK_INSTALL_DIR=/workspace/zephyr-sdk/zephyr-sdk-1.0.1

    cd target/zephyr/cm33
    west build -b ek_ra8d2/r7ka8d2kflcac/cm33 -d /tmp/twang-cm33-build -p always

    cd target/zephyr/cm85
    west build -b ek_ra8d2/r7ka8d2kflcac/cm85 -d /tmp/twang-cm85-build -p always

The linker prints a size table at the end of each build ("Memory region / Used
Size / Region Size / %age Used"). Section detail via
`arm-zephyr-eabi-size <build>/zephyr/zephyr.elf` (toolchain at
`/workspace/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin`).

Flash budget (the split is in the Zephyr tree's
`dts/arm/renesas/ra/ra8/r7ka8d2kflcac.dtsi`): cm33 gets **256 KB** code MRAM +
640 KB SRAM; cm85 gets 768 KB MRAM + 1 MB SRAM. The control core (cm33) is the
constrained one — ~70 KB flash (27%) as of 2026-09-17.

## Beads and commits

- Work is tracked in beads (`bd`, with `BEADS_DB` routed to the workspace where
  the code change lands).
- Fold each bead close into its work commit: `bd close <id>`, then
  `git add -A && git commit` once — code and the `.beads/issues.jsonl` export
  together. Do not emit a separate "chore(beads): close <id>" commit per bead.
- The bd git hooks (`.beads/hooks`, wired via `core.hooksPath`) auto-export
  `issues.jsonl` on every commit; never commit the export by hand as its own
  step, and never run `git add .beads/issues.jsonl` on its own.
- Batch: close beads as you go and commit at a natural checkpoint (or session
  end) — the export is cumulative.
