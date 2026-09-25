#ifndef TAUDIORESAMPLER_H
#define TAUDIORESAMPLER_H

#include "TAudioBuffer.h"
#include "defines.h"
#include <memory>

class ResampleBackend {
public:
    virtual ~ResampleBackend() = default;
    virtual void reset() = 0;
    virtual void set_ratio(double ratio) = 0;
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

    TAudioResampler(BackendType backend, double srcRatio, int quality, long blockSize);
    ~TAudioResampler();

    void reset();
    void set_ratio(double newRatio);

    // CLEAN & PURE: Strictly float-to-float mono resampling
    nframes_t process(const float* inputBuffer, nframes_t inputFrames,
                      float* outputBuffer, nframes_t maxOutputFrames,
                      bool endOfInput);

private:
    BackendType     m_backendType;
    double          m_srcRatio;
    long            m_blockSize;

    std::unique_ptr<ResampleBackend> m_backend;

    nframes_t       m_leftOverFrames;
    TRealTimeAudioBuffer m_inputWorkBuffer;
};

#endif // TAUDIORESAMPLER_H
