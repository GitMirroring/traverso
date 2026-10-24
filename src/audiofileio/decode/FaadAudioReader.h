/*
Copyright (C) 2026 Traverso Team

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

#include "TAudioDecoder.h"
#include "TFileIOBuffer.h"

class FaadAudioReader : public TAudioDecoder
{
public:
	explicit FaadAudioReader(const QString& filename);
	~FaadAudioReader() override;

    QString decoder_type() const final { return "faad"; }

	static bool can_decode(const QString& filename);

    bool seek(nframes_t frame) override;
    nframes_t read(TFileIOBuffer& fileIOBuffer, nframes_t count) override;

protected:
    // FAAD's decoding of frameCount frames into the channel buffers; the
    // frames it gave
    nframes_t decode(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);

	bool initDecoderInternal();

	class FaadDecoderPrivate;
	FaadDecoderPrivate* d;
};

