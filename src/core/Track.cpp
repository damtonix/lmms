/*
 * Track.cpp - implementation of Track class
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

/** \file Track.cpp
 *  \brief Implementation of Track class
 */


#include "Track.h"

#include <QDomElement>
#include <QUuid>
#include <QVariant>

#include <algorithm>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GroupTrack.h"
#include "InstrumentTrack.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleTrack.h"
#include "Song.h"


namespace lmms
{

Track::Track(Type type, TrackContainer* tc)
	: Model(tc)
	, m_trackContainer(tc)
	, m_type(type)
	, m_name()
	, m_mutedModel(false, this, tr("Mute"))
	, m_soloModel(false, this, tr("Solo"))
	, m_clips()
{	
	m_trackContainer->addTrack( this );
	m_height = -1;

	// Custom: children (group members, automation tracks of instruments) follow the mute button
	connect(&m_mutedModel, &BoolModel::dataChanged, this, &Track::propagateMuteToChildren, Qt::DirectConnection);
}




bool Track::s_inSoloChange = false;

void Track::propagateMuteToChildren()
{
	if (s_inSoloChange) { return; }
	if (Engine::getSong() && Engine::getSong()->isLoadingProject()) { return; }
	const bool muted = m_mutedModel.value();
	for (auto child : childTracks())
	{
		if (child == this) { continue; }
		if (muted)
		{
			child->m_mutedBeforeParent = child->m_mutedModel.value();
			child->setMuted(true);
		}
		else
		{
			child->setMuted(child->m_mutedBeforeParent);
		}
	}
}




Track::~Track()
{
	lock();
	emit destroyedTrack();

	while (!m_clips.empty())
	{
		delete m_clips.back();
	}

	m_trackContainer->removeTrack( this );
	unlock();
}




Track * Track::create( Type tt, TrackContainer * tc )
{
	Engine::audioEngine()->requestChangeInModel();

	Track * t = nullptr;

	switch( tt )
	{
		case Type::Instrument: t = new class InstrumentTrack( tc ); break;
		case Type::Pattern: t = new class PatternTrack( tc ); break;
		case Type::Sample: t = new class SampleTrack( tc ); break;
//		case Type::Event:
//		case Type::Video:
		case Type::Automation: t = new class AutomationTrack( tc ); break;
		case Type::HiddenAutomation:
						t = new class AutomationTrack( tc, true ); break;
		case Type::Group: t = new class GroupTrack( tc ); break;
		default: break;
	}

	if (tc == Engine::patternStore() && t)
	{
		t->createClipsForPattern(Engine::patternStore()->numOfPatterns() - 1);
	}

	tc->updateAfterTrackAdd();

	Engine::audioEngine()->doneChangeInModel();

	return t;
}




Track * Track::create( const QDomElement & element, TrackContainer * tc )
{
	Engine::audioEngine()->requestChangeInModel();

	Track * t = create(
		static_cast<Type>( element.attribute( "type" ).toInt() ),
									tc );
	if( t != nullptr )
	{
		t->restoreState( element );
	}

	Engine::audioEngine()->doneChangeInModel();

	return t;
}




Track* Track::clone()
{
	// Save track to temporary XML and load it to create a new identical track
	QDomDocument doc;
	QDomElement parent = doc.createElement("clonedtrack");
	cloneState(doc, parent);
	Track* t = create(parent.firstChild().toElement(), m_trackContainer);

	AutomationClip::resolveAllIDs();
	return t;
}


void Track::saveTrack(QDomDocument& doc, QDomElement& element, bool presetMode)
{
	if (!presetMode)
	{
		element.setTagName( "track" );
	}
	element.setAttribute( "type", static_cast<int>(type()) );
	element.setAttribute( "name", name() );
	m_mutedModel.saveSettings( doc, element, "muted" );
	m_soloModel.saveSettings( doc, element, "solo" );
	// Save the mutedBeforeSolo value so we can recover the muted state if any solo was active (issue 5562)
	element.setAttribute( "mutedBeforeSolo", int(m_mutedBeforeSolo) );

	if( m_height >= MINIMAL_TRACK_HEIGHT )
	{
		element.setAttribute( "trackheight", m_height );
	}
	
	if (m_color.has_value())
	{
		element.setAttribute("color", m_color->name());
	}

	if (!presetMode)
	{
		if (!m_groupId.isEmpty() && parentTrack() != nullptr) { element.setAttribute("group", m_groupId); }
		if (!m_uid.isEmpty()) { element.setAttribute("uid", m_uid); }
		if (m_collapsed) { element.setAttribute("collapsed", 1); }
		if (m_frozen) { element.setAttribute("frozen", 1); }
		if (m_mutedBeforeParent) { element.setAttribute("mutedbeforeparent", 1); }
		if (!m_freezeProxyOf.isEmpty()) { element.setAttribute("freezeproxyof", m_freezeProxyOf); }
		if (!(m_duck == DuckSettings{}))
		{
			element.setAttribute("duck", m_duck.enabled ? 1 : 0);
			element.setAttribute("duckdepth", m_duck.depth);
			element.setAttribute("duckhold", m_duck.holdMs);
			element.setAttribute("duckrelease", m_duck.releaseMs);
			element.setAttribute("duckrate", m_duck.rate);
			element.setAttribute("duckshape", m_duck.shape);
			element.setAttribute("ducksource", m_duck.source);
			element.setAttribute("duckvelocity", m_duck.followVelocity ? 1 : 0);
			element.setAttribute("duckattack", m_duck.attackMs);
			element.setAttribute("ducklookahead", m_duck.lookaheadMs);
			element.setAttribute("duckband", m_duck.band);
			element.setAttribute("ducklow", m_duck.lowHz);
			element.setAttribute("duckhigh", m_duck.highHz);
		}
		if (m_kickSource) { element.setAttribute("kicksource", 1); }
	}
	
	QDomElement tsDe = doc.createElement( nodeName() );
	// let actual track (InstrumentTrack, PatternTrack, SampleTrack etc.) save its settings
	element.appendChild( tsDe );
	saveTrackSpecificSettings(doc, tsDe, presetMode);

	if (presetMode)
	{
		return;
	}

	// now save settings of all Clip's
	for (const auto& clip : m_clips)
	{
		clip->saveState(doc, element);
	}
}


void Track::loadTrack(const QDomElement& element, bool presetMode)
{
	if( static_cast<Type>(element.attribute( "type" ).toInt()) != type() )
	{
		qWarning( "Current track-type does not match track-type of "
							"settings-node!\n" );
	}

	setName( element.hasAttribute( "name" ) ? element.attribute( "name" ) :
			element.firstChild().toElement().attribute( "name" ) );

	m_mutedModel.loadSettings( element, "muted" );
	m_soloModel.loadSettings( element, "solo" );
	// Get the mutedBeforeSolo value so we can recover the muted state if any solo was active.
	// Older project files that didn't have this attribute will set the value to false (issue 5562)
	m_mutedBeforeSolo = QVariant( element.attribute( "mutedBeforeSolo", "0" ) ).toBool();

	if (element.hasAttribute("color"))
	{
		setColor(QColor{element.attribute("color")});
	}

	if (!presetMode)
	{
		if (element.hasAttribute("uid"))
		{
			m_uid = element.attribute("uid");
			// a cloned track must not share the id of the original
			for (const auto& track : m_trackContainer->tracks())
			{
				if (track != this && track->m_uid == m_uid) { regenerateUid(); break; }
			}
		}
		setGroupId(element.attribute("group"));
		setCollapsed(element.attribute("collapsed", "0").toInt() != 0);
		setFrozen(element.attribute("frozen", "0").toInt() != 0);
		m_mutedBeforeParent = element.attribute("mutedbeforeparent", "0").toInt() != 0;
		m_freezeProxyOf = element.attribute("freezeproxyof");
		DuckSettings duck;
		if (element.hasAttribute("duck"))
		{
			duck.enabled = element.attribute("duck").toInt() != 0;
			duck.depth = std::clamp(element.attribute("duckdepth", "0.8").toFloat(), 0.f, 1.f);
			duck.holdMs = std::clamp(element.attribute("duckhold", "10").toFloat(), 0.f, 500.f);
			duck.releaseMs = std::clamp(element.attribute("duckrelease", "160").toFloat(), 5.f, 2000.f);
			duck.rate = std::clamp(element.attribute("duckrate", "0").toInt(), 0, 3);
			duck.shape = std::clamp(element.attribute("duckshape", "0").toInt(), 0, 2);
			duck.source = std::clamp(element.attribute("ducksource", "0").toInt(), 0, 1);
			duck.followVelocity = element.attribute("duckvelocity", "1").toInt() != 0;
			duck.attackMs = std::clamp(element.attribute("duckattack", "2").toFloat(), 0.5f, 50.f);
			duck.lookaheadMs = std::clamp(element.attribute("ducklookahead", "2").toFloat(), 0.f, 30.f);
			duck.band = std::clamp(element.attribute("duckband", "0").toInt(), 0, 3);
			duck.lowHz = std::clamp(element.attribute("ducklow", "150").toFloat(), 20.f, 20000.f);
			duck.highHz = std::clamp(element.attribute("duckhigh", "2000").toFloat(), 20.f, 20000.f);
		}
		setDuckSettings(duck);
		setKickSource(element.attribute("kicksource", "0").toInt() != 0);
	}

	if (presetMode)
	{
		QDomNode node = element.firstChild();
		while( !node.isNull() )
		{
			if( node.isElement() && node.nodeName() == nodeName() )
			{
				loadTrackSpecificSettings( node.toElement() );
				break;
			}
			node = node.nextSibling();
		}

		return;
	}

	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		deleteClips();
	}

	QDomNode node = element.firstChild();
	while( !node.isNull() )
	{
		if( node.isElement() )
		{
			if( node.nodeName() == nodeName() )
			{
				loadTrackSpecificSettings( node.toElement() );
			}
			else if( node.nodeName() != "muted"
			&& node.nodeName() != "solo"
			&& !node.toElement().attribute( "metadata" ).toInt() )
			{
				Clip * clip = createClip(
								TimePos( 0 ) );
				clip->restoreState( node.toElement() );
			}
		}
		node = node.nextSibling();
	}

	int storedHeight = element.attribute( "trackheight" ).toInt();
	if( storedHeight >= MINIMAL_TRACK_HEIGHT )
	{
		m_height = storedHeight;
	}
}

void Track::savePreset(QDomDocument & doc, QDomElement & element)
{
	saveTrack(doc, element, true);
}

void Track::loadPreset(const QDomElement & element)
{
	loadTrack(element, true);
}

void Track::saveSettings(QDomDocument& doc, QDomElement& element)
{
	// Assume that everything should be saved if we are called through SerializingObject::saveSettings
	saveTrack(doc, element, false);
}

void Track::loadSettings(const QDomElement& element)
{
	// Assume that everything should be loaded if we are called through SerializingObject::loadSettings 
	loadTrack(element, false);
}




Clip * Track::addClip( Clip * clip )
{
	m_clips.push_back( clip );

	emit clipAdded( clip );

	return clip; // just for convenience
}




void Track::removeClip( Clip * clip )
{
	auto it = std::ranges::find(m_clips, clip);
	if (it != m_clips.end())
	{
		m_clips.erase(it);
		if( Engine::getSong() )
		{
			Engine::getSong()->updateLength();
			Engine::getSong()->setModified();
		}
	}
}


void Track::deleteClips()
{
	while (!m_clips.empty())
	{
		delete m_clips.front();
	}
}




auto Track::getClip(std::size_t clipNum) -> Clip*
{
	if( clipNum < m_clips.size() )
	{
		return m_clips[clipNum];
	}
	printf( "called Track::getClip( %zu ), "
			"but Clip %zu doesn't exist\n", clipNum, clipNum );
	return createClip( clipNum * TimePos::ticksPerBar() );

}




int Track::getClipNum( const Clip * clip )
{
//	for( int i = 0; i < getTrackContentWidget()->numOfClips(); ++i )
	auto it = std::ranges::find(m_clips, clip);
	if (it != m_clips.end())
	{
/*		if( getClip( i ) == _clip )
		{
			return i;
		}*/
		return it - m_clips.begin();
	}
	qWarning( "Track::getClipNum(...) -> _clip not found!\n" );
	return 0;
}




void Track::getClipsInRange( clipVector & clipV, const TimePos & start,
							const TimePos & end )
{
	for( Clip* clip : m_clips )
	{
		int s = clip->startPosition();
		int e = clip->endPosition();
		if( ( s <= end ) && ( e >= start ) )
		{
			// Clip is within given range
			// Insert sorted by Clip's position
			clipV.insert(std::upper_bound(clipV.begin(), clipV.end(), clip, Clip::comparePosition),
						clip);
		}
	}
}




void Track::swapPositionOfClips( int clipNum1, int clipNum2 )
{
	qSwap( m_clips[clipNum1], m_clips[clipNum2] );

	const TimePos pos = m_clips[clipNum1]->startPosition();

	m_clips[clipNum1]->movePosition( m_clips[clipNum2]->startPosition() );
	m_clips[clipNum2]->movePosition( pos );
}




void Track::createClipsForPattern(int pattern)
{
	while( numOfClips() < pattern + 1 )
	{
		TimePos position = TimePos( numOfClips(), 0 );
		Clip * clip = createClip( position );
		clip->changeLength( TimePos( 1, 0 ) );
	}
}




//! @todo If we stepped through this list last to first, and the list was in ascending order by Clip time, once we hit
//! a Clip that was earlier than the insert time, we could fall out of the loop early.
void Track::insertBar( const TimePos & pos )
{
	// we'll increase the position of every Clip, positioned behind pos, by
	// one bar
	for (const auto& clip : m_clips)
	{
		if (clip->startPosition() >= pos)
		{
			clip->movePosition(clip->startPosition() + TimePos::ticksPerBar());
		}
	}
}




void Track::removeBar( const TimePos & pos )
{
	// we'll decrease the position of every Clip, positioned behind pos, by
	// one bar
	for (const auto& clip : m_clips)
	{
		if (clip->startPosition() >= pos)
		{
			clip->movePosition(clip->startPosition() - TimePos::ticksPerBar());
		}
	}
}




bar_t Track::length() const
{
	// find last end-position
	tick_t last = 0;
	for (const auto& clip : m_clips)
	{
		if (Engine::getSong()->isExporting() && clip->isMuted())
		{
			continue;
		}

		const tick_t cur = clip->endPosition();
		if( cur > last )
		{
			last = cur;
		}
	}

	return last / TimePos::ticksPerBar();
}



void Track::toggleSolo()
{
	// Custom: solo sets many mute states at once; children must not be re-muted by their parents meanwhile
	// (toggleSolo is re-entered for the other tracks, so restore the previous state instead of clearing)
	struct RestoreFlag { bool previous; ~RestoreFlag() { Track::s_inSoloChange = previous; } } restoreFlag{s_inSoloChange};
	s_inSoloChange = true;

	const TrackContainer::TrackList & tl = m_trackContainer->tracks();

	bool soloBefore = false;
	for (const auto& track : tl)
	{
		if (track != this)
		{
			if (track->m_soloModel.value())
			{
				soloBefore = true;
				break;
			}
		}
	}

	const bool solo = m_soloModel.value();

	// Custom: tracks that stay audible when this track is soloed.
	// Soloing a group keeps its members, soloing a member keeps its group (which has no audio itself).
	auto keepAudible = [this](const Track* track) {
		// the frozen audio behaves like the track it belongs to
		if (track->isFreezeProxy())
		{
			if (const auto owner = findTrackByUid(track->freezeProxyOf())) { track = owner; }
		}
		if (track == this) { return true; }
		if (type() == Type::Group && track->groupId() == static_cast<const GroupTrack*>(this)->uid()) { return true; }
		if (track->type() == Type::Group && !m_groupId.isEmpty()
			&& static_cast<const GroupTrack*>(track)->uid() == m_groupId) { return true; }
		return false;
	};

	// Should we use the new behavior of solo or the older/legacy one?
	const bool soloLegacyBehavior = ConfigManager::inst()->value("app", "sololegacybehavior", "0").toInt();

	for (const auto& track : tl)
	{
		if (solo)
		{
			// save mute-state in case no track was solo before
			if (!soloBefore)
			{
				track->m_mutedBeforeSolo = track->isMutedSelf();
			}
			// Don't mute AutomationTracks (keep their original state) unless we are on the sololegacybehavior mode
			if (keepAudible(track))
			{
				track->setMuted(false);
			}
			else if (soloLegacyBehavior || track->type() != Type::Automation)
			{
				track->setMuted(true);
			}
			if (track != this)
			{
				track->m_soloModel.setValue(false);
			}
		}
		else if (!soloBefore)
		{
			// Unless we are on the sololegacybehavior mode, only restores the
			// mute state if the track isn't an Automation Track
			if (soloLegacyBehavior || track->type() != Type::Automation)
			{
				track->setMuted(track->m_mutedBeforeSolo);
			}
		}
	}
}

bool Track::isMuted() const
{
	if (m_mutedModel.value()) { return true; }
	// Custom: the frozen audio follows the frozen track (and is silent without it)
	if (!m_freezeProxyOf.isEmpty())
	{
		const auto owner = findTrackByUid(m_freezeProxyOf);
		return owner == nullptr || !owner->isFrozen() || owner->isMuted();
	}
	if (m_groupId.isEmpty()) { return false; }
	return false; // Custom: children of muted parents get their own mute button set (see propagateMuteToChildren)
}




void Track::setGroupId(const QString& id)
{
	if (m_groupId == id) { return; }
	m_groupId = id;
	emit groupChanged();
}




const QString& Track::uid() const
{
	if (m_uid.isEmpty()) { m_uid = QUuid::createUuid().toString(QUuid::WithoutBraces); }
	return m_uid;
}




void Track::regenerateUid()
{
	m_uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
}




Track* Track::findTrackByUid(const QString& uid) const
{
	if (uid.isEmpty()) { return nullptr; }
	for (const auto& track : m_trackContainer->tracks())
	{
		if (track->m_uid == uid) { return track; }
	}
	return nullptr;
}




Track* Track::freezeProxy() const
{
	if (m_uid.isEmpty()) { return nullptr; }
	for (const auto& track : m_trackContainer->tracks())
	{
		if (track->m_freezeProxyOf == m_uid) { return track; }
	}
	return nullptr;
}




void Track::setDuckSettings(const DuckSettings& settings)
{
	if (m_duck == settings) { return; }
	m_duck = settings;
	emit duckChanged();
}

std::optional<Track::DuckSettings> Track::effectiveDuck() const
{
	if (m_duck.enabled) { return m_duck; }
	int guard = 0;
	for (auto parent = parentTrack(); parent != nullptr && guard < 8; parent = parent->parentTrack(), ++guard)
	{
		if (parent->m_duck.enabled) { return parent->m_duck; }
	}
	return std::nullopt;
}

int Track::duckRateTicks(int rate)
{
	switch (rate)
	{
		case 1: return DefaultTicksPerBar / 8;
		case 2: return DefaultTicksPerBar / 2;
		case 3: return DefaultTicksPerBar / 16;
		default: return DefaultTicksPerBar / 4;
	}
}

void Track::setKickSource(bool on)
{
	if (m_kickSource == on) { return; }
	m_kickSource = on;
	emit duckChanged();
}

float Track::duckGain(const DuckSettings& s, double ms, float strength)
{
	// amount of ducking: ramps in quickly (no click), holds, then releases
	if (ms < 0) { return 1.f; }
	const double attack = std::max(0.1f, s.attackMs);
	double amount;
	if (ms < attack)
	{
		const double a = ms / attack;
		amount = a * a * (3.0 - 2.0 * a);
	}
	else if (ms < attack + s.holdMs) { amount = 1.0; }
	else
	{
		const double r = std::min(1.0, (ms - attack - s.holdMs) / std::max(1.f, s.releaseMs));
		double recovered;
		switch (s.shape)
		{
			case 1: recovered = r; break;                       // linear
			case 2: recovered = r * r * r; break;               // pumping: stays down long, comes back late
			default: recovered = r * r * (3.0 - 2.0 * r); break; // smooth S-curve
		}
		amount = 1.0 - recovered;
	}
	return static_cast<float>(1.0 - s.depth * strength * amount);
}




Track* Track::parentTrack() const
{
	if (m_groupId.isEmpty()) { return nullptr; }
	for (const auto& track : m_trackContainer->tracks())
	{
		if (track != this && !track->m_uid.isEmpty() && track->m_uid == m_groupId) { return track; }
	}
	return nullptr;
}




GroupTrack* Track::group() const
{
	const auto parent = parentTrack();
	return parent != nullptr && parent->type() == Type::Group ? static_cast<GroupTrack*>(parent) : nullptr;
}




std::vector<Track*> Track::childTracks() const
{
	std::vector<Track*> result;
	if (m_uid.isEmpty()) { return result; }
	for (const auto& track : m_trackContainer->tracks())
	{
		if (track != this && track->m_groupId == m_uid) { result.push_back(track); }
	}
	return result;
}




bool Track::hasChildTracks() const
{
	if (m_uid.isEmpty()) { return false; }
	for (const auto& track : m_trackContainer->tracks())
	{
		if (track != this && track->m_groupId == m_uid) { return true; }
	}
	return false;
}




int Track::treeDepth() const
{
	int depth = 0;
	for (auto parent = parentTrack(); parent != nullptr && depth < 8; parent = parent->parentTrack()) { ++depth; }
	return depth;
}




bool Track::isHiddenByCollapse() const
{
	int guard = 0;
	for (auto parent = parentTrack(); parent != nullptr && guard < 8; parent = parent->parentTrack(), ++guard)
	{
		if (parent->isCollapsed()) { return true; }
	}
	return false;
}




void Track::setCollapsed(bool collapsed)
{
	if (m_collapsed == collapsed) { return; }
	m_collapsed = collapsed;
	emit collapsedChanged();
}




void Track::setFrozen(bool frozen)
{
	if (m_frozen == frozen) { return; }
	m_frozen = frozen;
	emit frozenChanged();
}




void Track::setColor(const std::optional<QColor>& color)
{
	m_color = color;
	emit colorChanged();
}

BoolModel *Track::getMutedModel()
{
	return &m_mutedModel;
}

void Track::setName(const QString& newName)
{
	if (m_name != newName)
	{
		m_name = newName;

		if (auto song = Engine::getSong())
		{
			song->setModified();
		}
		
		emit nameChanged();
	}
}

} // namespace lmms
