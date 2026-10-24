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
#include "TLog.h"

#include <algorithm>

TAudioReader::TAudioReader(const QString& fileName)
    : m_decoder(open_audio_decoder(fileName))
{
    if (!m_decoder) {
        TLOG_W("audiofile", "{}: no decoder for it", fileName.toStdString());
        return;
    }
    m_channels = m_decoder->channel_count();
    m_fileRate = m_decoder->rate();
    m_outputRate = m_fileRate;
    m_outputFrames = m_decoder->frame_count();
}

TAudioReader::~TAudioReader() = default;

void TAudioReader::set_output_rate_and_resampler(uint rate, TAudioResampler::BackendType backend, int quality)
{
    if (!m_decoder || rate == 0) {
        return;
    }

    m_outputRate = rate;
    // The length is the file's, at any output rate; the frames the file
    // gives at the output rate end where it ends
    m_outputFrames = rate == m_fileRate ? m_decoder->frame_count()
                                        : TTimeRef::to_nframes(TTimeRef::frame_at_or_after(m_decoder->length(), rate));

    // Another ratio is another resampler (soxr cannot change it once it ran)
    m_resampler.reset();
    if (rate != m_fileRate) {
        m_resampler = std::make_unique<TAudioResampler>(backend, double(rate) / m_fileRate, quality, m_channels);
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
    const TTimeRef common = TTimeRef::common_frame_at_or_before(position, m_fileRate, m_outputRate);
    const nframes_t fileFrame = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(common, m_fileRate));
    if (m_decoder->frame() != fileFrame && !m_decoder->seek(fileFrame)) {
        return false;
    }
    m_dropFrames = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(position - common, m_outputRate));
    m_frame = frame;
    return true;
}

nframes_t TAudioReader::read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count)
{
    if (!m_decoder || count == 0) {
        return 0;
    }
    // The reader writes into the buffer, so it makes room in it
    buffer.check_capacity(count, m_channels);

    if (first != m_frame && !reposition(first)) {
        // Nothing to read there: zeros, as past the end of the file
        buffer.silence(0, count, m_channels);
        return 0;
    }

    const nframes_t read = m_resampler ? read_resampled(buffer, count) : m_decoder->read(buffer, count);
    m_frame += read;
    return read;
}

nframes_t TAudioReader::read_resampled(TFileIOBuffer& buffer, nframes_t count)
{
    // Not past the frames the file gives at the output rate
    const nframes_t frames = m_frame < m_outputFrames ? std::min(count, m_outputFrames - m_frame) : 0;
    if (frames == 0) {
        buffer.silence(0, count, m_channels);
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
    buffer.silence(produced, count - produced, m_channels);
    return frames;
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
