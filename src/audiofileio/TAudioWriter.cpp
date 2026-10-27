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
#include "TLog.h"

std::expected<std::unique_ptr<TAudioWriter>, TAudioFileError> TAudioWriter::open(const TAudioEncodeSettings& settings, const QString& fileName,
                                                                                  uint inputSampleRate)
{
    const auto cannot = [](const QString& why) {
        return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
    };

    // The device's rate is 0 until a device is set up; a precondition once
    // the rate is guaranteed (task R6)
    if (inputSampleRate == 0) {
        return cannot(QStringLiteral("no input sample rate (no audio device set up)"));
    }
    std::unique_ptr<TAudioEncoder> encoder = create_audio_encoder(settings);
    if (!encoder) {
        return cannot(QStringLiteral("no encoder for this format in this build"));
    }
    // The resampler before the file: one that does not start leaves no
    // empty file behind
    std::unique_ptr<TAudioResampler> resampler;
    if (inputSampleRate != settings.sampleRate) {
        const double ratio = double(settings.sampleRate) / inputSampleRate;
        resampler = std::make_unique<TAudioResampler>(settings.resampler, ratio, settings.resampleQuality, settings.channelCount);
        if (!resampler->valid()) {
            return cannot(QStringLiteral("the %1 resampler did not start").arg(TAudioResampler::backend_name(settings.resampler)));
        }
    }
    if (TAudioFileStatus opened = encoder->open(fileName); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    return std::unique_ptr<TAudioWriter>(new TAudioWriter(settings, std::move(encoder), std::move(resampler), inputSampleRate));
}

TAudioWriter::TAudioWriter(const TAudioEncodeSettings& settings, std::unique_ptr<TAudioEncoder> encoder, std::unique_ptr<TAudioResampler> resampler,
                           uint inputSampleRate)
    : m_encoder(std::move(encoder))
    , m_settings(settings)
    , m_inputSampleRate(inputSampleRate)
    , m_resampler(std::move(resampler))
    , m_dither(nullptr)
{
    if (m_settings.dataFormat != TraversoDAW::DataFormat::FLOAT) {
        m_dither = gdither_new(
            m_settings.ditherType,
            m_settings.channelCount,
            m_settings.dither_size(),
            m_settings.bit_depth()
            );
    }
}

TAudioWriter::~TAudioWriter()
{
    Q_ASSERT_X(m_closed, "~TAudioWriter", "close() was not called");
    // A release build: still closed, and said, so the file is complete. Where
    // the writer is destroyed (for a recording the GUI thread), with file IO
    // and the resampler's flush there, only in this case.
    if (!m_closed) {
        if (TAudioFileStatus closed = close(); !closed) {
            TLOG_E("audiofile", "a writer destroyed without close(): closing failed ({})", closed.error().detail.toStdString());
        } else {
            TLOG_W("audiofile", "a writer destroyed without close(): closed it");
        }
    }
}

TAudioFileStatus TAudioWriter::close()
{
    if (m_closed) {
        return {};
    }
    m_closed = true;

    // The end of the input: what the resampler still holds goes into the
    // file too, else the file loses its last few milliseconds
    TAudioFileStatus flushed = m_resampler ? flush_resampler() : TAudioFileStatus{};

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }
    m_resampler.reset();

    // The encoder completes the file also when the flush failed; the first
    // error is the one told
    TAudioFileStatus closed = m_encoder->close();
    return flushed ? closed : flushed;
}

TAudioFileResult TAudioWriter::write(TFileIOBuffer& fileIOBuffer, nframes_t frameCount)
{
    if (frameCount == 0) {
        return {};
    }
    if (m_closed) {
        return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("the file is closed")}};
    }
    // Every channel of the file in the caller's buffer (one it lacks is
    // silence), so the resampler and the interleaving read no further than
    // the buffer goes
    fileIOBuffer.check_capacity(frameCount, m_settings.channelCount);

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
    // All input is taken, also when the resampler holds it for now (it gives
    // it at a later write, the rest at close())
    if (frames == 0) {
        return {frameCount, std::nullopt};
    }
    interleave(m_resampleEncodeBuffer, frames);
    // The file took less than the input became: a write error, and no
    // input frame count means anything then
    TAudioFileResult written = write_interleaved(m_resampleEncodeBuffer, frames);
    if (written.error) {
        return {0, std::move(written.error)};
    }
    return {frameCount, std::nullopt};
}

TAudioFileStatus TAudioWriter::flush_resampler()
{
    // The resampler says how much it can still hold: one flush of that many
    // frames gives all of it
    const nframes_t pending = m_resampler->pending_frames();
    m_resampleEncodeBuffer.check_capacity(pending, m_settings.channelCount);
    const nframes_t frames = m_resampler->flush(m_resampleEncodeBuffer.channel_buffers(), 0, pending);
    if (frames == 0) {
        return {};
    }
    interleave(m_resampleEncodeBuffer, frames);
    if (TAudioFileResult written = write_interleaved(m_resampleEncodeBuffer, frames); written.error) {
        return std::unexpected(std::move(*written.error));
    }
    return {};
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

TAudioFileResult TAudioWriter::write_interleaved(TFileIOBuffer& fileIOBuffer, nframes_t targetFrames)
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
    TAudioFileResult written;

    switch (m_settings.dataFormat) {
    case TraversoDAW::DataFormat::PCM_S8:
    case TraversoDAW::DataFormat::PCM_16:
    case TraversoDAW::DataFormat::PCM_24:
        Q_ASSERT(m_dither);
        Q_ASSERT(packedOutputBuffer);
        for (uint chn = 0; chn < m_settings.channelCount; ++chn) {
            gdither_runf(m_dither, chn, targetFrames, interleavedFloatBuffer, packedOutputBuffer);
        }
        written = m_encoder->write(packedOutputBuffer, targetFrames);
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
        written = m_encoder->write(packedOutputBuffer, targetFrames);
        break;
    }

    default:
        written = m_encoder->write(interleavedFloatBuffer, targetFrames);
        break;
    }

    return written;
}
