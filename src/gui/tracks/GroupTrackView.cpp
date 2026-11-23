/*
 * GroupTrackView.cpp - view for GroupTrack (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "GroupTrackView.h"

#include <QToolButton>

#include "embed.h"
#include "GroupTrack.h"
#include "TrackContainerView.h"
#include "TrackLabelButton.h"

namespace lmms::gui
{

GroupTrackView::GroupTrackView(GroupTrack* gt, TrackContainerView* tcv) :
	TrackView(gt, tcv)
{
	setFixedHeight(32);

	m_collapseButton = new QToolButton(getTrackSettingsWidget());
	m_collapseButton->setAutoRaise(true);
	m_collapseButton->setFocusPolicy(Qt::NoFocus);
	m_collapseButton->setFixedSize(24, 26);
	m_collapseButton->move(2, 2);
	m_collapseButton->setToolTip(tr("Gruppe ein-/ausklappen"));
	connect(m_collapseButton, &QToolButton::clicked, this, &GroupTrackView::toggleCollapsed);

	m_label = new TrackLabelButton(this, getTrackSettingsWidget());
	m_label->move(28, 1);
	m_label->setToolTip(tr("Klick: ein-/ausklappen, Doppelklick/Rechtsklick: umbenennen"));
	connect(m_label, &TrackLabelButton::clicked, this, &GroupTrackView::toggleCollapsed);
	m_label->show();

	connect(gt, &GroupTrack::collapsedChanged, this, &GroupTrackView::updateCollapsedState);
	connect(gt, &Track::colorChanged, this, &GroupTrackView::updateColor);
	connect(gt->getMutedModel(), &BoolModel::dataChanged, this, &GroupTrackView::notifyMembers);

	updateCollapsedState();
	updateColor();
	setModel(gt);
}




GroupTrack* GroupTrackView::groupTrack()
{
	return static_cast<GroupTrack*>(getTrack());
}




void GroupTrackView::toggleCollapsed()
{
	groupTrack()->setCollapsed(!groupTrack()->isCollapsed());
}




void GroupTrackView::updateCollapsedState()
{
	const bool collapsed = groupTrack()->isCollapsed();
	m_collapseButton->setIcon(embed::getIconPixmap(collapsed ? "folder" : "folder_opened"));
	m_label->setIcon(QIcon());
	m_collapseButton->setIconSize(QSize(18, 18));
	trackContainerView()->realignTracks();
}




void GroupTrackView::updateColor()
{
	auto settings = getTrackSettingsWidget();
	QPalette pal = settings->palette();
	const auto color = getTrack()->color();
	if (color.has_value())
	{
		pal.setColor(settings->backgroundRole(), color->darker(250));
	}
	else
	{
		pal = palette();
	}
	settings->setPalette(pal);
	notifyMembers();
}




void GroupTrackView::notifyMembers()
{
	for (auto tv : trackContainerView()->trackViews())
	{
		if (tv->getTrack()->group() == getTrack()) { tv->groupStateChanged(); }
	}
}

} // namespace lmms::gui
