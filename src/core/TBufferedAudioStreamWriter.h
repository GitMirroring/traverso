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

#pragma once

#include "TBufferedAudioStream.h"
#include "TProcessCallBackData.h"

class TExportSpecification;
class TPeak;
class TResampleAudioWriter;
class AudioBus;
class TFileIOBuffer;

class TBufferedAudioStreamWriter : public TBufferedAudioStream
{
    Q_OBJECT

public :
    TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName);
    ~TBufferedAudioStreamWriter();

    nframes_t ringbuffer_write(TProcessCallBackData &processData);
    int rb_file_write(TQueueBufferSlot* slot, TFileIOBuffer& fileIOBuffer);

    TBufferedAudioStreamStatus& get_buffer_status() final;
    TPeak* get_peak() { return m_peak; }

    int prepare_export(TExportSpecification* specification);
    int finish_export();

    void set_process_peaks(bool process);
    void set_recording(bool rec);

    bool is_recording() const { return m_isRecording; }

private:
    std::unique_ptr<TResampleAudioWriter>           m_resampleWriter;
    TPeak*                                          m_peak;

    nframes_t       m_sampleRate;
    bool            m_processPeaks;
    bool            m_isRecording;
    bool            m_exportFinished{false};

    TQueueBufferSlot* dequeue_from_free_queue(TProcessCallBackData &processData);

    friend class TDiskIOThread;
    void process_realtime_buffers(TFileIOBuffer &fileIOBuffer) final;
    void rb_seek_to_transport_location(TFileIOBuffer&, const TTimeRef &/*transportLocation*/) final {}
    void set_output_rate_and_convertor_type(int /*outputRate*/, int /*converterType*/) final {}

signals:
    void exportFinished();
};
