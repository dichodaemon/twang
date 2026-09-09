/// @file audio_out.h
/// @brief Realtime audio output abstraction.
///
/// The synth renders mono float32 audio through a device-independent callback.
/// A backend (miniaudio today; a hardware DMA driver on the M85 later) owns
/// the actual device. Callers see only this interface — no backend types leak
/// into the rest of the program, and the audio thread is handed a plain
/// render callback (the same shape the engine's `Render` already has).

#pragma once

namespace audio {

/// Realtime mono audio output.
///
/// The stream is mono float32 at the requested sample rate. The render
/// callback runs on the backend's audio thread and must be real-time safe.
class Output {
  public:
    /// Audio-thread render callback: fill `out[0..frames)` with mono float
    /// samples in [-1, 1]. `user` is the context passed to `Start`.
    using Callback = void (*)(void *user, float *out, int frames);

    Output() = default;
    ~Output();  ///< Stops and releases the device if started.

    Output(const Output &) = delete;
    Output &operator=(const Output &) = delete;

    /// @brief Open and start playback.
    /// @param sample_rate Stream sample rate in Hz.
    /// @param callback   Render callback (audio thread).
    /// @param user       Opaque context handed back to the callback.
    /// @return false if no playback device could be opened.
    bool Start(int sample_rate, Callback callback, void *user);

    /// @brief Stop playback and release the device. Safe when not started.
    void Stop();

  private:
    void *impl_ = nullptr;  ///< Opaque backend state (backend-defined).
};

}  // namespace audio
