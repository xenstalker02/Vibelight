/*
 * MicCapture implementation.
 *
 * Threading model:
 *   - start() / stop() are called from the Qt session thread.
 *   - audioCallback() runs on SDL's internal RT audio thread.
 *     It does ONLY: bounds-checked memcpy into the lock-free ring buffer,
 *     then condition_variable::notify_one(). No SDL calls, no malloc,
 *     no mutex lock, no exceptions, no encoding, no sending.
 *   - encoderLoop() runs on a normal-priority std::thread. It drains the
 *     ring buffer one frame at a time, calls opus_encode, then
 *     LiSendRawControlStreamPacket. All logging and error handling live here.
 *
 * This eliminates all RT thread violations that caused SIGABRT on
 * Steam Deck with SDL2-compat / SDL3 / PipeWire.
 */
#include "miccapture.h"

#include <Limelight.h>
#include <SDL_log.h>
#include <string.h>
#include <vector>

#define MIC_PACKET_TYPE 0x3003

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MicCapture::MicCapture()
{
}

MicCapture::~MicCapture()
{
    stop();
}

// ---------------------------------------------------------------------------
// setBitrate
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool MicCapture::start()
{
    try {
        // Create Opus encoder
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

        // Open audio capture device
        const char* deviceName = m_DeviceName.empty() ? nullptr : m_DeviceName.c_str();

        SDL_AudioSpec want = {};
        want.freq     = k_SampleRate;
        want.format   = AUDIO_S16SYS;
        want.channels = k_Channels;
        want.samples  = k_FrameSamples;
        want.callback = audioCallback;
        want.userdata = this;

        m_DeviceId = SDL_OpenAudioDevice(deviceName, 1, &want, &m_ObtainedSpec, 0);

        // Device fallback (named -> default)
        if (m_DeviceId == 0 && deviceName != nullptr) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Named device '%s' failed -- falling back to default", deviceName);
            m_DeviceId = SDL_OpenAudioDevice(nullptr, 1, &want, &m_ObtainedSpec, 0);
        }

        if (m_DeviceId == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[mic] No capture device -- mic passthrough disabled: %s", SDL_GetError());
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

        // Initialise ring buffer
        m_RingHead.store(0, std::memory_order_relaxed);
        m_RingTail.store(0, std::memory_order_relaxed);
        m_FirstPacketLogged = false;

        // Start encoder thread before unpausing SDL (order matters)
        m_StopEncoderThread.store(false, std::memory_order_relaxed);
        m_EncoderThread = std::thread(&MicCapture::encoderLoop, this);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Started (driver=%s, device=%u, name=%s, freq=%d, channels=%d)",
                    SDL_GetCurrentAudioDriver(), m_DeviceId,
                    deviceName ? deviceName : "(default)",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels);

        // Mark active and unpause SDL device
        m_Active.store(true, std::memory_order_release);
        SDL_PauseAudioDevice(m_DeviceId, 0);
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Mic init failed: %s -- streaming without mic", e.what());
        if (m_EncoderThread.joinable()) {
            m_StopEncoderThread.store(true, std::memory_order_release);
            m_BufferCondition.notify_all();
            m_EncoderThread.join();
        }
        if (m_Encoder) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
        if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
        return false;
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] Mic init failed (unknown exception) -- streaming without mic");
        if (m_EncoderThread.joinable()) {
            m_StopEncoderThread.store(true, std::memory_order_release);
            m_BufferCondition.notify_all();
            m_EncoderThread.join();
        }
        if (m_Encoder) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
        if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void MicCapture::stop()
{
    // Pause + close SDL device first so the callback stops firing
    if (m_DeviceId != 0) {
        m_Active.store(false, std::memory_order_release);
        SDL_PauseAudioDevice(m_DeviceId, 1);
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }

    // Wake encoder thread and join
    m_StopEncoderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) {
        m_EncoderThread.join();
    }

    if (m_Encoder) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

    m_FirstPacketLogged = false;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Stopped");
}

// ---------------------------------------------------------------------------
// audioCallback  -- RT THREAD -- ABSOLUTE MINIMUM WORK ONLY
//
// Forbidden here: SDL calls, malloc/free, mutex lock, exceptions, sleep,
// logging, opus_encode, LiSendRawControlStreamPacket.
// ---------------------------------------------------------------------------

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    auto* self = static_cast<MicCapture*>(userdata);
    if (!self) return;

    // m_Active is the only safe state check from this RT thread.
    if (!self->m_Active.load(std::memory_order_acquire)) return;

    const int16_t* src = reinterpret_cast<const int16_t*>(stream);
    const int newSamples = len / static_cast<int>(sizeof(int16_t));

    int tail = self->m_RingTail.load(std::memory_order_relaxed);

    for (int i = 0; i < newSamples; ++i) {
        int nextTail = (tail + 1) % k_RingSize;
        int head = self->m_RingHead.load(std::memory_order_acquire);
        if (nextTail == head) break; // ring full -- drop oldest incoming samples
        self->m_RingBuf[tail] = src[i];
        tail = nextTail;
    }
    self->m_RingTail.store(tail, std::memory_order_release);

    // Wake the encoder thread (notify_one does not acquire m_BufferMutex)
    self->m_BufferCondition.notify_one();
}

// ---------------------------------------------------------------------------
// encoderLoop -- normal priority thread -- all logic lives here
// ---------------------------------------------------------------------------

void MicCapture::encoderLoop()
{
    std::vector<int16_t> frame(k_FrameElements);

    while (!m_StopEncoderThread.load(std::memory_order_acquire)) {
        // Wait until ring has at least one full frame or we are asked to stop
        {
            std::unique_lock<std::mutex> lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this] {
                if (m_StopEncoderThread.load(std::memory_order_relaxed)) return true;
                int head = m_RingHead.load(std::memory_order_acquire);
                int tail = m_RingTail.load(std::memory_order_acquire);
                int avail = (tail - head + k_RingSize) % k_RingSize;
                return avail >= k_FrameElements;
            });
        }

        if (m_StopEncoderThread.load(std::memory_order_acquire)) break;

        // Drain one frame from the ring buffer
        int head = m_RingHead.load(std::memory_order_relaxed);
        for (int i = 0; i < k_FrameElements; ++i) {
            frame[i] = m_RingBuf[head];
            head = (head + 1) % k_RingSize;
        }
        m_RingHead.store(head, std::memory_order_release);

        // Encode
        if (!m_Encoder) continue;
        opus_int32 encodedBytes = opus_encode(m_Encoder,
                                              frame.data(),
                                              k_FrameSamples,
                                              m_PacketBuf,
                                              k_MaxPacketSize);
        if (encodedBytes < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] opus_encode error: %s -- skipping frame",
                        opus_strerror(encodedBytes));
            continue;
        }

        if (encodedBytes > 0) {
            int sendRet = LiSendRawControlStreamPacket(MIC_PACKET_TYPE, m_PacketBuf, (int)encodedBytes);
            if (sendRet != 0 && sendRet != LI_ERR_UNSUPPORTED) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] LiSendRawControlStreamPacket returned %d -- skipping frame",
                            sendRet);
                continue;
            }

            if (!m_FirstPacketLogged) {
                m_FirstPacketLogged = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] Sent first microphone packet (%d bytes Opus)", encodedBytes);
            }
        }
    }
}
