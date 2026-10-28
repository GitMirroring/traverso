/*
Copyright (C) 2007 Ben Levitt 

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

#include "TAudioEncoder.h"

#include "TTypes.h"
#include "wavpack/wavpack.h"

#include "TAudioEncodeSettings.h"

class WPAudioWriter : public TAudioEncoder
{
public:
    explicit WPAudioWriter(const TAudioEncodeSettings& settings);
    ~WPAudioWriter() override;

    TAudioFileStatus open(const QString& fileName) override;
    TAudioFileResult write(void* buffer, nframes_t frameCount) override;
    TAudioFileStatus close() override;

protected:
	int write_to_file(void *lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t *lpNumberOfBytesWritten);
	bool rewrite_first_block();
	
	static int write_block(void *id, void *data, int32_t length);
	
	WavpackConfig	m_config;
	WavpackContext*	m_wp{nullptr};
	FILE*		m_file{nullptr};
    char*		m_firstBlock{nullptr};
	int32_t 	m_firstBlockSize{0};
    // Why write_block() could not write a block (WavPack calls it while it
    // packs); the WavPack context stays, close() releases it
    QString m_writeError;
};

