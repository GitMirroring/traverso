// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioResampler.h"
#include "Debugger.h"
#include <samplerate.h>
#include <soxr.h>
#include <algorithm>
#include <cmath>
#include <cstring>

// What a backend does: frames of every channel in, frames of every channel
// out, in its own buffers' terms. Only TAudioResampler sees it.
class ResampleBackend
{
public:
    virtual ~ResampleBackend() = default;
    virtual void reset() = 0;
    // inFrames frames of in[channel]; out[channel] has room for maxOut
    // frames. endOfInput: no input follows, what is inside comes out. The
    // frames given; consumed: the input frames it took.
    virtual nframes_t process(const float* const* in, nframes_t inFrames, float* const* out, nframes_t maxOut, bool endOfInput,
                              nframes_t& consumed) = 0;
    // At most this many output frames are inside the filter
    virtual nframes_t delay_frames() const = 0;
};

// =========================================================================
// libsamplerate (Secret Rabbit Code): interleaved, so the channels are
// interleaved here and taken apart again
// =========================================================================
class LibSampleRateBackend : public ResampleBackend
{
public:
    LibSampleRateBackend(int quality, double ratio, uint channels)
        : m_channels(channels)
        , m_ratio(ratio)
    {
        int err = 0;
        m_state = src_new(quality, int(channels), &err);
        if (!m_state) {
            PERROR("LibSampleRateBackend: src_new failed");
            return;
        }
        src_set_ratio(m_state, ratio);
    }

    ~LibSampleRateBackend() override
    {
        if (m_state) {
            src_delete(m_state);
        }
    }

    void reset() override
    {
        if (m_state) {
            src_reset(m_state);
        }
    }

    nframes_t process(const float* const* in, nframes_t inFrames, float* const* out, nframes_t maxOut, bool endOfInput,
                      nframes_t& consumed) override
    {
        consumed = 0;
        if (!m_state) {
            return 0;
        }
        m_interleavedIn.resize(size_t(inFrames) * m_channels);
        for (uint channel = 0; channel < m_channels; ++channel) {
            for (nframes_t frame = 0; frame < inFrames; ++frame) {
                m_interleavedIn[size_t(frame) * m_channels + channel] = in[channel][frame];
            }
        }
        m_interleavedOut.resize(size_t(maxOut) * m_channels);

        SRC_DATA data;
        std::memset(&data, 0, sizeof(SRC_DATA));
        data.data_in = m_interleavedIn.data();
        data.input_frames = inFrames;
        data.data_out = m_interleavedOut.data();
        data.output_frames = maxOut;
        data.src_ratio = m_ratio;
        data.end_of_input = endOfInput ? 1 : 0;

        const int err = src_process(m_state, &data);
        if (err != 0) {
            PWARN(QString("libsamplerate error: %1").arg(src_strerror(err)).toLatin1().data());
            return 0;
        }

        const nframes_t given = nframes_t(data.output_frames_gen);
        for (uint channel = 0; channel < m_channels; ++channel) {
            for (nframes_t frame = 0; frame < given; ++frame) {
                out[channel][frame] = m_interleavedOut[size_t(frame) * m_channels + channel];
            }
        }
        consumed = nframes_t(data.input_frames_used);
        return given;
    }

    // libsamplerate does not say. Its longest filter (best sinc) reaches
    // about 143 frames to either side: of the input when it converts up, so
    // some 143 x ratio output frames; when it converts down it is wider at
    // the input rate by 1 / ratio, the same 143 or so output frames.
    // 1024 x max(1, ratio) output frames is several times that at any ratio
    // (test_resampler_quality flushes at ratios from 1/6 to 8.7)
    nframes_t delay_frames() const override { return nframes_t(std::ceil(1024.0 * std::max(1.0, m_ratio))); }

private:
    SRC_STATE* m_state{nullptr};
    uint m_channels;
    double m_ratio;
    std::vector<float> m_interleavedIn;
    std::vector<float> m_interleavedOut;
};

// =========================================================================
// soxr: the channels as they are (split buffers)
// =========================================================================
class LibSoxrBackend : public ResampleBackend
{
public:
    LibSoxrBackend(int quality, double ratio, uint channels)
    {
        const soxr_quality_spec_t qualitySpec = soxr_quality_spec(quality, 0);
        const soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_FLOAT32_S, SOXR_FLOAT32_S);
        soxr_error_t err = nullptr;
        m_soxr = soxr_create(1.0, ratio, channels, &err, &ioSpec, &qualitySpec, nullptr);
        if (err) {
            PERROR("LibSoxrBackend: soxr_create failed");
            m_soxr = nullptr;
        }
    }

    ~LibSoxrBackend() override
    {
        if (m_soxr) {
            soxr_delete(m_soxr);
        }
    }

    void reset() override
    {
        if (m_soxr) {
            soxr_clear(m_soxr);
        }
    }

    nframes_t process(const float* const* in, nframes_t inFrames, float* const* out, nframes_t maxOut, bool endOfInput,
                      nframes_t& consumed) override
    {
        consumed = 0;
        if (!m_soxr) {
            return 0;
        }
        size_t taken = 0;
        size_t given = 0;
        // soxr empties its filter on a null input only
        const soxr_in_t input = (endOfInput && inFrames == 0) ? nullptr : static_cast<soxr_in_t>(in);
        // Split buffers: soxr takes the array of channel pointers (soxr_bufs_t)
        const soxr_out_t output = const_cast<float**>(out);
        soxr_process(m_soxr, input, inFrames, &taken, output, maxOut, &given);
        consumed = nframes_t(taken);
        return nframes_t(given);
    }

    nframes_t delay_frames() const override { return m_soxr ? nframes_t(std::ceil(soxr_delay(m_soxr))) : 0; }

private:
    soxr_t m_soxr{nullptr};
};

// =========================================================================
// TAudioResampler
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


TAudioResampler::TAudioResampler(BackendType backend, double ratio, int quality, uint channels)
    : m_channels(channels)
    , m_ratio(ratio)
    , m_kept(channels)
    , m_inputPointers(channels)
    , m_outputPointers(channels)
{
    Q_ASSERT_X(qualities(backend).contains(quality), "TAudioResampler", "not a quality of this backend");
    Q_ASSERT(channels > 0);
    if (backend == BackendType::LIBSOXR) {
        m_backend = std::make_unique<LibSoxrBackend>(quality, ratio, channels);
    } else {
        m_backend = std::make_unique<LibSampleRateBackend>(quality, ratio, channels);
    }
}

TAudioResampler::~TAudioResampler() = default;

// The backend forgets its filter history (soxr_clear(), src_reset()): the
// next output is computed from the next input only, as at the start. The
// input kept from the last process() goes too: it is from before the seek.
void TAudioResampler::reset()
{
    m_backend->reset();
    m_keptFrames = 0;
}

nframes_t TAudioResampler::process(const TAudioBuffer* const* in, nframes_t inputFrames, TAudioBuffer* const* out, nframes_t at, nframes_t count)
{
    return run(in, inputFrames, out, at, count, false);
}

nframes_t TAudioResampler::pending_frames() const
{
    // The kept input at the output rate, rounded up, and what the filter
    // holds; a few frames more for the rounding of the backends
    return nframes_t(std::ceil(double(m_keptFrames) * m_ratio)) + m_backend->delay_frames() + 16;
}

nframes_t TAudioResampler::flush(TAudioBuffer* const* out, nframes_t at, nframes_t count)
{
    // First the input kept, then the filter's tail: until count frames came
    // out, or a call gave nothing and had nothing left to take
    nframes_t given = 0;
    while (given < count) {
        const nframes_t keptBefore = m_keptFrames;
        const nframes_t got = run(nullptr, 0, out, at + given, count - given, true);
        given += got;
        if (got == 0 && (keptBefore == 0 || m_keptFrames == keptBefore)) {
            break;
        }
    }
    return given;
}

nframes_t TAudioResampler::run(const TAudioBuffer* const* in, nframes_t inputFrames, TAudioBuffer* const* out, nframes_t at, nframes_t count,
                               bool endOfInput)
{
    // The input behind what was kept, so the backend sees one block
    const nframes_t total = m_keptFrames + inputFrames;
    for (uint channel = 0; channel < m_channels; ++channel) {
        std::vector<float>& kept = m_kept[channel];
        if (kept.size() < total) {
            kept.resize(total);
        }
        if (in && inputFrames > 0) {
            std::copy_n(in[channel]->data(0, inputFrames), inputFrames, kept.data() + m_keptFrames);
        }
        m_inputPointers[channel] = kept.data();
        m_outputPointers[channel] = out[channel]->data(at, count);
    }

    nframes_t consumed = 0;
    const nframes_t given = count > 0 ? m_backend->process(m_inputPointers.data(), total, m_outputPointers.data(), count, endOfInput, consumed) : 0;

    // What the backend did not take waits for the next call
    m_keptFrames = total - consumed;
    if (m_keptFrames > 0 && consumed > 0) {
        for (std::vector<float>& kept : m_kept) {
            std::memmove(kept.data(), kept.data() + consumed, size_t(m_keptFrames) * sizeof(float));
        }
    }
    return given;
}
