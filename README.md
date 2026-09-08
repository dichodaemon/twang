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
| [`engine/engine.h`](engine/engine.h) / [`.cpp`](engine/engine.cpp) | One voice: polyBLEP saw → TPT SVF → ADSR; no LVGL/SDL/OS deps |
| [`sim/main.c`](sim/main.c) | LVGL + SDL2 simulator (1024×600) |
| [`sim/lv_conf.h`](sim/lv_conf.h) | Minimal LVGL v9 config |
| [`tests/test_engine.c`](tests/test_engine.c) | Engine contract test |
| [`tools/wav_render.c`](tools/wav_render.c) | CLI: render audio to a WAV file |
| [`tools/live_render.c`](tools/live_render.c) | CLI: stream audio to the playback device |
| [`tools/bench.c`](tools/bench.c) | Cycle harness: ns/sample/voice |
| [`lvgl/`](lvgl) | Vendored LVGL (git submodule) |
| [`third_party/miniaudio/`](third_party/miniaudio) | Vendored miniaudio (single header) |

## API

The engine is callable from C and C++.

```c
#include "engine/engine.h"

#define ENGINE_SAMPLE_RATE 48000        /* Hz */
#define ENGINE_BLOCK_SIZE 64            /* samples per block */
#define ENGINE_CONTROL_DECIMATION 16    /* control step every N samples */

void engine_init(void);
void engine_note_on(float freq_hz);
void engine_note_off(void);
void engine_set_cutoff(float normalized);    /* [0,1] */
void engine_set_resonance(float normalized); /* [0,1] */
void engine_set_filter_env(float amount);    /* [0,1] */
void engine_set_adsr(float a, float d, float s, float r);
void render(float *out, int frames);         /* finite, clamped to [-1,1] */
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
| `wav_render` | executable | Render audio to a WAV file |
| `live_render` | executable | Stream audio to the playback device |
| `bench` | executable | Cycle harness: measure render cost |

## Design

- **Engine** — C++17 internally, `extern "C"` entry point, no LVGL/SDL/OS
  dependencies. One voice: polyBLEP saw → TPT SVF (Zavalishin/Simper) → ADSR.
  Fixed 64-sample block, sub-block control rate every 16 samples. Voice state
  is a memcpy-able struct; no allocation, `double`, or `sin` in the audio path.
- **Simulator** — vendored LVGL with the SDL2 driver at 1024×600 (the EK-RA8D2
  in-box panel resolution). `sim/lv_conf.h` enables only the SDL driver;
  everything else falls back to LVGL defaults (software renderer, no asserts,
  no vector graphics).

## Testing

Tests cover:

- [`tests/test_engine.c`](tests/test_engine.c) — voice lifecycle: non-silent
  and within [-1,1] while a note is held; silent after release.

Run:

```bash
ctest --test-dir build
```

## Reference Documents

| Document | Purpose |
|---|---|
| [`docs/workflow/briefs/2026-09-07_multitimbral-hybrid-synth_brief.md`](docs/workflow/briefs/2026-09-07_multitimbral-hybrid-synth_brief.md) | Project brief (scope, approach) |
| [`docs/random/synth-platform-exploration.md`](docs/random/synth-platform-exploration.md) | Platform/architecture digest |
