/*
 * ArrangementMarkerBar.cpp - named section markers (Intro, Drop, ...) above the song editor tracks (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ArrangementMarkerBar.h"

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>

#include "ColorChooser.h"
#include "Engine.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"

namespace lmms::gui
{

namespace
{
constexpr int BarHeight = 20;
constexpr int FlagPadding = 5;

QStringList presetNames()
{
	return {"Intro", "Build-up", "Drop", "Break", "Outro"};
}
}


ArrangementMarkerBar::ArrangementMarkerBar(SongEditor* editor, int xOffset)
	: QWidget(editor)
	, m_editor(editor)
	, m_xOffset(xOffset)
{
	setFixedHeight(BarHeight);
	setMouseTracking(true);
	setToolTip(tr("Abschnitte: Doppelklick = Marker setzen/umbenennen, Klick = hinspringen, "
		"Ziehen = Marker verschieben, Rechtsklick = Abschnitt duplizieren, tauschen, l\xc3\xb6schen ..."));

	auto song = Engine::getSong();
	connect(song, &Song::arrangementMarkersChanged, this, qOverload<>(&QWidget::update));
	connect(song, &Song::lengthChanged, this, qOverload<>(&QWidget::update));
	connect(editor, &TrackContainerView::positionChanged, this, qOverload<>(&QWidget::update));
	connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
	connect(&song->getTimeline(Song::PlayMode::Song), &Timeline::positionChanged, this, qOverload<>(&QWidget::update));
}


QColor ArrangementMarkerBar::defaultColor(int index)
{
	static const QColor colors[] = {
		QColor(0x5b, 0x8d, 0xb8), QColor(0xb8, 0x7a, 0x4b), QColor(0x7c, 0xa0, 0x5a), QColor(0x9b, 0x6b, 0xb0),
		QColor(0xb0, 0x5a, 0x6a), QColor(0x4f, 0xa3, 0x9a), QColor(0xa8, 0x9a, 0x4a), QColor(0x6a, 0x74, 0xb8)};
	return colors[static_cast<unsigned>(index) % (sizeof(colors) / sizeof(colors[0]))];
}


int ArrangementMarkerBar::tickToX(tick_t tick) const
{
	return m_xOffset + static_cast<int>((tick - m_editor->currentPosition().getTicks())
		* m_editor->pixelsPerBar() / TimePos::ticksPerBar());
}

tick_t ArrangementMarkerBar::xToTick(int x) const
{
	return m_editor->currentPosition().getTicks()
		+ static_cast<tick_t>((std::max(x, m_xOffset) - m_xOffset) * TimePos::ticksPerBar() / m_editor->pixelsPerBar());
}

tick_t ArrangementMarkerBar::snapped(tick_t tick, bool free) const
{
	if (free) { return std::max<tick_t>(0, tick); }
	return std::max<tick_t>(0, TimePos(tick).quantize(std::max(1.f, m_editor->getSnapSize())).getTicks());
}


int ArrangementMarkerBar::markerAtFlag(int x) const
{
	const auto& markers = Engine::getSong()->arrangementMarkers();
	const QFontMetrics fm(font());
	// later markers are drawn on top
	for (int i = static_cast<int>(markers.size()) - 1; i >= 0; --i)
	{
		const int mx = tickToX(markers[i].tick);
		const int w = fm.horizontalAdvance(markers[i].name) + 2 * FlagPadding;
		if (x >= mx - 3 && x <= mx + w) { return i; }
	}
	return -1;
}

int ArrangementMarkerBar::sectionAt(int x) const
{
	const auto song = Engine::getSong();
	const auto& markers = song->arrangementMarkers();
	const tick_t tick = xToTick(x);
	for (int i = static_cast<int>(markers.size()) - 1; i >= 0; --i)
	{
		if (markers[i].tick <= tick) { return tick < song->sectionEnd(i) ? i : -1; }
	}
	return -1;
}


void ArrangementMarkerBar::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(32, 35, 40));
	p.setClipRect(m_xOffset, 0, width() - m_xOffset, height());

	const auto song = Engine::getSong();
	const auto& markers = song->arrangementMarkers();
	const tick_t playPos = song->getTimeline(Song::PlayMode::Song).pos().getTicks();
	const QFontMetrics fm(font());

	if (markers.empty())
	{
		p.setPen(QColor(110, 116, 125));
		p.drawText(rect().adjusted(m_xOffset + 6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
			tr("Doppelklick: Abschnitts-Marker setzen (Intro, Drop, ...)"));
	}

	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		const auto& marker = markers[i];
		const QColor color = marker.color.isValid() ? marker.color : defaultColor(static_cast<int>(i));
		const int x1 = tickToX(marker.tick);
		const int x2 = tickToX(song->sectionEnd(i));
		const bool current = playPos >= marker.tick && playPos < song->sectionEnd(i);

		// section band
		QColor band = color;
		band.setAlpha(current ? 110 : 60);
		p.fillRect(QRect(x1, 2, x2 - x1, height() - 4), band);

		// flag with the name
		const int w = fm.horizontalAdvance(marker.name) + 2 * FlagPadding;
		p.fillRect(QRect(x1, 2, std::min(w, std::max(x2 - x1, 8)), height() - 4), color);
		p.fillRect(QRect(x1, 0, 2, height()), color.lighter(140));
		p.setPen(Qt::white);
		p.drawText(QRect(x1 + FlagPadding, 0, std::max(0, std::min(w, x2 - x1) - FlagPadding), height()),
			Qt::AlignVCenter | Qt::AlignLeft, marker.name);
	}
	p.setPen(QColor(20, 22, 26));
	p.drawLine(0, height() - 1, width(), height() - 1);
}


void ArrangementMarkerBar::mousePressEvent(QMouseEvent* event)
{
	m_dragIndex = -1;
	m_dragged = false;
	if (event->button() != Qt::LeftButton || event->x() < m_xOffset) { return; }
	m_dragIndex = markerAtFlag(event->x());
	m_pressX = event->x();
}

void ArrangementMarkerBar::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragIndex < 0 || !(event->buttons() & Qt::LeftButton))
	{
		setCursor(event->x() >= m_xOffset && markerAtFlag(event->x()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		return;
	}
	if (!m_dragged && std::abs(event->x() - m_pressX) < 4) { return; }
	m_dragged = true;
	setCursor(Qt::SizeHorCursor);

	auto song = Engine::getSong();
	auto markers = song->arrangementMarkers();
	if (m_dragIndex >= static_cast<int>(markers.size())) { return; }
	const tick_t tick = snapped(xToTick(event->x()), event->modifiers() & Qt::ControlModifier);
	if (markers[m_dragIndex].tick == tick) { return; }
	markers[m_dragIndex].tick = tick;
	// keep the index of the dragged marker after sorting
	const auto dragged = markers[m_dragIndex];
	song->setArrangementMarkers(markers);
	const auto& sorted = song->arrangementMarkers();
	for (std::size_t i = 0; i < sorted.size(); ++i)
	{
		if (sorted[i].tick == dragged.tick && sorted[i].name == dragged.name) { m_dragIndex = static_cast<int>(i); break; }
	}
	song->setModified();
}

void ArrangementMarkerBar::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && m_dragIndex >= 0 && !m_dragged)
	{
		// click on a flag: jump there
		const auto& markers = Engine::getSong()->arrangementMarkers();
		if (m_dragIndex < static_cast<int>(markers.size()))
		{
			auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
			timeline.setTicks(markers[m_dragIndex].tick);
			if (!Engine::getSong()->isPlaying())
			{
				Engine::getSong()->getTimeline(Song::PlayMode::None).setTicks(markers[m_dragIndex].tick);
			}
		}
	}
	m_dragIndex = -1;
	m_dragged = false;
	setCursor(Qt::ArrowCursor);
}

void ArrangementMarkerBar::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || event->x() < m_xOffset) { return; }
	const int flag = markerAtFlag(event->x());
	if (flag >= 0) { renameMarker(flag); return; }
	insertMarker(snapped(xToTick(event->x()), event->modifiers() & Qt::ControlModifier), QString());
}


void ArrangementMarkerBar::insertMarker(tick_t tick, const QString& name)
{
	auto song = Engine::getSong();
	QString finalName = name;
	if (finalName.isEmpty())
	{
		bool ok = false;
		finalName = QInputDialog::getItem(this, tr("Abschnitts-Marker"),
			tr("Name des Abschnitts ab Takt %1:").arg(tick / TimePos::ticksPerBar() + 1),
			presetNames(), 0, true, &ok).trimmed();
		if (!ok || finalName.isEmpty()) { return; }
	}
	song->addArrangementMarker(tick, finalName, QColor());
	// assign a fixed color so it stays when other markers are added
	auto markers = song->arrangementMarkers();
	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		if (!markers[i].color.isValid()) { markers[i].color = defaultColor(static_cast<int>(i)); }
	}
	song->setArrangementMarkers(markers);
}


void ArrangementMarkerBar::renameMarker(int index)
{
	auto song = Engine::getSong();
	auto markers = song->arrangementMarkers();
	if (index < 0 || index >= static_cast<int>(markers.size())) { return; }
	QStringList names = presetNames();
	names.removeAll(markers[index].name);
	names.prepend(markers[index].name);
	bool ok = false;
	const QString name = QInputDialog::getItem(this, tr("Marker umbenennen"), tr("Name:"),
		names, 0, true, &ok).trimmed();
	if (!ok || name.isEmpty() || name == markers[index].name) { return; }
	markers[index].name = name;
	song->setArrangementMarkers(markers);
	song->setModified();
}


void ArrangementMarkerBar::safetySnapshot(const QString& reason)
{
	Engine::getSong()->snapshotCurrentState(reason);
}


void ArrangementMarkerBar::contextMenuEvent(QContextMenuEvent* event)
{
	if (event->x() < m_xOffset) { return; }
	auto song = Engine::getSong();
	const auto& markers = song->arrangementMarkers();
	int index = markerAtFlag(event->x());
	if (index < 0) { index = sectionAt(event->x()); }
	const tick_t clickTick = snapped(xToTick(event->x()), QGuiApplication::keyboardModifiers() & Qt::ControlModifier);

	QMenu menu(this);
	if (index >= 0 && index < static_cast<int>(markers.size()))
	{
		const auto marker = markers[index];
		const tick_t begin = marker.tick;
		const tick_t end = song->sectionEnd(index);
		const int bars = (end - begin + TimePos::ticksPerBar() - 1) / TimePos::ticksPerBar();
		auto title = menu.addAction(tr("\"%1\"  (Takt %2, %3 Takte)").arg(marker.name)
			.arg(begin / TimePos::ticksPerBar() + 1).arg(bars));
		title->setEnabled(false);
		menu.addSeparator();

		menu.addAction(tr("Hinspringen"), this, [begin] {
			Engine::getSong()->getTimeline(Song::PlayMode::Song).setTicks(begin);
			if (!Engine::getSong()->isPlaying()) { Engine::getSong()->getTimeline(Song::PlayMode::None).setTicks(begin); }
		});
		menu.addAction(tr("Loop auf diesen Abschnitt setzen"), this, [begin, end] {
			auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
			timeline.setLoopPoints(TimePos(begin), TimePos(end));
			timeline.setLoopEnabled(true);
		});
		menu.addSeparator();
		menu.addAction(tr("Umbenennen..."), this, [this, index] { renameMarker(index); });
		menu.addAction(tr("Farbe..."), this, [this, index, marker] {
			const QColor color = ColorChooser{this}.withPalette(ColorChooser::Palette::Track)
				->getColor(marker.color.isValid() ? marker.color : defaultColor(index));
			if (!color.isValid()) { return; }
			auto song = Engine::getSong();
			auto all = song->arrangementMarkers();
			if (index >= static_cast<int>(all.size())) { return; }
			all[index].color = color;
			song->setArrangementMarkers(all);
			song->setModified();
		});
		menu.addSeparator();
		menu.addAction(embed::getIconPixmap("edit_copy"), tr("Abschnitt duplizieren (dahinter einf\xc3\xbcgen)"),
			this, [this, index] {
				safetySnapshot("vor-Abschnitt-duplizieren");
				Engine::getSong()->duplicateSection(index);
			});
		auto prev = menu.addAction(embed::getIconPixmap("arp_up"), tr("Mit vorherigem Abschnitt tauschen"),
			this, [this, index] {
				safetySnapshot("vor-Abschnitt-tauschen");
				Engine::getSong()->swapSectionWithNext(index - 1);
			});
		prev->setEnabled(index > 0 && markers[index - 1].tick < begin);
		auto next = menu.addAction(embed::getIconPixmap("arp_down"), tr("Mit n\xc3\xa4\x63hstem Abschnitt tauschen"),
			this, [this, index] {
				safetySnapshot("vor-Abschnitt-tauschen");
				Engine::getSong()->swapSectionWithNext(index);
			});
		next->setEnabled(index + 1 < static_cast<int>(markers.size()));
		menu.addAction(embed::getIconPixmap("cancel"), tr("Abschnitt l\xc3\xb6schen (mit allen Clips)..."),
			this, [this, index, marker, bars] {
				if (QMessageBox::question(this, tr("Abschnitt l\xc3\xb6schen"),
					tr("Abschnitt \"%1\" (%2 Takte) mit allen Clips l\xc3\xb6schen? Alles dahinter r\xc3\xbckt nach vorne.\n\n"
						"Der Stand davor wird als Snapshot gesichert.").arg(marker.name).arg(bars)) != QMessageBox::Yes)
				{
					return;
				}
				safetySnapshot("vor-Abschnitt-loeschen");
				Engine::getSong()->deleteSection(index);
			});
		menu.addSeparator();
		menu.addAction(tr("Nur den Marker entfernen"), this, [index] {
			auto song = Engine::getSong();
			auto all = song->arrangementMarkers();
			if (index >= static_cast<int>(all.size())) { return; }
			all.erase(all.begin() + index);
			song->setArrangementMarkers(all);
			song->setModified();
		});
		menu.addSeparator();
	}

	QMenu* insert = menu.addMenu(tr("Marker hier einf\xc3\xbcgen (Takt %1)").arg(clickTick / TimePos::ticksPerBar() + 1));
	for (const auto& name : presetNames())
	{
		insert->addAction(name, this, [this, clickTick, name] { insertMarker(clickTick, name); });
	}
	insert->addSeparator();
	insert->addAction(tr("Eigener Name..."), this, [this, clickTick] { insertMarker(clickTick, QString()); });

	menu.exec(event->globalPos());
}

} // namespace lmms::gui
