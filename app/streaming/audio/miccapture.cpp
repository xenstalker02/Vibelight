/*
 * MicCapture implementation -- Logan's exact architecture.
 *
 * Threading model:
 *   audioCallback   (RT thread)  : try-catch + delegate to handleAudioData ONLY
 *   handleAudioData (RT thread)  : std::mutex + insert + 12-frame cap + notify_one
 *   encoderLoop     (normal thread): wait + drain + sleep_until pacer + encode + send
 *
 * Root cause of SIGABRT on Steam Deck: sleep_until was in SDL callback or
 * handleAudioData (RT thread). Must be in encoderLoop (normal std::thread).
 * std::mutex in handleAudioData IS safe -- PipeWire only forbids SDL calls
 * and sleep in the RT callback.
 */
#include "miccapture.h"

#include <Limelight.h>
#include <SDL_log.h>

#define MIC_PACKET_TYPE 0x3003

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MicCapture::MicCapture()
{
}

MicCapture::~MicCapture()
{
    stop();
    m_StopEncoderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) m_EncoderThread.join();
    if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
    if (m_Encoder != nullptr) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
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
// start  (combines initialize + start from Logan's design)
// ---------------------------------------------------------------------------

bool MicCapture::start()
{
    try {
        if (m_Initialized) return true;

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] SDL_InitSubSystem: %s -- streaming without mic", SDL_GetError());
            return false;
        }

        int opusError = OPUS_OK;
        m_Encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &opusError);
        if (!m_Encoder || opusError != OPUS_OK) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] opus_encoder_create: %s -- streaming without mic",
                        opus_strerror(opusError));
            return false;
        }

        opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(m_Bitrate));
        opus_encoder_ctl(m_Encoder, OPUS_SET_VBR(1));
        opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(10));
        opus_encoder_ctl(m_Encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(m_Encoder, OPUS_SET_LSB_DEPTH(16));
        opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(0));
        opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(5));
        opus_encoder_ctl(m_Encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS));

        SDL_AudioSpec desired = {};
        desired.freq     = kSampleRate;
        desired.format   = AUDIO_S16SYS;
        desired.channels = kChannels;
        desired.samples  = kFrameSize;
        desired.callback = &MicCapture::audioCallback;
        desired.userdata = this;

        const char* dev = m_DeviceName.empty() ? nullptr : m_DeviceName.c_str();
        m_DeviceId = SDL_OpenAudioDevice(dev, 1, &desired, &m_ObtainedSpec, 0);
        if (m_DeviceId == 0 && dev != nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Named device '%s' failed -- falling back to default", dev);
            m_DeviceId = SDL_OpenAudioDevice(nullptr, 1, &desired, &m_ObtainedSpec, 0);
        }
        if (m_DeviceId == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] SDL_OpenAudioDevice: %s -- streaming without mic", SDL_GetError());
            opus_encoder_destroy(m_Encoder);
            m_Encoder = nullptr;
            return false;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Opened %s @ %dHz %dch fmt=0x%x samples=%d",
                    dev ? dev : "<default>",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels,
                    m_ObtainedSpec.format, m_ObtainedSpec.samples);

        if (m_ObtainedSpec.freq != kSampleRate || m_ObtainedSpec.channels != kChannels ||
            m_ObtainedSpec.format != AUDIO_S16SYS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Format mismatch: got %dHz %dch fmt=0x%x -- continuing anyway",
                        m_ObtainedSpec.freq, m_ObtainedSpec.channels, m_ObtainedSpec.format);
        }
        if (m_ObtainedSpec.samples != kFrameSize) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Backend %d-sample callbacks, Opus paced at %d (%dms)",
                        m_ObtainedSpec.samples, kFrameSize, (kFrameSize * 1000) / kSampleRate);
        }

        // Pause device until streaming is armed
        SDL_PauseAudioDevice(m_DeviceId, 1);
        m_SampleBuffer.reserve(kFrameSize * 4);
        m_StopEncoderThread.store(false, std::memory_order_release);
        m_EncoderThread = std::thread(&MicCapture::encoderLoop, this);
        m_Initialized = true;

        // Arm streaming and unpause SDL
        clearBufferedSamples();
        m_FirstPacketLogged = false;
        m_Streaming.store(true, std::memory_order_release);
        m_BufferCondition.notify_all();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Streaming started");
        SDL_PauseAudioDevice(m_DeviceId, 0);
        return true;

    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] init threw exception -- streaming without mic");
        if (m_EncoderThread.joinable()) {
            m_StopEncoderThread.store(true, std::memory_order_release);
            m_BufferCondition.notify_all();
            m_EncoderThread.join();
        }
        if (m_Encoder) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
        if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
        m_Initialized = false;
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void MicCapture::stop()
{
    if (m_DeviceId != 0) SDL_PauseAudioDevice(m_DeviceId, 1);
    m_Streaming.store(false, std::memory_order_release);
    clearBufferedSamples();
    m_BufferCondition.notify_all();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Stopped");
}

// ---------------------------------------------------------------------------
// audioCallback -- RT THREAD -- 3 lines only: try-catch + delegate
// Forbidden here: SDL calls, sleep, malloc, mutex lock (except via handleAudioData),
// opus_encode, LiSendRawControlStreamPacket, logging.
// ---------------------------------------------------------------------------

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    try {
        auto* capture = static_cast<MicCapture*>(userdata);
        if (capture != nullptr) capture->handleAudioData(stream, len);
    } catch (...) { /* never let exceptions escape SDL audio callback */ }
}

// ---------------------------------------------------------------------------
// handleAudioData -- called from RT thread
// Only: mutex + insert + 12-frame cap + notify
// std::mutex IS safe here -- PipeWire only forbids SDL calls and sleep
// ---------------------------------------------------------------------------

void MicCapture::handleAudioData(const Uint8* stream, int len)
{
    if (!m_Streaming.load(std::memory_order_acquire) || !stream || len <= 0) return;
    const auto* samples = reinterpret_cast<const opus_int16*>(stream);
    const int count = len / (int)sizeof(opus_int16);
    {
        std::lock_guard<std::mutex> lock(m_BufferMutex);
        m_SampleBuffer.insert(m_SampleBuffer.end(), samples, samples + count);
        constexpr size_t maxSamples = kFrameSize * kChannels * 12;
        if (m_SampleBuffer.size() > maxSamples) {
            auto trim = m_SampleBuffer.size() - maxSamples;
            m_SampleBuffer.erase(m_SampleBuffer.begin(),
                                 m_SampleBuffer.begin() + (int)trim);
        }
    }
    m_BufferCondition.notify_one();
}

// ---------------------------------------------------------------------------
// encoderLoop -- normal priority thread
// All encoding / sending / sleeping live here.
// sleep_until is SAFE here (normal std::thread, not RT callback).
// ---------------------------------------------------------------------------

void MicCapture::encoderLoop()
{
    // Wrap entire body: any uncaught exception would call std::terminate -> SIGABRT.
    // Known crash path: oversized Opus packet -> LiSendRawControlStreamPacket ->
    // sendMessageEnet -> __memcpy_chk(tempBuffer[256], payload, paylen, 252) -> abort()
    // when paylen > 252. Guard below prevents that, but try-catch is a second layer.
    try {

    if (!m_Initialized || !m_Encoder || m_DeviceId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] encoderLoop: not ready, exiting");
        return;
    }

    const int kFrameElements = kFrameSize * kChannels;
    std::vector<opus_int16> frame((size_t)kFrameElements);
    const auto frameDuration =
        std::chrono::milliseconds((kFrameSize * 1000) / kSampleRate);
    auto nextSendDeadline = std::chrono::steady_clock::now();
    bool pacingActive = false;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this, kFrameElements] {
                return m_StopEncoderThread.load(std::memory_order_acquire) ||
                       (m_Streaming.load(std::memory_order_acquire) &&
                        (int)m_SampleBuffer.size() >= kFrameElements);
            });
            if (m_StopEncoderThread.load(std::memory_order_acquire)) break;
            if (!m_Streaming.load(std::memory_order_acquire) ||
                (int)m_SampleBuffer.size() < kFrameElements) {
                pacingActive = false;
                continue;
            }
            std::copy_n(m_SampleBuffer.begin(), kFrameElements, frame.begin());
            m_SampleBuffer.erase(m_SampleBuffer.begin(),
                                 m_SampleBuffer.begin() + kFrameElements);
        }

        // Pacer -- SAFE: encoderLoop is a normal std::thread, not RT callback
        const auto now = std::chrono::steady_clock::now();
        if (!pacingActive) {
            nextSendDeadline = now;
            pacingActive = true;
        } else if (now > nextSendDeadline + (frameDuration * 2)) {
            nextSendDeadline = now; // re-sync after gap
        }
        if (nextSendDeadline > now) {
            std::this_thread::sleep_until(nextSendDeadline);
        }
        nextSendDeadline += frameDuration;

        int encodedBytes = opus_encode(m_Encoder,
                                       frame.data(),
                                       kFrameSize,
                                       m_EncodedPacket.data(),
                                       (opus_int32)m_EncodedPacket.size());
        if (encodedBytes <= 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] opus_encode error: %s -- skipping frame",
                        opus_strerror(encodedBytes));
            continue;
        }

        // Hard guard: sendMessageEnet uses a fixed char tempBuffer[256].
        // sizeof(NVCTL_ENET_PACKET_HEADER_V2)=4 leaves 252 bytes for payload.
        // __memcpy_chk calls abort() if paylen > 252. kMaxPacketSize=200 limits
        // what opus_encode produces, but this runtime check is the safety net.
        if (encodedBytes > kMaxPacketSize) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Oversized Opus packet %d bytes > %d limit -- dropping frame",
                        encodedBytes, kMaxPacketSize);
            continue;
        }

        int result = LiSendRawControlStreamPacket(
            MIC_PACKET_TYPE,
            (char*)m_EncodedPacket.data(),
            encodedBytes);
        if (result >= 0 && !m_FirstPacketLogged) {
            m_FirstPacketLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Sent first microphone packet (%d bytes Opus)", encodedBytes);
        } else if (result < 0 && result != LI_ERR_UNSUPPORTED) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] LiSendRawControlStreamPacket returned %d", result);
        }
    }

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] encoderLoop exception: %s -- thread exiting", e.what());
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] encoderLoop unknown exception -- thread exiting");
    }
}

// ---------------------------------------------------------------------------
// clearBufferedSamples
// ---------------------------------------------------------------------------

void MicCapture::clearBufferedSamples()
{
    std::lock_guard<std::mutex> lock(m_BufferMutex);
    m_SampleBuffer.clear();
}
