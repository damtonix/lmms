/*
 * ArrangementMarkerBar.h - named section markers (Intro, Drop, ...) above the song editor tracks (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_ARRANGEMENT_MARKER_BAR_H
#define LMMS_GUI_ARRANGEMENT_MARKER_BAR_H

#include <QWidget>

#include "TimePos.h"

namespace lmms::gui
{

class SongEditor;

class ArrangementMarkerBar : public QWidget
{
public:
	ArrangementMarkerBar(SongEditor* editor, int xOffset);

	static QColor defaultColor(int index);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	int tickToX(tick_t tick) const;
	tick_t xToTick(int x) const;
	tick_t snapped(tick_t tick, bool free) const;
	//! index of the marker whose flag is at @p x, or -1
	int markerAtFlag(int x) const;
	//! index of the section containing @p x, or -1
	int sectionAt(int x) const;
	void renameMarker(int index);
	void insertMarker(tick_t tick, const QString& name);
	void safetySnapshot(const QString& reason);

	SongEditor* m_editor;
	int m_xOffset;
	int m_dragIndex = -1;
	int m_pressX = 0;
	bool m_dragged = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGEMENT_MARKER_BAR_H
