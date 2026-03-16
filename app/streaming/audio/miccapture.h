#pragma once

/*
 * MicCapture — SDL2-based microphone capture with Opus encoding.
 *
 * CROSS-PLATFORM DESIGN:
 * ─────────────────────
 * SDL2's audio API is inherently cross-platform, so this file should compile
 * and work without modification on:
 *   • Linux / Steam Deck (PulseAudio / PipeWire via SDL2 — current platform)
 *   • Windows (SDL2 wraps WinMM/DirectSound/WASAPI)
 *   • macOS (SDL2 wraps CoreAudio)
 *   • Android (SDL2 wraps OpenSL ES / AAudio)
 *
 * Platform-specific considerations:
 *   • Audio device selection: SDL_OpenAudioDevice(nullptr, 1, ...) always opens
 *     the system default capture device. No device name or index is hardcoded.
 *     This works correctly whether the user has an internal mic, USB mic,
 *     Bluetooth headset, or any other input device set as the system default.
 *   • Sample rate / format: We request k_SampleRate (48 kHz) / AUDIO_S16SYS.
 *     If the device does not natively support this, SDL2 transparently resamples
 *     (SDL_OpenAudioDevice with allowed_changes=0 forces exact spec matching via
 *     SDL's internal converter). No platform-specific resampler is needed.
 *   • Opus encoder: libopus is itself cross-platform. The VOIP application mode
 *     at 32 kbps works well for voice on any platform.
 *   • Future Windows client: if a native WASAPI path is ever preferred over SDL2,
 *     create a WasapiMicCapture class implementing the same start()/stop() interface
 *     and select it at compile time with #ifdef _WIN32.
 *   • Future macOS client: CoreAudio via SDL2 already works; for a native path
 *     create a CoreAudioMicCapture variant.
 *   • Future Android client: SDL2 wraps AAudio/OpenSL; should work as-is.
 */

#include <SDL.h>
#include <atomic>
#include <opus/opus.h>

// MicCapture captures audio from the default microphone device, encodes it
// with Opus, and sends it to the host as packet type 0x3003 over the control stream.
class MicCapture
{
public:
    MicCapture();
    ~MicCapture();

    // Start capturing and sending mic audio.
    // Returns true on success, false if the mic or encoder could not be initialized.
    bool start();

    // Stop capturing mic audio.
    // Non-blocking: sets a stop flag so any in-progress SDL audio callback returns
    // immediately before SDL_CloseAudioDevice() is called. This prevents the
    // session teardown path from blocking on a long-running callback.
    void stop();

private:
    // SDL audio capture callback - called on the SDL audio thread
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);

    void processAudioData(const int16_t* samples, int sampleCount);

    static constexpr int k_SampleRate    = 48000;
    static constexpr int k_Channels      = 2;  // stereo - matches host Opus decoder expectation
    static constexpr int k_FrameSamples  = 960;  // 20 ms at 48 kHz
    static constexpr int k_MaxPacketSize = 4000;

    SDL_AudioDeviceID       m_DeviceId;
    OpusEncoder*            m_Encoder;
    std::atomic<bool>       m_StopFlag;  // Set before SDL_CloseAudioDevice so callback exits fast

    // Accumulation buffer: interleaved stereo samples, one full frame
    int16_t m_SampleBuf[k_FrameSamples * k_Channels];
    int     m_SampleBufCount;

    uint8_t m_PacketBuf[k_MaxPacketSize];
};
