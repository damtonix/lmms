/*
 * SamplePlayHandle.cpp - implementation of class SamplePlayHandle
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SamplePlayHandle.h"
#include "AudioEngine.h"
#include "AudioBusHandle.h"
#include "Engine.h"
#include "PatternTrack.h"
#include <algorithm>

#include "SampleClip.h"
#include "SampleTrack.h"

namespace lmms
{

SamplePlayHandle::SamplePlayHandle(Sample* sample, bool ownAudioBusHandle)
	: PlayHandle(Type::SamplePlayHandle)
	, m_sample(sample)
	, m_ownAudioBusHandle(ownAudioBusHandle)
{
	if (ownAudioBusHandle)
	{
		setAudioBusHandle(new AudioBusHandle("SamplePlayHandle", false));
	}
}




SamplePlayHandle::SamplePlayHandle( const QString& sampleFile ) :
	SamplePlayHandle(new Sample(SampleBuffer::fromFile(sampleFile)), true)
{
}




SamplePlayHandle::SamplePlayHandle( SampleClip* clip ) :
	SamplePlayHandle(&clip->sample(), false)
{
	m_track = clip->getTrack();
	setAudioBusHandle(((SampleTrack *)clip->getTrack())->audioBusHandle());

	// Custom: fades (in output frames, from the clip start; the sample starts startTimeOffset after it)
	if (clip->fadeIn() > 0 || clip->fadeOut() > 0)
	{
		const double framesPerTick = Engine::framesPerTick();
		const double rateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate())
			/ std::max<double>(1, m_sample->sampleRate());
		const double length = static_cast<double>(clip->length()) * framesPerTick;
		const double fadeIn = std::min<double>(clip->fadeIn() * framesPerTick, length);
		m_hasFades = true;
		m_clipLength = length;
		m_fadeIn = fadeIn;
		m_fadeOut = std::min<double>(clip->fadeOut() * framesPerTick, length - fadeIn);
		m_clipBasePos = m_sample->startFrame() * rateRatio + static_cast<double>(clip->startTimeOffset()) * framesPerTick;
	}
}




SamplePlayHandle::~SamplePlayHandle()
{
	if(m_ownAudioBusHandle)
	{
		delete audioBusHandle();
		delete m_sample;
	}
}




void SamplePlayHandle::play( SampleFrame* buffer )
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	//play( 0, _try_parallelizing );
	if( framesDone() >= totalFrames() )
	{
		zeroSampleFrames(buffer, fpp);
		return;
	}

	SampleFrame* workingBuffer = buffer;
	f_cnt_t frames = fpp;

	// apply offset for the first period
	if( framesDone() == 0 )
	{
		zeroSampleFrames(buffer, offset());
		workingBuffer += offset();
		frames -= offset();
	}

	if( !( m_track && m_track->isMuted() )
				&& !(m_patternTrack && m_patternTrack->isMuted()))
	{
/*		StereoVolumeVector v =
			{ { m_volumeModel->value() / DefaultVolume,
				m_volumeModel->value() / DefaultVolume } };*/
		// SamplePlayHandle always plays the sample at its original pitch;
		// it is used only for previews, SampleTracks and the metronome.
		if (!m_sample->play(workingBuffer, &m_state, frames))
		{
			zeroSampleFrames(workingBuffer, frames);
		}
		else if (m_hasFades)
		{
			const double first = m_clipBasePos + m_frame;
			const double last = first + frames;
			if (first < m_fadeIn || last > m_clipLength - m_fadeOut)
			{
				for (f_cnt_t i = 0; i < frames; ++i)
				{
					const float gain = SampleClip::fadeGain(first + i, m_clipLength, m_fadeIn, m_fadeOut);
					workingBuffer[i][0] *= gain;
					workingBuffer[i][1] *= gain;
				}
			}
		}
	}

	m_frame += frames;
}




bool SamplePlayHandle::isFinished() const
{
	return framesDone() >= totalFrames() && m_doneMayReturnTrue == true;
}




bool SamplePlayHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track || m_patternTrack == _track;
}




f_cnt_t SamplePlayHandle::totalFrames() const
{
	return (m_sample->endFrame() - m_sample->startFrame()) *
			(static_cast<float>(Engine::audioEngine()->outputSampleRate()) / m_sample->sampleRate());
}


} // namespace lmms
