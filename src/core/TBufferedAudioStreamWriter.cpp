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
#include "TResampleAudioWriter.h"
#include "TPeak.h"
#include "TQueueBufferSlot.h"
#include "TFileIOBuffer.h"
#include "Debugger.h"

TBufferedAudioStreamWriter::TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName)
    : TBufferedAudioStream(exportDir, exportFileName)
{
    m_resampleWriter = nullptr;
    m_peak = nullptr;
    m_channelCount = 0;
    m_isRecording = false;
    m_sampleRate = 0;
    m_processPeaks = false;
}

TBufferedAudioStreamWriter::~TBufferedAudioStreamWriter()
{
    PENTERDES;
    if (m_peak) {
        delete m_peak;
    }
}

int TBufferedAudioStreamWriter::prepare_export(TExportSpecification *specification)
{
    PENTER;
    Q_ASSERT(specification->is_valid() == 1);

    m_outputRate = specification->get_sample_rate();
    m_channelCount = specification->get_channel_count();
    m_sampleRate = audiodevice().get_sample_rate();

    set_name(get_name() + specification->get_file_extension());
    specification->print_export_data();

    m_resampleWriter = std::make_unique<TResampleAudioWriter>(specification);
    if (!m_resampleWriter->open(m_fileName, m_sampleRate)) {
        PERROR("Write Source failed to open via TResampleAudioWriter");
        return -1;
    }

    return 0;
}

int TBufferedAudioStreamWriter::finish_export()
{
    PENTER;

    if (m_peak && m_peak->finish_processing() < 0) {
        PERROR("WriteSource::finish_export : peak->finish_processing() failed!");
    }

    if (m_resampleWriter) {
        m_resampleWriter->close();
        m_resampleWriter.reset();
    }

    m_exportFinished = true;
    emit exportFinished();

    return 1;
}

int TBufferedAudioStreamWriter::rb_file_write(TQueueBufferSlot* slot, TFileIOBuffer& fileIOBuffer)
{
    nframes_t nframes = slot->get_buffer_size();

    fileIOBuffer.check_capacity(nframes * m_channelCount, m_channelCount);

    for (uint chan = 0; chan < m_channelCount; ++chan) {
        TAudioBuffer& targetChannelBuffer = fileIOBuffer.get_channel_buffer(chan);
        slot->read_buffer(targetChannelBuffer, chan, nframes);

        if (m_peak) {
            m_peak->process(chan, targetChannelBuffer.get_data(nframes), nframes);
        }
    }

    nframes_t writtenFrames = m_resampleWriter->write_planar(fileIOBuffer, nframes);

    if (writtenFrames != nframes) {
        PERROR(QString("Export write failure! Requested: %1, Written: %2").arg(nframes).arg(writtenFrames));
        return 0;
    }

    return nframes;
}

nframes_t TBufferedAudioStreamWriter::ringbuffer_write(TProcessCallBackData &processData)
{
    Q_ASSERT(m_rtBufferSlotsQueue);
    Q_ASSERT(m_freeBufferSlotsQueue);

    nframes_t nframes = processData.get_nframes_to_process();
    AudioBus* bus = processData.get_ringbuffer_write_bus();
    Q_ASSERT(bus);
    Q_ASSERT(bus->get_channel_count() == m_channelCount);

    TQueueBufferSlot* slot = dequeue_from_free_queue(processData);
    if (slot) {
        for (uint chan = 0; chan < m_channelCount; ++chan) {
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
        m_freeBufferSlotsQueue->try_dequeue(slot);
    } else {
        auto startTime = TTimeRef::get_nanoseconds_since_epoch();
        m_freeBufferSlotsQueue->wait_dequeue(slot);
        processData.add_ringbuffer_read_wait_time(TTimeRef::get_nanoseconds_since_epoch() - startTime);
    }

    return slot;
}

void TBufferedAudioStreamWriter::process_realtime_buffers(TFileIOBuffer &fileIOBuffer)
{
    if (m_exportFinished) {
        return;
    }
    Q_ASSERT(m_resampleWriter);

    TQueueBufferSlot* slot = nullptr;
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        rb_file_write(slot, fileIOBuffer);
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    if (!m_isRecording) {
        finish_export();
    }
}

void TBufferedAudioStreamWriter::set_process_peaks(bool process)
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
    }
}

void TBufferedAudioStreamWriter::set_recording(bool rec)
{
    m_isRecording = rec;
}

TBufferedAudioStreamStatus& TBufferedAudioStreamWriter::get_buffer_status()
{
    m_bufferstatus.set_fill_status((m_freeBufferSlotsQueue->size_approx() * 100) / m_slotcount);

    if (!m_isRecording) {
        m_bufferstatus.set_fill_status(0);
    }
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::IN_SYNC);
    return m_bufferstatus;
}
