/*
 * TrackOperationsWidget.cpp - implementation of TrackOperationsWidget class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "TrackOperationsWidget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

#include "AutomatableButton.h"
#include "AutomationClip.h"
#include "AutomationTrackView.h"
#include "ColorChooser.h"
#include "ConfigManager.h"
#include "DuckDialog.h"
#include "GroupTrack.h"
#include "MainWindow.h"
#include "GuiApplication.h"
#include "Song.h"
#include "embed.h"
#include "Engine.h"
#include "InstrumentTrackView.h"
#include "lmms_math.h"
#include "KeyboardShortcuts.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackGrip.h"
#include "TrackView.h"

namespace lmms::gui
{

TrackOperationsWidget::TrackOperationsWidget(TrackView* parent)
	: QWidget(parent)
	, m_trackView(parent)
{
	setToolTip(tr("Press <%1> while clicking on move-grip "
				"to begin a new drag'n'drop action." ).arg(UI_CTRL_KEY) );

	auto toMenu = new QMenu(this);
	connect( toMenu, SIGNAL(aboutToShow()), this, SLOT(updateMenu()));


	setObjectName( "automationEnabled" );

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->setAlignment(Qt::AlignTop);

	m_trackGrip = new TrackGrip(m_trackView, this);
	layout->addWidget(m_trackGrip);

	// This widget holds the gear icon and the mute and solo
	// buttons in a layout.
	auto operationsWidget = new QWidget(this);
	auto operationsLayout = new QHBoxLayout(operationsWidget);
	operationsLayout->setContentsMargins(2, 6, 0, 6);
	operationsLayout->setSpacing(2);

	m_trackOps = new QPushButton(operationsWidget);
	m_trackOps->setFocusPolicy( Qt::NoFocus );
	m_trackOps->setMenu( toMenu );
	m_trackOps->setToolTip(tr("Actions"));
	m_trackOps->setCursor(Qt::PointingHandCursor);

	m_muteBtn = new AutomatableButton(operationsWidget, tr("Mute"));
	m_muteBtn->setCheckable(true);
	m_muteBtn->setToolTip(tr("Mute"));
	m_muteBtn->setObjectName("btn-mute");
	m_soloBtn = new AutomatableButton(operationsWidget, tr("Solo"));
	m_soloBtn->setCheckable(true);
	m_soloBtn->setToolTip(tr("Solo"));
	m_soloBtn->setObjectName("btn-solo");

	operationsLayout->addWidget(m_trackOps);
	operationsLayout->addWidget(m_muteBtn);
	operationsLayout->addWidget(m_soloBtn);

	layout->addWidget(operationsWidget, 0, Qt::AlignTop | Qt::AlignLeading);

	connect( this, SIGNAL(trackRemovalScheduled(lmms::gui::TrackView*)),
			m_trackView->trackContainerView(),
				SLOT(deleteTrackView(lmms::gui::TrackView*)),
							Qt::QueuedConnection );

	connect( m_trackView->getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(update()));

	connect(m_trackView->getTrack(), SIGNAL(colorChanged()), this, SLOT(update()));
}


void TrackOperationsWidget::paintEvent(QPaintEvent*)
{
	QPainter p( this );

	p.fillRect(rect(), palette().brush(QPalette::Window));

	// Custom: small duck curve when the track (or its group) has Kick-Ducking
	const auto track = m_trackView->getTrack();
	if (const auto duck = track->effectiveDuck(); duck.has_value())
	{
		const bool own = track->duckSettings().enabled;
		// pump wave in the free strip below the buttons
		const qreal top = height() - 6.5, bottom = height() - 2.0;
		const qreal left = 12, right = width() - 4;
		const int waves = 5;
		const qreal step = (right - left) / waves;
		QPainterPath path;
		for (int i = 0; i < waves; ++i)
		{
			const qreal x = left + i * step;
			path.moveTo(x, top);
			path.lineTo(x + 1, bottom);
			path.quadTo(x + step * 0.45, bottom, x + step, top);
		}
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(own ? QColor(143, 186, 230) : QColor(120, 140, 160, 150), 1.2));
		p.drawPath(path);
	}
	else if (track->isKickSource())
	{
		// kick source: a few short impulses
		const qreal bottom = height() - 2.0, left = 12;
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(QColor(230, 160, 90), 1.4));
		const qreal step = (width() - 4 - left) / 5;
		for (int i = 0; i < 5; ++i)
		{
			const qreal x = left + i * step + 1;
			p.drawLine(QPointF(x, bottom), QPointF(x, bottom - 5));
			p.drawLine(QPointF(x, bottom), QPointF(x + step * 0.6, bottom));
		}
	}
}


bool TrackOperationsWidget::confirmRemoval()
{
	bool needConfirm = ConfigManager::inst()->value("ui", "trackdeletionwarning", "1").toInt();
	if (!needConfirm){ return true; }
	
	QString messageRemoveTrack = tr("After removing a track, it can not "
					"be recovered. Are you sure you want to remove track \"%1\"?")
					.arg(m_trackView->getTrack()->name());
	QString messageTitleRemoveTrack = tr("Confirm removal");
	QString askAgainText = tr("Don't ask again");
	auto askAgainCheckBox = new QCheckBox(askAgainText, nullptr);
	auto onCheckedStateChanged = [](auto state){
		// Invert button state, if it's checked we *shouldn't* ask again
		ConfigManager::inst()->setValue("ui", "trackdeletionwarning", state != Qt::Unchecked ? "0" : "1");
	};

#if (QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))
	connect(askAgainCheckBox, &QCheckBox::checkStateChanged, onCheckedStateChanged);
#else
	connect(askAgainCheckBox, &QCheckBox::stateChanged, onCheckedStateChanged);
#endif

	QMessageBox mb;
	mb.setText(messageRemoveTrack);
	mb.setWindowTitle(messageTitleRemoveTrack);
	mb.setIcon(QMessageBox::Warning);
	mb.addButton(QMessageBox::Cancel);
	mb.addButton(QMessageBox::Ok);
	mb.setCheckBox(askAgainCheckBox);
	mb.setDefaultButton(QMessageBox::Cancel);

	return mb.exec() == QMessageBox::Ok;
}


void TrackOperationsWidget::cloneTrack()
{
	TrackContainerView *tcView = m_trackView->trackContainerView();

	Track *newTrack = m_trackView->getTrack()->clone();
	TrackView *newTrackView = tcView->createTrackView( newTrack );

	int index = tcView->trackViews().indexOf( m_trackView );
	int i = tcView->trackViews().size();
	while ( i != index + 1 )
	{
		tcView->moveTrackView( newTrackView, i - 1 );
		i--;
	}

	if (m_soloBtn->model()->value())
	{
		// if this track was solo, make the new track the new solo
		newTrack->toggleSolo();
	}
}


void TrackOperationsWidget::clearTrack()
{
	Track * t = m_trackView->getTrack();
	t->addJournalCheckPoint();
	t->lock();
	t->deleteClips();
	t->unlock();
}


void TrackOperationsWidget::removeTrack()
{
	if (confirmRemoval())
	{
		emit trackRemovalScheduled(m_trackView);
	}
}

void TrackOperationsWidget::selectTrackColor()
{
	const auto newColor = ColorChooser{this}
		.withPalette(ColorChooser::Palette::Track)
		->getColor(m_trackView->getTrack()->color().value_or(Qt::white));

	if (!newColor.isValid()) { return; }

	const auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(newColor);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::resetTrackColor()
{
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(std::nullopt);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::randomizeTrackColor()
{
	QColor buffer = ColorChooser::getPalette(ColorChooser::Palette::Track)[fastRand(48)];
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(buffer);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::resetClipColors()
{
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	for (auto clip : track->getClips())
	{
		clip->setColor(std::nullopt);
	}
	Engine::getSong()->setModified();
}


void TrackOperationsWidget::updateMenu()
{
	QMenu * toMenu = m_trackOps->menu();
	toMenu->clear();
	toMenu->addAction( embed::getIconPixmap( "edit_copy", 16, 16 ),
						tr( "Clone this track" ),
						this, SLOT(cloneTrack()));
	toMenu->addAction( embed::getIconPixmap( "cancel", 16, 16 ),
						tr( "Remove this track" ),
						this, SLOT(removeTrack()));

	if( ! m_trackView->trackContainerView()->fixedClips() )
	{
		toMenu->addAction( tr( "Clear this track" ), this, SLOT(clearTrack()));
	}
	if (QMenu *mixerMenu = m_trackView->createMixerMenu(tr("Channel %1: %2"), tr("Assign to new Mixer Channel")))
	{
		toMenu->addMenu(mixerMenu);
	}

	if (auto trackView = dynamic_cast<InstrumentTrackView*>(m_trackView))
	{
		toMenu->addSeparator();
		toMenu->addMenu(trackView->midiMenu());
	}
	if( dynamic_cast<AutomationTrackView *>( m_trackView ) )
	{
		toMenu->addAction( tr( "Turn all recording on" ), this, SLOT(recordingOn()));
		toMenu->addAction( tr( "Turn all recording off" ), this, SLOT(recordingOff()));
	}

	addGroupMenu(toMenu);

	toMenu->addSeparator();

	QMenu* colorMenu = toMenu->addMenu(tr("Track color"));
	colorMenu->setIcon(embed::getIconPixmap("colorize"));
	colorMenu->addAction(tr("Change"), this, SLOT(selectTrackColor()));
	colorMenu->addAction(tr("Reset"), this, SLOT(resetTrackColor()));
	colorMenu->addAction(tr("Pick random"), this, SLOT(randomizeTrackColor()));
	colorMenu->addSeparator();
	colorMenu->addAction(tr("Reset clip colors"), this, SLOT(resetClipColors()));
}


void TrackOperationsWidget::addGroupMenu(QMenu* toMenu)
{
	auto tcv = m_trackView->trackContainerView();
	auto track = m_trackView->getTrack();
	const bool inSong = tcv->model() == Engine::getSong();

	// Custom: freeze / bounce for instrument tracks
	if (inSong && track->type() == Track::Type::Instrument)
	{
		toMenu->addSeparator();
		toMenu->addAction(embed::getIconPixmap(track->isFrozen() ? "freeze_off" : "freeze", 16, 16),
			track->isFrozen() ? tr("Spur auftauen") : tr("Spur einfrieren (als Audio, Instrument ruht)"),
			this, [track] {
				if (track->isFrozen()) { getGUI()->mainWindow()->unfreezeTrack(track); }
				else { getGUI()->mainWindow()->freezeTrack(track); }
			});
	}
	if (inSong && (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample))
	{
		toMenu->addAction(embed::getIconPixmap("bounce", 16, 16), tr("Bounce als Sample-Spur..."),
			this, [track] { getGUI()->mainWindow()->bounceTrack(track); });
	}

	// Custom: this track triggers Kick-Ducking
	if (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample)
	{
		auto kick = toMenu->addAction(tr("Als Kick-Quelle f\xc3\xbcr Ducking"), this, [track] {
			track->setKickSource(!track->isKickSource());
			Engine::getSong()->setModified();
			Engine::getSong()->updateKickMap();
		});
		kick->setCheckable(true);
		kick->setChecked(track->isKickSource());
	}

	if (!inSong) { return; }

	// Custom: beat-synced ducking for tracks with audio and for groups
	if (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample
		|| track->type() == Track::Type::Group)
	{
		toMenu->addSeparator();
		const bool on = track->duckSettings().enabled;
		auto action = toMenu->addAction(on ? tr("Kick-Ducking... (aktiv)") : tr("Kick-Ducking..."),
			this, [this, track] { DuckDialog::showFor(track, window()); });
		action->setCheckable(true);
		action->setChecked(on);
	}
	toMenu->addSeparator();

	// Custom: automation tracks belonging to an instrument track
	if (track->type() == Track::Type::Instrument)
	{
		toMenu->addAction(embed::getIconPixmap("automation_track", 16, 16), tr("Neue Automationsspur f\xc3\xbcr diese Spur"),
			this, [tcv, track] {
				auto automationTrack = Track::create(Track::Type::Automation, Engine::getSong());
				tcv->createTrackView(automationTrack);
				track->setCollapsed(false);
				tcv->assignTrackToGroup(automationTrack, track->uid());
			});
	}
	if (track->type() == Track::Type::Automation)
	{
		QMenu* ownerMenu = toMenu->addMenu(embed::getIconPixmap("instrument_track", 16, 16), tr("Instrument-Spur"));
		bool any = false;
		for (const auto& other : Engine::getSong()->tracks())
		{
			if (other->type() != Track::Type::Instrument) { continue; }
			any = true;
			auto action = ownerMenu->addAction(tr("Zu \"%1\" geh\xc3\xb6ren").arg(other->name()),
				this, [tcv, track, other] { tcv->assignTrackToGroup(track, other->uid()); });
			action->setCheckable(true);
			action->setChecked(track->parentTrack() == other);
		}
		if (!any) { ownerMenu->addAction(tr("(keine Instrument-Spuren)"))->setEnabled(false); }
		if (track->parentTrack() != nullptr && track->parentTrack()->type() == Track::Type::Instrument)
		{
			ownerMenu->addSeparator();
			ownerMenu->addAction(tr("Zuordnung l\xc3\xb6sen"), this, [tcv, track] { tcv->assignTrackToGroup(track, QString()); });
		}
	}

	// Custom: track groups
	if (track->type() == Track::Type::Group)
	{
		auto group = static_cast<GroupTrack*>(track);
		toMenu->addAction(embed::getIconPixmap(group->isCollapsed() ? "folder_opened" : "folder", 16, 16),
			group->isCollapsed() ? tr("Gruppe ausklappen") : tr("Gruppe einklappen"),
			this, [group] { group->setCollapsed(!group->isCollapsed()); });
		toMenu->addAction(tr("Gruppe aufl\xc3\xb6sen (Spuren behalten)"), this, [this, group] {
			for (auto member : group->members()) { member->setGroupId(QString()); }
			emit trackRemovalScheduled(m_trackView);
		});
		return;
	}

	QMenu* groupMenu = toMenu->addMenu(embed::getIconPixmap("folder", 16, 16), tr("Gruppe"));
	groupMenu->addAction(embed::getIconPixmap("add_folder", 16, 16), tr("Neue Gruppe mit dieser Spur"),
		this, [tcv, this] { tcv->createGroupForTrack(m_trackView); });

	bool hasGroups = false;
	for (const auto& other : Engine::getSong()->tracks())
	{
		if (other->type() != Track::Type::Group) { continue; }
		if (!hasGroups) { groupMenu->addSeparator(); hasGroups = true; }
		const QString uid = other->uid();
		auto action = groupMenu->addAction(tr("Zu \"%1\" hinzuf\xc3\xbcgen").arg(other->name()),
			this, [tcv, track, uid] { tcv->assignTrackToGroup(track, uid); });
		action->setCheckable(true);
		action->setChecked(track->groupId() == uid);
	}

	if (track->group() != nullptr)
	{
		groupMenu->addSeparator();
		groupMenu->addAction(tr("Aus Gruppe entfernen"), this, [tcv, track] { tcv->assignTrackToGroup(track, QString()); });
	}
}




void TrackOperationsWidget::toggleRecording( bool on )
{
	auto atv = dynamic_cast<AutomationTrackView*>(m_trackView);
	if( atv )
	{
		for( Clip * clip : atv->getTrack()->getClips() )
		{
			auto ap = dynamic_cast<AutomationClip*>(clip);
			if( ap ) { ap->setRecording( on ); }
		}
		atv->update();
	}
}


void TrackOperationsWidget::recordingOn()
{
	toggleRecording( true );
}


void TrackOperationsWidget::recordingOff()
{
	toggleRecording( false );
}


} // namespace lmms::gui
