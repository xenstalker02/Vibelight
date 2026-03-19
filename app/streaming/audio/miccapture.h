#pragma once

/*
 * MicCapture -- SDL2-based microphone capture with Opus encoding.
 *
 * CROSS-PLATFORM DESIGN:
 * SDL2's audio API is inherently cross-platform, so this file should compile
 * and work without modification on Linux, Windows, macOS, and Android.
 */

#include <SDL.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <opus/opus.h>

class MicCapture
{
public:
    MicCapture();
    ~MicCapture();

    void setDeviceName(const std::string& name) { m_DeviceName = name; }
    void setBitrate(int bitrate);

    bool start();
    void stop();

private:
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);

    void processAudioData(const int16_t* samples, int sampleCount);

    static constexpr int k_SampleRate    = 48000;
    static constexpr int k_Channels      = 2;
    static constexpr int k_FrameSamples  = 960;   // 20 ms at 48 kHz
    static constexpr int k_MaxPacketSize = 4000;
    static constexpr int k_DefaultBitrate = 64000;

    SDL_AudioDeviceID       m_DeviceId;
    SDL_AudioSpec           m_ObtainedSpec;
    OpusEncoder*            m_Encoder;
    std::atomic<bool>       m_StopFlag;

    std::string             m_DeviceName;
    int                     m_Bitrate;

    int16_t m_SampleBuf[k_FrameSamples * k_Channels];
    int     m_SampleBufCount;

    uint8_t m_PacketBuf[k_MaxPacketSize];

    // Deadline-based pacer state
    bool                                        m_PacingActive;
    std::chrono::steady_clock::time_point       m_NextSendDeadline;

    // First-packet log flag
    bool m_FirstPacketLogged;
};
