/*
 * SongClipLinks.cpp - linked clips share their content (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "Song.h"

#include <QDomDocument>
#include <QTextStream>
#include <QTimer>

#include "AudioEngine.h"
#include "Clip.h"
#include "Engine.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! attributes that belong to the single clip (where it is), not to the shared content
const QStringList& perClipAttributes()
{
	static const QStringList attributes{"pos", "len", "off", "muted", "mute", "looplen", "autoresize", "color"};
	return attributes;
}

//! the clip as XML, empty if it can not be saved right now (e.g. while it is dragged)
QDomElement clipElement(Clip* clip, QDomDocument& doc)
{
	QDomElement root = doc.createElement("linked");
	doc.appendChild(root);
	clip->saveState(doc, root);
	QDomElement element = root.firstChildElement();
	if (element.isNull()) { return element; }
	// the journal id differs per clip
	for (auto child = element.firstChildElement("journallingObject"); !child.isNull();
		child = element.firstChildElement("journallingObject"))
	{
		element.removeChild(child);
	}
	return element;
}

QString contentOf(const QDomElement& element)
{
	QDomElement copy = element.cloneNode(true).toElement();
	for (const auto& attribute : perClipAttributes()) { copy.removeAttribute(attribute); }
	QString text;
	QTextStream stream(&text);
	copy.save(stream, -1);
	return text;
}

} // namespace


void Song::scheduleLinkSync()
{
	if (m_linkSyncScheduled || m_syncingLinks) { return; }
	m_linkSyncScheduled = true;
	QTimer::singleShot(30, this, [this] {
		m_linkSyncScheduled = false;
		syncLinkedClips();
	});
}


void Song::syncLinkedClips()
{
	if (m_syncingLinks || isLoadingProject() || isSavingProject()) { return; }

	QHash<QString, QVector<Clip*>> groups;
	for (const auto track : tracks())
	{
		for (const auto clip : track->getClips())
		{
			if (clip->isLinked()) { groups[clip->linkId()].push_back(clip); }
		}
	}

	// the link symbol appears / disappears when a group gets a second member or loses it
	QHash<QString, int> sizes;
	for (auto it = groups.cbegin(); it != groups.cend(); ++it) { sizes.insert(it.key(), it.value().size()); }
	if (sizes != m_linkGroupSizes)
	{
		const auto old = m_linkGroupSizes;
		m_linkGroupSizes = sizes;
		for (auto it = groups.cbegin(); it != groups.cend(); ++it)
		{
			if ((old.value(it.key(), 0) >= 2) != (it.value().size() >= 2))
			{
				for (const auto clip : it.value()) { emit clip->dataChanged(); }
			}
		}
	}

	m_syncingLinks = true;
	for (auto it = groups.cbegin(); it != groups.cend(); ++it)
	{
		const auto& clips = it.value();
		if (clips.size() < 2) { continue; }

		std::vector<QDomDocument> docs(clips.size());
		std::vector<QDomElement> elements(clips.size());
		QStringList contents;
		bool complete = true;
		for (int i = 0; i < clips.size(); ++i)
		{
			elements[i] = clipElement(clips[i], docs[i]);
			if (elements[i].isNull()) { complete = false; break; }
			contents << contentOf(elements[i]);
		}
		// a clip is being dragged / edited without journal: wait
		if (!complete) { continue; }

		if (contents.count(contents.front()) == contents.size())
		{
			m_linkContent.insert(it.key(), contents.front());
			continue;
		}

		// the edited clip is the one that differs from the last synced content
		const QString known = m_linkContent.value(it.key());
		int source = 0;
		for (int i = 0; i < clips.size(); ++i)
		{
			if (contents[i] != known) { source = i; break; }
		}

		for (int i = 0; i < clips.size(); ++i)
		{
			if (i == source || contents[i] == contents[source]) { continue; }
			QDomElement element = elements[source].cloneNode(true).toElement();
			for (const auto& attribute : perClipAttributes())
			{
				if (elements[i].hasAttribute(attribute)) { element.setAttribute(attribute, elements[i].attribute(attribute)); }
				else { element.removeAttribute(attribute); }
			}
			auto guard = Engine::audioEngine()->requestChangesGuard();
			clips[i]->restoreState(element);
		}
		m_linkContent.insert(it.key(), contents[source]);
	}
	m_syncingLinks = false;
}

} // namespace lmms
