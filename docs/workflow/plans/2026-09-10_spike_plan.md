---
title: spike -- Implementation Plan
status: issued
date: 2026-09-10
author: Dizan Vasquez
arch-design: ../arch-designs/spike_arch-design.md
---

# spike

Migrate the twang cm33 controller off LVGL onto `spike`, the purpose-built minimal RGB565 renderer. This plan is the *how*; the *what* and *why* are the arch-design (`../arch-designs/spike_arch-design.md`). It does not duplicate the arch-design's contracts or acceptance criteria — it sequences the work and names the files.

## 1. Phases

1. **Font tooling** — vendor Terminus BDF + write the BDF→C emitter. Produces the generated glyph data (no deps).
2. **spike core** — `fb`, `font`, `damage` (the renderer) + their tests, wired into `CMakeLists.txt`. Depends on Phase 1's glyph data.
3. **spike panel** — the Nostromo port of `controller/ui.cc`. Depends on Phase 2.
4. **Backends** — SDL (desktop) + GLCDC (target). Depends on Phase 3.
5. **Cutover** — delete LVGL, retarget the build, amend stale docs. Depends on Phase 4.
6. **Verification** — flash measurement, desktop parity, hardware smoke.

## 2. Implementation Status

| # | Task | Status |
|---|---|---|
| 1.1 | Vendor `assets/fonts/ter-u20n.bdf`, `ter-u14n.bdf`, `assets/fonts/OFL.txt` (Terminus 4.49, OFL 1.1) | Pending |
| 1.2 | Write `tools/bdf_to_c.py` — parse BDF, emit `spike/font_data.h` (1-bpp left-aligned bitmaps + header); `--check` mode decodes the mockup's `A20`/`A14` and diffs against the emitted atlas | Pending |
| 1.3 | Generate `spike/font_data.h` by running the script | Pending |
| 1.4 | Verify: `python3 tools/bdf_to_c.py --check ui/mockup/nostromo_signal-flow.html` exits 0 (emitted atlas bit-identical to the mockup's `A20`/`A14` glyph data) | Pending |
| 2.1 | Implement `spike/fb.h` + `spike/fb.cc` — `Rect`, `Point`, `FrameBuffer`, `Color`, `FillRect`, `DrawHLine`, `DrawVLine`, `DrawPolyline`, `SetClip` (32-bit fill stores, clip to `fb.clip ∩` bounds) | Pending |
| 2.2 | Write test: `tests/test_fb.cc` — pixel-exact primitives + edge clipping | Pending |
| 2.3 | Implement `spike/font.h` + `spike/font.cc` — `Font` + `DrawGlyphRun` (1-bpp left-aligned, no blend) | Pending |
| 2.4 | Write test: `tests/test_font.cc` — `DrawGlyphRun`, incl. the 10-bit-in-16-bit left-alignment on `ter-u20n` | Pending |
| 2.5 | Implement `spike/damage.h` + `spike/damage.cc` — fixed-capacity rect list, merge-on-overlap, whole-screen fallback, two-frame union | Pending |
| 2.6 | Write test: `tests/test_damage.cc` — merge, fallback, two-frame union | Pending |
| 2.7 | Add `spike` lib (fb + font + damage) + `test_fb`/`test_font`/`test_damage` targets to `CMakeLists.txt` | Pending |
| 2.8 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` passes | Pending |
| 3.1 | Implement `spike/panel.h` + `spike/panel.cc` — `Panel` (state, dynamic-region table, damage, `TraceState`), `PanelCreate`, `PanelDraw`, `PanelPointer`, `PanelNoteOn`, `PanelNoteOff`, `PanelAudioTap` | Pending |
| 3.2 | Port the parameter math helpers (`norm_to_hz`, `hz_to_norm`, `q_of`, `res_to_db`, `db_to_res`) from `controller/ui.cc` into `spike/panel.cc` | Pending |
| 3.3 | Implement the static chrome (titlebar, 4 module frames, keyboard, nav tabs) per `ui/mockup/nostromo_signal-flow.html` | Pending |
| 3.4 | Implement the 4 dynamic regions (scope, filter response, envelope, output) with column-update traces + graticule-aware erase | Pending |
| 3.5 | Wire note-on/off → `engine::EngineNoteOn/Off` + envelope playhead; audio tap → `ScopeRing::Write` | Pending |
| 3.6 | Write test: `tests/test_panel.cc` — golden-image (fixed state → bitmap) + note-on drives playhead state + invalidation-gated redraw (dirty slot repaints, clean slot doesn't) | Pending |
| 3.7 | Add `panel` to the `spike` lib + register `test_panel` in `CMakeLists.txt` | Pending |
| 3.8 | Verify: `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` passes (incl. `test_panel`) | Pending |
| 4.1 | Implement `host/sdl_backend.cc` + rewrite `host/main.cc` — SDL window, blit, mouse/keyboard → `PointerEvent` → `PanelPointer` | Pending |
| 4.2 | Update `host/midi_io.h` + `host/midi_io.cc` — `Poll(Ui*)` → `Poll(Panel*)`, notes route through `PanelNoteOn/Off` | Pending |
| 4.3 | Implement `target/zephyr/cm33/src/glcdc_backend.cc` + rewrite `target/zephyr/cm33/src/main.cc` — GLCDC framebuffer + FT5336 touch, Panel in SDRAM; enable `CONFIG_INPUT` + `CONFIG_INPUT_FT5336` in `prj.conf` and add the FT5336 touch node (i2c1) to `app.overlay` | Pending |
| 4.4 | Verify: desktop smoke (host renders the panel to an SDL window); cm33 builds (`west build -d /tmp/zephyr-cm33`) | Pending |
| 5.1 | Remove `controller/ui.cc`, `controller/ui.h`, `controller/lv_conf.h` | Pending |
| 5.2 | Remove the `lvgl/` submodule + `add_subdirectory(lvgl)` + LVGL cache vars from `CMakeLists.txt` | Pending |
| 5.3 | Retarget `host` to spike + SDL and trim `controller` to fft + scope_ring in `CMakeLists.txt` (the `spike` lib was added in 2.7/3.7) | Pending |
| 5.4 | Update `target/zephyr/cm33/CMakeLists.txt` — add `src/glcdc_backend.cc` + spike sources, drop the LVGL module include | Pending |
| 5.5 | Remove `CONFIG_LV*` from `target/zephyr/cm33/prj.conf` (keep `CONFIG_DISPLAY`, `CONFIG_RENESAS_RA_GLCDC`, `CONFIG_MBOX`) | Pending |
| 5.6 | Amend `../arch-designs/spike_arch-design.md`: drop "still tested" from the `fft.cc`/`scope_ring.cc` criterion (no such tests exist), drop "descriptor interpreter + screen descriptors" from the engine-footprint criterion (descriptor deferred, DD §6), and mark the Nostromo appendix "adopted" (validated by the golden-image/parity checks, tasks 3.6/6.2) | Pending |
| 5.7 | Archive `../design-studies/2026-09-09_controller-engine-target-architecture_design-study.md` (references `ui_note_on`/`ui_audio_tap`/`Ui`, renamed/removed by this migration) | Pending |
| 5.8 | Verify: desktop `cmake --build /tmp/twang-build && ctest --test-dir /tmp/twang-build` and cm33 `west build -d /tmp/zephyr-cm33` all pass with LVGL absent | Pending |
| 6.1 | Measure cm33 flash (`nm --size-sort` on `/tmp/zephyr-cm33/zephyr/zephyr.elf`): FLASH ≤ 50% (≤ 131072 B) | Pending |
| 6.2 | Desktop parity: golden-image + SDL smoke render matches the mockup | Pending |
| 6.3 | Hardware smoke (if board available): panel renders to GLCDC, touch drives the filter XY pad | Pending |
| 6.4 | Verify: full build + ctest + flash measurement; record the flash % in bead `twang-nro.9` | Pending |

## 3. Architecture

### 3.1 Directory layout

| File | Change |
|---|---|
| `spike/fb.h`, `spike/fb.cc` | New: `Rect`, `Point`, `FrameBuffer`, `Color`, primitives + `SetClip` |
| `spike/font.h`, `spike/font.cc` | New: `Font`, `DrawGlyphRun` |
| `spike/damage.h`, `spike/damage.cc` | New: damage list, merge + fallback + two-frame rule |
| `spike/panel.h`, `spike/panel.cc` | New: `Panel` + the six entry points |
| `spike/font_data.h` | New (generated): 1-bpp glyph bitmaps + header |
| `tools/bdf_to_c.py` | New (authored): BDF→C emitter |
| `assets/fonts/ter-u20n.bdf`, `ter-u14n.bdf`, `OFL.txt` | New (vendored): Terminus BDF + license |
| `host/sdl_backend.cc` | New: SDL window + blit + input |
| `host/main.cc`, `host/midi_io.h`, `host/midi_io.cc` | Modified: LVGL → spike + SDL; `Poll(Ui*)` → `Poll(Panel*)` |
| `target/zephyr/cm33/src/glcdc_backend.cc` | New: GLCDC framebuffer + FT5336 touch |
| `target/zephyr/cm33/src/main.cc`, `target/zephyr/cm33/CMakeLists.txt`, `target/zephyr/cm33/prj.conf` | Modified: LVGL auto-init → spike + GLCDC; drop LVGL config |
| `target/zephyr/cm33/app.overlay` | Modified: add FT5336 touch node (i2c1) for spike input |
| `CMakeLists.txt` | Modified: add spike lib (fb+font+damage+panel) + spike test targets, trim controller, drop LVGL, retarget host |
| `tests/test_fb.cc`, `tests/test_font.cc`, `tests/test_damage.cc`, `tests/test_panel.cc` | New: spike tests |
| `controller/ui.cc`, `controller/ui.h`, `controller/lv_conf.h` | Removed: replaced by `spike/panel.*` |
| `lvgl/` | Removed: vendored submodule |
| `controller/fft.{h,cc}`, `controller/scope_ring.{h,cc}` | Unchanged |

### 3.2 Dependency graph

```
spike/panel ──> spike/fb, spike/font, spike/damage, controller/fft, controller/scope_ring, engine
spike/font  ──> spike/font_data.h (generated by tools/bdf_to_c.py), spike/fb
spike/damage ──> spike/fb
host        ──> spike, audio, rtmidi, SDL2
target cm33 ──> spike, engine, Zephyr (GLCDC, MBOX)
```

The `controller` lib is trimmed to `fft` + `scope_ring` and gains no new deps; `spike` is a new static lib that links `controller` + `engine`. LVGL disappears from every target.

## 4. Interface Changes

### `spike/fb.h` — types added

```cpp
namespace spike {
struct Rect { int x, y, w, h; };
struct Point { int x, y; };
struct FrameBuffer { std::uint16_t *px; int w, h, stride; Rect clip; };
using Color = std::uint16_t;
}  // namespace spike
```

### `spike/font.h` — types added

```cpp
struct Font { int w, h, base; const std::uint8_t *bitmap; };  // 1-bpp, (w+7)/8 bytes/row
```

### `spike/panel.h` — types added

```cpp
enum class PointerKind { kPress, kMove, kRelease };
struct PointerEvent { PointerKind kind; int x, y; };
struct DynRegion { Rect rect; void (*draw)(FrameBuffer&, const Rect&, void*); void* state; bool dirty; };
struct TraceState { std::uint8_t y0[230], y1[230]; };
struct Panel;  // opaque
```

### Functions added (all `namespace spike`)

```cpp
void FillRect    (FrameBuffer&, int x, int y, int w, int h, Color);
void DrawHLine   (FrameBuffer&, int x, int y, int w, Color);
void DrawVLine   (FrameBuffer&, int x, int y, int h, Color);
void DrawGlyphRun(FrameBuffer&, int x, int y, const char* s, int n, const Font&, Color, int tracking);
void DrawPolyline(FrameBuffer&, const Point* pts, int n, Color);
void SetClip     (FrameBuffer&, int x, int y, int w, int h);
void DrawDyn     (DynRegion&, FrameBuffer&);
Panel *PanelCreate();
void PanelDraw(Panel*, FrameBuffer&);
void PanelPointer(Panel*, PointerEvent);
void PanelNoteOn(Panel*, float freq_hz);
void PanelNoteOff(Panel*, float freq_hz);
void PanelAudioTap(Panel*, const float* samples, int n);
```

### Functions removed (replaced)

| Removed | Replacement | Callers updated |
|---|---|---|
| `ui_create(lv_obj_t*)` | `PanelCreate()` | `host/main.cc`, `target/zephyr/cm33/src/main.cc` |
| `ui_note_on(Ui*, float)` | `PanelNoteOn(Panel*, float)` | `host/midi_io.cc`, `target/zephyr/cm33/src/main.cc` |
| `ui_note_off(Ui*, float)` | `PanelNoteOff(Panel*, float)` | `host/midi_io.cc` |
| `ui_audio_tap(Ui*, const float*, int)` | `PanelAudioTap(Panel*, const float*, int)` | `host/main.cc` |

> `controller/ui.cc` also calls `ui_note_on`/`ui_note_off` internally (on-screen keyboard handler, lines 677/679); that file is deleted in task 5.1 and the keyboard note routing is re-implemented in task 3.5.

### Functions modified

#### `MidiIo::Poll` (`host/midi_io.h`, `host/midi_io.cc`)

```cpp
// before
void Poll(Ui *ui);
// after
void Poll(Panel *panel);
```

The note-on/note-off routing inside `Poll` switches from `ui_note_on`/`ui_note_off` to `PanelNoteOn`/`PanelNoteOff`; the parameter send path is unchanged. Single caller: `host/main.cc:60` (`midi.Poll(ui)` → `midi.Poll(panel)`).

### Types removed

- `struct Ui` (LVGL-laden: `lv_obj_t*` arrays + scratch) → `struct Panel` (spike-native: state + dynamic regions + damage + `TraceState`).
- `struct PlotData` → folded into the panel's dynamic-region table.
- `struct UiState`, `PlotKind`, `ScopeMode` (all defined in `ui.h`) → folded into `Panel`'s state; removed with the file.

## 5. Solution Breakdown

### 5.1 Font tooling (tasks 1.1–1.4)

Task 1.1 vendors the two Terminus BDFs + the OFL license; task 1.2 writes the emitter; task 1.3 runs it to produce `spike/font_data.h`; task 1.4 verifies. The emitter reads `ter-u20n.bdf` and `ter-u14n.bdf`, emits `spike/font_data.h` with one `const uint8_t` array per atlas (1-bpp, row-major, `(w+7)/8` bytes/row, 95 glyphs 0x20–0x7E) plus a header (`w`, `h`, `base`, glyph count).

- **Logic**: parse `FONTBOUNDINGBOX` for cell dims; for each `STARTCHAR` in the ASCII range, extract the hex bitmap, pack each row left-aligned into `ceil(w/8)` bytes.
- **Edge cases**: glyphs narrower than the cell are padded with the BDF `BBX` offset so they sit correctly in the cell; a 10-wide glyph is left-aligned in a 16-bit row word (`bit = 1 << (bytes_per_row*8 - 1 - x)`).
- **Dependencies**: produces `spike/font_data.h` (consumed by task 2.3).
- **Done**: task 1.4 — emitted atlas matches the mockup's `A20`/`A14` bit-for-bit.

### 5.2 `spike/fb` (task 2.1)

The primitives. `FillRect`/`DrawHLine`/`DrawVLine` write with 32-bit stores (two RGB565 pixels per store) and clip to `fb.clip ∩` the bounds; `DrawPolyline` is a Bresenham walk (the only scattered-access primitive); `SetClip` sets `fb.clip`.

- **Edge cases**: zero/negative width or height is a no-op; coordinates outside the framebuffer are clipped per-pixel.
- **Dependencies**: produces the types + primitives every later module consumes. Task 2.7 wires `fb` (with `font` + `damage`) and the three spike tests into `CMakeLists.txt` so Verify 2.8 can build and run them.
- **Done**: task 2.8 — pixel-exact tests pass, including edge clipping.

### 5.3 `spike/font` (task 2.3)

`DrawGlyphRun` blits `n` glyphs left-to-right at `x + i*(w + tracking)`, unpacking each row left-aligned and writing lit pixels only (no blend — never reads the destination).

- **Edge cases**: codepoints outside 0x20–0x7E fall back to glyph 0; the 10-bit-in-16-bit unpacking is unit-tested against `ter-u20n`.
- **Dependencies**: consumes `spike/font_data.h` (Phase 1) + `spike/fb`.
- **Done**: task 2.8.

### 5.4 `spike/damage` (task 2.5)

Fixed-capacity `Rect` array; `Add` merges overlapping rects; on overflow the whole list collapses to one full-screen rect; `RepaintUnion` returns `damage[n] ∪ damage[n−1]`.

- **Edge cases**: the two-frame union must keep a rect alive for one extra frame (double buffering); overflow degrades to full-screen rather than dropping a region.
- **Dependencies**: consumes `spike/fb` (`Rect`).
- **Done**: task 2.8.

### 5.5 `spike/panel` (tasks 3.1–3.5)

The Nostromo port. `PanelCreate` allocates the panel (SDRAM placement on the target); `PanelDraw` repaints the damage union and swaps; `PanelPointer` handles touch/drag (filter XY pad, envelope handles, keyboard); `PanelNoteOn/Off` drive the engine + envelope playhead; `PanelAudioTap` writes the scope ring.

- **Layout + chrome** (task 3.3) per the mockup: titlebar, four 242-px modules (`OSCILLATOR`/`FILTER`/`ENVELOPE`/`OUTPUT`), a 13-key keyboard, nav tabs, encoder legend — drawn with the `FillRect`/`DrawHLine`/`DrawVLine`/`DrawGlyphRun` vocabulary plus `brackets`/`panelHeader`/`cursor`/`plotFrame` helpers.
- **Dynamic regions** (task 3.4): the four plots + readouts are `DynRegion` slots. Each plot keeps a `TraceState` (two copies per buffer); per frame it updates only changed columns (`~30×` fewer pixels) and erases the old span graticule-aware (`is_graticule(y) ? grat : bg`, no mask). Redraws are driven by per-slot invalidation flags set by the parameter layer, never a global timer.
- **Math** (task 3.2): the frequency/resonance/envelope mapping (`norm_to_hz`, `hz_to_norm`, `q_of`, `res_to_db`, `db_to_res`) is ported unchanged — the UI never hardcodes a parameter.
- **Edge cases**: a slot animated in frame *n* repaints in *n+1* (two-frame rule); the sawtooth reset (three ~180-px columns) is the worst case and is bounded.
- **Dependencies**: consumes `spike/fb` + `spike/font` + `spike/damage` + `controller/fft` + `controller/scope_ring` + `engine`. Task 3.7 adds `panel` to the `spike` lib and registers `test_panel` in `CMakeLists.txt`.
- **Done**: task 3.8 — golden image matches the mockup (the reference bitmap is rendered once from the mockup's fixed state and stored as a test fixture; it is re-derived only when the mockup changes), note-on drives the playhead, invalidation gates the redraws.

### 5.6 SDL backend + MIDI routing (tasks 4.1–4.2)

Replaces the LVGL SDL drivers in `host/main.cc`: owns the framebuffer (double-buffered 1024×600 RGB565), presents it, translates SDL mouse/keyboard into `PointerEvent`, and calls `PanelDraw`/`PanelPointer`.

`host/midi_io.h` + `host/midi_io.cc` (task 4.2): `MidiIo::Poll(Ui*)` becomes `MidiIo::Poll(Panel*)`; the note-on/note-off cases route through `PanelNoteOn`/`PanelNoteOff` instead of `ui_note_on`/`ui_note_off`. The parameter send path is unchanged. Single caller updated: `host/main.cc:60`.

- **Done**: task 4.4 — the host renders the panel to an SDL window and MIDI notes drive the panel playhead.

### 5.7 GLCDC backend (task 4.3)

Replaces the LVGL auto-init in `target/zephyr/cm33/src/main.cc`: the framebuffer is the GLCDC frame buffer in SDRAM (`ext-ram = <&sdram1>`), the panel is placement-new'd into SDRAM (`TWANG_UI_SDRAM`), FT5336 touch becomes `PointerEvent`.

- **Done**: task 4.4 — the cm33 image builds and links spike + GLCDC without LVGL.

### 5.8 LVGL cutover (tasks 5.1–5.5)

Delete `controller/ui.{h,cc}` + `lv_conf.h`, the `lvgl/` submodule, the Zephyr `CONFIG_LV*` config, and the LVGL module include; retarget the builds. The portable `controller/fft.cc` + `scope_ring.cc` stay.

- **Done**: task 5.8 — both desktop and cm33 build clean with LVGL absent.

### 5.9 Document updates (tasks 5.6–5.7)

The migration renames/removes `ui_note_on`, `ui_audio_tap`, and `struct Ui`, so the arch-design needs three amendments (task 5.6) and the design study is superseded (task 5.7): drop "still tested" from the `fft.cc`/`scope_ring.cc` criterion (no such tests exist); drop "descriptor interpreter + screen descriptors" from the engine-footprint criterion (the descriptor is deferred, DD §6); mark the Nostromo appendix "adopted" (validated by tasks 3.6/6.2). Archive the design study (point-in-time artifact). The empty `sim/` directory (pre-split layout) is a leftover; removing it is optional and outside this migration.

- **Done**: task 5.8 — the amended arch-design and archived design study are consistent with the shipped code.

### 5.10 Verification (tasks 6.1–6.4)

Measurement and smoke, no production code: task 6.1 reads the cm33 ELF section sizes and asserts FLASH ≤ 50% (≤ 131072 B); task 6.2 re-runs the golden-image and SDL smoke against the mockup; task 6.3 is the conditional on-board smoke (render + touch XY pad). Task 6.4 is the phase gate that records the flash % in bead `twang-nro.9`.

- **Done**: task 6.4 — full build + ctest + flash measurement recorded.

## 6. Design Decisions

- **Font data is generated, never hand-edited.** `spike/font_data.h` is produced by `tools/bdf_to_c.py`; edits go to the script or the BDF sources. The atlas must match the mockup's `A20`/`A14` bit-for-bit (task 1.4) so the desktop and target render identically.
- **The panel is a redesign, not a 1:1 port.** `controller/ui.cc` uses a light teal/orange palette; Nostromo is green phosphor. The layout (titlebar, four modules, keyboard, tabs) is ported, the visual treatment follows the arch-design's Nostromo appendix. Nostromo is still marked "proposed" there; this plan adopts it as the shipped design (the mockup `nostromo_signal-flow.html` is the validation target), so that approval is folded into the golden-image + parity checks (tasks 3.6, 6.2).
- **Chrome is emitted drawing calls** (per the arch-design): the descriptor byte-stream is deferred until a second screen lands.
- **Left-alignment is a deliberate pitfall to test.** A 10-wide glyph in a 2-byte row word is left-aligned; the 8-wide atlas (where `w == 8`) would pass a test the 10-wide one fails, so `ter-u20n` is unit-tested specifically.
- **Axis labels use the 8×14 atlas as-is.** The arch-design leaves open whether 8×14 is cramped in a ~230 px plot ("shorter labels or a third atlas"). The port draws the `20Hz`/`20k`/`0dB` axis labels with `ter-u14n` unchanged; if the hardware smoke (task 6.3) shows cramping, shorten the labels or add a third atlas — never silently shrink the type.
- **Remaining Nostromo open questions are pinned to the mockup, not re-decided here.** The arch-design's Open Questions leave four items unresolved: dim-well weight (4 px vs faint), mod-matrix cell assignment (88 intersections vs 32 slots), save-dialogue default (overwrite vs Save As New), and uppercase-only patch names. This port pins each to whatever `nostromo_signal-flow.html` shows — the golden image enforces it (task 3.6) — and anything the mockup does not specify stays deferred to the hardware smoke (task 6.3). No silent redesign.

## 7. Success Criteria

### Renderer (spike core)
- [ ] `FillRect`/`DrawHLine`/`DrawVLine`/`DrawPolyline`/`SetClip` pass pixel-exact tests incl. edge clipping — Verify 2.8
- [ ] `DrawGlyphRun` renders `ter-u20n` left-aligned correctly — Verify 2.8
- [ ] Damage list merges, overflows to full-screen, and unions across frames — Verify 2.8

### Panel
- [ ] Golden image matches the mockup layout at fixed state — Verify 3.8
- [ ] `PanelNoteOn` drives the engine and the envelope playhead — Verify 3.8
- [ ] The 4 dynamic regions redraw only on invalidation (asserted by `test_panel`) — Verify 3.8

### Backends + cutover
- [ ] Host renders the panel to an SDL window — Verify 4.4
- [ ] cm33 builds + links spike + GLCDC without LVGL — Verify 4.4
- [ ] Desktop build + ctest + cm33 build pass with LVGL absent — Verify 5.8

### Flash
- [ ] cm33 flash ≤ 50% of 256 KiB (≤ 131072 B) — Verify 6.4
- [ ] `controller` lib is trimmed to `fft` + `scope_ring` and compiles without LVGL — Verify 5.8

## 8. Document Staleness Audit

| Document | Invalidated? | Action |
|---|---|---|
| `../arch-designs/spike_arch-design.md` | Yes — (a) criterion "`fft.cc`/`scope_ring.cc` unchanged and **still tested**" is false (no such tests exist); (b) the engine-footprint criterion lists "descriptor interpreter + screen descriptors", superseded by the descriptor deferral (DD §6); (c) the Nostromo appendix still says "Status: proposed" | Task 5.6: amend all three (drop "still tested"; drop the descriptor terms; mark Nostromo adopted) |
| `../../random/engine-recommendations.md` | No — non-prescriptive rationale source; the plan follows its settled decisions | None |
| `ui/mockup/nostromo_signal-flow.html` | No — reference for the atlas data + layout | None |
| `controller/ui.h` / `controller/ui.cc` | Yes — deleted by the cutover | Removed in Phase 5 (the code is the artifact, not a doc) |
| `../design-studies/2026-09-09_controller-engine-target-architecture_design-study.md` | Yes — references `ui_note_on`/`ui_audio_tap`/`Ui`, which the migration renames/removes | Task 5.7: archive (design studies are point-in-time) |

## 9. Cleanup

No diagnostic instrumentation is added by this plan. The flash measurement (task 6.1) reads `nm --size-sort` and the build's FLASH/RAM summary — nothing to remove afterward.
