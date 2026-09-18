// USB composite device (UAC2 capture + MIDI 2.0) setup for the cm85 image.
//
// The board's single UDC lives on cm85 (USB-HS); both classes register on it.
// Register order is load-bearing: UAC2 must own interface 0 (see the brief's
// class-order note) or snd-usb-audio exposes no PCM device.

#pragma once

namespace usb {

/// Set up and enable the composite USB device (UAC2 capture + MIDI 2.0).
/// @return 0 on success, negative errno on failure.
int Init();

}  // namespace usb
