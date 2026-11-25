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
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "ConfigManager.h"
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

} // namespace


SnapshotDialog::SnapshotDialog(QWidget* parent)
	: QDialog(parent)
	, m_projectFile(Engine::getSong()->projectFileName())
{
	setWindowTitle(tr("Snapshots"));
	resize(640, 440);
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
	});

	refresh();
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
