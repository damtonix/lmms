/*
 * SnapshotDialog.cpp - list and restore the automatic versions of the project (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "SnapshotDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <map>

#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

QString relativeTime(const QDateTime& when)
{
	const qint64 secs = when.secsTo(QDateTime::currentDateTime());
	if (secs < 60) { return SnapshotDialog::tr("gerade eben"); }
	if (secs < 3600) { return SnapshotDialog::tr("vor %1 min").arg(secs / 60); }
	if (secs < 86400) { return SnapshotDialog::tr("vor %1 h").arg(secs / 3600); }
	return SnapshotDialog::tr("vor %1 Tagen").arg(secs / 86400);
}

QString snapshotNote(const QString& baseName)
{
	const QString tag = baseName.mid(20);
	if (tag.isEmpty()) { return QString(); }
	if (tag == "vor-wiederherstellung") { return SnapshotDialog::tr("  (vor Wiederherstellung)"); }
	return "  (" + QString(tag).replace('-', ' ').replace("loeschen", SnapshotDialog::tr("l\xc3\xb6schen")) + ")";
}


//! Custom: what changed between two project files

struct TrackInfo
{
	QString name;
	QString place; //!< "" = song editor, otherwise pattern editor
	bool muted = false;
	QString volume;
	QString mixerChannel;
	QString settings; //!< instrument + effects, serialized
	int drafts = 0;
	std::map<QString, QString> clips; //!< kind@pos -> content
};

//! a model saved as <name value="..."/> (with id, scale_type) instead of an attribute
bool isModelElement(const QDomElement& element)
{
	if (!element.hasAttribute("value") || element.hasChildNodes()) { return false; }
	const auto attributes = element.attributes();
	for (int i = 0; i < attributes.count(); ++i)
	{
		const QString name = attributes.item(i).nodeName();
		if (name != "value" && name != "id" && name != "scale_type") { return false; }
	}
	return true;
}

//! canonical text of an element: sorted attributes, model children folded into attributes, no ids
QString elementString(const QDomElement& element)
{
	std::map<QString, QString> attributes;
	const auto attributeMap = element.attributes();
	for (int i = 0; i < attributeMap.count(); ++i)
	{
		const auto attribute = attributeMap.item(i).toAttr();
		if (attribute.name() == "id") { continue; }
		attributes[attribute.name()] = attribute.value();
	}
	QString children;
	for (auto child = element.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		if (child.isElement())
		{
			const QDomElement childElement = child.toElement();
			if (isModelElement(childElement)) { attributes[childElement.tagName()] = childElement.attribute("value"); }
			else { children += elementString(childElement); }
		}
		else if (child.isText() || child.isCDATASection())
		{
			children += child.nodeValue().trimmed();
		}
	}
	QString text = "<" + element.tagName();
	for (const auto& [name, value] : attributes) { text += " " + name + "=\"" + value + "\""; }
	return text + ">" + children + "</" + element.tagName() + ">";
}

//! value of a model that is saved either as attribute or as child element
QString modelValue(const QDomElement& element, const QString& name)
{
	if (element.hasAttribute(name)) { return element.attribute(name); }
	return element.firstChildElement(name).attribute("value");
}

bool insidePatternEditor(const QDomElement& track)
{
	for (auto p = track.parentNode(); !p.isNull(); p = p.parentNode())
	{
		if (p.nodeName() == "patterntrack") { return true; }
	}
	return false;
}

std::map<QString, TrackInfo> collectTracks(const QDomDocument& doc)
{
	std::map<QString, TrackInfo> result;
	std::map<QString, int> seen;
	const auto tracks = doc.elementsByTagName("track");
	for (int i = 0; i < tracks.count(); ++i)
	{
		const QDomElement track = tracks.item(i).toElement();
		if (track.isNull()) { continue; }
		// tracks in a journal or clipboard section are not part of the project
		if (track.parentNode().nodeName() != "trackcontainer") { continue; }
		TrackInfo info;
		info.name = track.attribute("name");
		info.place = insidePatternEditor(track) ? SnapshotDialog::tr("Pattern-Editor") : QString();
		info.muted = modelValue(track, "muted").toInt() != 0;
		for (auto child = track.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
		{
			const QString kind = child.nodeName();
			if (kind.endsWith("clip"))
			{
				info.clips[kind + "@" + child.attribute("pos")] = elementString(child);
			}
			else if (kind == "pthdrafts")
			{
				info.drafts = child.childNodes().count();
			}
			else if (kind == "instrumenttrack" || kind == "sampletrack")
			{
				info.volume = modelValue(child, "vol");
				info.mixerChannel = modelValue(child, "mixch");
				QDomElement copy = child.cloneNode(true).toElement();
				for (const auto attribute : {"vol", "pan", "mixch"})
				{
					copy.removeAttribute(attribute);
					const auto model = copy.firstChildElement(attribute);
					if (!model.isNull()) { copy.removeChild(model); }
				}
				info.settings = elementString(copy);
			}
		}
		QString key = track.attribute("uid");
		if (key.isEmpty())
		{
			key = info.place + "/" + info.name;
			key += "#" + QString::number(seen[key]++);
		}
		result[key] = info;
	}
	return result;
}

QString plural(int n, const QString& one, const QString& many) { return n == 1 ? one : many.arg(n); }

QStringList compareProjects(const QDomDocument& oldDoc, const QDomDocument& newDoc)
{
	QStringList lines;
	const auto esc = [](const QString& text) { return text.toHtmlEscaped(); };

	// tempo, time signature, master volume
	const QDomElement oldHead = oldDoc.documentElement().firstChildElement("head");
	const QDomElement newHead = newDoc.documentElement().firstChildElement("head");
	if (modelValue(oldHead, "bpm") != modelValue(newHead, "bpm"))
	{
		lines << SnapshotDialog::tr("Tempo: %1 \xe2\x86\x92 %2 BPM").arg(modelValue(oldHead, "bpm"), modelValue(newHead, "bpm"));
	}
	const auto timeSig = [](const QDomElement& head) {
		return modelValue(head, "timesig_numerator") + "/" + modelValue(head, "timesig_denominator");
	};
	if (timeSig(oldHead) != timeSig(newHead))
	{
		lines << SnapshotDialog::tr("Taktart: %1 \xe2\x86\x92 %2").arg(timeSig(oldHead), timeSig(newHead));
	}
	if (modelValue(oldHead, "mastervol") != modelValue(newHead, "mastervol"))
	{
		lines << SnapshotDialog::tr("Master-Lautst\xc3\xa4rke: %1 \xe2\x86\x92 %2 %").arg(modelValue(oldHead, "mastervol"), modelValue(newHead, "mastervol"));
	}

	// tracks
	const auto oldTracks = collectTracks(oldDoc);
	const auto newTracks = collectTracks(newDoc);
	const auto label = [&esc](const TrackInfo& info) {
		return "<b>" + esc(info.name) + "</b>" + (info.place.isEmpty() ? QString() : " (" + info.place + ")");
	};
	QStringList added, removed;
	for (const auto& [key, info] : newTracks)
	{
		if (!oldTracks.count(key)) { added << label(info); }
	}
	for (const auto& [key, info] : oldTracks)
	{
		if (!newTracks.count(key)) { removed << label(info); }
	}
	if (!added.isEmpty()) { lines << SnapshotDialog::tr("Neue Spuren: %1").arg(added.join(", ")); }
	if (!removed.isEmpty()) { lines << SnapshotDialog::tr("Gel\xc3\xb6schte Spuren: %1").arg(removed.join(", ")); }

	for (const auto& [key, now] : newTracks)
	{
		const auto it = oldTracks.find(key);
		if (it == oldTracks.end()) { continue; }
		const TrackInfo& before = it->second;
		QStringList changes;
		if (before.name != now.name) { changes << SnapshotDialog::tr("umbenannt (vorher \"%1\")").arg(esc(before.name)); }
		int newClips = 0, removedClips = 0, changedClips = 0;
		for (const auto& [clipKey, content] : now.clips)
		{
			const auto old = before.clips.find(clipKey);
			if (old == before.clips.end()) { ++newClips; }
			else if (old->second != content) { ++changedClips; }
		}
		for (const auto& [clipKey, content] : before.clips)
		{
			if (!now.clips.count(clipKey)) { ++removedClips; }
		}
		// a moved clip shows up as one removed and one new clip
		const int moved = std::min(newClips, removedClips);
		if (moved > 0 && before.clips.size() == now.clips.size())
		{
			changes << plural(moved, SnapshotDialog::tr("1 Block verschoben"), SnapshotDialog::tr("%1 Bl\xc3\xb6\x63ke verschoben"));
			newClips -= moved;
			removedClips -= moved;
		}
		if (newClips > 0) { changes << plural(newClips, SnapshotDialog::tr("1 Block neu"), SnapshotDialog::tr("%1 Bl\xc3\xb6\x63ke neu")); }
		if (removedClips > 0) { changes << plural(removedClips, SnapshotDialog::tr("1 Block gel\xc3\xb6scht"), SnapshotDialog::tr("%1 Bl\xc3\xb6\x63ke gel\xc3\xb6scht")); }
		if (changedClips > 0) { changes << plural(changedClips, SnapshotDialog::tr("1 Block bearbeitet"), SnapshotDialog::tr("%1 Bl\xc3\xb6\x63ke bearbeitet")); }
		if (before.muted != now.muted) { changes << (now.muted ? SnapshotDialog::tr("stumm geschaltet") : SnapshotDialog::tr("Stummschaltung aufgehoben")); }
		if (before.volume != now.volume) { changes << SnapshotDialog::tr("Lautst\xc3\xa4rke %1 \xe2\x86\x92 %2 %").arg(before.volume, now.volume); }
		if (before.mixerChannel != now.mixerChannel)
		{
			changes << SnapshotDialog::tr("Mixer-Kanal %1 \xe2\x86\x92 %2").arg(before.mixerChannel, now.mixerChannel);
		}
		if (before.settings != now.settings) { changes << SnapshotDialog::tr("Instrument/Effekte ge\xc3\xa4ndert"); }
		if (before.drafts != now.drafts) { changes << SnapshotDialog::tr("Entw\xc3\xbcrfe %1 \xe2\x86\x92 %2").arg(before.drafts).arg(now.drafts); }
		if (!changes.isEmpty()) { lines << label(now) + ": " + changes.join(", "); }
	}

	// mixer
	const auto channels = [](const QDomDocument& doc) {
		std::map<int, QString> result;
		const auto mixer = doc.documentElement().firstChildElement("song").firstChildElement("mixer");
		for (auto channel = mixer.firstChildElement("mixerchannel"); !channel.isNull();
			channel = channel.nextSiblingElement("mixerchannel"))
		{
			result[channel.attribute("num").toInt()] = elementString(channel);
		}
		return result;
	};
	const auto oldChannels = channels(oldDoc);
	const auto newChannels = channels(newDoc);
	if (oldChannels.size() != newChannels.size())
	{
		lines << SnapshotDialog::tr("Mixer: %1 \xe2\x86\x92 %2 Kan\xc3\xa4le").arg(oldChannels.size()).arg(newChannels.size());
	}
	int changedChannels = 0;
	for (const auto& [num, content] : newChannels)
	{
		const auto old = oldChannels.find(num);
		if (old != oldChannels.end() && old->second != content) { ++changedChannels; }
	}
	if (changedChannels > 0)
	{
		lines << plural(changedChannels, SnapshotDialog::tr("Mixer: 1 Kanal ge\xc3\xa4ndert (Lautst\xc3\xa4rke, Effekte oder Sends)"),
			SnapshotDialog::tr("Mixer: %1 Kan\xc3\xa4le ge\xc3\xa4ndert (Lautst\xc3\xa4rke, Effekte oder Sends)"));
	}

	// arrangement markers
	const auto markers = [](const QDomDocument& doc) {
		QStringList result;
		const auto node = doc.documentElement().firstChildElement("song").firstChildElement("pthmarkers");
		for (auto m = node.firstChildElement("marker"); !m.isNull(); m = m.nextSiblingElement("marker"))
		{
			result << m.attribute("name") + "@" + m.attribute("pos");
		}
		return result;
	};
	if (markers(oldDoc) != markers(newDoc)) { lines << SnapshotDialog::tr("Abschnitts-Marker ge\xc3\xa4ndert"); }
	return lines;
}

} // namespace


SnapshotDialog::SnapshotDialog(QWidget* parent)
	: QDialog(parent)
	, m_projectFile(Engine::getSong()->projectFileName())
{
	setWindowTitle(tr("Snapshots"));
	resize(700, 620);
	auto layout = new QVBoxLayout(this);

	auto intro = new QLabel(m_projectFile.isEmpty()
		? tr("Das Projekt wurde noch nicht gespeichert. Snapshots entstehen automatisch bei jedem Speichern.")
		: tr("Bei jedem Speichern wird automatisch eine Kopie von <b>%1</b> abgelegt (nur wenn sich etwas "
			"ge\xc3\xa4ndert hat). Wiederherstellen l\xc3\xa4\x64t die Version in das aktuelle Projekt; "
			"erst beim n\xc3\xa4\x63hsten Speichern wird die Projektdatei \xc3\xbc\x62\x65rschrieben.")
			.arg(QFileInfo(m_projectFile).fileName().toHtmlEscaped()), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	m_list = new QTreeWidget(this);
	m_list->setHeaderLabels({tr("Gespeichert"), tr("Vor"), tr("Gr\xc3\xb6\xc3\x9f\x65")});
	m_list->setRootIsDecorated(false);
	m_list->header()->setStretchLastSection(false);
	m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	layout->addWidget(m_list, 1);

	// Custom: what changed
	auto compareRow = new QHBoxLayout;
	compareRow->addWidget(new QLabel(tr("Unterschiede zeigen:"), this));
	m_compareMode = new QComboBox(this);
	m_compareMode->addItem(tr("Was hat sich seitdem ge\xc3\xa4ndert (bis zum aktuellen Stand)"));
	m_compareMode->addItem(tr("Was wurde in dieser Version ge\xc3\xa4ndert (gegen\xc3\xbc\x62\x65r der Version davor)"));
	compareRow->addWidget(m_compareMode, 1);
	layout->addLayout(compareRow);
	m_diff = new QTextBrowser(this);
	m_diff->setMinimumHeight(130);
	layout->addWidget(m_diff, 1);
	connect(m_compareMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { showDifferences(); });

	auto countRow = new QHBoxLayout;
	countRow->addWidget(new QLabel(tr("Anzahl behalten:"), this));
	m_count = new QSpinBox(this);
	m_count->setRange(1, 500);
	m_count->setValue(Song::maxSnapshots());
	countRow->addWidget(m_count);
	countRow->addStretch();
	layout->addLayout(countRow);
	connect(m_count, qOverload<int>(&QSpinBox::valueChanged), this, [](int value) {
		ConfigManager::inst()->setValue("app", "snapshotcount", QString::number(value));
	});

	auto buttons = new QDialogButtonBox(this);
	m_restore = buttons->addButton(tr("Wiederherstellen"), QDialogButtonBox::AcceptRole);
	auto openFolder = buttons->addButton(tr("Ordner \xc3\xb6\x66\x66nen"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_restore, &QPushButton::clicked, this, &SnapshotDialog::restoreSelected);
	connect(m_list, &QTreeWidget::itemDoubleClicked, this, &SnapshotDialog::restoreSelected);
	connect(openFolder, &QPushButton::clicked, this, [this] {
		const QString dir = Song::snapshotDirectory(m_projectFile);
		QDir().mkpath(dir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	openFolder->setEnabled(!m_projectFile.isEmpty());
	connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
		m_restore->setEnabled(!m_list->selectedItems().isEmpty());
		showDifferences();
	});

	refresh();
	showDifferences();
}


void SnapshotDialog::refresh()
{
	m_list->clear();
	m_restore->setEnabled(false);
	if (m_projectFile.isEmpty()) { return; }
	const QDir dir(Song::snapshotDirectory(m_projectFile));
	for (const auto& info : dir.entryInfoList({"*.mmp", "*.mmpz"}, QDir::Files, QDir::Name | QDir::Reversed))
	{
		auto when = QDateTime::fromString(info.completeBaseName().left(19), "yyyy-MM-dd_HH-mm-ss");
		if (!when.isValid()) { when = info.lastModified(); }
		auto item = new QTreeWidgetItem(m_list, {
			when.toString("dd.MM.yyyy  HH:mm:ss") + snapshotNote(info.completeBaseName()),
			relativeTime(when),
			QString("%1 KB").arg((info.size() + 1023) / 1024)});
		item->setData(0, Qt::UserRole, info.absoluteFilePath());
	}
	if (m_list->topLevelItemCount() == 0)
	{
		auto item = new QTreeWidgetItem(m_list, {tr("Noch keine Snapshots vorhanden")});
		item->setFlags(Qt::NoItemFlags);
	}
}


const QDomDocument* SnapshotDialog::currentState()
{
	if (!m_current)
	{
		// the current (maybe unsaved) state, written to a temporary file and read back
		QTemporaryDir dir;
		if (!dir.isValid()) { return nullptr; }
		const QString path = dir.filePath("current.mmp");
		if (!Engine::getSong()->saveProjectFile(path)) { return nullptr; }
		m_current = std::make_unique<QDomDocument>(DataFile(path));
	}
	return m_current.get();
}


void SnapshotDialog::showDifferences()
{
	const auto items = m_list->selectedItems();
	if (items.isEmpty())
	{
		m_diff->setHtml("<i>" + tr("Einen Snapshot ausw\xc3\xa4hlen, um zu sehen, was sich ge\xc3\xa4ndert hat.") + "</i>");
		return;
	}
	const QString file = items.first()->data(0, Qt::UserRole).toString();
	if (file.isEmpty()) { return; }
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const DataFile snapshot(file);
	QStringList lines;
	QString heading;
	if (m_compareMode->currentIndex() == 0)
	{
		heading = tr("Seit dieser Version ge\xc3\xa4ndert:");
		if (const auto current = currentState()) { lines = compareProjects(snapshot, *current); }
	}
	else
	{
		const int index = m_list->indexOfTopLevelItem(items.first());
		const auto older = m_list->topLevelItem(index + 1);
		const QString olderFile = older ? older->data(0, Qt::UserRole).toString() : QString();
		if (olderFile.isEmpty())
		{
			QApplication::restoreOverrideCursor();
			m_diff->setHtml("<i>" + tr("Das ist der \xc3\xa4lteste Snapshot, es gibt keine Version davor.") + "</i>");
			return;
		}
		heading = tr("In dieser Version ge\xc3\xa4ndert:");
		lines = compareProjects(DataFile(olderFile), snapshot);
	}
	QApplication::restoreOverrideCursor();
	if (lines.isEmpty())
	{
		m_diff->setHtml("<i>" + tr("Keine Unterschiede gefunden.") + "</i>");
		return;
	}
	m_diff->setHtml("<b>" + heading + "</b><ul><li>" + lines.join("</li><li>") + "</li></ul>");
}


void SnapshotDialog::restoreSelected()
{
	const auto items = m_list->selectedItems();
	if (items.isEmpty()) { return; }
	const QString snapshot = items.first()->data(0, Qt::UserRole).toString();
	if (snapshot.isEmpty() || !QFileInfo::exists(snapshot)) { return; }

	auto song = Engine::getSong();
	if (QMessageBox::question(this, tr("Snapshot wiederherstellen"),
		tr("Version vom %1 laden?\n\nDer aktuelle Stand wird vorher selbst als Snapshot gesichert, "
			"es geht also nichts verloren.").arg(items.first()->text(0).trimmed()))
		!= QMessageBox::Yes) { return; }

	// keep the current state (also unsaved changes) as a snapshot first
	song->snapshotCurrentState("vor-wiederherstellung");

	// load through a temporary copy next to the project so relative ("local:") paths still resolve
	const QString temp = QFileInfo(m_projectFile).absolutePath() + "/.pth-lmms-snapshot." + QFileInfo(snapshot).suffix();
	QFile::remove(temp);
	if (!QFile::copy(snapshot, temp))
	{
		QMessageBox::warning(this, tr("Snapshot wiederherstellen"),
			tr("Die tempor\xc3\xa4re Datei %1 konnte nicht angelegt werden.").arg(temp));
		return;
	}
	accept();
	song->reloadFromRewrittenFile(temp, m_projectFile);
	QFile::remove(temp);
}

} // namespace lmms::gui
