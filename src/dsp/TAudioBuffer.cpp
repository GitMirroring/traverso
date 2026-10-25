// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// The parts of TAudioBuffer that allocate: never called in the audio thread,
// so nothing is lost by not inlining them

#include "TAudioBuffer.h"

#include "TLog.h"

#include <algorithm>
#include <new>

#ifdef USE_MLOCK
#include <sys/mman.h>
#endif /* USE_MLOCK */

void TAudioBuffer::resize(nframes_t size)
{
    Q_ASSERT(size > 0);

    if (size == m_size) {
        return;
    }

    // Cache old buffer state variables before making changes
    audio_sample_t* oldBuffer = m_buffer;
    nframes_t oldSize = m_size;
#ifdef USE_MLOCK
    bool oldMemLocked = m_memLocked;
#endif /* USE_MLOCK */

    // Reset state for new allocation block
    m_buffer = nullptr;
    m_size = 0;
    m_memLocked = false;

    // Allocate the new memory block with correct size and alignment
    allocate_buffer_data(size);

    // Clear out the newly allocated block to prevent noise
    silence_data();

    // Migrate historical audio samples / leftovers if old data is valid
    if (oldBuffer && oldSize > 0) {
        nframes_t framesToCopy = std::min(oldSize, size);
        std::memcpy(m_buffer, oldBuffer, framesToCopy * sizeof(audio_sample_t));

#ifdef USE_MLOCK
        if (oldMemLocked) {
            munlock(oldBuffer, oldSize * sizeof(audio_sample_t));
        }
#endif /* USE_MLOCK */

        operator delete[](oldBuffer, std::align_val_t(AUDIO_BUFFER_ALIGNMENT));
    }
}

void TAudioBuffer::allocate_buffer_data(nframes_t size)
{
    Q_ASSERT(!m_buffer);
    Q_ASSERT(m_size == 0);

    m_buffer = new (std::align_val_t(AUDIO_BUFFER_ALIGNMENT)) audio_sample_t[size];
    m_size = size;

#ifdef USE_MLOCK
    if (m_wantsMemLock) {
        if (mlock (m_buffer, size * sizeof(audio_sample_t)) == -1) {
            TLOG_E("dsp", "could not lock a buffer of {} frames into memory", size);
        } else {
            m_memLocked = true;
        }
    }
#endif /* USE_MLOCK */
}

void TAudioBuffer::delete_buffer_data()
{
    if (!m_buffer) return;

#ifdef USE_MLOCK
    if (m_memLocked) {
        if (munlock (m_buffer, m_size * sizeof(audio_sample_t)) == -1) {
            TLOG_E("dsp", "could not unlock a buffer of {} frames from memory", m_size);
        }
        m_memLocked = false;
    }
#endif /* USE_MLOCK */

    operator delete[](m_buffer, std::align_val_t(AUDIO_BUFFER_ALIGNMENT));
    m_buffer = nullptr;
    m_size = 0;
}
