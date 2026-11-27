/*
 * TrackDrafts.cpp - "Entwürfe": a folder at the front of each track with clips that are not in the song (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "TrackDrafts.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDomDocument>
#include <QDrag>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTextStream>
#include <QWheelEvent>

#include "AudioEngine.h"
#include "Clip.h"
#include "Clipboard.h"
#include "ClipView.h"
#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

const QColor DefaultClipColor(201, 205, 210);

QString clipKey(const Track* track)
{
	return QString("clip_%1").arg(static_cast<int>(track->type()));
}

} // namespace


// ---------------------------------------------------------------- tab

TrackDraftsTab::TrackDraftsTab(Track* track, TrackDraftsPanel* panel, TrackContentWidget* parent)
	: QWidget(parent)
	, m_track(track)
	, m_panel(panel)
{
	panel->m_tab = this;
	setAcceptDrops(true);
	setCursor(Qt::PointingHandCursor);
	connect(track, &Track::draftsChanged, this, [this] {
		updateVisibility();
		update();
	});
	// show the tab while the mouse is over the track (or a clip is dragged in from outside)
	if (auto trackView = parent->parentWidget()) { trackView->installEventFilter(this); }
	updateVisibility();
}


bool TrackDraftsTab::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
		case QEvent::Enter:
		case QEvent::DragEnter:
			setHovered(true);
			break;
		case QEvent::Leave:
		case QEvent::DragLeave:
		case QEvent::Drop:
			setHovered(false);
			break;
		default:
			break;
	}
	return QWidget::eventFilter(watched, event);
}


void TrackDraftsTab::updateVisibility()
{
	const int count = static_cast<int>(m_track->drafts().size());
	setToolTip(tr("Entw\xc3\xbc" "rfe (%1): klicken zum \xc3\x96" "ffnen. Bl\xc3\xb6" "cke hierher ziehen, um sie "
		"als Entwurf abzulegen.").arg(count));
	setVisible(count > 0 || m_hovered || m_dropHint || m_panel->isOpen());
	if (isVisible()) { raise(); }
}


void TrackDraftsTab::setHovered(bool hovered)
{
	if (m_hovered == hovered) { return; }
	m_hovered = hovered;
	updateVisibility();
}


void TrackDraftsTab::setDropHint(bool on)
{
	if (m_dropHint == on) { return; }
	m_dropHint = on;
	updateVisibility();
	update();
	m_panel->update();
}


void TrackDraftsTab::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const bool open = m_panel->isOpen();
	QColor bg = m_dropHint ? QColor(143, 166, 192) : open ? QColor(70, 80, 94) : QColor(36, 40, 46, 235);
	p.fillRect(rect(), bg);
	p.setPen(QColor(20, 22, 26));
	p.drawLine(width() - 1, 0, width() - 1, height());

	// folder symbol
	const QColor fg = m_dropHint ? QColor(20, 22, 26) : QColor(210, 214, 220);
	const qreal x = 2.5, y = 4.5, w = width() - 5.5, h = 7;
	QPainterPath folder;
	folder.moveTo(x, y + h);
	folder.lineTo(x, y);
	folder.lineTo(x + w * 0.4, y);
	folder.lineTo(x + w * 0.5, y + 1.5);
	folder.lineTo(x + w, y + 1.5);
	folder.lineTo(x + w, y + h);
	folder.closeSubpath();
	p.setPen(QPen(fg, 1.1));
	p.setBrush(open ? fg : Qt::NoBrush);
	p.drawPath(folder);

	const int count = static_cast<int>(m_track->drafts().size());
	if (count > 0 && height() >= 26)
	{
		QFont f = font();
		f.setPixelSize(9);
		f.setBold(true);
		p.setFont(f);
		p.setPen(fg);
		p.drawText(QRect(0, 13, width() - 1, height() - 14), Qt::AlignHCenter | Qt::AlignTop, QString::number(count));
	}
}


void TrackDraftsTab::mousePressEvent(QMouseEvent* me)
{
	if (me->button() == Qt::LeftButton) { m_panel->toggle(); }
}


void TrackDraftsTab::dragEnterEvent(QDragEnterEvent* de)
{
	if (m_panel->acceptsDrag(de))
	{
		de->acceptProposedAction();
		m_panel->setOpen(true);
	}
}


void TrackDraftsTab::dropEvent(QDropEvent* de)
{
	m_panel->storeDropped(de);
}


// ---------------------------------------------------------------- panel

TrackDraftsPanel::TrackDraftsPanel(Track* track, TrackContentWidget* parent)
	: QWidget(parent)
	, m_track(track)
	, m_content(parent)
{
	setAcceptDrops(true);
	setMouseTracking(true);
	hide();
	connect(track, &Track::draftsChanged, this, [this] {
		rebuild();
		relayout();
	});
	rebuild();
}


void TrackDraftsPanel::toggle()
{
	setOpen(!m_open);
}


void TrackDraftsPanel::setOpen(bool open)
{
	if (m_open == open) { return; }
	m_open = open;
	relayout();
	if (m_tab) { m_tab->updateVisibility(); m_tab->update(); }
}


int TrackDraftsPanel::contentWidth() const
{
	const int count = static_cast<int>(m_infos.size());
	if (count == 0)
	{
		QFont small = font();
		small.setPixelSize(10);
		return QFontMetrics(small).horizontalAdvance(tr("Keine Entw\xc3\xbc" "rfe: Bl\xc3\xb6" "cke hierher ziehen")) + 18;
	}
	return std::max(150, Gap + count * (TileWidth + Gap));
}


void TrackDraftsPanel::relayout()
{
	if (!m_open)
	{
		hide();
		return;
	}
	const int maxWidth = std::max(240, std::min(480, m_content->width() * 2 / 3));
	const int w = std::min(contentWidth(), maxWidth);
	m_scroll = std::clamp(m_scroll, 0, std::max(0, contentWidth() - w));
	setGeometry(TrackDraftsTab::Width, 0, w, m_content->height());
	show();
	raise();
	if (m_tab) { m_tab->raise(); }
	update();
}


void TrackDraftsPanel::rebuild()
{
	m_infos.clear();
	for (const auto& xml : m_track->drafts())
	{
		Info info;
		QDomDocument doc;
		if (doc.setContent(xml))
		{
			const QDomElement element = doc.documentElement();
			info.kind = element.tagName();
			info.name = element.attribute("name");
			if (element.hasAttribute("color")) { info.color = QColor(element.attribute("color")); }
			info.length = std::max(1, element.attribute("len", "192").toInt());
			if (info.kind == "sampleclip" && info.name.isEmpty())
			{
				info.name = QFileInfo(element.attribute("src")).completeBaseName();
			}
			for (auto child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
			{
				if (child.tagName() == "note")
				{
					info.notes.push_back({child.attribute("pos").toDouble(), child.attribute("len").toDouble(),
						child.attribute("key").toDouble()});
				}
				else if (child.tagName() == "time")
				{
					info.points.emplace_back(child.attribute("pos").toDouble(), child.attribute("value").toDouble());
				}
			}
		}
		if (info.name.isEmpty())
		{
			info.name = info.kind == "automationclip" ? tr("Automation") : tr("Block");
		}
		m_infos.push_back(std::move(info));
	}
	update();
}


QRect TrackDraftsPanel::tileRect(int index) const
{
	return QRect(Gap + index * (TileWidth + Gap) - m_scroll, 3, TileWidth, height() - 6);
}


int TrackDraftsPanel::tileAt(const QPoint& pos) const
{
	for (int i = 0; i < static_cast<int>(m_infos.size()); ++i)
	{
		if (tileRect(i).contains(pos)) { return i; }
	}
	return -1;
}


void TrackDraftsPanel::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const bool hint = m_tab && m_tab->m_dropHint;
	p.fillRect(rect(), hint ? QColor(52, 62, 76, 245) : QColor(28, 31, 36, 240));
	p.setPen(hint ? QColor(143, 166, 192) : QColor(70, 76, 86));
	p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));

	QFont small = font();
	small.setPixelSize(10);
	p.setFont(small);

	if (m_infos.empty())
	{
		p.setPen(QColor(150, 156, 164));
		p.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
			tr("Keine Entw\xc3\xbc" "rfe: Bl\xc3\xb6" "cke hierher ziehen"));
		return;
	}

	const QColor trackColor = m_track->color().value_or(DefaultClipColor);
	for (int i = 0; i < static_cast<int>(m_infos.size()); ++i)
	{
		const QRect r = tileRect(i);
		if (r.right() < 0 || r.left() > width()) { continue; }
		const auto& info = m_infos[i];
		const QColor color = info.color.value_or(m_track->color().has_value() ? trackColor : DefaultClipColor);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(r), 3, 3);

		const QColor ink = color.lightness() > 140 ? QColor(25, 28, 32, 200) : QColor(240, 242, 245, 210);
		const QRectF inner = QRectF(r).adjusted(3, 13, -3, -2);
		// mini preview
		if (!info.notes.empty() && inner.height() > 3)
		{
			double low = 127, high = 0, end = info.length;
			for (const auto& n : info.notes)
			{
				low = std::min(low, n[2]);
				high = std::max(high, n[2]);
				end = std::max(end, n[0] + std::max(1.0, n[1]));
			}
			const double range = std::max(1.0, high - low);
			p.setBrush(ink);
			for (const auto& n : info.notes)
			{
				const double x = inner.left() + inner.width() * n[0] / end;
				const double w = std::max(1.5, inner.width() * std::max(1.0, n[1]) / end);
				const double y = inner.bottom() - 2 - (inner.height() - 2) * (n[2] - low) / range;
				p.drawRect(QRectF(x, y, w, 1.6));
			}
		}
		else if (info.points.size() > 0 && inner.height() > 3)
		{
			double low = info.points.front().second, high = low, end = info.length;
			for (const auto& [pos, value] : info.points)
			{
				low = std::min(low, value);
				high = std::max(high, value);
				end = std::max(end, pos);
			}
			const double range = std::max(1e-6, high - low);
			QPainterPath path;
			for (std::size_t k = 0; k < info.points.size(); ++k)
			{
				const QPointF pt(inner.left() + inner.width() * info.points[k].first / end,
					inner.bottom() - inner.height() * (info.points[k].second - low) / range);
				if (k == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
			}
			p.setPen(QPen(ink, 1.2));
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
		}

		p.setPen(ink);
		p.drawText(QRectF(r).adjusted(4, 0, -3, 0), Qt::AlignLeft | Qt::AlignTop,
			QFontMetrics(small).elidedText(info.name, Qt::ElideRight, r.width() - 7));
	}
}


void TrackDraftsPanel::mousePressEvent(QMouseEvent* me)
{
	m_pressed = me->button() == Qt::LeftButton ? tileAt(me->pos()) : -1;
	m_pressPos = me->pos();
	m_pressCtrl = me->modifiers() & Qt::ControlModifier;
}


void TrackDraftsPanel::mouseMoveEvent(QMouseEvent* me)
{
	if (m_pressed >= 0 && (me->buttons() & Qt::LeftButton)
		&& (me->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
	{
		const int index = m_pressed;
		m_pressed = -1;
		startDrag(index, m_pressCtrl || (me->modifiers() & Qt::ControlModifier));
	}
	setCursor(tileAt(me->pos()) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}


void TrackDraftsPanel::mouseDoubleClickEvent(QMouseEvent* me)
{
	const int index = tileAt(me->pos());
	if (index >= 0) { insertAtPlayPosition(index, me->modifiers() & Qt::ControlModifier); }
}


void TrackDraftsPanel::wheelEvent(QWheelEvent* we)
{
	const int delta = we->angleDelta().y() != 0 ? we->angleDelta().y() : we->angleDelta().x();
	m_scroll = std::clamp(m_scroll - delta / 2, 0, std::max(0, contentWidth() - width()));
	update();
	we->accept();
}


void TrackDraftsPanel::contextMenuEvent(QContextMenuEvent* ce)
{
	const int index = tileAt(ce->pos());
	QMenu menu(this);
	if (index >= 0)
	{
		menu.addAction(tr("An Abspielposition einf\xc3\xbcgen"), this, [this, index] { insertAtPlayPosition(index, false); });
		menu.addAction(tr("Kopie an Abspielposition einf\xc3\xbcgen (Entwurf behalten)"), this,
			[this, index] { insertAtPlayPosition(index, true); });
		menu.addSeparator();
		menu.addAction(tr("Entwurf l\xc3\xb6schen"), this, [this, index] {
			m_track->addJournalCheckPoint();
			m_track->removeDraft(index);
			Engine::getSong()->setModified();
		});
		menu.addSeparator();
	}
	menu.addAction(tr("Entw\xc3\xbc" "rfe schlie\xc3\x9f" "en"), this, [this] { setOpen(false); });
	menu.exec(ce->globalPos());
}


void TrackDraftsPanel::startDrag(int index, bool keep)
{
	if (index < 0 || index >= static_cast<int>(m_track->drafts().size())) { return; }
	QDomDocument draftDoc;
	if (!draftDoc.setContent(m_track->drafts()[index])) { return; }
	QDomElement draft = draftDoc.documentElement();
	const int pos = std::max(0, draft.attribute("pos").toInt());
	draft.setAttribute("pos", pos);

	// same format as a Ctrl+drag of clips, so the normal drop code places it
	const auto& tracks = m_track->trackContainer()->tracks();
	const int trackIndex = static_cast<int>(std::distance(tracks.begin(), std::ranges::find(tracks, m_track)));
	DataFile dataFile(DataFile::Type::DragNDropData);
	QDomElement clips = dataFile.createElement("clips");
	QDomElement clip = dataFile.createElement("clip");
	clip.setAttribute("trackIndex", trackIndex);
	clip.setAttribute("trackType", static_cast<int>(m_track->type()));
	clip.setAttribute("trackName", m_track->name());
	clip.appendChild(dataFile.importNode(draft, true));
	clips.appendChild(clip);
	dataFile.content().appendChild(clips);
	QDomElement metadata = dataFile.createElement("copyMetadata");
	metadata.setAttribute("initialTrackIndex", trackIndex);
	metadata.setAttribute("trackContainerId", 0); // never "the same place", so any bar is allowed
	metadata.setAttribute("grabbedClipPos", pos);
	dataFile.content().appendChild(metadata);

	auto mime = new QMimeData;
	mime->setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),
		QString(clipKey(m_track) + ":" + dataFile.toString()).toUtf8());
	auto drag = new QDrag(this);
	drag->setMimeData(mime);
	QPixmap pixmap(tileRect(index).size());
	pixmap.fill(Qt::transparent);
	{
		QPainter p(&pixmap);
		render(&p, QPoint(), QRegion(tileRect(index)));
	}
	drag->setPixmap(pixmap);
	drag->setHotSpot(m_pressPos - tileRect(index).topLeft());

	const auto clipsBefore = m_track->getClips().size();
	const QPointer<TrackDraftsPanel> self(this);
	// the drop (on whatever track) and removing the draft are one undo step
	ProjectJournal::Group journalGroup;
	const auto result = drag->exec(Qt::CopyAction, Qt::CopyAction);
	if (self.isNull() || result == Qt::IgnoreAction || keep) { return; }
	// moved out: the draft is gone. The drop already made an undo step for its track;
	// if the clip landed on another track, this track needs its own
	if (m_track->getClips().size() == clipsBefore) { m_track->addJournalCheckPoint(); }
	m_track->removeDraft(index);
	Engine::getSong()->setModified();
}


void TrackDraftsPanel::insertAtPlayPosition(int index, bool keep)
{
	auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
	const TimePos pos = timeline.pos();
	m_track->addJournalCheckPoint();
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		m_track->createClipFromDraft(index, pos);
	}
	if (!keep) { m_track->removeDraft(index); }
	Engine::getSong()->setModified();
	Engine::getSong()->updateLength();
}


bool TrackDraftsPanel::acceptsDrag(const QDropEvent* de) const
{
	return Clipboard::decodeKey(de->mimeData()) == clipKey(m_track);
}


void TrackDraftsPanel::dragEnterEvent(QDragEnterEvent* de)
{
	if (acceptsDrag(de) && de->source() != this) { de->acceptProposedAction(); }
}


void TrackDraftsPanel::storeDropped(QDropEvent* de)
{
	// a Ctrl+drag (copy) of clips: store copies as drafts, the originals stay
	if (!acceptsDrag(de) || de->source() == this) { return; }
	DataFile dataFile(Clipboard::decodeValue(de->mimeData()).toUtf8());
	const QDomElement clips = dataFile.content().firstChildElement("clips");
	bool any = false;
	for (auto clip = clips.firstChildElement("clip"); !clip.isNull(); clip = clip.nextSiblingElement("clip"))
	{
		if (clip.attribute("trackType").toInt() != static_cast<int>(m_track->type())) { continue; }
		QDomElement element = clip.firstChildElement().cloneNode(true).toElement();
		if (element.isNull()) { continue; }
		element.removeAttribute("link");
		if (!any) { m_track->addJournalCheckPoint(); }
		any = true;
		QString xml;
		QTextStream stream(&xml);
		element.save(stream, -1);
		m_track->addDraftXml(xml);
	}
	if (any)
	{
		de->acceptProposedAction();
		setOpen(true);
		Engine::getSong()->setModified();
	}
}


void TrackDraftsPanel::dropEvent(QDropEvent* de)
{
	storeDropped(de);
}


void TrackDraftsPanel::absorbClips(const QVector<ClipView*>& clipViews)
{
	QVector<ClipView*> accepted;
	for (const auto clipView : clipViews)
	{
		if (clipView->getClip()->getTrack()->type() == m_track->type()) { accepted.push_back(clipView); }
	}
	if (accepted.isEmpty()) { return; }

	// one undo step per involved track, taken before anything changes
	QVector<Track*> touched{m_track};
	for (const auto clipView : accepted)
	{
		if (!touched.contains(clipView->getClip()->getTrack())) { touched.push_back(clipView->getClip()->getTrack()); }
	}
	for (const auto clipView : accepted) { clipView->getClip()->setJournalling(true); }
	for (const auto track : touched) { track->addJournalCheckPoint(); }

	for (const auto clipView : accepted)
	{
		Clip* clip = clipView->getClip();
		m_track->addDraft(clip);
		clipView->removeWithoutJournal();
	}
	setOpen(true);
	Engine::getSong()->setModified();
	Engine::getSong()->updateLength();
}


TrackDraftsPanel* TrackDraftsPanel::panelAt(const QPoint& globalPos)
{
	const auto editor = getGUI()->songEditor()->m_editor;
	for (const auto trackView : editor->trackViews())
	{
		const auto content = trackView->getTrackContentWidget();
		if (content->draftsPanel() == nullptr || !content->isVisible()) { continue; }
		const QPoint local = content->mapFromGlobal(globalPos);
		if (!content->rect().contains(local)) { continue; }
		const auto panel = content->draftsPanel();
		const bool onTab = local.x() < TrackDraftsTab::Width;
		const bool onPanel = panel->isVisible() && panel->geometry().contains(local);
		return onTab || onPanel ? panel : nullptr;
	}
	return nullptr;
}


void TrackDraftsPanel::updateDropHint(const QPoint* globalPos)
{
	const auto target = globalPos != nullptr ? panelAt(*globalPos) : nullptr;
	const auto editor = getGUI()->songEditor()->m_editor;
	for (const auto trackView : editor->trackViews())
	{
		if (const auto panel = trackView->getTrackContentWidget()->draftsPanel(); panel && panel->m_tab)
		{
			panel->m_tab->setDropHint(panel == target);
		}
	}
}

} // namespace lmms::gui
