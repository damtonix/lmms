/*
 * ProjectFilesDialog.h - overview and relinking of all files used by the project (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_PROJECT_FILES_DIALOG_H
#define LMMS_GUI_PROJECT_FILES_DIALOG_H

#include <QDialog>
#include <QDomDocument>
#include <QDomAttr>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace lmms::gui
{

//! Lists every file the project refers to (samples, soundfonts, VST plugins, ...),
//! grouped by folder, shows which ones are missing and lets the user move whole
//! folders, search missing files or store paths relative to the project.
class ProjectFilesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ProjectFilesDialog(QWidget* parent = nullptr);

private slots:
	void replaceFolder();
	void searchMissing();
	void changeSingleFile();
	void apply();
	void updateButtons();

private:
	struct FileRef
	{
		QDomAttr attribute;   //!< where the path is stored in the project XML
		QString original;     //!< value as stored (may carry a prefix like "userSamples:")
		QString absolute;     //!< resolved absolute path
		QString newAbsolute;  //!< new location chosen by the user (empty = unchanged)
		QString usedBy;       //!< track / plugin using the file
	};

	void collect();
	void rebuildTree();
	QString currentAbsolute(const FileRef& ref) const;
	QString selectedFolder() const;

	QDomDocument m_doc;
	QString m_tempFile;
	std::vector<FileRef> m_refs;

	QTreeWidget* m_tree;
	QLabel* m_summary;
	QCheckBox* m_relative;
	QPushButton* m_replaceButton;
	QPushButton* m_singleButton;
	QPushButton* m_applyButton;
};

} // namespace lmms::gui

#endif // LMMS_GUI_PROJECT_FILES_DIALOG_H
