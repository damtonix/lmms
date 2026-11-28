/*
 * MixerTrackSync.cpp - Custom (PtH-LMMS): every instrument/sample track and every group
 *                      owns its own mixer channel, ordered like the tracks
 */

#include "MixerTrackSync.h"

#include <algorithm>
#include <map>
#include <set>

#include <QApplication>
#include <QDomDocument>

#include "AudioBusHandle.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GroupTrack.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "MixerView.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

IntModel* channelModelOf(Track* track)
{
	if (track->type() == Track::Type::Instrument) { return static_cast<InstrumentTrack*>(track)->mixerChannelModel(); }
	if (track->type() == Track::Type::Sample) { return static_cast<SampleTrack*>(track)->mixerChannelModel(); }
	return nullptr;
}

EffectChain* trackEffectsOf(Track* track)
{
	if (track->type() == Track::Type::Instrument) { return static_cast<InstrumentTrack*>(track)->audioBusHandle()->effects(); }
	if (track->type() == Track::Type::Sample) { return static_cast<SampleTrack*>(track)->audioBusHandle()->effects(); }
	return nullptr;
}

void setChannelName(MixerChannel* ch, const QString& name)
{
	ch->m_name = name;
	ch->m_volumeModel.setDisplayName(name + ">" + Mixer::tr("Volume"));
	ch->m_muteModel.setDisplayName(name + ">" + Mixer::tr("Mute"));
	ch->m_soloModel.setDisplayName(name + ">" + Mixer::tr("Solo"));
}

} // namespace


MixerTrackSync::MixerTrackSync(MixerView* view)
	: QObject(view)
	, m_view(view)
{
	m_timer.setSingleShot(true);
	m_timer.setInterval(30);
	connect(&m_timer, &QTimer::timeout, this, &MixerTrackSync::sync);

	for (TrackContainer* tc : {static_cast<TrackContainer*>(Engine::getSong()), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		// (trackAdded comes from the Track base constructor: the derived track is not built yet,
		// so the track is only watched in sync())
		connect(tc, &TrackContainer::trackAdded, this, &MixerTrackSync::scheduleSync);
		connect(tc, &TrackContainer::trackRemoved, this, &MixerTrackSync::scheduleSync);
		connect(tc, &TrackContainer::trackMoved, this, &MixerTrackSync::scheduleSync);
		for (auto track : tc->tracks()) { watchTrack(track); }
	}
	connect(Engine::getSong(), &Song::projectLoaded, this, &MixerTrackSync::scheduleSync);
	scheduleSync();
}


bool MixerTrackSync::ownsChannel(const Track* track)
{
	const auto type = track->type();
	return (type == Track::Type::Instrument || type == Track::Type::Sample || type == Track::Type::Group)
		&& !track->isFreezeProxy();
}


Track* MixerTrackSync::trackByUid(const QString& uid)
{
	if (uid.isEmpty()) { return nullptr; }
	for (const auto& tracks : {Engine::getSong()->tracks(), Engine::patternStore()->tracks()})
	{
		for (auto track : tracks)
		{
			if (track->uid() == uid) { return track; }
		}
	}
	return nullptr;
}


bool MixerTrackSync::sendAllowed(const MixerChannel* from, const MixerChannel* to)
{
	if (from == to) { return false; }
	// independent channels and master can always be fed
	if (!to->isTrackChannel()) { return true; }
	// a track channel: the members of its group feed it ...
	if (from->isTrackChannel()) { return from->m_busOwnerUid == to->m_ownerUid; }
	// ... and independent channels may send back into a group bus (not into an instrument/sample channel)
	if (from->index() == 0) { return false; }
	const auto target = trackByUid(to->m_ownerUid);
	return target != nullptr && target->type() == Track::Type::Group;
}


Track* MixerTrackSync::groupFedBy(const MixerChannel* ch)
{
	if (ch->isTrackChannel() || ch->index() == 0) { return nullptr; }
	for (auto route : ch->m_sends)
	{
		const auto receiver = route->receiver();
		if (!receiver->isTrackChannel()) { continue; }
		const auto track = trackByUid(receiver->m_ownerUid);
		if (track != nullptr && track->type() == Track::Type::Group) { return track; }
	}
	return nullptr;
}


Track* MixerTrackSync::busGroupOf(const Track* track)
{
	int guard = 0;
	for (auto p = track->parentTrack(); p != nullptr && guard < 16; p = p->parentTrack(), ++guard)
	{
		if (p->type() == Track::Type::Group) { return p; }
	}
	return nullptr;
}


void MixerTrackSync::watchTrack(Track* track)
{
	connect(track, &Track::nameChanged, this, &MixerTrackSync::scheduleSync, Qt::UniqueConnection);
	connect(track, &Track::colorChanged, this, &MixerTrackSync::scheduleSync, Qt::UniqueConnection);
	connect(track, &Track::groupChanged, this, &MixerTrackSync::scheduleSync, Qt::UniqueConnection);
	connect(track, &Track::frozenChanged, this, &MixerTrackSync::trackFrozenChanged, Qt::UniqueConnection);
	if (auto model = channelModelOf(track))
	{
		connect(model, &IntModel::dataChanged, this, &MixerTrackSync::scheduleSync, Qt::UniqueConnection);
	}
	if (auto fx = trackEffectsOf(track))
	{
		// effects arriving in the track (old project, preset) move to its channel
		connect(fx, &EffectChain::dataChanged, this, &MixerTrackSync::scheduleSync, Qt::UniqueConnection);
	}
}


bool MixerTrackSync::channelFrozenFor(const Track* track)
{
	return track->isFrozen() && track->freezeProxy() == nullptr;
}


void MixerTrackSync::trackFrozenChanged()
{
	// immediately (also while exporting: stem export thaws tracks for a moment)
	auto track = qobject_cast<Track*>(sender());
	if (track == nullptr) { return; }
	const int idx = Engine::mixer()->channelOwnedBy(track->uid());
	if (idx <= 0) { return; }
	Engine::mixer()->mixerChannel(idx)->m_frozen = channelFrozenFor(track);
	m_view->updateMixerChannel(idx);
}


void MixerTrackSync::scheduleSync()
{
	if (!m_running) { m_timer.start(30); }
}


void MixerTrackSync::sync()
{
	auto song = Engine::getSong();
	if (m_running || song->isLoadingProject() || song->isExporting()) { return; }
	if (QApplication::mouseButtons() != Qt::NoButton)
	{
		// a drag (moving tracks, groups, ...) is going on: sort the mixer once it is dropped
		m_timer.start(100);
		return;
	}
	m_running = true;
	const bool journalling = Engine::projectJournal()->isJournalling();
	Engine::projectJournal()->setJournalling(false);

	Mixer* mx = Engine::mixer();
	bool structure = false;
	bool cosmetic = false;
	bool fxMoved = false;
	MixerChannel* selected = m_view->currentMixerChannel() != nullptr
		? mx->mixerChannel(m_view->currentMixerChannel()->channelIndex()) : nullptr;

	// tracks owning a channel, in the order of the editors (song editor top to bottom, then pattern editor)
	std::vector<Track*> owners;
	const auto addOwner = [&owners](Track* track) {
		if (ownsChannel(track) && std::find(owners.begin(), owners.end(), track) == owners.end()) { owners.push_back(track); }
	};
	if (getGUI()->songEditor() != nullptr)
	{
		for (auto tv : getGUI()->songEditor()->m_editor->trackViews()) { addOwner(tv->getTrack()); }
	}
	for (auto track : song->tracks()) { addOwner(track); }
	for (auto track : Engine::patternStore()->tracks()) { addOwner(track); }

	for (auto track : owners) { watchTrack(track); }

	std::set<QString> ownerUids;
	std::map<int, int> users; // channel -> number of tracks routed to it
	for (auto track : owners)
	{
		ownerUids.insert(track->uid());
		if (auto model = channelModelOf(track)) { ++users[model->value()]; }
	}

	// 1. channels of deleted tracks go away
	for (int i = mx->numChannels() - 1; i > 0; --i)
	{
		const auto ch = mx->mixerChannel(i);
		if (ch->isTrackChannel() && ownerUids.count(ch->m_ownerUid) == 0)
		{
			if (selected == ch) { selected = nullptr; }
			m_view->deleteChannel(i);
			structure = true;
		}
	}

	// 2. every owner gets its channel
	for (auto track : owners)
	{
		const QString uid = track->uid();
		const auto busGroup = busGroupOf(track);
		const QString expectedBus = busGroup != nullptr ? busGroup->uid() : QString();
		auto model = channelModelOf(track);
		int idx = mx->channelOwnedBy(uid);
		if (idx < 0)
		{
			const int c = model != nullptr ? model->value() : 0;
			MixerChannel* old = c > 0 && c < mx->numChannels() ? mx->mixerChannel(c) : nullptr;
			if (old != nullptr && !old->isTrackChannel() && users[c] == 1 && old->m_receives.empty())
			{
				// the track's own channel already (old project): take it over
				idx = c;
				old->m_ownerUid = uid;
				// sends to master move to the group bus, other routings stay as they are
				old->m_busOwnerUid = mx->channelSendModel(c, 0) != nullptr ? QString() : expectedBus;
			}
			else
			{
				idx = m_view->addNewChannel(); // sends to master
				auto ch = mx->mixerChannel(idx);
				ch->m_ownerUid = uid;
				ch->m_busOwnerUid = QString();
				if (old != nullptr && !old->isTrackChannel())
				{
					// shared channel of an old project: it stays independent, the new channel feeds it (same sound)
					mx->deleteChannelSend(idx, 0);
					mx->createChannelSend(idx, c);
					ch->m_busOwnerUid = expectedBus;
				}
				else if (old != nullptr)
				{
					// duplicated track (or restored by undo): take over the effects and the fader of that channel
					QDomDocument doc;
					auto parent = doc.createElement("copy");
					old->m_fxChain.saveState(doc, parent);
					ch->m_fxChain.restoreState(parent.firstChildElement(ch->m_fxChain.nodeName()));
					ch->m_volumeModel.setValue(old->m_volumeModel.value());
				}
				structure = true;
			}
		}
		if (model != nullptr && model->value() != idx) { model->setValue(idx); }

		// effects only live in the mixer channel: move those of the track there (in front, same sound)
		if (auto fx = trackEffectsOf(track); fx != nullptr && !fx->isEmpty())
		{
			if (fx->moveAllEffectsTo(mx->mixerChannel(idx)->m_fxChain)) { fxMoved = true; }
		}
	}

	// 3. fixed bus routing, name, color
	for (auto track : owners)
	{
		const int idx = mx->channelOwnedBy(track->uid());
		if (idx < 0) { continue; }
		auto ch = mx->mixerChannel(idx);
		const auto busGroup = busGroupOf(track);
		const QString expectedBus = busGroup != nullptr ? busGroup->uid() : QString();
		if (ch->m_busOwnerUid != expectedBus)
		{
			// the track changed its group: move the bus send (keeping its amount)
			const int oldBus = ch->m_busOwnerUid.isEmpty() ? 0 : mx->channelOwnedBy(ch->m_busOwnerUid);
			float amount = 1.0f;
			if (oldBus >= 0)
			{
				if (auto send = mx->channelSendModel(idx, oldBus))
				{
					amount = send->value();
					mx->deleteChannelSend(idx, oldBus);
				}
			}
			const int newBus = expectedBus.isEmpty() ? 0 : mx->channelOwnedBy(expectedBus);
			if (newBus >= 0 && newBus != idx && !mx->isInfiniteLoop(idx, newBus)) { mx->createChannelSend(idx, newBus, amount); }
			ch->m_busOwnerUid = expectedBus;
			structure = true;
		}
		if (const bool frozen = channelFrozenFor(track); ch->m_frozen != frozen) { ch->m_frozen = frozen; cosmetic = true; }
		if (ch->m_name != track->name()) { setChannelName(ch, track->name()); cosmetic = true; }
		if (ch->color() != track->color()) { ch->setColor(track->color()); cosmetic = true; }
	}

	// 4. track channels only receive from the members of their group (group busses also from independent channels)
	for (int i = 0; i < mx->numChannels(); ++i)
	{
		auto ch = mx->mixerChannel(i);
		const auto sends = ch->m_sends;
		for (auto route : sends)
		{
			const auto receiver = route->receiver();
			if (!receiver->isTrackChannel()) { continue; }
			if (!sendAllowed(ch, receiver))
			{
				mx->deleteChannelSend(route);
				structure = true;
			}
		}
	}

	// 5. order: master, track channels like the tracks, then the independent channels
	std::vector<int> order{0};
	for (auto track : owners)
	{
		const int idx = mx->channelOwnedBy(track->uid());
		if (idx > 0 && std::find(order.begin(), order.end(), idx) == order.end()) { order.push_back(idx); }
	}
	for (int i = 1; i < mx->numChannels(); ++i)
	{
		if (std::find(order.begin(), order.end(), i) == order.end()) { order.push_back(i); }
	}
	bool identity = true;
	for (std::size_t n = 0; n < order.size(); ++n) { identity = identity && order[n] == static_cast<int>(n); }
	if (!identity)
	{
		mx->reorderChannels(order);
		structure = true;
	}
	else if (structure)
	{
		// channels added/removed but nothing to sort: still put the views into their areas
		for (std::size_t n = 0; n < order.size(); ++n) { order[n] = static_cast<int>(n); }
	}

	if (structure)
	{
		m_view->reorderChannelViews(order);
		if (selected != nullptr) { m_view->setCurrentMixerChannel(selected->index()); }
		if (!song->isLoadingProject()) { song->setModified(); }
	}
	else if (fxMoved && !song->isLoadingProject()) { song->setModified(); }
	if (!structure && cosmetic)
	{
		for (int i = 0; i < mx->numChannels(); ++i) { m_view->updateMixerChannel(i); }
	}

	Engine::projectJournal()->setJournalling(journalling);
	m_running = false;
}

} // namespace lmms::gui
