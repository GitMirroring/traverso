#pragma once

#include "TAudioBuffer.h"
#include "defines.h"
#include <QList>
#include <QString>
#include <memory>

class ResampleBackend {
public:
    virtual ~ResampleBackend() = default;
    virtual void reset() = 0;
    virtual nframes_t process(const float* input, nframes_t inFrames,
                              float* output, nframes_t maxOutFrames,
                              double ratio, bool endOfInput, nframes_t& framesConsumed) = 0;
};

class TAudioResampler
{
public:
    enum class BackendType {
        LIBSAMPLERATE,
        LIBSOXR
    };

    // The qualities of a backend, best first, in its own numbers:
    // libsamplerate's sinc converter types (SRC_SINC_BEST_QUALITY ...
    // SRC_SINC_FASTEST), soxr's recipes (SOXR_VHQ ... SOXR_QQ). A quality of
    // one backend means nothing to the other.
    static QList<int> qualities(BackendType backend);
    static QString quality_name(BackendType backend, int quality);
    static QString quality_description(BackendType backend, int quality);
    // For files written (exports, conversions): soxr's own default (20 bit),
    // libsamplerate's medium sinc
    static int default_quality(BackendType backend);
    // For on-the-fly reading, many files at a time: soxr's 16 bit medium,
    // libsamplerate's fastest sinc
    static int default_realtime_quality(BackendType backend);
    // As the settings name them: "soxr", "libsamplerate"; another name is soxr
    static QString backend_name(BackendType backend);
    static BackendType backend_from_name(const QString& name);

    // quality: one of qualities(backend). The ratio stays: another ratio is
    // another resampler (soxr cannot change it once it ran).
    TAudioResampler(BackendType backend, double srcRatio, int quality, long blockSize);
    ~TAudioResampler();

    void reset();

    // CLEAN & PURE: Strictly float-to-float mono resampling
    nframes_t process(const float* inputBuffer, nframes_t inputFrames,
                      float* outputBuffer, nframes_t maxOutputFrames,
                      bool endOfInput);
    // The end of the input: what the resampler still holds (input it kept
    // and the tail of its filter), up to maxOutputFrames. Called until it
    // gives nothing.
    nframes_t flush(float* outputBuffer, nframes_t maxOutputFrames);

private:
    BackendType     m_backendType;
    double          m_srcRatio;
    long            m_blockSize;

    std::unique_ptr<ResampleBackend> m_backend;

    nframes_t       m_leftOverFrames;
    TRealTimeAudioBuffer m_inputWorkBuffer;
};

