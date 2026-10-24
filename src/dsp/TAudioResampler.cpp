#include "TAudioResampler.h"
#include "Debugger.h"
#include <samplerate.h>
#include <soxr.h>
#include <cstring>
#include <algorithm>

// =========================================================================
// BACKEND 1: Libsamplerate (Secret Rabbit Code) - Pure Mono
// =========================================================================
class LibSampleRateBackend : public ResampleBackend {
private:
    SRC_STATE* m_state;
public:
    LibSampleRateBackend(int quality, double ratio) {
        int err;
        m_state = src_new(quality, 1, &err); // Forced to 1 channel (Mono)
        if (!m_state) {
            PERROR("LibSampleRateBackend: src_new failed");
        }
        src_set_ratio(m_state, ratio);
    }

    ~LibSampleRateBackend() override {
        if (m_state) {
            src_delete(m_state);
        }
    }

    void reset() override {
        if (m_state) {
            src_reset(m_state);
        }
    }

    nframes_t process(const float* input, nframes_t inFrames, float* output,
                      nframes_t maxOutFrames, double ratio, bool endOfInput, nframes_t& framesConsumed) override {
        if (!m_state) return 0;

        SRC_DATA data;
        std::memset(&data, 0, sizeof(SRC_DATA));

        data.data_in = const_cast<float*>(input);
        data.input_frames = inFrames;
        data.data_out = output;
        data.output_frames = maxOutFrames;
        data.src_ratio = ratio;
        data.end_of_input = endOfInput ? 1 : 0;

        int err = src_process(m_state, &data);
        if (err != 0) {
            PWARN(QString("libsamplerate internal mono error: %1").arg(src_strerror(err)).toLatin1().data());
            framesConsumed = 0;
            return 0;
        }

        framesConsumed = nframes_t(data.input_frames_used);
        return nframes_t(data.output_frames_gen);
    }
};

// =========================================================================
// BACKEND 2: Libsoxr (High performance alternative) - Pure Mono
// =========================================================================
class LibSoxrBackend : public ResampleBackend {
private:
    soxr_t m_resampler;
public:
    LibSoxrBackend(int quality, double ratio) {
        soxr_quality_spec_t q_spec = soxr_quality_spec(quality, 0);
        soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
        soxr_error_t err;
        m_resampler = soxr_create(1.0, ratio, 1, &err, &io_spec, &q_spec, nullptr); // Forced to 1 channel (Mono)
        if (err) {
            PERROR("LibSoxrBackend: soxr_create failed");
        }
    }

    ~LibSoxrBackend() override {
        if (m_resampler) {
            soxr_delete(m_resampler);
        }
    }

    void reset() override {
        if (m_resampler) {
            soxr_clear(m_resampler);
        }
    }

    nframes_t process(const float* input, nframes_t inFrames, float* output,
                      nframes_t maxOutFrames, double ratio, bool endOfInput, nframes_t& framesConsumed) override {
        Q_UNUSED(ratio);
        if (!m_resampler) return 0;

        size_t odone = 0;
        size_t idone = 0;

        soxr_process(m_resampler, (endOfInput && inFrames == 0) ? nullptr : input,
                     inFrames, &idone, output, maxOutFrames, &odone);

        framesConsumed = nframes_t(idone);
        return nframes_t(odone);
    }
};

// =========================================================================
// MAIN CLASS: TAudioResampler Mono Core Orchestration
// =========================================================================
QList<int> TAudioResampler::qualities(BackendType backend)
{
    // Not libsamplerate's zero order hold and linear converters: they lag,
    // one input frame and one and a half, so what they give is not where
    // it belongs
    if (backend == BackendType::LIBSAMPLERATE) {
        return {SRC_SINC_BEST_QUALITY, SRC_SINC_MEDIUM_QUALITY, SRC_SINC_FASTEST};
    }
    // The five standard qualities of soxr, as SoX has them
    return {SOXR_VHQ, SOXR_HQ, SOXR_MQ, SOXR_LQ, SOXR_QQ};
}

QString TAudioResampler::quality_name(BackendType backend, int quality)
{
    if (backend == BackendType::LIBSAMPLERATE) {
        return QString(src_get_name(quality));
    }
    switch (quality) {
    case SOXR_VHQ: return QStringLiteral("Very High Quality");
    case SOXR_HQ: return QStringLiteral("High Quality");
    case SOXR_MQ: return QStringLiteral("Medium Quality");
    case SOXR_LQ: return QStringLiteral("Low Quality");
    case SOXR_QQ: return QStringLiteral("Quick");
    default: return QString();
    }
}

QString TAudioResampler::quality_description(BackendType backend, int quality)
{
    if (backend == BackendType::LIBSAMPLERATE) {
        return QString(src_get_description(quality));
    }
    switch (quality) {
    // As soxr.h describes them
    case SOXR_VHQ: return QStringLiteral("28 bit precision.");
    case SOXR_HQ: return QStringLiteral("20 bit precision.");
    case SOXR_MQ: return QStringLiteral("16 bit precision with medium roll-off.");
    case SOXR_LQ: return QStringLiteral("16 bit precision with larger roll-off.");
    case SOXR_QQ: return QStringLiteral("Cubic interpolation.");
    default: return QString();
    }
}

int TAudioResampler::default_quality(BackendType backend)
{
    return backend == BackendType::LIBSAMPLERATE ? SRC_SINC_MEDIUM_QUALITY : SOXR_HQ;
}

int TAudioResampler::default_realtime_quality(BackendType backend)
{
    return backend == BackendType::LIBSAMPLERATE ? SRC_SINC_FASTEST : SOXR_MQ;
}

QString TAudioResampler::backend_name(BackendType backend)
{
    return backend == BackendType::LIBSAMPLERATE ? QStringLiteral("libsamplerate") : QStringLiteral("soxr");
}

TAudioResampler::BackendType TAudioResampler::backend_from_name(const QString& name)
{
    return name == QLatin1String("libsamplerate") ? BackendType::LIBSAMPLERATE : BackendType::LIBSOXR;
}

TAudioResampler::TAudioResampler(BackendType backend, double srcRatio, int quality, long blockSize)
    : m_backendType(backend)
    , m_srcRatio(srcRatio)
    , m_blockSize(blockSize)
    , m_leftOverFrames(0)
    , m_inputWorkBuffer(blockSize * 4)   // Pure mono frames allocation
{
    Q_ASSERT_X(qualities(backend).contains(quality), "TAudioResampler", "not a quality of this backend");
    if (m_backendType == BackendType::LIBSOXR) {
        m_backend = std::make_unique<LibSoxrBackend>(quality, srcRatio);
    } else {
        m_backend = std::make_unique<LibSampleRateBackend>(quality, srcRatio);
    }
}

TAudioResampler::~TAudioResampler() = default;

// The backend forgets its filter history (soxr_clear(), src_reset()): the
// next output is computed from the next input only, as at the start. The
// input kept from the last process() goes too: it is from before the seek.
void TAudioResampler::reset() {
    if (m_backend) {
        m_backend->reset();
    }
    m_leftOverFrames = 0;
}

nframes_t TAudioResampler::process(const float* inputBuffer, nframes_t inputFrames,
                                   float* outputBuffer, nframes_t maxOutputFrames,
                                   bool endOfInput)
{
    // COMPILER FIX: We now check m_backend presence instead of the removed m_srcState
    if (!m_backend) return 0;

    nframes_t totalRequiredFrames = m_leftOverFrames + inputFrames;

    // Check capacity for data-safe container expansion
    if (totalRequiredFrames > m_inputWorkBuffer.get_size()) {
        m_inputWorkBuffer.resize(totalRequiredFrames);
    }

    float* workBufferPtr = m_inputWorkBuffer.get_data(m_inputWorkBuffer.get_size());

    // Stitch incoming samples behind the mono remnants of the previous block
    if (inputFrames > 0 && inputBuffer) {
        std::memcpy(workBufferPtr + m_leftOverFrames, inputBuffer, inputFrames * sizeof(float));
    }

    nframes_t framesConsumed = 0;
    nframes_t generatedFrames = m_backend->process(workBufferPtr, totalRequiredFrames, outputBuffer,
                                                   maxOutputFrames, m_srcRatio, endOfInput, framesConsumed);

    // Compute and shift cached remnants for the next period
    m_leftOverFrames = totalRequiredFrames - framesConsumed;
    if (m_leftOverFrames > 0) {
        std::memmove(workBufferPtr, workBufferPtr + framesConsumed, m_leftOverFrames * sizeof(float));
    }

    return generatedFrames;
}

nframes_t TAudioResampler::flush(float* outputBuffer, nframes_t maxOutputFrames)
{
    // First the input it kept, then, with nothing left of that, the tail of
    // the filter (soxr flushes on an empty input only)
    nframes_t produced = process(nullptr, 0, outputBuffer, maxOutputFrames, true);
    if (produced < maxOutputFrames && m_leftOverFrames == 0) {
        produced += process(nullptr, 0, outputBuffer + produced, maxOutputFrames - produced, true);
    }
    return produced;
}
