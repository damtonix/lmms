/*
 * AudioBusHandle.cpp - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioBusHandle.h"
#include "Track.h"

#include <QMutexLocker>

#include <algorithm>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Engine.h"
#include "MixHelpers.h"
#include "PatternStore.h"
#include "Song.h"

#include <cmath>

namespace lmms
{

AudioBusHandle::AudioBusHandle(const QString& name, bool hasEffectChain,
	FloatModel* volumeModel, FloatModel* panningModel,
	BoolModel* mutedModel) :
	m_bufferUsage(false),
	m_buffer(Engine::audioEngine()->framesPerPeriod()),
	m_extOutputEnabled(false),
	m_nextMixerChannel(0),
	m_name(name),
	m_effects(hasEffectChain ? new EffectChain(nullptr) : nullptr),
	m_volumeModel(volumeModel),
	m_panningModel(panningModel),
	m_mutedModel(mutedModel)
{
	m_buffer.allocateInterleavedBuffer();

	Engine::audioEngine()->addAudioBusHandle(this);
	setExtOutputEnabled(true);
}




AudioBusHandle::~AudioBusHandle()
{
	setExtOutputEnabled(false);
	Engine::audioEngine()->removeAudioBusHandle(this);
}




void AudioBusHandle::setExtOutputEnabled(bool enabled)
{
	if (enabled != m_extOutputEnabled)
	{
		m_extOutputEnabled = enabled;
		if (m_extOutputEnabled)
		{
			Engine::audioEngine()->audioDev()->registerPort(this);
		}
		else
		{
			Engine::audioEngine()->audioDev()->unregisterPort(this);
		}
	}
}




void AudioBusHandle::setName(const QString& newName)
{
	m_name = newName;
	Engine::audioEngine()->audioDev()->renamePort(this);
}




bool AudioBusHandle::processEffects()
{
	if (m_effects)
	{
		bool more = m_effects->processAudioBuffer(m_buffer);
		return more;
	}
	return false;
}


void AudioBusHandle::doProcessing()
{
	if ((m_mutedModel && m_mutedModel->value()) || (m_track && (m_track->isMuted() || m_track->isFrozen())))
	{
		return;
	}

	CpuTimeProbe cpuProbe(m_track ? &m_track->cpuTime() : nullptr); // Custom

	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

	// clear the buffer
	m_buffer.silenceAllChannels();

	//qDebug( "Playhandles: %d", m_playHandles.size() );
	for (PlayHandle* ph : m_playHandles) // now we mix all playhandle buffers into our internal buffer
	{
		if (ph->buffer())
		{
			if (ph->usesBuffer()
				&& (ph->type() == PlayHandle::Type::NotePlayHandle
					|| !MixHelpers::isSilent(ph->buffer(), fpp)))
			{
				m_bufferUsage = true;

				// Writing to temporary interleaved buffer until PlayHandle and MixHelpers switch to planar
				MixHelpers::add(m_buffer.interleavedBuffer().asSampleFrames().data(), ph->buffer(), fpp);
			}
			ph->releaseBuffer(); 	// gets rid of playhandle's buffer and sets
									// pointer to null, so if it doesn't get re-acquired we know to skip it next time
		}
	}

	if (m_bufferUsage)
	{
		// PlayHandle buffers were written to the temporary interleaved buffer
		auto buffer = m_buffer.interleavedBuffer();

		// handle volume and panning
		// has both vol and pan models
		if (m_volumeModel && m_panningModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();
			ValueBuffer* panBuf = m_panningModel->valueBuffer();

			// both vol and pan have s.ex.data:
			if (volBuf && panBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					float p = panBuf->values()[f] * 0.01f;
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// only vol has s.ex.data:
			else if (volBuf)
			{
				float p = m_panningModel->value() * 0.01f;
				float l = (p <= 0 ? 1.0f : 1.0f - p);
				float r = (p >= 0 ? 1.0f : 1.0f + p);
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					buffer[f][0] *= v * l;
					buffer[f][1] *= v * r;
				}
			}

			// only pan has s.ex.data:
			else if (panBuf)
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float p = panBuf->values()[f] * 0.01f;
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// neither has s.ex.data:
			else
			{
				float p = m_panningModel->value() * 0.01f;
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}
		}

		// has vol model only
		else if (m_volumeModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();

			if (volBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					buffer[f][0] *= v;
					buffer[f][1] *= v;
				}
			}
			else
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					buffer[f][0] *= v;
					buffer[f][1] *= v;
				}
			}
		}

		// Copy from temporary interleaved buffer to the main planar buffer
		// so they stay in sync
		toPlanar(buffer, m_buffer.groupBuffers(0));

		const auto sanitized = Engine::audioEngine()->sanitizationEnabled() ? m_buffer.sanitizeAll() : false;
		m_corrupted.store(sanitized, std::memory_order_relaxed);

		// Update silence status of all channels for instrument output
		m_buffer.updateAllSilenceFlags();
	}
	// as of now there's no situation where we only have panning model but no volume model
	// if we have neither, we don't have to do anything here - just pass the audio as is

	// handle effects
	const bool anyOutputAfterEffects = processEffects();
	if (anyOutputAfterEffects || m_bufferUsage)
	{
		applyDucking(fpp);
		// TODO: improve the flow here - convert to pull model
		Engine::mixer()->mixToChannel(m_buffer, m_nextMixerChannel); // send output to mixer
		m_bufferUsage = false;
	}
}


void DuckCrossover::set(double freq, double sampleRate)
{
	// RBJ Butterworth sections (Q = 1/sqrt(2)), two of them in a row = Linkwitz-Riley 4th order
	const double w0 = 2.0 * 3.141592653589793 * std::clamp(freq, 10.0, sampleRate * 0.45) / sampleRate;
	const double cosw = std::cos(w0);
	const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
	const double a0 = 1.0 + alpha;
	const auto setup = [&](Biquad& q, bool lowpass) {
		const double k = lowpass ? 1.0 - cosw : 1.0 + cosw;
		q.b0 = k / 2.0 / a0;
		q.b1 = (lowpass ? k : -k) / a0;
		q.b2 = q.b0;
		q.a1 = -2.0 * cosw / a0;
		q.a2 = (1.0 - alpha) / a0;
	};
	setup(m_lp1, true);
	setup(m_lp2, true);
	setup(m_hp1, false);
	setup(m_hp2, false);
}


namespace
{

//! gain per frame of this period, false if the whole period stays at 1
bool computeDuckGains(const Track* track, const Track::DuckSettings& duck, f_cnt_t fpp, std::vector<float>& gains)
{
	const auto song = Engine::getSong();
	const double framesPerTick = Engine::framesPerTick();
	const double msPerFrame = 1000.0 / Engine::audioEngine()->outputSampleRate();
	const double msPerTick = msPerFrame * framesPerTick;
	const double startTick = song->periodStartTick();
	gains.assign(fpp, 1.f);
	bool any = false;

	const auto map = song->periodKickMap();
	if (duck.source == 0 && map != nullptr && map->hasSource)
	{
		// the kick never ducks itself (e.g. when it sits in a ducked group)
		if (track->isKickSource()) { return false; }
		const std::vector<Song::KickHit>* hits = nullptr;
		double wrap = 0.0;
		if (song->playMode() == Song::PlayMode::Song) { hits = &map->song; }
		else if (song->playMode() == Song::PlayMode::Pattern)
		{
			const int index = Engine::patternStore()->currentPattern();
			if (index >= 0 && index < static_cast<int>(map->patterns.size()))
			{
				hits = &map->patterns[index];
				wrap = map->patternLength[index];
			}
		}
		if (hits == nullptr || hits->empty()) { return false; }

		// the attack starts "lookahead" ms before the hit
		const double aheadTicks = duck.lookaheadMs / msPerTick;
		const auto lastHitBefore = [hits](double tick) {
			const auto it = std::upper_bound(hits->begin(), hits->end(), tick,
				[](double t, const Song::KickHit& hit) { return t < hit.tick; });
			return static_cast<long>(it - hits->begin()) - 1;
		};
		double previous = -1e300;
		long index = -1;
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			double tick = startTick + f / framesPerTick;
			if (wrap > 0.0) { tick = std::fmod(tick, wrap); }
			const double ahead = tick + aheadTicks;
			if (f == 0 || ahead < previous) { index = lastHitBefore(ahead); }
			else
			{
				while (index + 1 < static_cast<long>(hits->size()) && (*hits)[index + 1].tick <= ahead) { ++index; }
			}
			previous = ahead;
			if (index < 0) { continue; }
			const auto& hit = (*hits)[index];
			const double ms = (tick - hit.tick) * msPerTick + duck.lookaheadMs;
			gains[f] = Track::duckGain(duck, ms, duck.followVelocity ? hit.strength : 1.f);
			any = any || gains[f] < 1.f;
		}
		return any;
	}

	const double periodFrames = Track::duckRateTicks(duck.rate) * framesPerTick;
	const double startFrame = startTick * framesPerTick + duck.lookaheadMs / msPerFrame;
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		const double inPeriod = std::fmod(startFrame + f, periodFrames);
		gains[f] = Track::duckGain(duck, inPeriod * msPerFrame);
		any = any || gains[f] < 1.f;
	}
	return any;
}

} // namespace


void AudioBusHandle::applyDucking(f_cnt_t fpp)
{
	// Custom: "Kick-Ducking" after the track effects, on the real kick hits or on a fixed grid,
	// on the whole signal or only on a frequency range
	if (m_track == nullptr) { return; }
	const auto song = Engine::getSong();
	if (song == nullptr || !song->periodPlaying()) { return; }
	const auto duck = m_track->effectiveDuck();
	if (!duck.has_value() || duck->depth <= 0.f) { return; }

	const bool any = computeDuckGains(m_track, *duck, fpp, m_duckGains);
	auto left = m_buffer.buffer(0);
	auto right = m_buffer.buffer(1);
	const auto& g = m_duckGains;

	if (duck->band == 0)
	{
		if (!any) { return; }
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			left[f] *= g[f];
			right[f] *= g[f];
		}
		return;
	}

	// band ducking: the crossover always runs (also without ducking), so the sound does not change in between
	auto& st = m_duckBand;
	const float sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	const float lowHz = duck->lowHz;
	const float highHz = duck->band == 2 ? std::max(duck->highHz, duck->lowHz * 1.25f) : duck->highHz;
	if (st.band != duck->band || st.lowHz != lowHz || st.highHz != highHz || st.sampleRate != sampleRate)
	{
		const bool reset = st.band != duck->band || st.sampleRate != sampleRate;
		st.band = duck->band;
		st.lowHz = lowHz;
		st.highHz = highHz;
		st.sampleRate = sampleRate;
		for (int ch = 0; ch < 2; ++ch)
		{
			if (reset) { st.first[ch].reset(); st.second[ch].reset(); st.lowAllpass[ch].reset(); }
			st.first[ch].set(duck->band == 3 ? highHz : lowHz, sampleRate);
			st.second[ch].set(highHz, sampleRate);
			st.lowAllpass[ch].set(highHz, sampleRate);
		}
	}

	for (int ch = 0; ch < 2; ++ch)
	{
		auto buf = ch == 0 ? left : right;
		auto& first = st.first[ch];
		switch (duck->band)
		{
			case 1: // only the lows
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float low, high;
					first.split(buf[f], low, high);
					buf[f] = low * g[f] + high;
				}
				break;
			case 3: // only the highs
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float low, high;
					first.split(buf[f], low, high);
					buf[f] = low + high * g[f];
				}
				break;
			default: // a band in the middle
			{
				auto& second = st.second[ch];
				auto& lowAllpass = st.lowAllpass[ch];
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float low, rest, mid, high;
					first.split(buf[f], low, rest);
					second.split(rest, mid, high);
					buf[f] = lowAllpass.allpass(low) + mid * g[f] + high;
				}
				break;
			}
		}
	}
}


void AudioBusHandle::addPlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	m_playHandles.append(handle);
}


void AudioBusHandle::removePlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	if (auto it = std::ranges::find(m_playHandles, handle); it != m_playHandles.end())
	{
		m_playHandles.erase(it);
	}
}

} // namespace lmms
