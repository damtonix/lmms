/*
 * GroupTrack.cpp - folder-like track that groups other tracks (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "GroupTrack.h"

#include <QDomElement>
#include <algorithm>
#include <QUuid>

#include "Clip.h"
#include "GroupTrackView.h"
#include "TrackContainer.h"

namespace lmms
{

GroupTrack::GroupTrack(TrackContainer* tc) :
	Track(Type::Group, tc)
{
	uid(); // groups always have an id
	setName(tr("Gruppe"));
}




GroupTrack::~GroupTrack() = default;




bool GroupTrack::play(const TimePos&, const f_cnt_t, const f_cnt_t, int)
{
	return false;
}




gui::TrackView* GroupTrack::createView(gui::TrackContainerView* tcv)
{
	return new gui::GroupTrackView(this, tcv);
}




Clip* GroupTrack::createClip(const TimePos&)
{
	return nullptr;
}




void GroupTrack::saveTrackSpecificSettings(QDomDocument&, QDomElement&, bool)
{
	// uid and collapsed state are saved by Track
}




void GroupTrack::loadTrackSpecificSettings(const QDomElement& element)
{
	// projects saved by the first group version stored these here
	if (element.hasAttribute("collapsed")) { setCollapsed(element.attribute("collapsed").toInt() != 0); }
	if (element.hasAttribute("uid") && !element.parentNode().toElement().hasAttribute("uid"))
	{
		const QString oldUid = element.attribute("uid");
		bool duplicate = false;
		for (const auto& track : trackContainer()->tracks())
		{
			if (track != this && track->type() == Type::Group && static_cast<GroupTrack*>(track)->uid() == oldUid)
			{
				duplicate = true;
			}
		}
		if (!duplicate) { setUidForLoading(oldUid); }
	}
}




std::vector<Track*> GroupTrack::members() const
{
	std::vector<Track*> result;
	for (const auto& track : trackContainer()->tracks())
	{
		if (track != this && track->groupId() == uid()) { result.push_back(track); }
	}
	return result;
}




std::vector<std::pair<tick_t, tick_t>> GroupTrack::memberClipRanges() const
{
	std::vector<std::pair<tick_t, tick_t>> ranges;
	for (const auto& member : members())
	{
		for (const auto& clip : member->getClips())
		{
			const tick_t start = clip->startPosition();
			const tick_t end = clip->endPosition();
			if (end > start) { ranges.emplace_back(start, end); }
		}
	}
	std::sort(ranges.begin(), ranges.end());

	std::vector<std::pair<tick_t, tick_t>> merged;
	for (const auto& range : ranges)
	{
		if (!merged.empty() && range.first <= merged.back().second)
		{
			merged.back().second = std::max(merged.back().second, range.second);
		}
		else
		{
			merged.push_back(range);
		}
	}
	return merged;
}

} // namespace lmms
