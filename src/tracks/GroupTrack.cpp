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
#include <QUuid>

#include "GroupTrackView.h"
#include "TrackContainer.h"

namespace lmms
{

GroupTrack::GroupTrack(TrackContainer* tc) :
	Track(Type::Group, tc),
	m_uid(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
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




void GroupTrack::saveTrackSpecificSettings(QDomDocument&, QDomElement& element, bool)
{
	element.setAttribute("uid", m_uid);
	element.setAttribute("collapsed", m_collapsed ? 1 : 0);
}




void GroupTrack::loadTrackSpecificSettings(const QDomElement& element)
{
	const QString uid = element.attribute("uid");
	if (!uid.isEmpty()) { m_uid = uid; }

	// A cloned group must not share the id of the original
	for (const auto& track : trackContainer()->tracks())
	{
		if (track != this && track->type() == Type::Group && static_cast<GroupTrack*>(track)->uid() == m_uid)
		{
			regenerateUid();
			break;
		}
	}

	setCollapsed(element.attribute("collapsed", "0").toInt() != 0);
}




void GroupTrack::setCollapsed(bool collapsed)
{
	if (m_collapsed == collapsed) { return; }
	m_collapsed = collapsed;
	emit collapsedChanged();
}




std::vector<Track*> GroupTrack::members() const
{
	std::vector<Track*> result;
	for (const auto& track : trackContainer()->tracks())
	{
		if (track != this && track->groupId() == m_uid) { result.push_back(track); }
	}
	return result;
}




void GroupTrack::regenerateUid()
{
	m_uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace lmms
