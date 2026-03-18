#pragma once

/*
 * MicCapture â SDL2-based microphone capture with Opus encoding.
 *
 * CROSS-PLATFORM DESIGN:
 * SDL2's audio API is inherently cross-platform, so this file should compile
 * and work without modification on Linux, Windows, macOS, and Android.
 */

#include <SDL.h>
#include <atomic>
#include <string>
#include <opus/opus.h>

// MicCapture captures audio from a microphone device, encodes it
// with Opus, and sends it to the host as packet type 0x3003 over the control stream.
class MicCapture
{
public:
    MicCapture();
    ~MicCapture();

    // Set the SDL audio device name. Empty string = system default.
    void setDeviceName(const std::string& name) { m_DeviceName = name; }

    // Set the Opus bitrate in bps. Clamped to [6000, 510000].
    void setBitrate(int bitrate);

    // Start capturing and sending mic audio.
    // Returns true on success, false if the mic or encoder could not be initialized.
    bool start();

    // Stop capturing mic audio.
    void stop();

private:
    // SDL audio capture callback - called on the SDL audio thread
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);

    void processAudioData(const int16_t* samples, int sampleCount);

    static constexpr int k_SampleRate    = 48000;
    static constexpr int k_Channels      = 2;  // stereo - matches host Opus decoder expectation
    static constexpr int k_FrameSamples  = 960;  // 20 ms at 48 kHz
    static constexpr int k_MaxPacketSize = 4000;
    static constexpr int k_DefaultBitrate = 96000;

    SDL_AudioDeviceID       m_DeviceId;
    OpusEncoder*            m_Encoder;
    std::atomic<bool>       m_StopFlag;

    std::string             m_DeviceName;  // SDL device name, empty = default
    int                     m_Bitrate;     // Opus bitrate in bps

    // Accumulation buffer: interleaved stereo samples, one full frame
    int16_t m_SampleBuf[k_FrameSamples * k_Channels];
    int     m_SampleBufCount;

    uint8_t m_PacketBuf[k_MaxPacketSize];
};
