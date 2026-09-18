// USB composite device (UAC2 capture + MIDI 2.0) setup for the cm85 image.
//
// The board's single UDC lives on cm85 (USB-HS); both classes register on it.
// Register order is load-bearing: UAC2 must own interface 0 (see the brief's
// class-order note) or snd-usb-audio exposes no PCM device.

#pragma once

#include <cstdint>

namespace usb {

/// Set up and enable the composite USB device (UAC2 capture + MIDI 2.0).
/// @return 0 on success, negative errno on failure.
int Init();

/// Push rendered int16-stereo frames into the UAC2 audio FIFO. Called by the
/// render loop (main thread); the SOF callback drains it to the host.
/// @param stereo Interleaved int16 stereo samples (2 channels per frame).
/// @param frames Number of frames to push.
void AudioPush(const int16_t *stereo, int frames);

}  // namespace usb
