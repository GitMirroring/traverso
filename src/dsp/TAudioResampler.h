// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TResamplerChoice.h"

#include "TChannelBuffer.h"
#include "defines.h"
#include <QList>
#include <QString>
#include <memory>
#include <vector>

class ResampleBackend;

/**
 * @brief Converts audio from one rate to another, all channels of a stream
 * at once: every channel gives the same frames, by construction.
 *
 * The caller hands channel buffers (one TChannelBuffer per channel) and a
 * range; the backends' plain pointers stay inside this class. Input that a
 * process() cannot take yet (the output range is full) is kept for the next
 * one. At the end of the input, flush() gives what is still inside: the input
 * kept and the tail of the filter, at most pending_frames() frames.
 *
 * Two backends: soxr (the default) and libsamplerate. Not for the audio
 * thread: it allocates when the input grows.
 */
class TAudioResampler
{
public:
    // The qualities of a backend, best first, in its own numbers:
    // libsamplerate's sinc converter types (SRC_SINC_BEST_QUALITY ...
    // SRC_SINC_FASTEST), soxr's recipes (SOXR_VHQ ... SOXR_QQ). A quality of
    // one backend means nothing to the other.
    static QList<int> qualities(TResamplerChoice::Backend backend);
    static QString quality_name(TResamplerChoice::Backend backend, int quality);
    static QString quality_description(TResamplerChoice::Backend backend, int quality);
    // For files written (exports, conversions): soxr's own default (20 bit),
    // libsamplerate's medium sinc
    static int default_quality(TResamplerChoice::Backend backend);
    // For on-the-fly reading, many files at a time: soxr's 16 bit medium,
    // libsamplerate's fastest sinc
    static int default_realtime_quality(TResamplerChoice::Backend backend);
    // As the settings name them: "soxr", "libsamplerate"; another name is soxr
    static QString backend_name(TResamplerChoice::Backend backend);
    static TResamplerChoice::Backend backend_from_name(const QString& name);

    // ratio: the output rate over the input rate. choice: a backend and one
    // of its qualities(). The ratio stays: another ratio is another
    // resampler (soxr cannot change it once it ran).
    TAudioResampler(TResamplerChoice choice, double ratio, uint channels);
    ~TAudioResampler();

    uint channel_count() const { return m_channels; }
    // Whether the backend made its converter; one that did not gives no
    // output (the backend logs why)
    bool valid() const;

    // Forgets the frames fed so far (filter history, kept input): before
    // feeding frames from another position than the last one's
    void reset();

    // Feeds inputFrames frames of in (one buffer per channel, from frame 0)
    // and puts what comes out into out (one buffer per channel) from frame
    // at, at most count frames. The frames given, the same in every channel;
    // fewer than count while the filter fills.
    nframes_t process(const TChannelBuffer* const* in, nframes_t inputFrames, TChannelBuffer* const* out, nframes_t at, nframes_t count);

    // The end of the input: at most this many frames still come out (the
    // input kept and the tail of the filter)
    nframes_t pending_frames() const;
    // The end of the input: what is still inside into out from frame at, at
    // most count frames. The frames given; fewer than count once it is all
    // out. A flush of pending_frames() frames gives all of it.
    nframes_t flush(TChannelBuffer* const* out, nframes_t at, nframes_t count);

private:
    // The kept input followed by inputFrames frames of in (when in is given)
    // through the backend into out from frame at, at most count frames
    nframes_t run(const TChannelBuffer* const* in, nframes_t inputFrames, TChannelBuffer* const* out, nframes_t at, nframes_t count,
                  bool endOfInput);

    uint m_channels;
    double m_ratio;
    std::unique_ptr<ResampleBackend> m_backend;

    // The input a process() could not hand on yet, per channel
    std::vector<std::vector<float>> m_kept;
    nframes_t m_keptFrames{0};
    // The backend's view of the input and the output, per channel
    std::vector<const float*> m_inputPointers;
    std::vector<float*> m_outputPointers;
};
