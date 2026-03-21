/*
 * MicCapture implementation.
 *
 * Threading model:
 *   - start() / stop() are called from the Qt session thread.
 *   - audioCallback() / processAudioData() run on SDL's internal audio thread.
 *   - m_StopFlag is an atomic bool shared between the two threads.
 *
 * IMPORTANT: The SDL audio callback must NEVER sleep, block, or call any
 * SDL audio API function (e.g. SDL_GetAudioDeviceStatus). SDL2-compat on
 * SDL3/PipeWire runs the callback on a real-time priority thread while
 * holding the internal audio device lock. Calling SDL_GetAudioDeviceStatus
 * from inside the callback re-acquires that same lock, causing a deadlock
 * assertion (SIGABRT). The deadline pacer (sleep_until) has also been
 * removed; SDL delivers audio at the correct rate via the PipeWire clock.
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
    try {
        m_StopFlag.store(false, std::memory_order_release);

        int error = 0;
        m_Encoder = opus_encoder_create(k_SampleRate, k_Channels, OPUS_APPLICATION_VOIP, &error);
        if (!m_Encoder) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[mic] Failed to create Opus encoder: %d -- streaming without mic", error);
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

        // Frame spec mismatch detection (non-fatal)
        if (m_ObtainedSpec.freq != k_SampleRate || m_ObtainedSpec.channels != k_Channels) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] WARNING: capture format mismatch: got %dHz %dch, expected %dHz %dch",
                        m_ObtainedSpec.freq, m_ObtainedSpec.channels, k_SampleRate, k_Channels);
        }
        if (m_ObtainedSpec.samples != k_FrameSamples) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] capture backend delivering %d-sample callbacks; Opus frame=%d samples (20ms)",
                        m_ObtainedSpec.samples, k_FrameSamples);
        }

        m_SampleBufCount = 0;
        m_FirstPacketLogged = false;

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Started (driver=%s, device=%u, name=%s, freq=%d, channels=%d)",
                    SDL_GetCurrentAudioDriver(), m_DeviceId,
                    deviceName ? deviceName : "(default)",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels);

        SDL_PauseAudioDevice(m_DeviceId, 0);
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Mic init failed: %s -- streaming without mic", e.what());
        if (m_Encoder) {
            opus_encoder_destroy(m_Encoder);
            m_Encoder = nullptr;
        }
        if (m_DeviceId != 0) {
            SDL_CloseAudioDevice(m_DeviceId);
            m_DeviceId = 0;
        }
        return false;
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Mic init failed (unknown exception) -- streaming without mic");
        if (m_Encoder) {
            opus_encoder_destroy(m_Encoder);
            m_Encoder = nullptr;
        }
        if (m_DeviceId != 0) {
            SDL_CloseAudioDevice(m_DeviceId);
            m_DeviceId = 0;
        }
        return false;
    }
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
    m_FirstPacketLogged = false;
}

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    // NOTE: This callback runs on an SDL RT audio thread (SDL3/PipeWire).
    // NEVER sleep, block, or call any SDL audio API here -- in particular,
    // SDL_GetAudioDeviceStatus acquires the device lock that this callback
    // already holds, causing a deadlock assertion -> SIGABRT.
    // Return as fast as possible.
    try {
        auto* self = static_cast<MicCapture*>(userdata);
        if (!self) return;

        // m_StopFlag is the ONLY safe way to check state from this thread.
        if (self->m_StopFlag.load(std::memory_order_acquire)) {
            return;
        }

        const int16_t* samples = reinterpret_cast<const int16_t*>(stream);
        int numSamples = len / sizeof(int16_t);
        self->processAudioData(samples, numSamples);
    } catch (...) {
        // Swallow all exceptions -- throwing from an SDL audio callback is fatal
    }
}

void MicCapture::processAudioData(const int16_t* samples, int sampleCount)
{
    // NOTE: Called from the SDL audio callback thread. Must NOT sleep or block.
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
            if (!m_Encoder) {
                // Defensive guard: encoder not ready, discard frame
                m_SampleBufCount = 0;
                continue;
            }

            // No sleep/pacing here -- SDL audio thread is already clocked by
            // PipeWire/PulseAudio at the correct rate.
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
                int sendRet = LiSendRawControlStreamPacket(MIC_PACKET_TYPE, m_PacketBuf, (int)encodedBytes);
                if (sendRet != 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "[mic] LiSendRawControlStreamPacket returned %d -- skipping frame",
                                sendRet);
                    m_SampleBufCount = 0;
                    continue;
                }

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
