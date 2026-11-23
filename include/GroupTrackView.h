/*
 * GroupTrackView.h - view for GroupTrack (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_GROUP_TRACK_VIEW_H
#define LMMS_GUI_GROUP_TRACK_VIEW_H

#include "TrackView.h"

class QToolButton;

namespace lmms
{

class GroupTrack;

namespace gui
{

class TrackLabelButton;

class GroupTrackView : public TrackView
{
	Q_OBJECT
public:
	GroupTrackView(GroupTrack* gt, TrackContainerView* tcv);
	~GroupTrackView() override = default;

	GroupTrack* groupTrack();

private slots:
	void toggleCollapsed();
	void updateCollapsedState();
	void updateColor();
	void notifyMembers();

private:
	QToolButton* m_collapseButton;
	TrackLabelButton* m_label;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_GROUP_TRACK_VIEW_H
