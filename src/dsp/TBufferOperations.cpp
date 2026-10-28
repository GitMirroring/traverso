/*
    Copyright (C) 2005-2006 Remon Sijrier 
 
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
 
    $Id: Mixer.cpp,v 1.3 2007/11/05 19:19:23 r_sijrier Exp $
*/

#include "TBufferOperations.h"
#include "defines.h"
#include <cmath> // used for fabs
#if defined(Q_OS_MACOS)
#include <Accelerate/Accelerate.h>
#endif

TBufferOperations::UpdatePeak  TBufferOperations::update_peak   = nullptr;
TBufferOperations::ApplyGain   TBufferOperations::apply_gain    = nullptr;
TBufferOperations::MixWithGain TBufferOperations::mix_with_gain = nullptr;
TBufferOperations::Mix         TBufferOperations::mix           = nullptr;

float portable_update_peak (const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
        for (nframes_t i = 0; i < nframes; ++i) {
                current = f_max (current, fabsf (buf[i]));
        }

        return current;
}

void portable_apply_gain (audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
        for (nframes_t i=0; i<nframes; i++)
                buf[i] *= gain;
}

void portable_mix_with_gain (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
        for (nframes_t i = 0; i < nframes; i++) {
                dst[i] += src[i] * gain;
        }
}

void portable_mix (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE
{
        for (nframes_t i=0; i < nframes; i++) {
                dst[i] += src[i];
        }
}


#if defined (Q_OS_MACOS)

float accel_compute_peak (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE
{
	float tmpmax = 0.0f;
	// Accelerate's vDSP: real-time safe by contract
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_maxmgv(buf, 1, &tmpmax, nsamples);)
	return f_max(current, tmpmax);
}

void accel_apply_gain_to_buffer (audio_sample_t * buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vsmul(buf, 1, &gain, buf, 1, nframes);)
}

void accel_mix_buffers_with_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vsma(src, 1, &gain, dst, 1, dst, 1, nframes);)
}

void accel_mix_buffers_no_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vadd(src, 1, dst, 1, dst, 1, nframes);)
}

#endif
