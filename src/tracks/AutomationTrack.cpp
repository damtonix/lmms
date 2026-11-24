/*
 * AutomationTrack.cpp - AutomationTrack handles automation of objects without
 *                       a track
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

#include "AutomationTrack.h"

#include "AutomationTrackView.h"
#include "AutomationClip.h"
#include "AutomatableModel.h"
#include "ProjectJournal.h"
#include "Engine.h"

namespace lmms
{


AutomationTrack::AutomationTrack( TrackContainer* tc, bool _hidden ) :
	Track( _hidden ? Type::HiddenAutomation : Type::Automation, tc )
{
	setName( tr( "Automation track" ) );
}

bool AutomationTrack::play( const TimePos & time_start, const f_cnt_t _frames,
							const f_cnt_t _frame_base, int _clip_num )
{
	return false;
}




gui::TrackView* AutomationTrack::createView( gui::TrackContainerView* tcv )
{
	return new gui::AutomationTrackView( this, tcv );
}




Clip* AutomationTrack::createClip(const TimePos & pos)
{
	auto p = new AutomationClip(this);
	p->movePosition(pos);
	return p;
}




void AutomationTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& _this, bool presetMode)
{
	if (const auto model = target()) { _this.setAttribute("target", ProjectJournal::idToSave(model->id())); }
}




AutomatableModel* AutomationTrack::target() const
{
	if (m_targetId != 0)
	{
		if (auto model = dynamic_cast<AutomatableModel*>(Engine::projectJournal()->journallingObject(m_targetId)))
		{
			return model;
		}
	}
	// older projects: use the control of the first clip
	for (const auto& clip : getClips())
	{
		auto autoClip = dynamic_cast<AutomationClip*>(clip);
		if (autoClip && !autoClip->objects().empty() && autoClip->objects().front()) { return autoClip->objects().front(); }
	}
	return nullptr;
}




void AutomationTrack::setTarget(AutomatableModel* model)
{
	m_targetId = model ? model->id() : 0;
	if (model != nullptr)
	{
		for (const auto& clip : getClips())
		{
			if (auto autoClip = dynamic_cast<AutomationClip*>(clip))
			{
				autoClip->clearObjects();
				autoClip->addObject(model, false);
			}
		}
	}
	emit targetChanged();
}




Track* AutomationTrack::owningTrack(const AutomatableModel* model)
{
	for (Model* m = model ? model->parentModel() : nullptr; m != nullptr; m = m->parentModel())
	{
		if (auto track = dynamic_cast<Track*>(m))
		{
			return track->type() == Type::Instrument ? track : nullptr;
		}
	}
	return nullptr;
}




void AutomationTrack::loadTrackSpecificSettings( const QDomElement & _this )
{
	m_targetId = static_cast<jo_id_t>(_this.attribute("target", "0").toUInt());
	// just in case something somehow wrent wrong...
	if( type() == Type::HiddenAutomation )
	{
		setMuted( false );
	}
}


} // namespace lmms
