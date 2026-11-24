/*
 * ProjectFilesDialog.cpp - overview and relinking of all files used by the project (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ProjectFilesDialog.h"

#include <map>
#include <set>

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "Engine.h"
#include "PathUtil.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

const std::set<QString>& fileExtensions()
{
	static const std::set<QString> extensions = {
		"wav", "ogg", "flac", "mp3", "aif", "aiff", "aifc", "au", "voc", "raw", "ds", "spx", "wv", "opus",
		"sf2", "sf3", "gig", "pat", "xiz", "xpf", "dll", "so", "vst3", "lv2", "clap", "mid", "midi"
	};
	return extensions;
}

bool looksLikeFile(const QString& attributeName, const QString& value)
{
	if (value.isEmpty() || value.size() > 1024 || attributeName == "name") { return false; }
	const QString stripped = PathUtil::stripPrefix(value);
	if (fileExtensions().count(QFileInfo(stripped).suffix().toLower()) == 0) { return false; }
	return value.contains('/') || value.contains('\\')
		|| PathUtil::baseLookup(value) != PathUtil::Base::Absolute
		|| QFileInfo(value).isAbsolute();
}

QString usedByText(const QDomElement& element)
{
	QString plugin = element.tagName();
	for (QDomNode node = element; !node.isNull(); node = node.parentNode())
	{
		const QDomElement e = node.toElement();
		if (e.tagName() == "track")
		{
			return QString("%1  (%2)").arg(e.attribute("name"), plugin);
		}
	}
	return plugin;
}

} // namespace




ProjectFilesDialog::ProjectFilesDialog(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Projektdateien"));
	resize(900, 560);

	auto layout = new QVBoxLayout(this);

	m_summary = new QLabel(this);
	layout->addWidget(m_summary);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(4);
	m_tree->setHeaderLabels({tr("Ordner / Datei"), tr("Status"), tr("Verwendet von"), tr("Neuer Pfad")});
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->header()->setStretchLastSection(true);
	m_tree->setColumnWidth(0, 330);
	m_tree->setColumnWidth(1, 90);
	m_tree->setColumnWidth(2, 200);
	layout->addWidget(m_tree, 1);

	m_relative = new QCheckBox(tr("Dateien im Projektordner relativ zum Projekt speichern "
		"(Projektordner kann dann samt Samples verschoben werden)"), this);
	m_relative->setEnabled(!Engine::getSong()->projectFileName().isEmpty());
	layout->addWidget(m_relative);

	auto buttons = new QHBoxLayout;
	m_replaceButton = new QPushButton(tr("Ordner ersetzen..."), this);
	m_replaceButton->setToolTip(tr("Alle Dateien aus dem gew\xc3\xa4hlten Ordner (inkl. Unterordner) "
		"an einem neuen Ort suchen, z.B. C:/Example/1/Samples \xe2\x86\x92 C:/Example/2/Samples"));
	m_singleButton = new QPushButton(tr("Datei \xc3\xa4ndern..."), this);
	auto searchButton = new QPushButton(tr("Fehlende suchen in..."), this);
	searchButton->setToolTip(tr("Einen Ordner (mit Unterordnern) nach fehlenden Dateien mit gleichem Namen durchsuchen"));
	m_applyButton = new QPushButton(tr("\xc3\x9c""bernehmen"), this);
	auto closeButton = new QPushButton(tr("Schlie\xc3\x9f""en"), this);
	buttons->addWidget(m_replaceButton);
	buttons->addWidget(m_singleButton);
	buttons->addWidget(searchButton);
	buttons->addStretch(1);
	buttons->addWidget(m_applyButton);
	buttons->addWidget(closeButton);
	layout->addLayout(buttons);

	connect(m_replaceButton, &QPushButton::clicked, this, &ProjectFilesDialog::replaceFolder);
	connect(m_singleButton, &QPushButton::clicked, this, &ProjectFilesDialog::changeSingleFile);
	connect(searchButton, &QPushButton::clicked, this, &ProjectFilesDialog::searchMissing);
	connect(m_applyButton, &QPushButton::clicked, this, &ProjectFilesDialog::apply);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ProjectFilesDialog::updateButtons);
	connect(m_relative, &QCheckBox::toggled, this, &ProjectFilesDialog::updateButtons);

	collect();
	rebuildTree();
	updateButtons();
}




void ProjectFilesDialog::collect()
{
	Song* song = Engine::getSong();
	const QString projectFile = song->projectFileName();
	m_tempFile = projectFile.isEmpty()
		? ConfigManager::inst()->workingDir() + "pth-lmms-projectfiles.mmp"
		: QFileInfo(projectFile).absolutePath() + "/.pth-lmms-projectfiles.mmp";

	// serialize the current state of the project (also unsaved changes)
	song->saveProjectFile(m_tempFile);
	QFile file(m_tempFile);
	if (!file.open(QIODevice::ReadOnly) || !m_doc.setContent(&file))
	{
		m_summary->setText(tr("Das Projekt konnte nicht gelesen werden."));
		return;
	}
	file.close();
	QFile::remove(m_tempFile);

	// walk all elements and collect attributes that point to files
	std::vector<QDomElement> stack{m_doc.documentElement()};
	while (!stack.empty())
	{
		const QDomElement element = stack.back();
		stack.pop_back();
		const QDomNamedNodeMap attributes = element.attributes();
		for (int i = 0; i < attributes.count(); ++i)
		{
			QDomAttr attribute = attributes.item(i).toAttr();
			if (!looksLikeFile(attribute.name(), attribute.value())) { continue; }
			FileRef ref;
			ref.attribute = attribute;
			ref.original = attribute.value();
			ref.absolute = QDir::cleanPath(PathUtil::toAbsolute(attribute.value()));
			ref.usedBy = usedByText(element);
			m_refs.push_back(ref);
		}
		for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
		{
			stack.push_back(child);
		}
	}
}




QString ProjectFilesDialog::currentAbsolute(const FileRef& ref) const
{
	return ref.newAbsolute.isEmpty() ? ref.absolute : ref.newAbsolute;
}




void ProjectFilesDialog::rebuildTree()
{
	// remember the selection
	const QString selected = selectedFolder();

	m_tree->clear();
	std::map<QString, std::vector<size_t>> byFolder;
	for (size_t i = 0; i < m_refs.size(); ++i)
	{
		byFolder[QFileInfo(currentAbsolute(m_refs[i])).absolutePath()].push_back(i);
	}

	int missingTotal = 0;
	int changedTotal = 0;
	const QColor missingColor(240, 90, 90);
	const QColor changedColor(110, 210, 130);
	for (const auto& [folder, indices] : byFolder)
	{
		auto folderItem = new QTreeWidgetItem(m_tree);
		folderItem->setData(0, Qt::UserRole, folder);
		int missing = 0;
		for (const size_t index : indices)
		{
			const auto& ref = m_refs[index];
			const QString path = currentAbsolute(ref);
			const bool exists = QFileInfo::exists(path);
			const bool changed = !ref.newAbsolute.isEmpty();
			if (!exists) { ++missing; }
			if (changed) { ++changedTotal; }

			auto item = new QTreeWidgetItem(folderItem);
			item->setData(0, Qt::UserRole + 1, static_cast<qulonglong>(index));
			item->setText(0, QFileInfo(path).fileName());
			item->setText(1, exists ? tr("vorhanden") : tr("fehlt"));
			item->setText(2, ref.usedBy);
			item->setText(3, changed ? path : QString());
			item->setToolTip(0, path);
			if (!exists) { item->setForeground(1, missingColor); item->setForeground(0, missingColor); }
			else if (changed) { item->setForeground(1, changedColor); item->setForeground(3, changedColor); }
		}
		missingTotal += missing;
		folderItem->setText(0, QDir::toNativeSeparators(folder));
		folderItem->setText(1, missing > 0 ? tr("%1 fehlen").arg(missing) : tr("ok"));
		folderItem->setText(2, tr("%n Datei(en)", "", static_cast<int>(indices.size())));
		if (missing > 0) { folderItem->setForeground(1, missingColor); }
		folderItem->setExpanded(missing > 0 || byFolder.size() <= 3);
		if (folder == selected) { folderItem->setSelected(true); }
	}

	m_summary->setText(tr("%1 Dateien in %2 Ordnern \xe2\x80\x93 %3 fehlen, %4 ge\xc3\xa4ndert")
		.arg(m_refs.size()).arg(byFolder.size()).arg(missingTotal).arg(changedTotal));
}




QString ProjectFilesDialog::selectedFolder() const
{
	const auto items = m_tree->selectedItems();
	if (items.isEmpty()) { return QString(); }
	auto item = items.front();
	if (item->parent() != nullptr) { item = item->parent(); }
	return item->data(0, Qt::UserRole).toString();
}




void ProjectFilesDialog::updateButtons()
{
	const auto items = m_tree->selectedItems();
	m_replaceButton->setEnabled(!items.isEmpty());
	m_singleButton->setEnabled(!items.isEmpty() && items.front()->parent() != nullptr);
	bool changed = m_relative->isChecked();
	for (const auto& ref : m_refs) { changed = changed || !ref.newAbsolute.isEmpty(); }
	m_applyButton->setEnabled(changed);
}




void ProjectFilesDialog::replaceFolder()
{
	const QString folder = selectedFolder();
	if (folder.isEmpty()) { return; }
	const QString newFolder = QFileDialog::getExistingDirectory(this,
		tr("Neuer Ort f\xc3\xbcr %1").arg(QDir::toNativeSeparators(folder)), folder);
	if (newFolder.isEmpty()) { return; }

	const QDir oldDir(folder);
	for (auto& ref : m_refs)
	{
		const QString path = currentAbsolute(ref);
		if (path != folder && !path.startsWith(folder + "/")) { continue; }
		ref.newAbsolute = QDir::cleanPath(QDir(newFolder).filePath(oldDir.relativeFilePath(path)));
		if (ref.newAbsolute == ref.absolute) { ref.newAbsolute.clear(); }
	}
	rebuildTree();
	updateButtons();
}




void ProjectFilesDialog::changeSingleFile()
{
	const auto items = m_tree->selectedItems();
	if (items.isEmpty() || items.front()->parent() == nullptr) { return; }
	auto& ref = m_refs[items.front()->data(0, Qt::UserRole + 1).toULongLong()];
	const QString path = currentAbsolute(ref);
	const QString file = QFileDialog::getOpenFileName(this, tr("Datei w\xc3\xa4hlen"), QFileInfo(path).absolutePath());
	if (file.isEmpty()) { return; }
	ref.newAbsolute = QDir::cleanPath(file) == ref.absolute ? QString() : QDir::cleanPath(file);
	rebuildTree();
	updateButtons();
}




void ProjectFilesDialog::searchMissing()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Fehlende Dateien suchen in..."));
	if (dir.isEmpty()) { return; }

	std::map<QString, QString> byName;
	QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
	int scanned = 0;
	while (it.hasNext() && scanned < 300000)
	{
		const QString path = it.next();
		byName.emplace(QFileInfo(path).fileName().toLower(), path);
		++scanned;
	}

	int found = 0;
	for (auto& ref : m_refs)
	{
		if (QFileInfo::exists(currentAbsolute(ref))) { continue; }
		const auto match = byName.find(QFileInfo(ref.absolute).fileName().toLower());
		if (match != byName.end())
		{
			ref.newAbsolute = QDir::cleanPath(match->second);
			++found;
		}
	}
	rebuildTree();
	updateButtons();
	QMessageBox::information(this, tr("Fehlende Dateien"), tr("%1 fehlende Datei(en) gefunden.").arg(found));
}




void ProjectFilesDialog::apply()
{
	Song* song = Engine::getSong();
	const QString projectFile = song->projectFileName();
	const QString projectDir = projectFile.isEmpty() ? QString() : QFileInfo(projectFile).absolutePath();
	const bool relative = m_relative->isChecked() && !projectDir.isEmpty();

	int changes = 0;
	for (auto& ref : m_refs)
	{
		const QString path = currentAbsolute(ref);
		QString value = ref.original;
		if (relative && path.startsWith(projectDir + "/"))
		{
			value = PathUtil::basePrefix(PathUtil::Base::LocalDir) + QDir(projectDir).relativeFilePath(path);
		}
		else if (!ref.newAbsolute.isEmpty())
		{
			value = PathUtil::toShortestRelative(ref.newAbsolute);
		}
		if (value != ref.original)
		{
			ref.attribute.setValue(value);
			++changes;
		}
	}
	if (changes == 0) { accept(); return; }

	if (QMessageBox::question(this, tr("Projektdateien"),
		tr("%1 Pfad(e) werden ge\xc3\xa4ndert und das Projekt mit den neuen Pfaden neu geladen.\n"
			"Der R\xc3\xbc""ckg\xc3\xa4ngig-Verlauf wird dabei geleert. Fortfahren?").arg(changes))
		!= QMessageBox::Yes)
	{
		return;
	}

	QFile file(m_tempFile);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		QMessageBox::warning(this, tr("Projektdateien"), tr("Tempor\xc3\xa4re Datei konnte nicht geschrieben werden."));
		return;
	}
	file.write(m_doc.toByteArray(1)); // UTF-8
	file.close();

	song->reloadFromRewrittenFile(m_tempFile, projectFile);
	QFile::remove(m_tempFile);
	accept();
}

} // namespace lmms::gui
