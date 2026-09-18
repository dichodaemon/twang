---
title: USB Audio + MIDI Device (Composite)
date: 2026-09-18
author: Dizan Vasquez
---

# USB Audio + MIDI Device (Composite)

## 1. Objective

Frame the EK-RA8D2 prototype's external I/O so that MIDI control input and audio output land on one USB connection: the board enumerates as a composite USB device (UAC2 audio + MIDI 2.0) hosted by the Mac/Linux, which also routes MIDI from the controllers in software. The immediate work is a spike that proves the composite is viable on this board before any engine integration is attempted.

## 2. Scope

| In scope | Out of scope |
|---|---|
| Transport decision: MIDI input + audio output for the prototype | Engine integration (which core hosts USB, cross-core data path) |
| Composite USB device approach: `usbd_uac2` + `usbd_midi2` on one UDC | Full Stage-1 audio deliverable (UAC2 driven by the SSIE0 I²S clock) |
| The spike: build, flash, and verify the composite on the EK-RA8D2 | Native USB host on the RA8D2 (deferred to its own epic) |
| The UART/DIN fallback, documented as requiring hardware | UART/DIN implementation |

## 3. Sources

1. Zephyr tree at `/workspace/zephyrproject/zephyr` (release **4.4.2**):
   - `subsys/usb/device_next/class/usbd_uac2.c`, `usbd_midi2.c` — the two class drivers.
   - `drivers/usb/udc/udc_renesas_ra.c` — RA device controller; `drivers/usb/uhc/` — host controllers (no RA entry).
   - `subsys/usb/device_next/usbd_init.c` — endpoint assignment (`assign_ep_addr`).
   - Samples: `samples/subsys/usb/midi`, `samples/subsys/usb/uac2_implicit_feedback`.
2. `engine/midi.{h,cc}` — transport-agnostic MIDI handler (`MidiDispatch` → `EngineControl`), already built and tested; both transports end here.
3. `target/zephyr/` — the cm33/cm85 app split; hardware is the EK-RA8D2 (USBFS + USBHS USB-C connectors, plus a separate J-Link debug USB).

## 4. Approach

### 4.1 The settled decision

The board is a **USB device**, never a host, on a **single** USB connection, carrying both audio and MIDI as one composite device:

```
X-Touch/Deluge ──► H2MIDI Pro ── USB-C ──► Mac/Linux ──(route)──► Renesas MIDI2 endpoint
Renesas ── USB (one cable) ──► Mac/Linux   (composite: UAC2 audio + MIDI2)
                                        └──► Scarlett (audio monitor)
```

Rejected alternatives and why:

- **USB host** (H2MIDI Pro USB-C client → board): Zephyr has no RA USB host driver (`drivers/usb/uhc/` = max3421e/mcux/virtual only); FSP ships `r_usb_host` but no MIDI class. Weeks of bring-up.
- **Two USB devices** (MIDI device to H2MIDI USB-A *and* audio device to Mac): the device-next stack is single-UDC per firmware; two concurrent USB devices is unproven.
- **UART/DIN** (H2MIDI DIN OUT → board SCI): firmware is ~a day, but the board has no DIN circuitry — it needs a DIN-5 jack + optoisolator + passives. That is parts and soldering, violating the "no soldering" goal. Retained only as the fallback.

### 4.2 The spike

Prove the composite is real on this board before committing to engine integration. Two specific failure modes sit between "composite idea" and "working dev rig", so the spike must test the *combination*, not the pieces:

1. **Composite, not two samples.** `samples/subsys/usb/midi` and `uac2_implicit_feedback` run separately would each pass and prove nothing about the case at risk.
2. **MIDI 2.0 negotiation, not enumeration.** `usbd_midi2` advertises a MIDI 1.0 alternate setting but its implementation is a stub (`LOG_WRN "... not implemented!"`); every functional path is gated on `MIDI2_ALTERNATE`. A host that negotiates MIDI 1.0 gets a well-formed, *silent* device. Verify the host settles on alternate 1 (`amidi -l` vs the UMP interface, plus `dmesg`, on Linux).
3. **Endpoint overlap.** The allocator (`assign_ep_addr`) tracks endpoints in a bitmap keyed on number+direction and defers capability to `udc_ep_try_config`; the RA driver marks every index bulk+interrupt+iso at once. Zephyr issue #103324 reports bulk/isochronous index collision in composite devices (disconnect/reconnect loop). Check `lsusb -v` for overlapping endpoint numbers before concluding.

If the composite passes all three, it becomes the path. If it fails either risk, fall back to UART/DIN (accepting the DIN input circuit).

### 4.3 Spike outcome (2026-09-18)

Passed all three. `2fe3:000f "twang composite spike"` enumerates as one device; Linux 6.8 negotiates MIDI 2.0 alternate 1 (the `MIDI 2.0` UMP port and `Group 1` are present); and `lsusb -v` shows clean endpoints — EP 1 bulk (MIDI), EP 2 isochronous (UAC2), no overlap. The composite is viable; the UART/DIN fallback is not needed for the composite itself.

One correction to record: the first bring-up attempts returned `-EINVAL` from `usbd_init`, which was *not* a Zephyr bug — `uac2_init()` hard-requires `usbd_uac2_set_ops()` and the spike app had omitted it. Fixed in-app (~5 lines); no Zephyr change.

Follow-up (resolved, 2026-09-18): the PCM rejection was diagnosed via `snd_usb_audio` dynamic debug — `3:1 : bogus bTerminalLink 3`. Root cause: snd-usb-audio takes the *first* AudioControl interface as its control interface (`sound/usb/card.c`); Zephyr's `usbd_register_all_classes()` orders classes by linker-section order, and `usbd_midi2.o` links before `usbd_uac2.o`, so MIDI 2.0's UAC1-style AudioControl interface got interface 0. The UAC2 AS (interface 3) terminal lookup then searched MIDI's AC (which has no output terminal) and failed. Fix (verified in the spike): register `uac2_0` before `midi_0` — `usbd_class_append` is `sys_slist_append`, so call order = interface order. Result: `arecord -l` shows the capture PCM (card 3) and MIDI 2.0 still enumerates (Group 1).

## 5. Constraints

- **Zephyr 4.4.2 only** — no RA USB host driver; host mode is out regardless of the H2MIDI Pro's capabilities.
- **`usbd_midi2` is MIDI 2.0 only** — there is no MIDI 1.0 device class; the host must be MIDI 2.0-capable (macOS 11+, Linux ≥ 6.5 with ALSA UMP) *and* select alternate 1.
- **Endpoint-overlap bug may be live** — 4.4.2 carries no reference to the #103324 fix; assume it until the spike shows otherwise.
- **Single USB device per firmware** — the device-next stack is single-UDC; the one device role is consumed by audio+MIDI together.
- **No soldering** — the board has no DIN or analog-out circuitry; the composite is the only zero-hardware path.

## 6. Open Questions

- Does `udc_renesas_ra` actually work on the RA8D2 (the driver is 2024 code, unproven on this part)? — *Answered: yes — the composite enumerates and MIDI works.*
- Which core hosts the USB device, cm33 or cm85, given audio renders on cm85 and MIDI feeds control on cm33? — *Answered: cm85 — the board's `zephyr_udc0` sits under `&usbhs` in the cm85 devicetree.*
- Will the Mac's MIDI stack negotiate alternate 1 (MIDI 2.0) rather than falling back to the mute stub? — *Answered: Linux 6.8 + ALSA UMP does (verified); macOS untested but expected (11+).*

## 7. Appendix: Stage-1 execution plan (Audio + MIDI)

Goal: play the synth end-to-end — X-Touch (MIDI) → Linux → (route) → Renesas
MIDI 2.0 → engine → UAC2 → Linux → monitor — on one composite USB connection.
Tracked under epic "Audio & Midi - Stage 1".

Settled inputs:

- **X-Touch connects direct to Linux**, no H2MIDI Pro (its config tool is
  macOS/Windows/Android only). `aconnect` routes X-Touch → the Renesas MIDI 2.0
  endpoint. Firmware-neutral.
- **The on-board codec stays as clock + debug tap only.** SSIE keeps running
  (it is the sample clock; J38 analog is unfitted so it costs nothing), and the
  scope tap remains the engine-vs-USB discriminator. Audible output is UAC2 →
  host → monitor.

Stages:

1. **Durable composite in cm85** — move the spike's UAC2 (capture-only) +
   MIDI 2.0 overlay and setup into `target/zephyr/cm85`, with the class-order
   fix (`uac2_0` before `midi_0`) and the runtime guard. Both classes enumerate
   on the real app.
2. **Render clock onto a kernel timer** (alone) — replace the `k_busy_wait`
   block timer with a `k_timer`-driven render loop. Verify on the scope tap:
   identical sound, now timer-clocked. No UAC2 yet — one behavioural change at
   a time. *Deviation (2026-09-18): the SSIE MCLK (GPT PWM 3.072 MHz) does not
   complete transfers on this board — a DMA-pull loop deadlocks after the 4
   slab blocks are consumed (`num_used=4`, stuck on `k_mem_slab_alloc`). The
   SSIE cannot serve as the render clock; the `k_timer` replaces it. The MCLK
   fix is tracked separately.*
3. **UAC2 audio out** — elastic FIFO between the render loop and UAC2: the
   `k_timer` render loop writes int16-stereo blocks into a FIFO; `sof_cb`
   drains into `usbd_uac2_send()`. `usbd_uac2_send()` is driven from the SOF
   callback, not the render cadence — async IN absorbs host/device clock drift
   by varying packet size per frame (HS: 125 µs microframes, nominal 6 samples
   / 24 B each; the FIFO absorbs the ±1-sample drift).
4. **MIDI in** — reverse cm85→cm33 channel carrying **UMP 32-bit words** (no
   downconvert on cm85); cm33 downconverts to MIDI 1.0 immediately before
   `MidiMessage(control, kXtouchCompact, …)`, preserving MIDI 2.0 resolution
   for later.

Order is audio-out before MIDI-in so there is a working output path to hear
MIDI arrive on.
