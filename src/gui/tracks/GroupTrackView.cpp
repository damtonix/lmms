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

#include <QTimer>
#include <QToolButton>

#include "TrackContentWidget.h"

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

	// fold arrow
	m_collapseButton = new QToolButton(getTrackSettingsWidget());
	m_collapseButton->setAutoRaise(true);
	m_collapseButton->setFocusPolicy(Qt::NoFocus);
	m_collapseButton->setFixedSize(20, 26);
	m_collapseButton->move(2, 2);
	m_collapseButton->setToolTip(tr("Gruppe ein-/ausklappen"));
	connect(m_collapseButton, &QToolButton::clicked, this, &GroupTrackView::toggleCollapsed);

	m_label = new TrackLabelButton(this, getTrackSettingsWidget());
	m_label->setIcon(embed::getIconPixmap("folder"));
	m_label->move(24, 1);
	m_label->setToolTip(tr("Klick: ein-/ausklappen, Doppelklick/Rechtsklick: umbenennen"));
	connect(m_label, &TrackLabelButton::clicked, this, &GroupTrackView::toggleCollapsed);
	m_label->show();

	// color swatch
	m_colorButton = new QToolButton(getTrackSettingsWidget());
	m_colorButton->setFocusPolicy(Qt::NoFocus);
	m_colorButton->setFixedSize(22, 22);
	m_colorButton->move(m_label->x() + m_label->width() + 4, 5);
	m_colorButton->setToolTip(tr("Gruppenfarbe w\xc3\xa4hlen"));
	connect(m_colorButton, &QToolButton::clicked, this, &GroupTrackView::chooseColor);

	// the row is already visible at this point, so children must be shown explicitly
	m_collapseButton->show();
	m_colorButton->show();

	connect(gt, &Track::collapsedChanged, this, &GroupTrackView::updateCollapsedState);
	connect(gt, &Track::colorChanged, this, &GroupTrackView::updateColor);
	connect(gt->getMutedModel(), &BoolModel::dataChanged, this, &GroupTrackView::notifyMembers);
	connect(gt->getMutedModel(), &BoolModel::dataChanged, getTrackContentWidget(), qOverload<>(&QWidget::update));

	// repaint the group lane when member clips change
	m_laneTimer = new QTimer(this);
	connect(m_laneTimer, &QTimer::timeout, this, &GroupTrackView::refreshLaneIfChanged);
	m_laneTimer->start(300);

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
	m_collapseButton->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
	m_label->setIcon(embed::getIconPixmap(collapsed ? "folder" : "folder_opened"));
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

	QPixmap swatch(14, 14);
	swatch.fill(color.value_or(palette().color(QPalette::Highlight)));
	m_colorButton->setIcon(QIcon(swatch));

	getTrackContentWidget()->update();
	notifyMembers();
}




void GroupTrackView::chooseColor()
{
	// reuse the regular "Track color > Change" dialog
	QMetaObject::invokeMethod(getTrackOperationsWidget(), "selectTrackColor");
}




void GroupTrackView::refreshLaneIfChanged()
{
	if (isHidden()) { return; }
	std::vector<std::pair<int, int>> ranges;
	for (const auto& [start, end] : groupTrack()->memberClipRanges()) { ranges.emplace_back(start, end); }
	if (ranges != m_lastRanges)
	{
		m_lastRanges = std::move(ranges);
		getTrackContentWidget()->update();
	}
}




void GroupTrackView::notifyMembers()
{
	for (auto tv : trackContainerView()->trackViews())
	{
		if (tv != this) { tv->groupStateChanged(); }
	}
}

} // namespace lmms::gui
