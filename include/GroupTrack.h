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

	const QString& uid() const { return m_uid; }

	bool isCollapsed() const { return m_collapsed; }
	void setCollapsed(bool collapsed);

	//! All tracks of the same container that belong to this group, in container order
	std::vector<Track*> members() const;

	//! Generate a fresh, unique id (used when a group is cloned)
	void regenerateUid();

signals:
	void collapsedChanged();

private:
	QString m_uid;
	bool m_collapsed = false;
};

} // namespace lmms

#endif // LMMS_GROUP_TRACK_H
