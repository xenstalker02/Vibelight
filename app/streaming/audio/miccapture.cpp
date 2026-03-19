/*
 * MicCapture implementation.
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
      m_ObtainedSpec{},
      m_Encoder(nullptr),
      m_StopFlag(false),
      m_Bitrate(k_DefaultBitrate),
      m_SampleBufCount(0),
      m_PacingActive(false),
      m_FirstPacketLogged(false)
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

    int error = 0;
    m_Encoder = opus_encoder_create(k_SampleRate, k_Channels, OPUS_APPLICATION_VOIP, &error);
    if (!m_Encoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Failed to create Opus encoder: %d", error);
        return false;
    }

    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(m_Bitrate));
    opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(m_Encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(0));
    opus_encoder_ctl(m_Encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[mic] Opus encoder: bitrate=%d complexity=10 vbr=1 fec=1 plp=5%% dtx=0 frame=20ms",
                m_Bitrate);

    const char* deviceName = m_DeviceName.empty() ? nullptr : m_DeviceName.c_str();

    SDL_AudioSpec want = {};
    want.freq     = k_SampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = k_Channels;
    want.samples  = k_FrameSamples;
    want.callback = audioCallback;
    want.userdata = this;

    if (m_StopFlag.load(std::memory_order_acquire)) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        return false;
    }

    m_DeviceId = SDL_OpenAudioDevice(deviceName, 1, &want, &m_ObtainedSpec, 0);

    // Device fallback (named -> default)
    if (m_DeviceId == 0 && deviceName != nullptr) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Named device '%s' failed -- falling back to default",
                    deviceName);
        m_DeviceId = SDL_OpenAudioDevice(nullptr, 1, &want, &m_ObtainedSpec, 0);
    }

    if (m_DeviceId == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] No capture device -- mic passthrough disabled: %s",
                     SDL_GetError());
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        return false;
    }

    // Frame spec mismatch detection
    if (m_ObtainedSpec.freq != k_SampleRate || m_ObtainedSpec.channels != k_Channels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] WARNING: capture format mismatch: got %dHz %dch, expected %dHz %dch",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels, k_SampleRate, k_Channels);
    }
    if (m_ObtainedSpec.samples != k_FrameSamples) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] capture backend delivering %d-sample callbacks; Opus paced at %d samples (20ms)",
                    m_ObtainedSpec.samples, k_FrameSamples);
    }

    m_SampleBufCount = 0;
    m_PacingActive = false;
    m_FirstPacketLogged = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[mic] Started (driver=%s, device=%u, name=%s, freq=%d, channels=%d)",
                SDL_GetCurrentAudioDriver(), m_DeviceId,
                deviceName ? deviceName : "(default)",
                m_ObtainedSpec.freq, m_ObtainedSpec.channels);

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
    m_PacingActive = false;
    m_FirstPacketLogged = false;
}

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    auto* self = static_cast<MicCapture*>(userdata);

    if (self->m_StopFlag.load(std::memory_order_acquire)) {
        return;
    }

    SDL_AudioStatus status = SDL_GetAudioDeviceStatus(self->m_DeviceId);
    if (status == SDL_AUDIO_STOPPED) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Capture device stopped unexpectedly in callback");
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
        if (m_StopFlag.load(std::memory_order_acquire)) {
            m_SampleBufCount = 0;
            return;
        }

        int needed = (k_FrameSamples * k_Channels) - m_SampleBufCount;
        int available = sampleCount - offset;
        int toCopy = (available < needed) ? available : needed;

        memcpy(m_SampleBuf + m_SampleBufCount, samples + offset, toCopy * sizeof(int16_t));
        m_SampleBufCount += toCopy;
        offset += toCopy;

        if (m_SampleBufCount == k_FrameSamples * k_Channels) {
            // Deadline-based pacer
            static constexpr auto kFrameDuration =
                std::chrono::microseconds((k_FrameSamples * 1000000LL) / k_SampleRate);

            auto now = std::chrono::steady_clock::now();
            if (!m_PacingActive) {
                m_NextSendDeadline = now;
                m_PacingActive = true;
            } else if (now > m_NextSendDeadline + (kFrameDuration * 2)) {
                m_NextSendDeadline = now;
            }
            if (m_NextSendDeadline > now) {
                std::this_thread::sleep_until(m_NextSendDeadline);
            }
            m_NextSendDeadline += kFrameDuration;

            opus_int32 encodedBytes = opus_encode(m_Encoder,
                                                  m_SampleBuf,
                                                  k_FrameSamples,
                                                  m_PacketBuf,
                                                  k_MaxPacketSize);
            if (encodedBytes < 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] opus_encode error: %s -- skipping frame",
                            opus_strerror(encodedBytes));
                m_SampleBufCount = 0;
                continue;
            }

            if (encodedBytes > 0) {
                LiSendRawControlStreamPacket(MIC_PACKET_TYPE, m_PacketBuf, (int)encodedBytes);

                // First-packet log flag
                if (!m_FirstPacketLogged) {
                    m_FirstPacketLogged = true;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "[mic] Sent first microphone packet (%d bytes Opus)",
                                encodedBytes);
                }
            }
            m_SampleBufCount = 0;
        }
    }
}
