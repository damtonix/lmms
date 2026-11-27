/*
 * AudioBusHandle.h - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2025 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#ifndef LMMS_AUDIO_BUS_HANDLE_H
#define LMMS_AUDIO_BUS_HANDLE_H

#include <array>
#include <memory>
#include <vector>
#include <QString>
#include <QMutex>

#include "AudioBuffer.h"
#include "PlayHandle.h"

namespace lmms
{

class EffectChain;
class Track;
class FloatModel;
class BoolModel;

/**
	@brief Job between @ref PlayHandle and @ref MixerChannel

	A @ref ThreadableJob class located at the exit point of each @ref PlayHandle into a @ref MixerChannel
	(or into an audio device, in case of AudioJack, but this is not supported in AudioBusHandle yet).
	It contains an optional @ref EffectChain which is e.g. visualized in the
	@ref gui::InstrumentTrackWindow or @ref gui::SampleTrackWindow.
	For processing, it adds all input play handles into an internal buffer,
	processes the @ref EffectChain (if existing) on that buffer
	and finally merges the buffer into its @ref MixerChannel.
*/
//! Custom: Linkwitz-Riley crossover (24 dB/oct) for band ducking, low + high sums to an allpass
class DuckCrossover
{
public:
	void set(double freq, double sampleRate);
	void split(float in, float& low, float& high)
	{
		low = m_lp2.process(m_lp1.process(in));
		high = m_hp2.process(m_hp1.process(in));
	}
	//! same phase shift as split() but without splitting (keeps the bands in phase)
	float allpass(float in)
	{
		float low, high;
		split(in, low, high);
		return low + high;
	}
	void reset() { m_lp1.reset(); m_lp2.reset(); m_hp1.reset(); m_hp2.reset(); }

private:
	struct Biquad
	{
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		double z1 = 0, z2 = 0;
		float process(float x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return static_cast<float>(y);
		}
		void reset() { z1 = z2 = 0; }
	};
	Biquad m_lp1, m_lp2, m_hp1, m_hp2;
};


class AudioBusHandle : public ThreadableJob
{
public:
	//! Custom: use the track's effective mute state (includes group mute)
	void setTrack(const Track* track) { m_track = track; }
	const Track* track() const { return m_track; }
	AudioBusHandle(const QString& name, bool hasEffectChain = true,
		FloatModel* volumeModel = nullptr, FloatModel* panningModel = nullptr,
		BoolModel* mutedModel = nullptr);
	virtual ~AudioBusHandle();

	// indicate whether JACK & Co should provide output-buffer at ext. port
	bool extOutputEnabled() const { return m_extOutputEnabled; }
	void setExtOutputEnabled(bool enabled);

	// next mixer-channel after this audio-bus-handle
	// (-1 = none  0 = master)
	mix_ch_t nextMixerChannel() const { return m_nextMixerChannel; }
	void setNextMixerChannel(const mix_ch_t chnl) { m_nextMixerChannel = chnl; }

	const QString& name() const { return m_name; }
	void setName(const QString& newName);

	EffectChain* effects() { return m_effects.get(); }
	bool processEffects();

	// ThreadableJob stuff
	void doProcessing() override;
	bool requiresProcessing() const override { return true; }

	void addPlayHandle(PlayHandle* handle);
	void removePlayHandle(PlayHandle* handle);

	//! @returns true if the processing outputted corrupted audio (infs/nans).
	bool isCorrupted() const { return m_corrupted.load(std::memory_order_relaxed); }

private:
	void applyDucking(f_cnt_t fpp);
	std::vector<float> m_duckGains;
	struct DuckBandState
	{
		int band = -1;
		float lowHz = 0, highHz = 0, sampleRate = 0;
		std::array<DuckCrossover, 2> first, second, lowAllpass; //!< per channel
	} m_duckBand;

	volatile bool m_bufferUsage;

	AudioBuffer m_buffer;

	bool m_extOutputEnabled;
	mix_ch_t m_nextMixerChannel;

	QString m_name;

	std::unique_ptr<EffectChain> m_effects;

	PlayHandleList m_playHandles;
	QMutex m_playHandleLock;

	FloatModel* m_volumeModel;
	FloatModel* m_panningModel;
	BoolModel* m_mutedModel;
	const Track* m_track = nullptr; //!< Custom: owning track, for group mute
	
	std::atomic<bool> m_corrupted = false;

	friend class AudioEngine;
	friend class AudioEngineWorkerThread;
};

} // namespace lmms

#endif // LMMS_AUDIO_BUS_HANDLE_H
