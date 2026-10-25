/*
Copyright (C) 2007 - 2026 Ben Levitt, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/


#include "TAudioReader.h"
#include "TAudioDecoder.h"
#include "TLog.h"

#include <algorithm>

bool TAudioReader::can_read(const QString& fileName)
{
    return can_decode_audio(fileName);
}

std::unique_ptr<TAudioReader> TAudioReader::open(const QString& fileName)
{
    // The factory logs why it has none
    std::unique_ptr<TAudioDecoder> decoder = open_audio_decoder(fileName);
    if (!decoder) {
        return nullptr;
    }
    return std::unique_ptr<TAudioReader>(new TAudioReader(std::move(decoder)));
}

TAudioReader::TAudioReader(std::unique_ptr<TAudioDecoder> decoder)
    : m_decoder(std::move(decoder))
    , m_outputRate(m_decoder->rate())
    , m_outputFrames(m_decoder->frame_count())
{
}

TAudioReader::~TAudioReader() = default;

uint TAudioReader::channel_count() const
{
    return m_decoder->channel_count();
}

uint TAudioReader::file_rate() const
{
    return m_decoder->rate();
}

nframes_t TAudioReader::file_frame_count() const
{
    return m_decoder->frame_count();
}

TTimeRef TAudioReader::length() const
{
    return m_decoder->length();
}

QString TAudioReader::decoder_type() const
{
    return m_decoder->decoder_type();
}

void TAudioReader::set_output_rate_and_resampler(uint rate, TAudioResampler::BackendType backend, int quality)
{
    if (rate == 0) {
        return;
    }

    m_outputRate = rate;
    // The length is the file's, at any output rate; the frames the file
    // gives at the output rate end where it ends
    m_outputFrames = rate == file_rate() ? m_decoder->frame_count()
                                         : TTimeRef::to_nframes(TTimeRef::frame_at_or_after(m_decoder->length(), rate));

    // Another ratio is another resampler (soxr cannot change it once it ran)
    m_resampler.reset();
    if (rate != file_rate()) {
        m_resampler = std::make_unique<TAudioResampler>(backend, double(rate) / file_rate(), quality, channel_count());
    }
    m_dropFrames = 0;

    // The reader is where it was, now at this rate: the decoder sought again
    // and the new resampler primed from the frame before it. The owner seeks
    // too after a change of rate (TBufferedAudioStream::step()).
    reposition(m_frame);
}

// Forgets what the resampler holds of the frames fed so far (the filter
// history the next output frames would be computed from, and input kept for
// the next feed) and the frames still to drop after a seek: all of it belongs
// to the position read before, and the next read is from another one
void TAudioReader::reset()
{
    if (m_resampler) {
        m_resampler->reset();
    }
    m_dropFrames = 0;
}

bool TAudioReader::reposition(nframes_t frame)
{
    if (!m_resampler) {
        // A decoder that is there already does not seek
        if (m_decoder->frame() != frame && !m_decoder->seek(frame)) {
            return false;
        }
        m_frame = frame;
        m_decoderMoved = false;
        return true;
    }

    // The resampler's filter history and kept input are from the position
    // read before: fed on, they would put that position's tail (a click, the
    // length of the filter) and its kept frames at the new position. Before
    // the drop is set below, since the reset clears it.
    reset();

    // A position in output frames is mostly no whole frame of the file, and
    // a seek to the nearest file frame would shift the output by a fraction
    // of a frame. The file is sought to the last frame before frame where a
    // frame of both rates coincide (every 147 frames of 44.1 kHz and 160 of
    // 48 kHz); the next read drops the output frames from there to frame.
    const TTimeRef position(frame, m_outputRate);
    const TTimeRef common = TTimeRef::common_frame_at_or_before(position, file_rate(), m_outputRate);
    const nframes_t fileFrame = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(common, file_rate()));
    if (m_decoder->frame() != fileFrame && !m_decoder->seek(fileFrame)) {
        return false;
    }
    m_dropFrames = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(position - common, m_outputRate));
    m_frame = frame;
    m_decoderMoved = false;
    return true;
}

nframes_t TAudioReader::read(TFileIOBuffer& buffer, const TTimeRef& location, nframes_t count)
{
    if (count == 0) {
        return 0;
    }
    // The reader writes into the buffer, so it makes room in it
    buffer.check_capacity(count, channel_count());

    // The output frame nearest to location; before the file: frame 0. The
    // one rounding of a read: it moves with the location, so a read at the
    // location where the last one ended goes on from there, no seek
    const nframes_t first = TTimeRef::to_nframes(std::max<qint64>(0, TTimeRef::frame_nearest(location, m_outputRate)));
    // Where the output left off, after the file's own frames were read: the
    // decoder back on its frame (where it cannot go, a seek as below)
    if (first == m_frame && m_decoderMoved && (m_decoder->frame() == m_resumeFrame || m_decoder->seek(m_resumeFrame))) {
        m_decoderMoved = false;
    }
    if ((first != m_frame || m_decoderMoved) && !reposition(first)) {
        // Nothing to read there: zeros, as past the end of the file
        buffer.silence(0, count, channel_count());
        return 0;
    }

    const nframes_t read = m_resampler ? read_resampled(buffer, count) : m_decoder->read(buffer, count);
    m_frame += read;
    return read;
}

nframes_t TAudioReader::read_file_frames(TFileIOBuffer& buffer, nframes_t firstFrame, nframes_t count)
{
    if (count == 0) {
        return 0;
    }
    // The decoder moves: where the output left it, for the next read()
    if (!m_decoderMoved) {
        m_resumeFrame = m_decoder->frame();
        m_decoderMoved = true;
    }
    return m_decoder->read_from(buffer, firstFrame, count);
}

nframes_t TAudioReader::read_resampled(TFileIOBuffer& buffer, nframes_t count)
{
    // Not past the frames the file gives at the output rate
    const nframes_t frames = m_frame < m_outputFrames ? std::min(count, m_outputFrames - m_frame) : 0;
    if (frames == 0) {
        buffer.silence(0, count, channel_count());
        return 0;
    }

    // After a seek: the output frames between the common frame the file was
    // sought to and the frame asked for (reposition()), in pieces the buffer
    // holds; the output asked for writes over them
    while (m_dropFrames > 0) {
        const nframes_t toDrop = std::min(m_dropFrames, frames);
        m_dropFrames = resample_into(buffer, toDrop) < toDrop ? 0 : m_dropFrames - toDrop;
    }

    // Zeros only past what the resampler gives: padding a short read
    // anywhere else would shift all that follows
    const nframes_t produced = resample_into(buffer, frames);
    buffer.silence(produced, count - produced, channel_count());
    return frames;
}

nframes_t TAudioReader::file_frames_for(nframes_t outputFrames) const
{
    return TTimeRef::to_nframes(TTimeRef::frames_covering(outputFrames, m_outputRate, file_rate()));
}

nframes_t TAudioReader::resample_into(TFileIOBuffer& out, nframes_t count)
{
    // The resampler gives what its input allows: after a seek it holds back
    // the length of its filter, and soxr releases its output in blocks of its
    // own. So it is fed until it gave all that was asked for, reading as much
    // of the file as that takes; at the end of the file it is flushed, also
    // over several reads when they ask for less than it holds.
    nframes_t produced = 0;
    while (produced < count) {
        if (m_decoder->at_end()) {
            produced += m_resampler->flush(out.channel_buffers(), produced, count - produced);
            break;
        }
        // The decoder makes room in its buffer and leaves no stale frames in it
        const nframes_t inputFrames = m_decoder->read(m_decodeBuffer, file_frames_for(count - produced));
        if (inputFrames == 0) {
            // A read error: not at the end, and nothing came
            break;
        }
        produced += m_resampler->process(m_decodeBuffer.channel_buffers(), inputFrames, out.channel_buffers(), produced, count - produced);
    }
    return produced;
}
