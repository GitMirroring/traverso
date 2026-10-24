/*
Copyright (C) 2007-2026 Ben Levitt, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "TResampleAudioWriter.h"
#include "AbstractAudioWriter.h"
#include "TAudioResampler.h"
#include "TFileIOBuffer.h"
#include "gdither.h"
#include "TLog.h"

#include <algorithm>

TResampleAudioWriter::TResampleAudioWriter(const TAudioEncodeSettings& settings)
    : m_settings(settings)
    , m_inputSampleRate(0)
    , m_outputSampleRate(0)
    , m_channelCount(0)
    , m_dither(nullptr)
{
    m_codecWriter = AbstractAudioWriter::create_audio_writer(m_settings);
}

TResampleAudioWriter::~TResampleAudioWriter()
{
    close();
}

bool TResampleAudioWriter::open(const QString& filename, uint inputSampleRate)
{
    Q_ASSERT(m_codecWriter);

    if (!m_codecWriter->open(filename)) {
        return false;
    }

    m_inputSampleRate = inputSampleRate;
    m_outputSampleRate = m_settings.sampleRate;
    m_channelCount = m_settings.channelCount;

    m_resamplers.clear();
    if (m_inputSampleRate != m_outputSampleRate) {
        double ratio = double(m_outputSampleRate) / m_inputSampleRate;
        for (uint chn = 0; chn < m_channelCount; ++chn) {
            m_resamplers.push_back(std::make_unique<TAudioResampler>(
                m_settings.resampler, ratio, m_settings.resampleQuality, m_settings.blockSize
                ));
        }
    }

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    if (m_settings.dataFormat != TraversoDAW::DataFormat::FLOAT) {
        m_dither = gdither_new(
            m_settings.ditherType,
            m_channelCount,
            m_settings.ditherSize,
            m_settings.bitDepth
            );
    }

    return true;
}

bool TResampleAudioWriter::close()
{
    // The end of the input: what the resamplers still hold goes into the
    // file too, else the file loses its last few milliseconds
    if (!m_resamplers.empty() && m_codecWriter) {
        flush_resamplers();
    }

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    m_resamplers.clear();

    if (m_codecWriter) {
        return m_codecWriter->close();
    }

    return false;
}

nframes_t TResampleAudioWriter::write_planar(TFileIOBuffer& fileIOBuffer, nframes_t frameCount)
{
    if (!m_codecWriter || frameCount == 0) {
        return 0;
    }

    if (m_resamplers.empty()) {
        interleave(fileIOBuffer, frameCount);
        return write_interleaved(fileIOBuffer, frameCount);
    }

    // Resampled into the writer's own buffer: the output has another length
    // than the input it is made from, and the input is still read
    const double ratio = double(m_outputSampleRate) / m_inputSampleRate;
    const nframes_t maxOutputFrames = nframes_t(frameCount * ratio) + 64;
    m_resampleEncodeBuffer.check_capacity(maxOutputFrames, m_channelCount);
    nframes_t frames = 0;
    for (uint chan = 0; chan < m_channelCount; ++chan) {
        const nframes_t produced = m_resamplers[chan]->process(fileIOBuffer.get_channel_buffer(chan).get_data(frameCount), frameCount,
                                                               m_resampleEncodeBuffer.get_channel_buffer(chan).get_data(maxOutputFrames),
                                                               maxOutputFrames, false);
        frames = least_frames(chan, produced, frames);
    }
    if (frames == 0) {
        return 0;
    }
    interleave(m_resampleEncodeBuffer, frames);
    return write_interleaved(m_resampleEncodeBuffer, frames);
}

void TResampleAudioWriter::flush_resamplers()
{
    // A resampler does not say how much it still holds (the input it kept
    // and the tail of its filter), so it is flushed in the writer's blocks,
    // the size the resamplers were made with, until it gives nothing
    const nframes_t block = m_settings.blockSize;
    m_resampleEncodeBuffer.check_capacity(block, m_channelCount);
    while (true) {
        nframes_t frames = 0;
        for (uint chan = 0; chan < m_channelCount; ++chan) {
            const nframes_t produced = m_resamplers[chan]->flush(m_resampleEncodeBuffer.get_channel_buffer(chan).get_data(block), block);
            frames = least_frames(chan, produced, frames);
        }
        if (frames == 0) {
            break;
        }
        interleave(m_resampleEncodeBuffer, frames);
        write_interleaved(m_resampleEncodeBuffer, frames);
    }
}

nframes_t TResampleAudioWriter::least_frames(uint chan, nframes_t produced, nframes_t least)
{
    if (chan == 0) {
        return produced;
    }
    if (produced != least && !m_frameCountMismatchLogged) {
        // Cannot be made good here: the least drops the surplus of the one
        // channel, so the channels drift by the difference from here on
        m_frameCountMismatchLogged = true;
        TLOG_W("audiofileio", "the resamplers of channel 0 and channel {} gave {} and {} frames; the least is written",
               chan, least, produced);
        Q_ASSERT_X(false, "TResampleAudioWriter::least_frames", "the channels' resamplers gave different frame counts");
    }
    return std::min(least, produced);
}

void TResampleAudioWriter::interleave(TFileIOBuffer& buffer, nframes_t frames)
{
    buffer.check_capacity(frames, m_channelCount);
    float* interleaved = buffer.get_file_io_interleaved_buffer().get_data(frames * m_channelCount);
    for (uint chan = 0; chan < m_channelCount; ++chan) {
        const float* data = buffer.get_channel_buffer(chan).get_data(frames);
        for (nframes_t f = 0; f < frames; ++f) {
            interleaved[f * m_channelCount + chan] = data[f];
        }
    }
}

nframes_t TResampleAudioWriter::write_interleaved(TFileIOBuffer& fileIOBuffer, nframes_t targetFrames)
{
    nframes_t finalInterleavedSamples = targetFrames * m_channelCount;
    // Not const: gdither and the codec writers take a plain pointer
    float* interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(finalInterleavedSamples);

    // The bytes a sample takes in memory, as gdither writes it and the
    // codec writer reads it: a 24 bit sample sits in a 32 bit word
    // (GDither32bit, sf_writef_int()), not in the three bytes it takes in
    // the file (m_settings.sampleBytes)
    size_t containerBytes = 4;
    switch (m_settings.dataFormat) {
    case TraversoDAW::DataFormat::PCM_S8:
        containerBytes = 1;
        break;
    case TraversoDAW::DataFormat::PCM_16:
        containerBytes = 2;
        break;
    default:
        break;
    }
    if (m_settings.dataFormat != TraversoDAW::DataFormat::FLOAT) {
        fileIOBuffer.check_packed_byte_capacity(size_t(finalInterleavedSamples) * containerBytes);
    }

    void* packedOutputBuffer = fileIOBuffer.get_packed_byte_buffer();
    nframes_t writtenFrames = 0;

    switch (m_settings.dataFormat) {
    case TraversoDAW::DataFormat::PCM_S8:
    case TraversoDAW::DataFormat::PCM_16:
    case TraversoDAW::DataFormat::PCM_24:
        Q_ASSERT(m_dither);
        Q_ASSERT(packedOutputBuffer);
        for (uint chn = 0; chn < m_channelCount; ++chn) {
            gdither_runf(m_dither, chn, targetFrames, interleavedFloatBuffer, packedOutputBuffer);
        }
        writtenFrames = m_codecWriter->write(packedOutputBuffer, targetFrames);
        break;

    case TraversoDAW::DataFormat::PCM_32:
    {
        Q_ASSERT(packedOutputBuffer);
        int32_t* ob = static_cast<int32_t*>(packedOutputBuffer);
        const double int_max = double(INT_MAX);
        const double int_min = double(INT_MIN);

        for (uint chn = 0; chn < m_channelCount; ++chn) {
            for (nframes_t x = 0; x < targetFrames; ++x) {
                uint i = chn + (x * m_channelCount);

                if (interleavedFloatBuffer[i] > 1.0f) {
                    ob[i] = INT_MAX;
                } else if (interleavedFloatBuffer[i] < -1.0f) {
                    ob[i] = INT_MIN;
                } else {
                    if (interleavedFloatBuffer[i] >= 0.0f) {
                        ob[i] = lrintf(int_max * interleavedFloatBuffer[i]);
                    } else {
                        ob[i] = -lrintf(int_min * interleavedFloatBuffer[i]);
                    }
                }
            }
        }
        writtenFrames = m_codecWriter->write(packedOutputBuffer, targetFrames);
        break;
    }

    default:
        writtenFrames = m_codecWriter->write(interleavedFloatBuffer, targetFrames);
        break;
    }

    return writtenFrames;
}
