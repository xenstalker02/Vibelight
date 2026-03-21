#pragma once

/*
 * MicCapture -- SDL2-based microphone capture with Opus encoding.
 *
 * CROSS-PLATFORM DESIGN:
 * SDL2's audio API is inherently cross-platform, so this file should compile
 * and work without modification on Linux, Windows, macOS, and Android.
 *
 * RT-SAFE ARCHITECTURE:
 * SDL2-compat on SDL3/PipeWire runs the audio callback on a real-time priority
 * thread while holding the internal device lock. Any blocking operation in the
 * callback causes deadlock (SIGABRT). The solution is full architectural
 * separation:
 *
 *   SDL callback  (RT thread)  : ONLY memcpy into lock-free ring buffer + notify
 *   Encoder thread (normal pri) : drain ring buffer, opus_encode, send, log
 *
 * This eliminates all RT thread violations: sleep, mutex lock, SDL calls,
 * heap alloc, exceptions, opus_encode, LiSendRawControlStreamPacket, logging.
 */

#include <SDL.h>
#include <atomic>
#include <array>
#include <condition_variable>
#include <mutex>
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
    void encoderLoop();

    static constexpr int k_SampleRate    = 48000;
    static constexpr int k_Channels      = 2;
    static constexpr int k_FrameSamples  = 960;   // 20 ms at 48 kHz
    static constexpr int k_FrameElements = k_FrameSamples * k_Channels;
    static constexpr int k_MaxPacketSize = 4000;
    static constexpr int k_DefaultBitrate = 64000;

    // Lock-free ring buffer (written by SDL callback, read by encoder thread).
    // Size: 32 frames of headroom.
    static constexpr int k_RingSize = k_FrameElements * 32;
    std::array<int16_t, k_RingSize> m_RingBuf{};
    std::atomic<int> m_RingHead{0}; // read index  (encoder thread)
    std::atomic<int> m_RingTail{0}; // write index (SDL callback)

    // Encoder thread synchronization
    std::mutex              m_BufferMutex;
    std::condition_variable m_BufferCondition;

    // Encoder thread
    std::thread             m_EncoderThread;
    std::atomic<bool>       m_StopEncoderThread{false};

    SDL_AudioDeviceID       m_DeviceId{0};
    SDL_AudioSpec           m_ObtainedSpec{};
    OpusEncoder*            m_Encoder{nullptr};
    std::atomic<bool>       m_Active{false};

    std::string             m_DeviceName;
    int                     m_Bitrate{k_DefaultBitrate};

    uint8_t                 m_PacketBuf[k_MaxPacketSize]{};
    bool                    m_FirstPacketLogged{false};
};
