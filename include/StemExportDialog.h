/*
 * StemExportDialog.h - choose which groups/tracks are exported as stems (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_STEM_EXPORT_DIALOG_H
#define LMMS_GUI_STEM_EXPORT_DIALOG_H

#include <QDialog>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

class StemExportDialog : public QDialog
{
public:
	struct Stem
	{
		QString name;
		std::vector<Track*> tracks;
	};

	explicit StemExportDialog(QWidget* parent);

	std::vector<Stem> selectedStems() const;
	QString targetDirectory() const;
	QString filePrefix() const;
	bool withMasterEffects() const;
	bool onlyLoop() const;

private:
	void rebuildList();

	std::vector<Stem> m_stems;
	QComboBox* m_mode;
	QListWidget* m_list;
	QLineEdit* m_dir;
	QLineEdit* m_prefix;
	QCheckBox* m_masterFx;
	QCheckBox* m_loop;
};

} // namespace lmms::gui

#endif // LMMS_GUI_STEM_EXPORT_DIALOG_H
