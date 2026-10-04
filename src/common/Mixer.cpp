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

#include "Mixer.h"
#include "defines.h"
#include <cmath> // used for fabs
#if defined(Q_OS_MAC)
#include <Accelerate/Accelerate.h>
#endif

Mixer::compute_peak_t			Mixer::compute_peak 		= nullptr;
Mixer::apply_gain_to_buffer_t		Mixer::apply_gain_to_buffer 	= nullptr;
Mixer::mix_buffers_with_gain_t		Mixer::mix_buffers_with_gain 	= nullptr;
Mixer::mix_buffers_no_gain_t		Mixer::mix_buffers_no_gain 	= nullptr;

float default_compute_peak (const audio_sample_t* buf, nframes_t nsamples, float current)
{
        for (nframes_t i = 0; i < nsamples; ++i) {
                current = f_max (current, fabsf (buf[i]));
        }

        return current;
}

void default_apply_gain_to_buffer (audio_sample_t* buf, nframes_t nframes, float gain)
{
        for (nframes_t i=0; i<nframes; i++)
                buf[i] *= gain;
}

void default_mix_buffers_with_gain (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain)
{
        for (nframes_t i = 0; i < nframes; i++) {
                dst[i] += src[i] * gain;
        }
}

void default_mix_buffers_no_gain (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes)
{
        for (nframes_t i=0; i < nframes; i++) {
                dst[i] += src[i];
        }
}


#if defined (Q_OS_MAC)

float accel_compute_peak (const audio_sample_t* buf, nframes_t nsamples, float current)
{
	float tmpmax = 0.0f;
	vDSP_maxmgv(buf, 1, &tmpmax, nsamples);
	return f_max(current, tmpmax);
}

void accel_find_peaks (const audio_sample_t* buf, nframes_t nframes, float *min, float *max)
{
	vDSP_maxv (const_cast<audio_sample_t*>(buf), 1, max, nframes);
	vDSP_minv (const_cast<audio_sample_t*>(buf), 1, min, nframes);
}

void accel_apply_gain_to_buffer (audio_sample_t * buf, nframes_t nframes, float gain)
{
	vDSP_vsmul(buf, 1, &gain, buf, 1, nframes);
}

void accel_mix_buffers_with_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes, float gain)
{
	vDSP_vsma(src, 1, &gain, dst, 1, dst, 1, nframes);
}

void accel_mix_buffers_no_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes)
{
	vDSP_vadd(src, 1, dst, 1, dst, 1, nframes);
}

#endif

float Mixer::db_to_fader_position(float dB)
{
    if (dB <= min_fader_dB()) return 0.0f;
    if (dB >= max_fader_dB()) return 1.0f;

    // Secure 0.0 dB unity gain perfectly at 92% of the physical fader height
    const float zeroDbPos = 0.85f;
    float gain = dB_to_scale_factor(dB);

    if (dB < 0.0f) {
        // PROGRESSIVE COMPRESSION: Normalize linear gain between absolute silence and 0 dB (1.0f)
        float minGain = min_fader_gain();
        float normalizedGain = (gain - minGain) / (1.0f - minGain);

        // Applying a 0.47f warp over the gain domain expands the 0 to -10dB zone,
        // while compressing the -30 to -60dB zone into a small, tight pixel area underin.
        return zeroDbPos * ::powf(std::clamp(normalizedGain, 0.0f, 1.0f), 0.47f);
    } else {
        // Compress the positive boost range (+0 dB to +12 dB) tightly into the top 8%
        float maxGain = max_fader_gain();
        float normalizedGain = (gain - 1.0f) / (maxGain - 1.0f);

        return zeroDbPos + ((1.0f - zeroDbPos) * ::powf(std::clamp(normalizedGain, 0.0f, 1.0f), 0.85f));
    }
}

float Mixer::fader_position_to_gain(float position)
{
    if (position <= 0.0f) return 0.0f;
    if (position >= 1.0f) return max_fader_gain();

    const float zeroDbPos = 0.85f;

    if (position < zeroDbPos) {
        // Reverse the progressive negative gain domain warp
        float normalizedGain = ::powf(position / zeroDbPos, 1.0f / 0.47f);
        float minGain = min_fader_gain();
        return (normalizedGain * (1.0f - minGain)) + minGain;
    } else {
        // Reverse the highly compressed positive boost warp
        float normalizedGain = ::powf((position - zeroDbPos) / (1.0f - zeroDbPos), 1.0f / 0.85f);
        float maxGain = max_fader_gain();
        return (normalizedGain * (maxGain - 1.0f)) + 1.0f;
    }
}
