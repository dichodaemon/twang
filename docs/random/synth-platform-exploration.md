# Multitimbral Hybrid Synth — Platform Exploration

A working digest of a design exploration: choosing a hardware platform for a
multitimbral digital synth with a control-CPU / audio-DSP split, and deciding
whether analog filters belong in it.

---

## 1. How the question evolved

| Stage | Question | Outcome |
|---|---|---|
| Start | Modern DSP56000 + CPU dev board? | None exist; family is EOL |
| | Platform for prototyping digital synths, ideally analog filters | Daisy / SHARC / Eurorack |
| Pivot | Actually want the *architecture* Gearmulator emulates, in real silicon | Reframed to heterogeneous compute |
| Scoping | Micromonsta-2 class as an intermediate target | 12 voices, 2 parts, one filter each |
| Landing | EK-RA8D2 (Cortex-M85 + M33) | Ordered (2026-09-09) |

---

## 2. Silicon findings

### The DSP56000 family is gone
- DSP56311 / DSP56321: end of life, no replacement (NXP support)
- DSP56371: End of Life at distribution; DSP56374: obsolete
- No FPGA soft core found publicly. FireBee (Atari ColdFire) ships **without**
  the DSP — an ACP developer confirmed it was started but never finished.
  No Falcon core on MiSTer.
- Live activity is in **emulation**: `dsp56300` (cycle-level 56300 emulator,
  C++17) and Gearmulator, which emulate the DSP *and* the host CPU and run
  original firmware ROMs. Covers Virus A/B/C/TI, Nord Lead 3, Waldorf Q and
  Microwave II, Supernova, Nova.
- Toolchain for real 56k, if ever wanted: Rust `dsp56300` crate (assembler,
  disassembler, Cranelift JIT), `a56` (Debian), Motorola ASM56000 freeware.

### Platform shortlist as evaluated

| | Audio core | Control core | Notes |
|---|---|---|---|
| SHARC Audio Module (SC589) | 2× SHARC+ @500 MHz | Cortex-A5 @500 MHz | Ecosystem frozen at 2018; Faust integration broken |
| ADSP-SC594/598 | 2× SHARC+ up to 1 GHz | A5 / A55 | Maintained (BSP rev 4.0.0) but SOM+carrier, automotive, no music community |
| STM32H747I-DISCO | M7 @480 MHz | M4 @240 MHz | Out of stock; DISC1 also unavailable |
| MIMXRT1170-EVKB | M7 @1 GHz | M4 @400 MHz | $182.85; documented dual-core debug (AN13264) |
| **EK-RA8D2** | **M85 @1 GHz + Helium** | **M33 @250 MHz** | Screen in kit, J-Link OB, Zephyr dual-core |
| i.MX 95 / 8M Plus | A55 cluster @2 GHz | M7 @800 MHz | Real-time core is *slower*; upgrade means Linux |

**Key structural insight:** there is no next tier within Cortex-M. Moving up
means Cortex-A with NEON under RT Linux, which trades away the determinism that
motivated the split. The pragmatic scaling step is *two* MCUs (Fred's Lab's
approach), not one bigger chip.

### EK-RA8D2 — confirmed board features (Zephyr board page)
- 1 MB MRAM, 2 MB SRAM w/ ECC: **256 KB M85 TCM**, **128 KB M33 TCM**, 1664 KB user SRAM
- 64 MB SDRAM, 64 MB octal-SPI flash
- **DA7212 audio codec** (stereo), PDM MEMS mics
- USB HS + FS, host and device, USB-C
- Ethernet, SD/MMC, GLCDC + 2D drawing engine
- On-board SEGGER J-Link OB; runners: `jlink`, `pyocd` (flash/debug/attach/rtt)
- I2S: `renesas,ra-i2s-ssie`, **two instances**
- Renesas IPC **MBOX** driver in device tree
- Dual-core: M85 boots and starts M33; MRAM/SRAM split adjustable via
  `code_mram_cm85`/`sram0` and `code_mram_cm33`/`sram1`.
  Targets: `ek_ra8d2/r7ka8d2kflcac/cm85` and `.../cm33` (via `--sysbuild`),
  with `CONFIG_SOC_RA_ENABLE_START_SECOND_CORE`
- **Not populated:** native pin headers, Grove, Qwiic, mikroBUS.
  **Populated:** 2× Pmod, Arduino Uno R3

### DA7212 notes
- Ultra-low-power wearable codec (~$3.56); stereo Class-G headphone driver +
  mono Class-AB speaker driver
- 100 dB SNR **into 16 Ω**, measured at VDD 1.8 V — decent, not Burr-Brown
- **DAC high-pass filter is enabled by default after reset.** Disable it or
  lose your low end.
- Built-in beep generator (10 Hz–12 kHz) — useful for isolating codec config
  from I2S/DMA during bring-up

---

## 3. Reference instruments (calibration points)

| Instrument | Compute | Architecture |
|---|---|---|
| Virus TI2 | 2× DSP56321 (~220–275 MHz) | 16 parts, per-part FX, 20–90 voices (SOS: ~40 on dense patches) |
| Deluge | RZ/A1L, A9 @400 MHz, 3 MB SRAM, 64 MB SDRAM | One core does everything; PIC24 scans buttons |
| Manatee (Fred's Lab) | 2× dual-core 200 MHz 16-bit DSP | 10–16 voices / 4 parts, per-part FX, 4× 24-bit Burr Brown, €749 |
| Töörö (Fred's Lab) | undisclosed | 6 voices / 4 parts, **per-voice analog filter (FL A847, optocoupler-based)**, €399 |
| Micromonsta 2 | undisclosed | 12 voices, 2 parts, 3 osc + **one** filter per voice, shared FX, $299 |
| Ambika | ATmega644p + 6× ATmega328p | 6 parts, per-voice analog filter, **mixable filter designs** |
| UDO Super 6 | FPGA | 12 voices, DDS oscillators clocked at 40–50 MHz, 7 osc/voice |

**Scoping conclusion:** Micromonsta 2 is the right first target. Its spec fits
comfortably on one M85, leaving room for a first implementation to be
inefficient.

---

## 4. Software architecture

### The split
- **M33:** MIDI parsing, voice allocation, part management, patch load/save,
  LVGL + touch, encoders, LED rings, CV output
- **M85:** sample generation only

### Inter-core design (synthesised from Ambika + Deluge)
- **Events** (note-on/off, voice assign) → lock-free ring buffer / IPC mailbox
- **Continuous parameters** → double-buffered struct in shared memory, swapped
  at block boundaries
- Mixing these into one mechanism forces locking. Keep them separate.

### Ambika — verified from source (`pichenettes/ambika`)

`common/protocol.h` — the complete controller↔voicecard contract:

```
0x00 BULK size data...
0x10 noteH noteL velocity: triggers note
0x11 noteH noteL velocity: triggers note with legato
0x20 address value: write patch data
0x30 address value: write part data
0x40 address value: write entry in modulation matrix
0x5n value: write lfo n value
0x60 release
0x70 kill
0x8n retrigger envelope
0xf8 reset all controllers
0xf9 reset
```

Reading of the split:
- **Global LFOs 1–3 computed on the controller**, pushed as scalar values
- **Envelopes run on the card** — controller only sends retrigger
- **Mod matrix evaluated on the card** — controller writes entries into it
- `voicecard/voice.cc` `LoadSources()` computes locally: noise, 3 envelopes,
  pitch, gate, and `MOD_SRC_LFO_4` (per-voice LFO)

**Principle:** anything that must be phase-coherent across voices is central;
anything per-note is local.

Rates (`voicecard/voicecard.h`):
```c
static const uint8_t kControlRate = 40;   // 1 control sample per 40 audio samples
static const uint8_t kAudioBlockSize = kControlRate;
// latency 1 ms, buffer stores 4 ms
```

Main loop (`voicecard/voicecard.cc:132`) — the digital→analog filter CV path:
```c
while (1) {
  if (audio_buffer.writable() >= kAudioBlockSize) {
    voice.ProcessBlock();
    vcf_cutoff_out.Write(voice.cutoff());
    vcf_resonance_out.Write(voice.resonance());
    vcf_mode.Write(filter_mode_bytes[voice.patch().filter[0].mode]);
    update_vca = 1;
  }
  voicecard_rx.Process();
}
```
The audio ISR (`TIMER2_OVF_vect`) also services SPI reception.
`ProcessBlock()` skips oscillator rendering entirely when `vca() < 2`.

Part→card binding (`controller/part.cc:224`) is a **bitmask of physical slots**:
```c
void Part::AssignVoices(uint8_t allocation) {
  uint8_t mask = 1;
  num_allocated_voices_ = 0;
  for (uint8_t i = 0; i < kNumVoices; ++i) {
    if (allocation & mask) allocated_voices_[num_allocated_voices_++] = i;
    mask <<= 1;
  }
```
Combined with three different voicecard filter designs (4-pole LM13700,
4-pole SSM2164, 2-pole SVF SSM2164) this gives **genuinely different analog
filter circuits per part** — the only instrument found doing so.

`InternalNoteOn` also has a CHAIN mode that forwards excess notes to MIDI out
to daisy-chain another Ambika.

### Deluge — verified from source (`SynthstromAudible/DelugeFirmware`)

- `kSampleRate = 44100` (`definitions_cxx.hpp:1043`)
- `SSI_TX_BUFFER_NUM_SAMPLES 128` (`definitions.h:51`)

**Block size is variable** — derived from how far the DMA write pointer has
advanced (`audio_engine.cpp:583`):
```c
size_t numSamples = ((uint32_t)(saddr - i2sTXBufferPos) >> ...)
                    & (SSI_TX_BUFFER_NUM_SAMPLES - 1);
```

**Block size is used as a load metric** (`cullVoices`, `audio_engine.cpp:426`):
```c
constexpr int32_t numSamplesLimit = 80;
auto max_num_samples = numSamplesLimit + (20 * voices_started_this_render);
int32_t num_samples_over_limit = numSamples - max_num_samples;
if (num_samples_over_limit >= 32) {
    int32_t num_to_cull = (num_samples_over_limit >> 4);
    // soft cull one, terminate half the remainder, kill the other half
```
Falling behind kills voices rather than dropping out. Distinguishes
"terminate" (fast release) from "kill" (immediate); preserves `MIN_VOICES`.

`Voice::render` (`model/voice/voice.cpp:712`, `[[gnu::hot]]`) deliberately
spends one block doing nothing on creation, to spread note-on cost:
```c
if (justCreated == false) { justCreated = true; return true; }
```

Envelopes and local LFOs render **only if patched to something**.
`Patcher::performPatching` (`modulation/patch/patcher.cpp:63`) uses a
dirty-source bitmask and skips destinations whose sources didn't change.

Modulation is data, not code — patches contain explicit cables:
```xml
<patchCable source="envelope1" destination="oscBVolume" amount="0x3FFFFFE8" />
```
Amounts are Q31 fixed point. `NE10` (Arm's NEON library) is in the tree.

### Design conclusions
- Take **Ambika's rate discipline and boundary contract**
- Take **Deluge's parameter model** — named parameters, runtime-evaluated
  modulation matrix, control mapping by name. This is the same table the UI,
  MIDI CC mapping, patch save/load and any external editor all read.
- Both do aggressive dirtiness tracking. Neither computes what it doesn't need.

---

## 5. Audio output pipeline

```
Oscillator → Filter → Envelope → float→int24
                                      ↓
                        Double buffer in TCM (A / B)
                                      ↓
                        SSIE (I2S) ← DMA circular
                                      ↓
                                    Codec → line out
```

- Configure codec over I²C, start circular DMA over the whole double buffer,
  never touch the transmit path again
- Two interrupts per cycle: **half-complete** and **complete**; each says which
  half is free to write
- Deadline = `block_frames / sample_rate`. At 64 @ 48 kHz = **1.333 ms**
- Miss it and DMA transmits stale data → click, not a graceful slowdown

**Buffer sizing does not scale with voices.** Voices are summed.
`64 × 2ch × 4 bytes × 2 = 1 KB`. What scales is per-voice *state*
(~100–200 bytes) and *effect memory* (reverb delay lines — the real consumer).

**Non-negotiables:**
- Buffers in **TCM** — not cached, so the DMA/cache coherency class of bug
  never arises
- **Clamp before casting.** A float over ±1.0 wraps to full-scale noise
- **Sub-block control rate** — recompute envelopes/coefficients every ~16
  samples inside the block, or fast attacks are audibly stepped. Decide this
  before writing any module.

**Routing indirection to design in from day one:**
```c
for each active voice v:
    render(v, scratch, N);
    add_panned(scratch, bus[v.output_bus].L, bus[v.output_bus].R, v.pan, v.level);
```
One bus = mono-output synth. Four = per-part outputs. Same code.
Hardcoding `main_L, main_R` makes this a rewrite later.

**Per-part outputs:** 4 parts × stereo = 8 channels = one TDM8 link on one
SSIE instance. Board has two instances, so 16 channels is reachable without
external hardware. *Verify Zephyr's SSIE driver exposes TDM slot config —
the Zephyr I2S API is stereo-shaped.*

---

## 6. Control interface

**Use the X-Touch Compact.** 16 endless encoders with detent, push, and 13-segment
LED rings; 9 motorised faders; 39 illuminated buttons; two preset layers;
**5-pin DIN MIDI in and out**; stand-alone mode sends its own MIDI.

This removes the entire panel design problem from the critical path. Ring
feedback is MIDI sent *back*, which exercises bidirectional MIDI you need anyway.

- Configure in **standard MIDI mode** (not Mackie Control) via X-Touch Editor
- Use **relative mode** if available — absolute 7-bit CC is 128 steps, too
  coarse for cutoff (*verify this in the editor*)
- Abstract the mapping layer: "control 7 moved by +3", not "CC 23 arrived".
  Swapping to a local encoder panel later becomes a driver change.

**MIDI plumbing:** board has no DIN. During development, route through the Mac
(both devices are USB MIDI class-compliant) — you get message logging in both
directions, which no hardware box gives you. For computer-free operation, the
**CME H2MIDI Pro** (USB-A host + USB-C client + DIN in/out, HxMIDI Tools for
routing, NRPN/SysEx/MPE) is preferable to the Kenton MIDI USB Host mk3 because
it hosts your board *and* connects to your Mac simultaneously.

**If you later build a panel:** Adafruit I2C Quad Rotary Encoder Breakout
(#5752) — 4 encoders/board, ATtiny817 running seesaw does the decoding and
signals on change; takes standard PEC11-pinout encoders. Pair with 2×
IS31FL3731 (16×9 charlieplex, per-LED PWM, frame buffering) for 16 rings.
Reference designs: Monome Arc, Intech Studio EN16.

---

## 7. The analog filter question

### The case for
- The hybrid architecture is commercially validated: Töörö, Waldorf Quantum,
  Novation Peak/Summit, UDO Super 6 all pair digital oscillators with analog
  filters
- **Roland JD-XA** already does the per-part version: 4 analog parts with true
  analog filters, and digital parts routable through the analog filter section,
  routed per analog part — up to four filters

### The genuinely unexplored region
All-digital oscillators, **four deliberately different analog filter circuits**,
one per part, direct. JD-XA replicates the same analog section four times;
Ambika achieves circuit heterogeneity but per-voice via distribution.

### The critique (take seriously)
1. **The premise is untested.** Modern ZDF/TPT filters are very good. Deluge and
   Manatee are fully digital and respected. And you'd be A/B-ing a $3.50
   wearable codec against a dedicated analog path — half of any difference is
   converter quality.
2. **Paraphony is not a footnote.** Per-part filtering means one filter and one
   filter envelope per part. New notes retrigger the filter on held notes. This
   is the main criticism of the MicroFreak. It makes polyphonic parts *worse*
   than the digital path.
3. **The maths doesn't favour it.** Four filters, vs Töörö's six per-voice at
   €399 and Ambika's six swappable on 2011 hardware.
4. **Negative precedent.** JD-XA shipped in 2015 and nobody copied it.
5. **Calibration is a permanent tax.** Drift, unit variation, per-filter tables.
   AS3320 alone specs 4% exponential error on frequency control.
6. **Wrong discipline.** The original interest was systems architecture. Analog
   design is op-amps, CV scaling, PCB layout, calibration — a different project.
7. **Everything good about the design works with zero analog hardware.**

### The resolution
Make the **part bus routable** and put jacks on it. Analog filtering becomes a
*capability* of the architecture rather than its foundation. Any part can use
any filter; you can build one, four, or none.

### Cheapest test, before any of this
Line output into a borrowed/bought Eurorack filter, cutoff by hand.
A couple of hundred dollars, one evening, answers whether analog earns its cost.

### If pursuing CV
- Codec outputs are **AC-coupled** — cannot carry CV
- Eurorack cutoff CV is 1V/octave; do the exponential conversion in firmware
- Off-the-shelf: **Expert Sleepers ES-9** (8 DC-coupled outs at ±10V, ~$569,
  USB — needs the Mac) or **Expert Sleepers FH-2** (8 CV outs at 14-bit,
  ±5V/0–10V selectable, DIN MIDI via breakout, no USB required, NRPN/MPE)
- MIDI-to-CV is inherently stepped; fine for character comparison, not for
  judging snappy filter envelopes

---

## 8. Digital filters — what to implement

**Read:** Vadim Zavalishin, *The Art of VA Filter Design* (free from Native
Instruments). Origin of TPT and the correct answer to zero-delay feedback.
Also: Andrew Simper / Cytomic SVF papers; Antti Huovilainen's 2004 DAFx paper
on the nonlinear Moog ladder; Julius O. Smith's CCRMA books.

**Code:**
- `stmlib/dsp/filter.h` (Mutable, **MIT**) — compact TPT SVF, cycle-conscious.
  Usable directly.
- Deluge `src/deluge/dsp/filter/` — 12/24 dB ladders with drive, SVF with
  morph and filter FM, Q31 fixed point
- Surge XT — widest collection of topologies in any open codebase
- Vital — modern C++, notable comb and formant filters

**Order:** TPT SVF first (~15 lines, unconditionally stable, cheap
coefficients). Ladder second, for character. **Do not** start with a
cookbook bilinear biquad — coefficient warping near Nyquist, and clicks on
cutoff modulation.

**What actually determines perceived quality** (not topology):
nonlinearity handling (saturation in the feedback path *is* "analog character")
and smooth cutoff modulation. A linear SVF modulated well beats a sophisticated
ladder modulated badly. Worth knowing before concluding analog is better.

---

## 9. Development plan

### Before hardware arrives
**Engine (desktop):** plain C++ `render(float* out, int frames)` + parameter
bank, no hardware dependencies. Wrap with a command-line WAV renderer first
(20 minutes), then miniaudio/RtAudio for a playable standalone. Skip JUCE —
it's cross-format packaging and GUI, and you need neither.

Embedded discipline from day one, or the port becomes a rewrite:
- No allocation in the audio path; fixed voice pool
- No exceptions, no RTTI
- Fixed block size as a compile-time constant; process in blocks throughout
- Explicit state layout — each voice a plain memcpy-able struct (this is what
  enables TCM placement and Helium vectorisation later)
- Float only, no `double`, no `std::sin` in inner loops

**Cycle harness:** render N seconds of M voices, report time/sample/voice.
Absolute numbers are meaningless on desktop; **ratios are not**.

**Architecture:** run the control/audio split as two threads with a lock-free
ring buffer + double-buffered parameter block. The port swaps the transport,
not the design — and you shake out ordering bugs with a thread sanitiser.

**GUI:** LVGL's PC simulator (SDL). Build at the target resolution.
Set up **encoder input groups** from the start, not mouse clicks — focus
traversal produces different layouts than direct addressing.

**Parameter descriptor table** — the single most important structure. Name,
range, unit, formatter, target address. UI walks the table rather than knowing
about specific parameters. Makes MIDI CC mapping, patch save/load and external
editing nearly free.

### Phase 0 on hardware (the go/no-go gate)
Four single binaries, in order. Do not write a synth until all four pass:
1. Stock example flashes; audio passthrough works
2. Fixed 440 Hz sine from the M85 audio callback
3. MIDI bytes into the M33, printed over RTT
4. **Note-on on the M33 changes the sine's frequency on the M85**

Step 4 is the gate — the smallest program containing the entire architecture.
If it takes more than a weekend, reconsider the platform.

### Phases 1–3
1. One voice mono: polyBLEP saw, TPT SVF, ADSR. Deliverable is a *number*:
   cycles/voice/sample.
2. Allocator on the M33: guaranteed per-part minimum + shared surplus pool.
   Steal within the part first, then from parts over their reservation, never
   from a part at/below reservation. Voice stealing needs a few-ms release
   ramp, not an instant cut.
3. Measure, then scale.

**Build the part abstraction from day one.** Retrofitting means threading a
part ID and parameter-bank pointer through every line of the engine.

**Revised POC target:** 4 parts, 4 voices each, shared effect buses, 48 kHz.
Then measure cycles/voice and cycles/effect-instance. Those two numbers decide
whether per-part inserts are affordable.

---

## 10. Test and measurement

**Not yet needed:** oscilloscope. Buy a **$15 USB logic analyser** (sigrok /
PulseView) for I²S / I²C bring-up — better than a scope for that job and far
cheaper.

**Software covers the audio domain better than any scope:** render to WAV and
analyse; RTT logging via J-Link; DWT cycle counters.

**The one scope technique worth the purchase:** toggle a GPIO high at the start
of the audio callback and low at the end. Duty cycle = CPU load, live, zero
overhead.

**Scope becomes mandatory** when the DAC/analog board exists. 12-bit entry level
as of 2026: Rigol DHO802 ($329, 2ch), DHO804 ($439, 4ch), Siglent SDS804X HD
(~$461, cleaner front end per EEVblog, 70 MHz software-unlockable to 200).
Bandwidth is irrelevant — fastest signal is a ~12 MHz TDM bit clock.
**Channel count matters** — 4 lets you watch BCLK, LRCLK, SDATA and the load pin
together.

**Spectrum analysis:** REW's RTA mode already does this (FFT length 32k–64k,
Hann window, Blackman-Harris for low-level detail next to a loud fundamental).
Add a **test-signal mode to the firmware** — noise or sweep replacing the
oscillator — so REW can measure the filter directly. Twenty lines, permanently
useful. Or render sweeps offline and analyse the WAVs with numpy/scipy in a
regression test.

---

## 11. Open questions to resolve

| Question | Why it matters | Where to look |
|---|---|---|
| EK-RA8D2 price and availability | Deciding vs MIMXRT1170-EVKB | **Resolved** — board ordered |
| Simultaneous M85 + M33 debug | Central to the whole project. NXP has AN13264 proving it | **Resolved** — R01AN7982EU0101 (see note below) |
| Does Zephyr's SSIE driver expose TDM slot config? | Per-part outputs depend on it | Driver source; fall back to FSP / direct registers |
| X-Touch Compact relative-mode CC | 7-bit absolute is too coarse for cutoff | X-Touch Editor |
| Does Helium help *your* workload? | 4× is Arm's DSP-kernel figure; IIR filters don't vectorise | Measure oscillators vs filters separately |
| Does analog filtering earn its cost? | The premise of the whole analog thread | Borrowed Eurorack filter, one evening |

**Resolved since writing (2026-09-09):**

- **EK-RA8D2 ordered.**
- **Simultaneous M85 + M33 debug** — the Renesas equivalent of NXP's AN13264 is
  **R01AN7982EU0101** *"Multicore Setup and Running Hello World on Dual-Core"*
  (Rev 1.01, Oct 16 2025; Rev 1.01 explicitly added RA8D2). e² studio's Multicore
  Solution Project Wizard emits a *Launch Group* (one launch config per core) that
  starts a combined multicore debug session. Prerequisite: initialize the device
  first (Renesas Flash Programmer "Initialize Device", or Renesas Device Partition
  Manager) to Protection Level 2 with the TrustZone boundary unset — skipping this
  causes download/debug failures. CPU0 (M85) is primary and boots first; it starts
  CPU1 via `R_BSP_SecondaryCoreStart()`. The on-board SEGGER J-Link connects to the
  primary core; a "Device Configuration Information Register for Debug" at
  `0x02C9F04C` bit 0 steers which core is primary (SEGGER notes full Renesas
  dual-core debug documentation is still expected Q3/2026). Zephyr side:
  `ek_ra8d2` CM33 target (zephyrproject-rtos/zephyr#103884) plus per-core
  `cm85`/`cm33` targets via `--sysbuild`.

---

## 12. Reference codebases

| Project | Licence | Why |
|---|---|---|
| `pichenettes/ambika` | MIT | Hard control/audio boundary; per-voice analog; the SPI contract |
| `pichenettes/eurorack` (`stmlib`) | MIT | Best embedded DSP primitives; usable directly |
| `SynthstromAudible/DelugeFirmware` | GPL3 | Multitimbral allocation, modulation-as-data, load-adaptive rendering |
| `gramster/delugemu` | — | QEMU emulator booting unmodified Deluge firmware (no USB) |
| preenFM3 (Xavier Hosxe) | GPL3 | 16 voices / 6 instruments, per-instrument FX, 6 outputs, STM32 |
| Surge XT | GPL3 | Widest filter collection; actively maintained by a team |
| Vital | GPL3 | Modern C++; no PRs accepted; naming/preset restrictions |
| Fred's Lab Töörö / Manatee | — | Complete schematics published in the user manuals |

**Licensing note:** Mutable is MIT — usable anywhere. Deluge, Vital, Surge,
preenFM are GPL. Fine for personal work, a constraint if this becomes a product.

---

## 13. Recommended immediate next steps

1. ~~Resolve the debug question for EK-RA8D2, then order the board.~~ **Done** —
   debug resolved (R01AN7982EU0101, §11) and board ordered.
2. **Start the desktop engine now** — WAV renderer, one voice, cycle harness.
   Nothing here is blocked on hardware.
3. **Start the LVGL simulator UI** with the parameter descriptor table.
4. **Do the borrowed-filter test** whenever convenient. It's cheap and it
   settles the largest open question in the design.
