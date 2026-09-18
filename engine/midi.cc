#include "midi.h"

#include <cmath>

namespace engine {

float MidiNoteToFreq(std::uint8_t note) {
    // A4 (note 69) = 440 Hz; equal temperament.
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

}  // namespace engine
