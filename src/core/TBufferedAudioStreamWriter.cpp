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
#include "TLog.h"
#include "TPeakCache.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveform.h"
#include "TQueueBufferSlot.h"
#include "TFileIOBuffer.h"
#include "Debugger.h"

TBufferedAudioStreamWriter::TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName)
    : TBufferedAudioStream(exportDir, exportFileName)
{
    m_resampleWriter = nullptr;
    m_channelCount = 0;
    m_isRecording = false;
    m_sampleRate = 0;
    m_processPeaks = false;
}

TBufferedAudioStreamWriter::~TBufferedAudioStreamWriter()
{
    PENTERDES;
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

    m_resampleWriter = std::make_unique<TResampleAudioWriter>(specification->get_encode_settings());
    if (!m_resampleWriter->open(m_fileName, m_sampleRate)) {
        PERROR("Write Source failed to open via TResampleAudioWriter");
        return -1;
    }

    return 0;
}

int TBufferedAudioStreamWriter::finish_export()
{
    PENTER;

    if (m_resampleWriter) {
        m_resampleWriter->close();
        m_resampleWriter.reset();
    }

    // The peaks are on disk already except for the last period: the tail
    // and the header follow once the audio file is complete (its size and
    // time are part of the peak file's validation). The clip puts the file
    // under its name (TAudioClip::finish_write_source()).
    if (m_peaks) {
        QString error;
        if (m_peaks->finish(m_fileName, &error) != TPeakFile::Status::Ok) {
            TLOG_W("peaks", "finishing {} failed: {}", m_peaks->peak_path().toStdString(), error.toStdString());
        } else if (!m_peaksAreRead) {
            // An import or a merge: nobody reads its peaks while it writes
            // them, so the file gets its name here (no mapping to let go of)
            if (m_peaks->publish(&error) == TPeakFile::Status::Ok) {
                TPeakCache::touch(m_peaks->peak_path());
                TPeakCache::evict_later();
            } else {
                TLOG_W("peaks", "publishing {} failed: {}", m_peaks->peak_path().toStdString(), error.toStdString());
            }
        }
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
    }

    if (m_peaks) {
        for (uint chan = 0; chan < m_channelCount; ++chan) {
            m_peakChannels[chan] = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
        }
        m_peaks->append(m_peakChannels.data(), nframes);
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
            TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "WriteSource::ringbuffer_write: Failed to write to rt buffer queue");
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

TDiskIOPlanner::Stream TBufferedAudioStreamWriter::disk_state(const TTimeRef& /*transportLocation*/)
{
    // The fill is the free part: the fullest queue goes first
    const bool work = m_rtBufferSlotsQueue->size_approx() > 0 || (!m_isRecording && !m_exportFinished);
    return TDiskIOPlanner::Stream{work, false, get_buffer_status().get_fill_status()};
}

void TBufferedAudioStreamWriter::set_process_peaks(bool process)
{
    m_processPeaks = process;

    if (!m_processPeaks) {
        return;
    }

    Q_ASSERT(!m_peaks);
    Q_ASSERT(m_channelCount > 0);
    m_peaks = std::make_shared<TPeakStream>(audiodevice().get_sample_rate(), int(m_channelCount), TWaveform::peak_path(this));
    QString error;
    if (m_peaks->open(&error) != TPeakFile::Status::Ok) {
        // Painted from memory meanwhile; built from the audio afterwards
        TLOG_W("peaks", "cannot write {}: {}", m_peaks->peak_path().toStdString(), error.toStdString());
    }
    m_peakChannels.assign(m_channelCount, nullptr);
}

void TBufferedAudioStreamWriter::set_recording(bool rec)
{
    m_isRecording = rec;
    if (rec) {
        m_peaksAreRead = true;
    }
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
