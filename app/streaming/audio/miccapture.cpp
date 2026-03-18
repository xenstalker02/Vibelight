/*
 * MicCapture implementation.
 *
 * See miccapture.h for the cross-platform design overview.
 *
 * Threading model:
 *   - start() / stop() are called from the Qt session thread.
 *   - audioCallback() / processAudioData() run on SDL's internal audio thread.
 *   - m_StopFlag is an atomic bool shared between the two threads.
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
      m_Bitrate(k_DefaultBitrate),
      m_SampleBufCount(0)
{
}

MicCapture::~MicCapture()
{
    stop();
}

void MicCapture::setBitrate(int bitrate)
{
    if (bitrate < 6000) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] micBitrate %d clamped to minimum 6000", bitrate);
        bitrate = 6000;
    } else if (bitrate > 510000) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] micBitrate %d clamped to maximum 510000", bitrate);
        bitrate = 510000;
    }
    m_Bitrate = bitrate;
}

bool MicCapture::start()
{
    m_StopFlag.store(false, std::memory_order_release);

    // Create the Opus encoder
    int error = 0;
    m_Encoder = opus_encoder_create(k_SampleRate, k_Channels, OPUS_APPLICATION_VOIP, &error);
    if (!m_Encoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Failed to create Opus encoder: %d", error);
        return false;
    }

    // Set the configured bitrate
    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(m_Bitrate));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[mic] Opus bitrate set to %d bps", m_Bitrate);

    // Open the capture device.
    // If m_DeviceName is set, use it; otherwise pass NULL for system default.
    const char* deviceName = m_DeviceName.empty() ? nullptr : m_DeviceName.c_str();

    SDL_AudioSpec want = {}, have = {};
    want.freq     = k_SampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = k_Channels;
    want.samples  = k_FrameSamples;
    want.callback = audioCallback;
    want.userdata = this;

    m_DeviceId = SDL_OpenAudioDevice(deviceName, 1 /* capture */, &want, &have, 0);
    if (m_DeviceId == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] No capture device -- mic passthrough disabled: %s",
                     SDL_GetError());
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        return false;
    }

    m_SampleBufCount = 0;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[mic] Started (driver=%s, device=%u, name=%s, freq=%d, channels=%d)",
                SDL_GetCurrentAudioDriver(), m_DeviceId,
                deviceName ? deviceName : "(default)",
                have.freq, have.channels);

    // Unpause to start capturing
    SDL_PauseAudioDevice(m_DeviceId, 0);
    return true;
}

void MicCapture::stop()
{
    if (m_DeviceId != 0) {
        m_StopFlag.store(true, std::memory_order_release);

        SDL_PauseAudioDevice(m_DeviceId, 1);
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Stopped");
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

    if (self->m_StopFlag.load(std::memory_order_acquire)) {
        return;
    }

    // Check if the capture device has stopped unexpectedly
    SDL_AudioStatus status = SDL_GetAudioDeviceStatus(self->m_DeviceId);
    if (status == SDL_AUDIO_STOPPED) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Capture device stopped unexpectedly in callback");
        // Cannot safely reopen from callback context; just return.
        // The device-lost condition will be handled by the session layer.
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
        int needed = (k_FrameSamples * k_Channels) - m_SampleBufCount;
        int available = sampleCount - offset;
        int toCopy = (available < needed) ? available : needed;

        memcpy(m_SampleBuf + m_SampleBufCount, samples + offset, toCopy * sizeof(int16_t));
        m_SampleBufCount += toCopy;
        offset += toCopy;

        if (m_SampleBufCount == k_FrameSamples * k_Channels) {
            if (m_StopFlag.load(std::memory_order_acquire)) {
                m_SampleBufCount = 0;
                return;
            }

            opus_int32 encodedBytes = opus_encode(m_Encoder,
                                                  m_SampleBuf,
                                                  k_FrameSamples,
                                                  m_PacketBuf,
                                                  k_MaxPacketSize);
            if (encodedBytes < 0) {
                // 6b: Opus encode error handling - log and skip frame, do not crash
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] opus_encode error: %s -- skipping frame",
                            opus_strerror(encodedBytes));
                m_SampleBufCount = 0;
                continue;
            }

            if (encodedBytes > 0) {
                // 6c: LiSendRawControlStreamPacket is fire-and-forget in moonlight-common-c.
                // It internally queues the packet; there is no timeout mechanism exposed
                // by the API. If the control stream is congested, the packet is silently
                // dropped by the underlying ENet reliable channel (which has its own
                // timeout). Adding a wrapper timeout here would require changes to
                // moonlight-common-c internals, so we accept the existing behavior.
                // Known risk: if the control stream is blocked, this call may block
                // the SDL audio thread briefly. In practice this has not been observed.
                LiSendRawControlStreamPacket(MIC_PACKET_TYPE, m_PacketBuf, (int)encodedBytes);
            }
            m_SampleBufCount = 0;
        }
    }
}
