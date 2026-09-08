# Multitimbral Hybrid Synth

A 4-part / 4-voice multitimbral hybrid synthesizer, developed desktop-first: a
pure C++ audio engine plus an LVGL UI, targeting the Renesas EK-RA8D2
(Cortex-M85 audio, Cortex-M33 control). The engine renders a single voice
(polyBLEP saw → TPT SVF → ADSR); the simulator shows a minimal LVGL screen at
the panel resolution.

## Contents

| File | Description |
|---|---|
| [`CMakeLists.txt`](CMakeLists.txt) | Build: engine, sim, tests, vendored LVGL |
| [`engine/engine.h`](engine/engine.h) / [`.cc`](engine/engine.cc) | One voice: polyBLEP saw → TPT SVF → ADSR; no LVGL/SDL/OS deps |
| [`engine/dsp.h`](engine/dsp.h) | Per-sample DSP primitives (oscillator, SVF) |
| [`engine/params.h`](engine/params.h) / [`.cc`](engine/params.cc) | Parameter descriptor table + accessors |
| [`sim/main.cc`](sim/main.cc) | LVGL + SDL2 simulator (1024×600) |
| [`sim/lv_conf.h`](sim/lv_conf.h) | Minimal LVGL v9 config |
| [`tests/test_engine.cc`](tests/test_engine.cc) | Engine contract test |
| [`tests/test_params.cc`](tests/test_params.cc) | Parameter table contract test |
| [`tools/wav_render.cc`](tools/wav_render.cc) | CLI: render audio to a WAV file |
| [`tools/live_render.cc`](tools/live_render.cc) | CLI: stream audio to the playback device |
| [`tools/bench.cc`](tools/bench.cc) | Cycle harness: ns/sample/voice |
| [`lvgl/`](lvgl) | Vendored LVGL (git submodule) |
| [`third_party/miniaudio/`](third_party/miniaudio) | Vendored miniaudio (single header) |

## API

The engine is C++ — a restricted embedded subset: no exceptions, no RTTI, no
heap allocation in the audio path. (STL is fine outside the audio path.)

```cpp
#include "engine/engine.h"
#include "engine/params.h"

namespace engine {
inline constexpr int kSampleRate = 48000;        // Hz
inline constexpr int kBlockSize = 64;            // samples per block
inline constexpr int kControlDecimation = 16;    // control step every N samples

// control thread — queue events / set parameters
void EngineInit();
void EngineNoteOn(float freq_hz);
void EngineNoteOff();
void EngineSetParam(ParamId id, float norm);     // normalized 0..1
void EngineSetParamDisp(ParamId id, float disp); // display units

// audio thread — render, draining events/params at each block boundary
void Render(float *out, int frames);             // finite, clamped to [-1,1]

// parameters — walk the table instead of hardcoding (params.h)
int   ParamCount();
float ParamGet(const Voice *v, ParamId id);          // normalized 0..1
void  ParamSet(Voice *v, ParamId id, float norm);    // normalized 0..1
void  ParamSetDisp(Voice *v, ParamId id, float d);   // display units
int   ParamFormat(const Voice *v, ParamId id, char *buf, std::size_t n);
}  // namespace engine
```

## Usage

### Prerequisites

Ubuntu 22.04+:

```bash
sudo apt install -y cmake ninja-build libsdl2-dev
```

### Build

```bash
git submodule update --init          # fetch vendored LVGL
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run

```bash
./build/sim                        # LVGL simulator (needs a display)
./build/wav_render 1 out.wav       # render 1 s of audio to a WAV file
./build/live_render                # stream audio to speakers (Enter to stop)
./build/bench 5                    # cycle harness: ns/sample/voice
ctest --test-dir build             # run tests
```

## Dependencies

### Internal

None — the engine is self-contained.

### External

| Package | Version |
|---|---|
| LVGL | 9.6 (vendored submodule) |
| SDL2 | 2.x (`libsdl2-dev`) |
| miniaudio | 0.11.25 (vendored header) |

## Build Targets

| Target | Type | Description |
|---|---|---|
| `engine` | static library | Audio render core |
| `sim` | executable | LVGL + SDL2 simulator |
| `test_engine` | executable | Engine contract test |
| `test_ring` | executable | Event ring contract test |
| `test_param_block` | executable | Parameter block contract test |
| `test_split` | executable | Control/audio split test (two threads) |
| `wav_render` | executable | Render audio to a WAV file |
| `live_render` | executable | Stream audio to the playback device |
| `bench` | executable | Cycle harness: measure render cost |

## Design

- **Engine** — C++17, restricted subset (no exceptions/RTTI, no heap in the
  audio path). One voice: polyBLEP saw → TPT SVF (Zavalishin/Simper) → ADSR.
  Fixed 64-sample block, sub-block control rate every 16 samples. Voice state
  is a memcpy-able struct; no allocation, `double`, or `sin` in the audio path.
- **Parameter model** — a `constexpr` descriptor table (name, unit, display
  range, curve, target offset) in `params.h`/`params.cc`; every parameter is
  stored normalized 0..1. The UI, MIDI CC mapping, and patch save/load walk the
  table instead of knowing individual parameters.
- **Control/audio split** — the control thread queues note events over a
  lock-free single-producer/single-consumer ring and writes parameters to a
  double-buffered block; the audio thread drains both at each block boundary
  (`ipc.h`). On the target the transport is swapped for the M33↔M85 mailbox;
  the boundary contract stays the same.
- **Simulator** — vendored LVGL with the SDL2 driver at 1024×600 (the EK-RA8D2
  in-box panel resolution). `sim/lv_conf.h` enables only the SDL driver;
  everything else falls back to LVGL defaults (software renderer, no asserts,
  no vector graphics).

## Testing

Tests cover:

- [`tests/test_engine.cc`](tests/test_engine.cc) — voice lifecycle: non-silent
  and within [-1,1] while a note is held; silent after release.
- [`tests/test_params.cc`](tests/test_params.cc) — parameter table contract:
  clamping, normalized↔display round-trip, curve mapping, defaults.
- [`tests/test_ring.cc`](tests/test_ring.cc) — event ring: FIFO order, full/empty.
- [`tests/test_param_block.cc`](tests/test_param_block.cc) — parameter block:
  set/commit round-trip, defaults.
- [`tests/test_split.cc`](tests/test_split.cc) — control/audio split (two threads).

Run:

```bash
ctest --test-dir build
```

### ThreadSanitizer

Verify the control/audio split for data races:

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTWANG_ENABLE_TSAN=ON
cmake --build build-tsan --target test_split test_ring test_param_block test_engine test_params
setarch $(uname -m) -R ctest --test-dir build-tsan   # disable ASLR (TSAN requires it)
```

TSAN's runtime rejects the kernel's default ASLR layout (`unexpected memory
mapping`), so disable ASLR with `setarch -R`. If that is unavailable, build and
run on the host instead of a container.

## Reference Documents

| Document | Purpose |
|---|---|
| [`docs/workflow/briefs/2026-09-07_multitimbral-hybrid-synth_brief.md`](docs/workflow/briefs/2026-09-07_multitimbral-hybrid-synth_brief.md) | Project brief (scope, approach) |
| [`docs/random/synth-platform-exploration.md`](docs/random/synth-platform-exploration.md) | Platform/architecture digest |
