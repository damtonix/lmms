/*
 * SongKickMap.cpp - real kick hits for Kick-Ducking (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "Song.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"

namespace lmms
{

namespace
{

float noteStrength(const Note& note)
{
	// a ghost kick at half volume ducks noticeably less, but not linearly
	return static_cast<float>(std::sqrt(std::clamp(note.getVolume() / static_cast<double>(DefaultVolume), 0.0, 1.0)));
}

//! hits of one MIDI clip in the song (same rules as InstrumentTrack::play, incl. the virtual loop)
void addMidiClipHits(const MidiClip* clip, std::vector<Song::KickHit>& out)
{
	if (clip->isMuted()) { return; }
	const tick_t offset = clip->startTimeOffset();
	const tick_t base = clip->startPosition() + offset;
	const tick_t visibleBegin = -offset;
	const tick_t visibleEnd = clip->length() - offset;
	const tick_t loopLen = clip->loopLength();
	for (const auto note : clip->notes())
	{
		const tick_t pos = note->pos();
		if (loopLen > 0 && pos >= loopLen) { continue; }
		for (tick_t local = pos; local < visibleEnd; local += loopLen)
		{
			if (local >= visibleBegin) { out.push_back({static_cast<double>(base + local), noteStrength(*note)}); }
			if (loopLen <= 0) { break; }
		}
	}
}

//! onsets of a sample in frames of the sample, cached by buffer
struct Onset
{
	double frame;
	float strength;
};

struct CachedOnsets
{
	std::shared_ptr<const SampleBuffer> keep; //!< the address can not be reused while cached
	std::vector<Onset> onsets;
	bool used = true;
};

std::map<const SampleBuffer*, CachedOnsets>& onsetCache()
{
	static std::map<const SampleBuffer*, CachedOnsets> cache;
	return cache;
}

const std::vector<Onset>& sampleOnsets(const Sample& sample)
{
	auto& cache = onsetCache();
	auto buffer = sample.shareBuffer();
	if (auto it = cache.find(buffer.get()); it != cache.end())
	{
		it->second.used = true;
		return it->second.onsets;
	}

	std::vector<Onset> onsets;
	const auto data = sample.data();
	const auto frames = sample.sampleSize();
	const int rate = std::max(1, sample.sampleRate());
	const auto win = static_cast<size_t>(std::max(1, rate / 1000)); // 1 ms windows
	std::vector<float> env;
	env.reserve(frames / win + 1);
	float peak = 0.f;
	for (size_t i = 0; i < frames; i += win)
	{
		float m = 0.f;
		for (size_t j = i; j < std::min(frames, i + win); ++j)
		{
			m = std::max(m, std::max(std::abs(data[j][0]), std::abs(data[j][1])));
		}
		env.push_back(m);
		peak = std::max(peak, m);
	}
	if (peak > 0.0005f)
	{
		// onset = the 1 ms peak jumps clearly above the decaying level of the last ~40 ms
		const float threshold = peak * 0.12f;
		float slow = 0.f;
		int refractory = 0;
		const float decay = std::exp(-1.f / 40.f);
		for (size_t w = 0; w < env.size(); ++w)
		{
			const float e = env[w];
			if (refractory > 0) { --refractory; }
			else if (e > threshold && e > slow * 2.2f + peak * 0.02f)
			{
				// strength = loudest peak in the next 15 ms
				float hit = e;
				for (size_t k = w; k < std::min(env.size(), w + 15); ++k) { hit = std::max(hit, env[k]); }
				onsets.push_back({static_cast<double>(w * win), std::sqrt(hit / peak)});
				refractory = 50; // a kick roll faster than 50 ms is not plausible
			}
			slow = std::max(e, slow * decay);
		}
	}
	const auto key = buffer.get();
	return cache.emplace(key, CachedOnsets{std::move(buffer), std::move(onsets)}).first->second.onsets;
}

void addSampleClipHits(SampleClip* clip, std::vector<Song::KickHit>& out)
{
	if (clip->isMuted()) { return; }
	auto& sample = clip->sample();
	if (sample.sampleSize() == 0 || sample.reversed()) { return; }
	const double framesPerTick = Engine::framesPerTick(sample.sampleRate());
	if (framesPerTick <= 0) { return; }
	const double begin = clip->startPosition();
	const double end = clip->endPosition();
	// sample frame f plays at song tick start + startTimeOffset + f / framesPerTick (see SampleTrack::play)
	const double base = begin + clip->startTimeOffset();
	for (const auto& onset : sampleOnsets(sample))
	{
		const double tick = base + onset.frame / framesPerTick;
		if (tick >= begin - 0.5 && tick < end) { out.push_back({tick, onset.strength}); }
	}
}

void sortAndMerge(std::vector<Song::KickHit>& hits)
{
	std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
	// two kick layers on the same position give one hit
	std::vector<Song::KickHit> merged;
	for (const auto& hit : hits)
	{
		if (!merged.empty() && hit.tick - merged.back().tick < 1.0)
		{
			merged.back().strength = std::max(merged.back().strength, hit.strength);
			continue;
		}
		merged.push_back(hit);
	}
	hits.swap(merged);
}

} // namespace


std::shared_ptr<const Song::KickMap> Song::kickMap() const
{
	std::lock_guard<std::mutex> lock(m_kickMapMutex);
	return m_kickMap;
}


void Song::updateKickMap()
{
	if (isLoadingProject()) { return; }
	auto map = std::make_shared<KickMap>();
	for (auto& [key, entry] : onsetCache()) { entry.used = false; }
	const auto patternStore = Engine::patternStore();

	// kick tracks in the song
	for (const auto track : tracks())
	{
		if (!track->isKickSource()) { continue; }
		map->hasSource = true;
		for (const auto clip : track->getClips())
		{
			if (auto midi = dynamic_cast<MidiClip*>(clip)) { addMidiClipHits(midi, map->song); }
			else if (auto sampleClip = dynamic_cast<SampleClip*>(clip)) { addSampleClipHits(sampleClip, map->song); }
		}
	}

	// kick tracks in the pattern editor
	if (patternStore != nullptr)
	{
		std::vector<Track*> patternKicks;
		for (const auto track : patternStore->tracks())
		{
			if (track->isKickSource() && track->type() == Track::Type::Instrument) { patternKicks.push_back(track); }
		}
		if (!patternKicks.empty())
		{
			map->hasSource = true;
			const int count = patternStore->numOfPatterns();
			map->patterns.resize(count);
			map->patternLength.resize(count);
			std::vector<std::vector<KickHit>> inPattern(count);
			for (int index = 0; index < count; ++index)
			{
				const double len = patternStore->lengthOfPattern(index) * static_cast<double>(TimePos::ticksPerBar());
				map->patternLength[index] = len;
				for (const auto track : patternKicks)
				{
					const auto clip = dynamic_cast<MidiClip*>(track->getClip(index));
					if (clip == nullptr || clip->isMuted()) { continue; }
					for (const auto note : clip->notes())
					{
						if (note->pos() < len) { inPattern[index].push_back({static_cast<double>(note->pos()), noteStrength(*note)}); }
					}
				}
				sortAndMerge(inPattern[index]);
				// repeated before and after, so the lookup works across the pattern loop
				for (int rep = -1; rep <= 1; ++rep)
				{
					for (const auto& hit : inPattern[index]) { map->patterns[index].push_back({hit.tick + rep * len, hit.strength}); }
				}
			}

			// pattern clips in the song: the pattern loops inside the clip (see PatternTrack::play)
			for (const auto track : tracks())
			{
				const auto patternTrack = dynamic_cast<PatternTrack*>(track);
				if (patternTrack == nullptr) { continue; }
				const int index = patternTrack->patternIndex();
				if (index < 0 || index >= count || inPattern[index].empty()) { continue; }
				const double len = map->patternLength[index];
				if (len <= 0) { continue; }
				for (const auto clip : track->getClips())
				{
					if (clip->isMuted()) { continue; }
					const double begin = clip->startPosition();
					const double end = clip->endPosition();
					double shift = len - std::fmod(static_cast<double>(clip->startTimeOffset()), len);
					if (shift >= len) { shift -= len; }
					// pattern position = song - begin + shift (mod len)
					for (double cycle = begin - shift; cycle < end; cycle += len)
					{
						for (const auto& hit : inPattern[index])
						{
							const double tick = cycle + hit.tick;
							if (tick >= begin && tick < end) { map->song.push_back({tick, hit.strength}); }
						}
					}
				}
			}
		}
	}
	sortAndMerge(map->song);
	std::erase_if(onsetCache(), [](const auto& entry) { return !entry.second.used; });

	std::lock_guard<std::mutex> lock(m_kickMapMutex);
	if (m_kickMap != nullptr && *m_kickMap == *map) { return; }
	m_kickMap = std::move(map);
}

} // namespace lmms
