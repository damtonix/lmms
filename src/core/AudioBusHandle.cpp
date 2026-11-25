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


void AudioBusHandle::applyDucking(f_cnt_t fpp)
{
	// Custom: "Kick-Ducking" after the track effects, on the real kick hits or on a fixed grid
	if (m_track == nullptr) { return; }
	const auto song = Engine::getSong();
	if (song == nullptr || !song->periodPlaying()) { return; }
	const auto duck = m_track->effectiveDuck();
	if (!duck.has_value() || duck->depth <= 0.f) { return; }

	const double framesPerTick = Engine::framesPerTick();
	const double msPerFrame = 1000.0 / Engine::audioEngine()->outputSampleRate();
	const double startTick = song->periodStartTick();
	auto left = m_buffer.buffer(0);
	auto right = m_buffer.buffer(1);

	const auto map = song->periodKickMap();
	if (duck->source == 0 && map != nullptr && map->hasSource)
	{
		// the kick never ducks itself (e.g. when it sits in a ducked group)
		if (m_track->isKickSource()) { return; }
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
		if (hits == nullptr || hits->empty()) { return; }

		const double msPerTick = msPerFrame * framesPerTick;
		// the volume is fully down exactly on the hit: the short attack starts a bit earlier
		const double attackTicks = Track::DuckAttackMs / msPerTick;
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
			const double ahead = tick + attackTicks;
			if (ahead < previous) { index = lastHitBefore(ahead); }
			else if (f == 0) { index = lastHitBefore(ahead); }
			else
			{
				while (index + 1 < static_cast<long>(hits->size()) && (*hits)[index + 1].tick <= ahead) { ++index; }
			}
			previous = ahead;
			if (index < 0) { continue; }
			const auto& hit = (*hits)[index];
			const double ms = (tick - hit.tick) * msPerTick + Track::DuckAttackMs;
			const float gain = Track::duckGain(*duck, ms, duck->followVelocity ? hit.strength : 1.f);
			left[f] *= gain;
			right[f] *= gain;
		}
		return;
	}

	const double periodFrames = Track::duckRateTicks(duck->rate) * framesPerTick;
	const double startFrame = startTick * framesPerTick;
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		const double inPeriod = std::fmod(startFrame + f, periodFrames);
		const float gain = Track::duckGain(*duck, inPeriod * msPerFrame);
		left[f] *= gain;
		right[f] *= gain;
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
