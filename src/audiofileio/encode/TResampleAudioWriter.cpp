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
#include "TExportSpecification.h"
#include "TAudioResampler.h"
#include "TFileIOBuffer.h"
#include "gdither.h"

TResampleAudioWriter::TResampleAudioWriter(TExportSpecification* spec)
    : m_spec(spec)
    , m_inputSampleRate(0)
    , m_outputSampleRate(0)
    , m_channelCount(0)
    , m_dither(nullptr)
{
    Q_ASSERT(m_spec);
    m_codecWriter = AbstractAudioWriter::create_audio_writer(m_spec);
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
    m_outputSampleRate = m_spec->get_sample_rate();
    m_channelCount = m_spec->get_channel_count();

    m_resamplers.clear();
    if (m_inputSampleRate != m_outputSampleRate) {
        double ratio = double(m_outputSampleRate) / m_inputSampleRate;
        TAudioResampler::BackendType backend = TAudioResampler::BackendType::LIBSOXR;
        int quality = m_spec->get_sample_rate_conversion_quality();

        for (uint chn = 0; chn < m_channelCount; ++chn) {
            m_resamplers.push_back(std::make_unique<TAudioResampler>(
                backend, ratio, quality, m_spec->get_block_size()
                ));
        }
    }

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    if (m_spec->get_data_format() != TraversoDAW::DataFormat::FLOAT) {
        m_dither = gdither_new(
            m_spec->get_dither_type(),
            m_channelCount,
            m_spec->get_dither_size(),
            m_spec->get_bit_depth()
            );
    }

    return true;
}

bool TResampleAudioWriter::close()
{
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

    nframes_t targetFrames = frameCount;
    float* interleavedFloatBuffer = nullptr;
    uint chan;

    if (!m_resamplers.empty()) {
        double ratio = double(m_outputSampleRate) / m_inputSampleRate;
        nframes_t maxExpectedOutFrames = nframes_t(frameCount * ratio) + 64;
        nframes_t requiredInterleavedSamples = maxExpectedOutFrames * m_channelCount;

        fileIOBuffer.check_capacity(requiredInterleavedSamples, m_channelCount);
        interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(requiredInterleavedSamples);

        std::vector<std::vector<float>> tmpMonoBuffers(m_channelCount, std::vector<float>(maxExpectedOutFrames));

        for (chan = 0; chan < m_channelCount; ++chan) {
            float* inputMonoData = fileIOBuffer.get_channel_buffer(chan).get_data(frameCount);
            targetFrames = m_resamplers[chan]->process(
                inputMonoData,
                frameCount,
                tmpMonoBuffers[chan].data(),
                maxExpectedOutFrames,
                false
                );
        }

        if (targetFrames > 0) {
            for (chan = 0; chan < m_channelCount; ++chan) {
                for (uint f = 0; f < targetFrames; ++f) {
                    interleavedFloatBuffer[f * m_channelCount + chan] = tmpMonoBuffers[chan][f];
                }
            }
        }
    }
    else {
        nframes_t requiredInterleavedSamples = frameCount * m_channelCount;
        fileIOBuffer.check_capacity(requiredInterleavedSamples, m_channelCount);
        interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(requiredInterleavedSamples);

        for (chan = 0; chan < m_channelCount; ++chan) {
            float* readBuffer = fileIOBuffer.get_channel_buffer(chan).get_data(frameCount);
            for (uint f = 0; f < frameCount; ++f) {
                interleavedFloatBuffer[f * m_channelCount + chan] = readBuffer[f];
            }
        }
    }

    if (targetFrames == 0) {
        return 0;
    }

    nframes_t finalInterleavedSamples = targetFrames * m_channelCount;
    uint sampleBytes = m_spec->get_sample_bytes();

    if (sampleBytes > 0) {
        fileIOBuffer.check_packed_byte_capacity(finalInterleavedSamples * sampleBytes);
    }

    void* packedOutputBuffer = fileIOBuffer.get_packed_byte_buffer();
    nframes_t writtenFrames = 0;

    switch (m_spec->get_data_format()) {
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
