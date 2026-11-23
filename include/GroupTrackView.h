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

#include <utility>
#include <vector>

#include "TrackView.h"

class QTimer;

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
	void chooseColor();
	void refreshLaneIfChanged();

private:
	QToolButton* m_collapseButton;
	QToolButton* m_colorButton;
	TrackLabelButton* m_label;
	QTimer* m_laneTimer;
	std::vector<std::pair<int, int>> m_lastRanges;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_GROUP_TRACK_VIEW_H
