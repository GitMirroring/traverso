// SPDX-FileCopyrightText: 2007 Ben Levitt
// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioEncoder.h"
#include "SFAudioWriter.h"
#include "WPAudioWriter.h"

#ifdef M4A_ENCODE_SUPPORT
#include "FaacAudioWriter.h"
#endif

std::unique_ptr<TAudioEncoder> create_audio_encoder(const TAudioEncodeSettings& settings)
{
    switch (settings.writerType) {
    case TraversoDAW::WriterType::WAVPACK:
        return std::make_unique<WPAudioWriter>(settings);

    case TraversoDAW::WriterType::M4A:
#ifdef M4A_ENCODE_SUPPORT
        return std::make_unique<FaacAudioWriter>(settings);
#else
        // Built without AAC
        return nullptr;
#endif

    case TraversoDAW::WriterType::SNDFILE:
    default:
        return std::make_unique<SFAudioWriter>(settings);
    }
}
