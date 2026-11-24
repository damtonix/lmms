/*
 * AutomationTrackView.cpp
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2006-2008 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
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
 
#include "AutomationTrackView.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "TrackLabelButton.h"

namespace lmms::gui
{

AutomationTrackView::AutomationTrackView( AutomationTrack * _at, TrackContainerView* tcv ) :
	TrackView( _at, tcv )
{
        setFixedHeight( 32 );
		auto tlb = new TrackLabelButton(this, getTrackSettingsWidget());
		tlb->setIcon(embed::getIconPixmap("automation_track"));
		tlb->move(3, 1);
		tlb->show();
		m_label = tlb;
		connect(_at, &AutomationTrack::targetChanged, this, &AutomationTrackView::updateTargetToolTip);
		updateTargetToolTip();
		setModel(_at);
}

void AutomationTrackView::dragEnterEvent( QDragEnterEvent * _dee )
{
	StringPairDrag::processDragEnterEvent( _dee, "automatable_model" );
}




void AutomationTrackView::dropEvent( QDropEvent * _de )
{
	QString type = StringPairDrag::decodeKey( _de );
	QString val = StringPairDrag::decodeValue( _de );
	if( type == "automatable_model" )
	{
		auto mod = dynamic_cast<AutomatableModel*>(Engine::projectJournal()->journallingObject(val.toInt()));
		if( mod != nullptr )
		{
			// Custom: the dropped control becomes the (only) target of this automation track
			auto track = static_cast<AutomationTrack*>(getTrack());
			track->addJournalCheckPoint();
			const auto oldTarget = track->target();
			const bool autoNamed = track->name() == AutomationTrack::tr("Automation track")
				|| (oldTarget && (track->name() == oldTarget->displayName() || track->name() == oldTarget->fullDisplayName()));
			track->setTarget(mod);

			// name the track after the control (only the control, without the instrument name)
			if (autoNamed) { track->setName(mod->displayName()); }

			// attach to the instrument track owning the control
			if (auto owner = AutomationTrack::owningTrack(mod);
				owner != nullptr && owner->trackContainer() == track->trackContainer())
			{
				trackContainerView()->assignTrackToGroup(track, owner->uid());
			}
			Engine::getSong()->setModified();
			_de->accept();
		}
	}

	update();
}


void AutomationTrackView::updateTargetToolTip()
{
	const auto model = static_cast<AutomationTrack*>(getTrack())->target();
	m_label->setToolTip(model
		? tr("Gesteuert: %1").arg(model->fullDisplayName())
		: tr("Kein Regler festgelegt \xe2\x80\x93 ziehe einen Regler mit Strg auf diese Spur"));
}


} // namespace lmms::gui
