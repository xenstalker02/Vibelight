#pragma once

/*
 * MicCapture -- SDL2-based microphone capture with Opus encoding.
 *
 * Logan's architecture (logabell/moonlight-qt-mic):
 *
 *   audioCallback   (RT thread)  : ONLY try-catch + delegate to handleAudioData
 *   handleAudioData (RT thread)  : std::mutex + insert samples + 12-frame cap + notify
 *   encoderLoop     (normal thread): wait + drain frame + sleep_until pacer + encode + send
 *
 * std::mutex in handleAudioData IS safe -- PipeWire only forbids SDL calls
 * and sleep in the RT callback, not mutex locks.
 * sleep_until in encoderLoop is safe -- it is a normal std::thread.
 */

#include <SDL.h>
#include <opus/opus.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    // Constants
    static constexpr int kSampleRate    = 48000;
    static constexpr int kChannels      = 1;
    static constexpr int kFrameSize     = 960;   // 20ms at 48kHz
    static constexpr int kMaxPacketSize = 1400;
    static constexpr int kDefaultBitrate = 64000;

    // Internal methods
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
    void handleAudioData(const Uint8* stream, int len);
    void encoderLoop();
    void clearBufferedSamples();

    // State
    bool m_Initialized = false;
    bool m_FirstPacketLogged = false;
    SDL_AudioDeviceID m_DeviceId = 0;
    SDL_AudioSpec m_ObtainedSpec = {};
    OpusEncoder* m_Encoder = nullptr;

    // Buffer and sync
    std::vector<opus_int16> m_SampleBuffer;
    std::array<unsigned char, kMaxPacketSize> m_EncodedPacket = {};
    std::mutex m_BufferMutex;
    std::condition_variable m_BufferCondition;

    // Thread
    std::thread m_EncoderThread;
    std::atomic_bool m_StopEncoderThread{false};
    std::atomic_bool m_Streaming{false};

    std::string m_DeviceName;
    int m_Bitrate{kDefaultBitrate};
};
