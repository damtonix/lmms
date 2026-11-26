/*
 * TrackDrafts.h - "Entwürfe": a folder at the front of each track with clips that are not in the song (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_TRACK_DRAFTS_H
#define LMMS_GUI_TRACK_DRAFTS_H

#include <array>
#include <optional>
#include <vector>

#include <QColor>
#include <QPoint>
#include <QString>
#include <QWidget>

namespace lmms
{
class Track;
}

namespace lmms::gui
{

class ClipView;
class TrackContentWidget;
class TrackDraftsPanel;

//! small tab at the left edge of the track lane, opens the drafts panel
class TrackDraftsTab : public QWidget
{
public:
	TrackDraftsTab(Track* track, TrackDraftsPanel* panel, TrackContentWidget* parent);
	TrackDraftsPanel* panel() const { return m_panel; }
	void updateVisibility();
	void setHovered(bool hovered);
	void setDropHint(bool on);
	static constexpr int Width = 14;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* me) override;
	void dragEnterEvent(QDragEnterEvent* de) override;
	void dropEvent(QDropEvent* de) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	friend class TrackDraftsPanel;
	Track* m_track;
	TrackDraftsPanel* m_panel;
	bool m_hovered = false;
	bool m_dropHint = false;
};


//! the opened folder: one tile per draft, drag tiles out into the song, drag clips in
class TrackDraftsPanel : public QWidget
{
public:
	TrackDraftsPanel(Track* track, TrackContentWidget* parent);

	void toggle();
	void setOpen(bool open);
	bool isOpen() const { return m_open; }
	void relayout();
	Track* track() const { return m_track; }

	//! moves the given clips into the drafts of this track (removes them from the song)
	void absorbClips(const QVector<ClipView*>& clipViews);
	//! the drafts panel/tab of a track under the given global position, if any
	static TrackDraftsPanel* panelAt(const QPoint& globalPos);
	//! highlights the tab/panel under the cursor while clips are moved (nullptr pos = off)
	static void updateDropHint(const QPoint* globalPos);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* me) override;
	void mouseMoveEvent(QMouseEvent* me) override;
	void mouseDoubleClickEvent(QMouseEvent* me) override;
	void wheelEvent(QWheelEvent* we) override;
	void contextMenuEvent(QContextMenuEvent* ce) override;
	void dragEnterEvent(QDragEnterEvent* de) override;
	void dropEvent(QDropEvent* de) override;

private:
	friend class TrackDraftsTab;
	struct Info
	{
		QString name;
		QString kind;                         //!< node name: midiclip, automationclip, sampleclip ...
		std::optional<QColor> color;
		double length = 192;
		std::vector<std::array<double, 3>> notes; //!< pos, len, key
		std::vector<std::pair<double, double>> points; //!< automation: pos, value
	};
	void rebuild();
	int tileAt(const QPoint& pos) const;
	QRect tileRect(int index) const;
	int contentWidth() const;
	void startDrag(int index, bool keep);
	void insertAtPlayPosition(int index, bool keep);
	bool acceptsDrag(const QDropEvent* de) const;
	void storeDropped(QDropEvent* de);

	Track* m_track;
	TrackContentWidget* m_content;
	TrackDraftsTab* m_tab = nullptr;
	std::vector<Info> m_infos;
	bool m_open = false;
	int m_scroll = 0;
	int m_pressed = -1;
	QPoint m_pressPos;
	bool m_pressCtrl = false;
	static constexpr int TileWidth = 78;
	static constexpr int Gap = 4;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_DRAFTS_H
