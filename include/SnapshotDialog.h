/*
 * SnapshotDialog.h - list and restore the automatic versions of the project (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_SNAPSHOT_DIALOG_H
#define LMMS_GUI_SNAPSHOT_DIALOG_H

#include <QDialog>

class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace lmms::gui
{

class SnapshotDialog : public QDialog
{
public:
	explicit SnapshotDialog(QWidget* parent);

private:
	void refresh();
	void restoreSelected();

	QString m_projectFile;
	QTreeWidget* m_list;
	QPushButton* m_restore;
	QSpinBox* m_count;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SNAPSHOT_DIALOG_H
