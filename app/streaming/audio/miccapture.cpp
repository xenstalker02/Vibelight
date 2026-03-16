/*
 * MicCapture implementation.
 *
 * See miccapture.h for the cross-platform design overview.
 *
 * Threading model:
 *   - start() / stop() are called from the Qt session thread.
 *   - audioCallback() / processAudioData() run on SDL's internal audio thread.
 *   - m_StopFlag is an atomic bool shared between the two threads.
 *     stop() sets it before calling SDL_PauseAudioDevice + SDL_CloseAudioDevice,
 *     so the audio thread returns from the callback immediately if stop() races
 *     with an in-progress callback, keeping SDL_CloseAudioDevice non-blocking.
 */
#include "miccapture.h"

#include <Limelight.h>
#include <SDL_log.h>
#include <string.h>

#define MIC_PACKET_TYPE 0x3003

MicCapture::MicCapture()
    : m_DeviceId(0),
      m_Encoder(nullptr),
      m_StopFlag(false),
      m_SampleBufCount(0)
{
}

MicCapture::~MicCapture()
{
    stop();
}

bool MicCapture::start()
{
    m_StopFlag.store(false, std::memory_order_release);

    // Create the Opus encoder
    int error = 0;
    m_Encoder = opus_encoder_create(k_SampleRate, k_Channels, OPUS_APPLICATION_VOIP, &error);
    if (!m_Encoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MicCapture: Failed to create Opus encoder: %d", error);
        return false;
    }

    // Use a constant bitrate of 32 kbps for voice
    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(32000));

    // Open the default capture device.
    // Passing nullptr selects the system default — this works on all platforms
    // (Linux/PulseAudio, Windows/WASAPI, macOS/CoreAudio, Android/AAudio) with no
    // platform-specific code. SDL2 handles format conversion internally when
    // SDL_OpenAudioDevice is called with allowed_changes=0 (exact spec required).
    SDL_AudioSpec want = {}, have = {};
    want.freq     = k_SampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = k_Channels;
    want.samples  = k_FrameSamples;
    want.callback = audioCallback;
    want.userdata = this;

    m_DeviceId = SDL_OpenAudioDevice(nullptr, 1 /* capture */, &want, &have, 0);
    if (m_DeviceId == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MicCapture: Failed to open capture device: %s", SDL_GetError());
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        return false;
    }

    m_SampleBufCount = 0;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicCapture: Started (driver=%s, device=%u, freq=%d, channels=%d)",
                SDL_GetCurrentAudioDriver(), m_DeviceId, have.freq, have.channels);

    // Unpause to start capturing
    SDL_PauseAudioDevice(m_DeviceId, 0);
    return true;
}

void MicCapture::stop()
{
    if (m_DeviceId != 0) {
        // Signal the audio callback to return immediately if it is currently running.
        // This makes SDL_CloseAudioDevice() non-blocking: it waits for the current
        // callback invocation to finish, but with the flag set that finishes in <1 us.
        m_StopFlag.store(true, std::memory_order_release);

        SDL_PauseAudioDevice(m_DeviceId, 1);
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "MicCapture: Stopped");
    }
    if (m_Encoder) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }
}

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    auto* self = static_cast<MicCapture*>(userdata);

    // Exit immediately if stop() has been called. This ensures SDL_CloseAudioDevice()
    // in stop() does not block waiting for a long-running callback.
    if (self->m_StopFlag.load(std::memory_order_acquire)) {
        return;
    }

    const int16_t* samples = reinterpret_cast<const int16_t*>(stream);
    int numSamples = len / sizeof(int16_t);
    self->processAudioData(samples, numSamples);
}

void MicCapture::processAudioData(const int16_t* samples, int sampleCount)
{
    int offset = 0;
    while (offset < sampleCount) {
        // Fill the accumulation buffer.
        // k_FrameSamples * k_Channels = total interleaved int16_t values per Opus frame.
        int needed = (k_FrameSamples * k_Channels) - m_SampleBufCount;
        int available = sampleCount - offset;
        int toCopy = (available < needed) ? available : needed;

        memcpy(m_SampleBuf + m_SampleBufCount, samples + offset, toCopy * sizeof(int16_t));
        m_SampleBufCount += toCopy;
        offset += toCopy;

        // If we have a full frame, encode and send it
        if (m_SampleBufCount == k_FrameSamples * k_Channels) {
            // Check stop flag again before sending — may have been set while accumulating.
            if (m_StopFlag.load(std::memory_order_acquire)) {
                m_SampleBufCount = 0;
                return;
            }

            opus_int32 encodedBytes = opus_encode(m_Encoder,
                                                  m_SampleBuf,
                                                  k_FrameSamples,
                                                  m_PacketBuf,
                                                  k_MaxPacketSize);
            if (encodedBytes > 0) {
                LiSendRawControlStreamPacket(MIC_PACKET_TYPE, m_PacketBuf, (int)encodedBytes);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "MicCapture: Opus encode error: %d", (int)encodedBytes);
            }
            m_SampleBufCount = 0;
        }
    }
}
