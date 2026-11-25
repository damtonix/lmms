/*
 * StemExportDialog.cpp - choose which groups/tracks are exported as stems (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "StemExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "Engine.h"
#include "FileDialog.h"
#include "GuiApplication.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

//! tracks in the order shown in the song editor
std::vector<Track*> tracksInEditorOrder()
{
	std::vector<Track*> result;
	if (getGUI() != nullptr && getGUI()->songEditor() != nullptr)
	{
		for (auto view : getGUI()->songEditor()->m_editor->trackViews()) { result.push_back(view->getTrack()); }
	}
	else
	{
		for (auto track : Engine::getSong()->tracks()) { result.push_back(track); }
	}
	return result;
}

bool producesAudio(const Track* track)
{
	const auto type = track->type();
	return !track->isFreezeProxy()
		&& (type == Track::Type::Instrument || type == Track::Type::Sample || type == Track::Type::Pattern);
}

} // namespace


StemExportDialog::StemExportDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Stems exportieren"));
	resize(460, 520);
	auto layout = new QVBoxLayout(this);

	auto intro = new QLabel(tr("Jede ausgew\xc3\xa4hlte Gruppe bzw. Spur wird einzeln als WAV gerendert "
		"(alle anderen stumm). Alle Dateien haben dieselbe L\xc3\xa4nge und lassen sich in jeder DAW "
		"bei 0 anlegen."), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto form = new QFormLayout;
	m_mode = new QComboBox(this);
	m_mode->addItems({tr("Pro Gruppe (Spuren ohne Gruppe einzeln)"), tr("Jede Spur einzeln")});
	form->addRow(tr("Aufteilung:"), m_mode);
	layout->addLayout(form);

	m_list = new QListWidget(this);
	layout->addWidget(m_list, 1);

	auto selectRow = new QHBoxLayout;
	auto all = new QPushButton(tr("Alle"), this);
	auto none = new QPushButton(tr("Keine"), this);
	selectRow->addWidget(all);
	selectRow->addWidget(none);
	selectRow->addStretch();
	layout->addLayout(selectRow);
	connect(all, &QPushButton::clicked, this, [this] {
		for (int i = 0; i < m_list->count(); ++i) { m_list->item(i)->setCheckState(Qt::Checked); }
	});
	connect(none, &QPushButton::clicked, this, [this] {
		for (int i = 0; i < m_list->count(); ++i) { m_list->item(i)->setCheckState(Qt::Unchecked); }
	});

	// target folder: <project folder>/<project> Stems
	const auto song = Engine::getSong();
	const QString projectFile = song->projectFileName();
	const QString projectName = projectFile.isEmpty() ? tr("Projekt") : QFileInfo(projectFile).completeBaseName();
	QString baseDir = projectFile.isEmpty() ? ConfigManager::inst()->value("app", "quickexportdir")
		: QFileInfo(projectFile).absolutePath();
	if (baseDir.isEmpty() || !QFileInfo(baseDir).isDir()) { baseDir = ConfigManager::inst()->userSamplesDir(); }

	auto dirRow = new QHBoxLayout;
	m_dir = new QLineEdit(QDir(baseDir).filePath(projectName + " Stems"), this);
	auto browse = new QPushButton(tr("..."), this);
	browse->setFixedWidth(32);
	dirRow->addWidget(m_dir, 1);
	dirRow->addWidget(browse);
	auto form2 = new QFormLayout;
	form2->addRow(tr("Ordner:"), dirRow);
	m_prefix = new QLineEdit(projectName + " - ", this);
	form2->addRow(tr("Dateiname-Anfang:"), m_prefix);
	layout->addLayout(form2);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString dir = FileDialog::getExistingDirectory(this, tr("Ordner f\xc3\xbcr die Stems"), m_dir->text());
		if (!dir.isEmpty()) { m_dir->setText(dir); }
	});

	m_masterFx = new QCheckBox(tr("Master-Effekte und Master-Lautst\xc3\xa4rke anwenden"), this);
	m_masterFx->setToolTip(tr("Aus: Stems ohne Master-Bus-Bearbeitung (zum Mischen/Mastern woanders)."));
	layout->addWidget(m_masterFx);
	m_loop = new QCheckBox(tr("Nur den Loop-Bereich exportieren"), this);
	m_loop->setEnabled(song->getTimeline(Song::PlayMode::Song).loopEnabled());
	layout->addWidget(m_loop);

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Exportieren"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &StemExportDialog::rebuildList);
	rebuildList();
}


void StemExportDialog::rebuildList()
{
	m_stems.clear();
	m_list->clear();
	const bool perGroup = m_mode->currentIndex() == 0;
	const auto tracks = tracksInEditorOrder();

	for (auto track : tracks)
	{
		if (perGroup)
		{
			if (track->parentTrack() != nullptr) { continue; }
			if (track->type() == Track::Type::Group)
			{
				// only groups that contain something audible
				bool hasAudio = false;
				for (auto other : tracks)
				{
					if (!producesAudio(other)) { continue; }
					int guard = 0;
					for (auto p = other->parentTrack(); p != nullptr && guard < 8; p = p->parentTrack(), ++guard)
					{
						if (p == track) { hasAudio = true; break; }
					}
					if (hasAudio) { break; }
				}
				if (!hasAudio) { continue; }
			}
			else if (!producesAudio(track)) { continue; }
		}
		else if (!producesAudio(track)) { continue; }

		m_stems.push_back({track->name(), {track}});
		QString label = track->name();
		if (track->type() == Track::Type::Group) { label = tr("%1  (Gruppe)").arg(label); }
		if (track->isMutedSelf()) { label += tr("  [stumm]"); }
		auto item = new QListWidgetItem(label, m_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(track->isMutedSelf() ? Qt::Unchecked : Qt::Checked);
	}
}


std::vector<StemExportDialog::Stem> StemExportDialog::selectedStems() const
{
	std::vector<Stem> result;
	for (int i = 0; i < m_list->count() && i < static_cast<int>(m_stems.size()); ++i)
	{
		if (m_list->item(i)->checkState() == Qt::Checked) { result.push_back(m_stems[i]); }
	}
	return result;
}

QString StemExportDialog::targetDirectory() const { return QDir::cleanPath(m_dir->text()); }
QString StemExportDialog::filePrefix() const { return m_prefix->text(); }
bool StemExportDialog::withMasterEffects() const { return m_masterFx->isChecked(); }
bool StemExportDialog::onlyLoop() const { return m_loop->isEnabled() && m_loop->isChecked(); }

} // namespace lmms::gui
