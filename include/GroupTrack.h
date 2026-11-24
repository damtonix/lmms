/*
 * GroupTrack.h - folder-like track that groups other tracks (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GROUP_TRACK_H
#define LMMS_GROUP_TRACK_H

#include <vector>

#include "Track.h"

namespace lmms
{

namespace gui
{
class GroupTrackView;
}

//! A track without audio of its own. Other tracks reference it via Track::groupId().
//! Muting the group mutes all members, soloing it solos all members, collapsing it hides them.
class LMMS_EXPORT GroupTrack : public Track
{
	Q_OBJECT
public:
	GroupTrack(TrackContainer* tc);
	~GroupTrack() override;

	bool play(const TimePos& start, const f_cnt_t frames, const f_cnt_t frameBase, int clipNum = -1) override;

	QString nodeName() const override
	{
		return "grouptrack";
	}

	gui::TrackView* createView(gui::TrackContainerView*) override;
	//! Group tracks hold no clips; always returns nullptr
	Clip* createClip(const TimePos& pos) override;

	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings(const QDomElement& element) override;

	//! All tracks of the same container that belong to this group, in container order
	std::vector<Track*> members() const;

	//! Merged [start, end) tick ranges covered by the clips of all members
	std::vector<std::pair<tick_t, tick_t>> memberClipRanges() const;

};

} // namespace lmms

#endif // LMMS_GROUP_TRACK_H
