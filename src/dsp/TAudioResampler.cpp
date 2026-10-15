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

    void set_ratio(double ratio) override {
        if (m_state) {
            src_set_ratio(m_state, ratio);
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

    void set_ratio(double ratio) override {
        if (m_resampler) {
            soxr_set_io_ratio(m_resampler, ratio, 0);
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
TAudioResampler::TAudioResampler(BackendType backend, double srcRatio, int quality, long blockSize)
    : m_backendType(backend)
    , m_srcRatio(srcRatio)
    , m_blockSize(blockSize)
    , m_leftOverFrames(0)
    , m_inputWorkBuffer(blockSize * 4)   // Pure mono frames allocation
{
    if (m_backendType == BackendType::LIBSOXR) {
        m_backend = std::make_unique<LibSoxrBackend>(quality, srcRatio);
    } else {
        m_backend = std::make_unique<LibSampleRateBackend>(quality, srcRatio);
    }
}

TAudioResampler::~TAudioResampler() = default;

void TAudioResampler::reset() {
    if (m_backend) {
        m_backend->reset();
    }
    m_leftOverFrames = 0;
}

void TAudioResampler::set_ratio(double newRatio) {
    m_srcRatio = newRatio;
    if (m_backend) {
        m_backend->set_ratio(newRatio);
    }
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
