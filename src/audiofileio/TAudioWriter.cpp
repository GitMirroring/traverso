/*
Copyright (C) 2007-2026 Ben Levitt, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "TAudioWriter.h"
#include "TAudioEncoder.h"
#include "TAudioResampler.h"
#include "TFileIOBuffer.h"
#include "gdither.h"

TAudioWriter::TAudioWriter(const TAudioEncodeSettings& settings)
    : m_settings(settings)
    , m_inputSampleRate(0)
    , m_dither(nullptr)
{
    m_encoder = create_audio_encoder(m_settings);
}

TAudioWriter::~TAudioWriter()
{
    close();
}

bool TAudioWriter::open(const QString& filename, uint inputSampleRate)
{
    if (!m_encoder || !m_encoder->open(filename)) {
        return false;
    }

    m_inputSampleRate = inputSampleRate;

    m_resampler.reset();
    if (m_inputSampleRate != m_settings.sampleRate) {
        const double ratio = double(m_settings.sampleRate) / m_inputSampleRate;
        m_resampler = std::make_unique<TAudioResampler>(m_settings.resampler, ratio, m_settings.resampleQuality, m_settings.channelCount);
    }

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    if (m_settings.dataFormat != TraversoDAW::DataFormat::FLOAT) {
        m_dither = gdither_new(
            m_settings.ditherType,
            m_settings.channelCount,
            m_settings.dither_size(),
            m_settings.bit_depth()
            );
    }

    return true;
}

bool TAudioWriter::close()
{
    // The end of the input: what the resampler still holds goes into the
    // file too, else the file loses its last few milliseconds
    bool flushed = true;
    if (m_resampler && m_encoder) {
        flushed = flush_resampler();
    }

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    m_resampler.reset();

    if (m_encoder) {
        return m_encoder->close() && flushed;
    }

    return false;
}

nframes_t TAudioWriter::write(TFileIOBuffer& fileIOBuffer, nframes_t frameCount)
{
    if (!m_encoder || frameCount == 0) {
        return 0;
    }

    if (!m_resampler) {
        interleave(fileIOBuffer, frameCount);
        return write_interleaved(fileIOBuffer, frameCount);
    }

    // Resampled into the writer's own buffer: the output has another length
    // than the input it is made from, and the input is still read
    const double ratio = double(m_settings.sampleRate) / m_inputSampleRate;
    const nframes_t maxOutputFrames = nframes_t(frameCount * ratio) + 64;
    m_resampleEncodeBuffer.check_capacity(maxOutputFrames, m_settings.channelCount);
    const nframes_t frames = m_resampler->process(fileIOBuffer.channel_buffers(), frameCount, m_resampleEncodeBuffer.channel_buffers(), 0,
                                                  maxOutputFrames);
    if (frames == 0) {
        return 0;
    }
    interleave(m_resampleEncodeBuffer, frames);
    return write_interleaved(m_resampleEncodeBuffer, frames);
}

bool TAudioWriter::flush_resampler()
{
    // The resampler says how much it can still hold: one flush of that many
    // frames gives all of it
    const nframes_t pending = m_resampler->pending_frames();
    m_resampleEncodeBuffer.check_capacity(pending, m_settings.channelCount);
    const nframes_t frames = m_resampler->flush(m_resampleEncodeBuffer.channel_buffers(), 0, pending);
    if (frames == 0) {
        return true;
    }
    interleave(m_resampleEncodeBuffer, frames);
    return write_interleaved(m_resampleEncodeBuffer, frames) == frames;
}

void TAudioWriter::interleave(TFileIOBuffer& buffer, nframes_t frames)
{
    buffer.check_capacity(frames, m_settings.channelCount);
    float* interleaved = buffer.get_file_io_interleaved_buffer().get_data(frames * m_settings.channelCount);
    for (uint chan = 0; chan < m_settings.channelCount; ++chan) {
        const float* data = buffer.get_channel_buffer(chan).get_data(frames);
        for (nframes_t f = 0; f < frames; ++f) {
            interleaved[f * m_settings.channelCount + chan] = data[f];
        }
    }
}

nframes_t TAudioWriter::write_interleaved(TFileIOBuffer& fileIOBuffer, nframes_t targetFrames)
{
    nframes_t finalInterleavedSamples = targetFrames * m_settings.channelCount;
    // Not const: gdither and the codec writers take a plain pointer
    float* interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(finalInterleavedSamples);

    // The bytes a sample takes in memory, as gdither writes it and the
    // codec writer reads it: a 24 bit sample sits in a 32 bit word
    // (GDither32bit, sf_writef_int()), not in the three bytes it takes in
    // the file
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
        for (uint chn = 0; chn < m_settings.channelCount; ++chn) {
            gdither_runf(m_dither, chn, targetFrames, interleavedFloatBuffer, packedOutputBuffer);
        }
        writtenFrames = m_encoder->write(packedOutputBuffer, targetFrames);
        break;

    case TraversoDAW::DataFormat::PCM_32:
    {
        Q_ASSERT(packedOutputBuffer);
        int32_t* ob = static_cast<int32_t*>(packedOutputBuffer);
        const double int_max = double(INT_MAX);
        const double int_min = double(INT_MIN);

        for (uint chn = 0; chn < m_settings.channelCount; ++chn) {
            for (nframes_t x = 0; x < targetFrames; ++x) {
                uint i = chn + (x * m_settings.channelCount);

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
        writtenFrames = m_encoder->write(packedOutputBuffer, targetFrames);
        break;
    }

    default:
        writtenFrames = m_encoder->write(interleavedFloatBuffer, targetFrames);
        break;
    }

    return writtenFrames;
}
