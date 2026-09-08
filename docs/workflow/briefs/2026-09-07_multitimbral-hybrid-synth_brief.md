---
title: Multitimbral Hybrid Synth
date: 2026-09-07
author: Dizan Vasquez
---

# Multitimbral Hybrid Synth

## 1. Objective

Settle the scope and approach for building a 4-part / 4-voice multitimbral
hybrid synthesizer on the Renesas EK-RA8D2 (Cortex-M85 audio, Cortex-M33
control), so the follow-on specification and implementation plan target one
instrument rather than an open platform exploration.

## 2. Scope

| In scope | Out of scope |
|---|---|
| 4-part / 4-voice digital POC (v1) | Analog filter hardware — deferred until a borrowed-filter test decides |
| Desktop engine first (WAV renderer + cycle harness) | Onboard panel — X-Touch Compact is the control surface |
| Control/audio split: event ring + double-buffered parameters | Enclosure, productization, manufacturing |
| Parameter-as-data modulation + one descriptor table | Per-part insert FX unless the effect-instance measurement permits |
| Routable part buses, analog-ready | GPL-derived code, if a product path is ever in scope |
| Hardware Phase-0 bring-up gate | |

## 3. Sources

1. `docs/random/synth-platform-exploration.md` — the platform/architecture digest; primary source and supersedes any summary here.
2. Reference codebases — Ambika + stmlib (MIT), Deluge, Surge XT, Vital, preenFM3 (GPL3).
3. Reference instruments — Micromonsta 2, Töörö, Manatee, Deluge, Virus TI2 (calibration points).
4. Folio doc-definitions for brief/spec/plan — the formats this and follow-on documents conform to.

## 4. Approach

1. Resolve the blocking open questions (dual-core debug, board price/availability), then order the board.
2. Build the desktop engine first — WAV renderer, cycle harness, one voice, then 4-part — with the control/audio split running as two threads. No hardware dependency.
3. Build the LVGL simulator UI against the parameter descriptor table.
4. Gate on hardware with the Phase-0 four-binary bring-up sequence, ending in: M33 note-on changes the M85 sine frequency.
5. Measure cycles/voice/sample and cycles/effect-instance; those numbers decide scaling and per-part inserts.
6. Write a spec (what/why) then a plan (how) from this brief before committing implementation.

## 5. Constraints

- Audio path: no allocation, exceptions, RTTI, or double precision; fixed block in TCM; float-only; memcpy-able per-voice state.
- Board has no DIN MIDI — route through the Mac (USB class-compliant) or a CME H2MIDI Pro.
- DA7212 codec: DAC high-pass filter enabled by default (must be disabled); outputs are AC-coupled, so cannot carry CV.
- Licensing: Mutable/stmlib is MIT; Deluge, Surge, Vital, preenFM are GPL.
- Documents conform to folio standards; no links to documents outside this repository.

## 6. Open Questions

| Question | Owner / where answered |
|---|---|
| Simultaneous M85 + M33 debug on EK-RA8D2 (NXP has AN13264; Renesas equivalent?) | Renesas forum / Zephyr `attach` runner |
| EK-RA8D2 price and availability vs MIMXRT1170-EVKB | Renesas / Mouser / DigiKey |
| Does Zephyr's SSIE driver expose TDM slot config (per-part outputs)? | Driver source; fall back to FSP |
| X-Touch Compact relative-mode CC (7-bit absolute too coarse for cutoff) | X-Touch Editor |
| Does Helium help this workload (IIR filters don't vectorize)? | Measure oscillators vs filters separately |
| Does analog filtering earn its cost? | Borrowed Eurorack filter test |
