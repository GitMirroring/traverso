/*
Copyright (C) 2006-2026 Remon Sijrier

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

#include "TBufferedAudioStreamWriter.h"
#include "TExportSpecification.h"
#include "AudioBus.h"
#include "TAudioDevice.h"
#include "AbstractAudioWriter.h"
#include "TPeak.h"
#include "TQueueBufferSlot.h"
#include "Debugger.h"
#include "TAudioResampler.h"
#include "TFileIOBuffer.h"
#include "gdither.h"
#include <cmath>
#include <climits>

TBufferedAudioStreamWriter::TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName)
    : TBufferedAudioStream(exportDir, exportFileName)
    , m_resampleOutputBuffer(1)
{
    m_writer = nullptr;
    m_peak = nullptr;
    m_channelCount = 0;
    m_isRecording = false;
    m_dither = nullptr;
    m_dataFormat = TraversoDAW::DataFormat::FLOAT;
    m_sampleBytes = 0;
    m_sampleRate = 0;
}

TBufferedAudioStreamWriter::~TBufferedAudioStreamWriter()
{
    PENTERDES;
    if (m_peak) {
        delete m_peak;
    }
}

int TBufferedAudioStreamWriter::finish_export()
{
    PENTER;

    if (m_peak && m_peak->finish_processing() < 0) {
        PERROR("WriteSource::finish_export : peak->finish_processing() failed!");
    }

    if (m_writer) {
        m_writer->close();
        m_writer.reset();
    }

    m_exportResamplers.clear();

    if (m_dither) {
        gdither_free(m_dither);
        m_dither = nullptr;
    }

    m_exportFinished = true;
    emit exportFinished();

    return 1;
}

int TBufferedAudioStreamWriter::prepare_export(TExportSpecification *specification)
{
    PENTER;
    Q_ASSERT(specification->is_valid() == 1);

    m_outputRate = audiodevice().get_sample_rate();
    m_channelCount = specification->get_channel_count();
    m_sampleBytes = specification->get_sample_bytes();
    m_dataFormat = specification->get_data_format();
    m_sampleRate = specification->get_sample_rate();

    set_name(get_name() + specification->get_file_extension());

    specification->print_export_data();

    m_writer = AbstractAudioWriter::create_audio_writer(specification);
    if (!m_writer->open(m_fileName)) {
        PERROR("Write Source failed to open");
        return -1;
    }

    m_exportResamplers.clear();

    if (m_outputRate != m_sampleRate) {
        double ratio = double(m_sampleRate) / m_outputRate;
        TAudioResampler::BackendType backend = TAudioResampler::BackendType::LIBSOXR;
        int quality = specification->get_sample_rate_conversion_quality();

        for (uint c = 0; c < m_channelCount; ++c) {
            m_exportResamplers.push_back(std::make_unique<TAudioResampler>(
                backend, ratio, quality, specification->get_block_size()
                ));
        }

        nframes_t maxOutFramesPerChannel = nframes_t(specification->get_block_size() * ratio) + 64;
        m_resampleOutputBuffer.resize(maxOutFramesPerChannel * m_channelCount);
    }

    if (m_dataFormat != TraversoDAW::DataFormat::FLOAT) {
        m_dither = gdither_new(specification->get_dither_type(), m_channelCount, specification->get_dither_size(), specification->get_bit_depth());
    } else {
        m_dither = nullptr;
    }

    return 0;
}

int TBufferedAudioStreamWriter::rb_file_write(TQueueBufferSlot* slot, TFileIOBuffer& fileIOBuffer)
{
    nframes_t writtenFrames = 0;
    uint chan;
    nframes_t nframes = slot->get_buffer_size();

    nframes_t requiredInputSamples = nframes * m_channelCount;

    fileIOBuffer.check_capacity(requiredInputSamples, m_channelCount);

    for (chan = 0; chan < m_channelCount; ++chan) {
        TAudioBuffer& targetChannelBuffer = fileIOBuffer.get_channel_buffer(chan);
        slot->read_buffer(targetChannelBuffer, chan, nframes);

        if (m_peak) {
            m_peak->process(chan, targetChannelBuffer.get_data(nframes), nframes);
        }
    }

    nframes_t targetFrames = nframes;
    float* interleavedFloatBuffer = nullptr;

    if (!m_exportResamplers.empty()) {
        double ratio = double(m_sampleRate) / m_outputRate;
        nframes_t maxExpectedOutFrames = nframes_t(nframes * ratio) + 64;
        nframes_t requiredInterleavedSamples = maxExpectedOutFrames * m_channelCount;

        fileIOBuffer.check_capacity(requiredInterleavedSamples, m_channelCount);
        interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(requiredInterleavedSamples);

        nframes_t maxOutFramesPerChannel = m_resampleOutputBuffer.get_size() / m_channelCount;
        float* baseResamplePtr = m_resampleOutputBuffer.get_data(m_resampleOutputBuffer.get_size());

        for (chan = 0; chan < m_channelCount; ++chan) {
            float* inputMonoData = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
            float* outputMonoTarget = baseResamplePtr + (chan * maxOutFramesPerChannel);

            targetFrames = m_exportResamplers[chan]->process(
                inputMonoData, nframes, outputMonoTarget, maxOutFramesPerChannel, false
                );
        }

        if (targetFrames > 0) {
            for (chan = 0; chan < m_channelCount; chan++) {
                float* monoChannelBase = baseResamplePtr + (chan * maxOutFramesPerChannel);
                for (uint f = 0; f < targetFrames; f++) {
                    interleavedFloatBuffer[f * m_channelCount + chan] = monoChannelBase[f];
                }
            }
        }
    } else {
        nframes_t requiredInterleavedSamples = nframes * m_channelCount;
        fileIOBuffer.check_capacity(requiredInterleavedSamples, m_channelCount);
        interleavedFloatBuffer = fileIOBuffer.get_file_io_interleaved_buffer().get_data(requiredInterleavedSamples);

        for (chan = 0; chan < m_channelCount; chan++) {
            auto readBuffer = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
            for (uint f = 0; f < nframes; f++) {
                interleavedFloatBuffer[f * m_channelCount + chan] = readBuffer[f];
            }
        }
    }

    if (targetFrames == 0) {
        return 0;
    }

    nframes_t finalInterleavedSamples = targetFrames * m_channelCount;

    if (m_sampleBytes > 0) {
        fileIOBuffer.check_packed_byte_capacity(finalInterleavedSamples * m_sampleBytes);
    }

    void* packedOutputBuffer = fileIOBuffer.get_packed_byte_buffer();

    switch (m_dataFormat) {
    case TraversoDAW::DataFormat::PCM_S8:
    case TraversoDAW::DataFormat::PCM_16:
    case TraversoDAW::DataFormat::PCM_24:
        Q_ASSERT(m_dither);
        Q_ASSERT(packedOutputBuffer);
        for (uint chn = 0; chn < m_channelCount; ++chn) {
            gdither_runf(m_dither, chn, targetFrames, interleavedFloatBuffer, packedOutputBuffer);
        }
        writtenFrames = m_writer->write(packedOutputBuffer, targetFrames);
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
    }
        writtenFrames = m_writer->write(packedOutputBuffer, targetFrames);
        break;

    default: // TraversoDAW::DataFormat::FLOAT
        writtenFrames = m_writer->write(interleavedFloatBuffer, targetFrames);
        break;
    }

    return (writtenFrames == targetFrames) ? nframes : 0;
}

nframes_t TBufferedAudioStreamWriter::ringbuffer_write(TProcessCallBackData &processData)
{
    Q_ASSERT(m_rtBufferSlotsQueue);
    Q_ASSERT(m_freeBufferSlotsQueue);


    nframes_t nframes = processData.get_nframes_to_process();
    AudioBus* bus = processData.get_ringbuffer_write_bus();
    Q_ASSERT(bus);
    Q_ASSERT(bus->get_channel_count() == m_channelCount);

    TQueueBufferSlot* slot = nullptr;

    if ((slot = dequeue_from_free_queue(processData)) )
    {
        Q_ASSERT(slot);

        for (uint chan=0; chan < m_channelCount; ++chan) {
            slot->write_buffer(processData.get_start_location(), TTimeRef(), bus->get_buffer(chan).get_data(nframes), chan, nframes);
        }

        if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
            printf("WriteSource::ringbuffer_write: Failed to write to rt buffer queue\n");
            return 0;
        }

        return slot->get_buffer_size();
    }

    return 0;
}

TQueueBufferSlot* TBufferedAudioStreamWriter::dequeue_from_free_queue(TProcessCallBackData &processData)
{
    TQueueBufferSlot* slot = nullptr;

    if (processData.get_is_real_time()) {
        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
        }
    } else {
        auto startTime = TTimeRef::get_nanoseconds_since_epoch();
        m_freeBufferSlotsQueue->wait_dequeue(slot);
        processData.add_ringbuffer_read_wait_time(TTimeRef::get_nanoseconds_since_epoch() - startTime);
    }

    return slot;
}

void TBufferedAudioStreamWriter::process_realtime_buffers(TFileIOBuffer &fileDecodeBuffer)
{
    if (m_exportFinished) {
        return;
    }
    Q_ASSERT(m_writer);

    TQueueBufferSlot* slot = nullptr;
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        rb_file_write(slot, fileDecodeBuffer);
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    if (!m_isRecording) {
        finish_export();
    }
}

void TBufferedAudioStreamWriter::set_process_peaks( bool process )
{
    m_processPeaks = process;

    if (!m_processPeaks) {
        return;
    }

    Q_ASSERT(!m_peak);

    m_peak = new TPeak(this);

    if (m_peak->prepare_processing(audiodevice().get_sample_rate()) < 0) {
        PERROR("Cannot process peaks realtime");
        m_processPeaks = false;
        delete m_peak;
        m_peak = nullptr;

        return;
    }
}

void TBufferedAudioStreamWriter::set_recording(bool rec )
{
    m_isRecording = rec;
}

TBufferedAudioStreamStatus& TBufferedAudioStreamWriter::get_buffer_status()
{
   m_bufferstatus.set_fill_status((m_freeBufferSlotsQueue->size_approx() * 100) / m_slotcount);
    // FIXME
    // Ugly hack to let DiskIO keep calling process_realtime_buffers()
    // which will then call finish_export()
    if (!m_isRecording) {
        m_bufferstatus.set_fill_status(0);
    }
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::IN_SYNC);
    return m_bufferstatus;
}

