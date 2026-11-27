/*
 * SampleClipView.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */
 
#include "SampleClipView.h"

#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "FileDialog.h"
#include "GuiApplication.h"
#include "AutomationEditor.h"
#include "embed.h"
#include "Engine.h"
#include "PathUtil.h"
#include "SampleClip.h"
#include "SampleThumbnailRenderer.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "TextFloat.h"
#include "TrackView.h"

namespace lmms::gui
{


SampleClipView::SampleClipView( SampleClip * _clip, TrackView * _tv ) :
	ClipView( _clip, _tv ),
	m_clip( _clip ),
	m_paintPixmap(),
	m_paintPixmapXPosition(0)
{
	// update UI and tooltip
	updateSample();

	// track future changes of SampleClip
	connect(m_clip, SIGNAL(sampleChanged()), this, SLOT(updateSample()));

	connect(m_clip, SIGNAL(wasReversed()), this, SLOT(update()));
	connect(m_clip, &Clip::dataChanged, this, [this] { update(); });

	setStyle( QApplication::style() );
}

void SampleClipView::updateSample()
{
	update();

	// set tooltip to filename so that user can see what sample this
	// sample-clip contains
	setToolTip(
		!m_clip->m_sample.sampleFile().isEmpty()
			? PathUtil::toAbsolute(m_clip->m_sample.sampleFile())
			: tr("Double-click to open sample")
	);
}




void SampleClipView::constructContextMenu(QMenu* cm)
{
	cm->addSeparator();

	/*contextMenu.addAction( embed::getIconPixmap( "record" ),
				tr( "Set/clear record" ),
						m_clip, SLOT(toggleRecord()));*/

	cm->addAction(
		embed::getIconPixmap("flip_x"),
		tr("Umdrehen (Reverse)"),
		this,
		SLOT(reverseSample())
	);

	// Custom: fades (drag the small handles at the top corners)
	auto fadeMenu = cm->addMenu(tr("Fades"));
	const auto setFadesForSelection = [this](tick_t fadeIn, tick_t fadeOut, bool onlyIfNone) {
		for (const auto clipView : getClickedClips())
		{
			auto clip = dynamic_cast<SampleClip*>(clipView->getClip());
			if (!clip) { continue; }
			if (onlyIfNone && (clip->fadeIn() > 0 || clip->fadeOut() > 0)) { continue; }
			clip->addJournalCheckPoint();
			clip->setFades(fadeIn < 0 ? clip->fadeIn() : fadeIn, fadeOut < 0 ? clip->fadeOut() : fadeOut);
		}
		Engine::getSong()->setModified();
	};
	// one tick at 120 BPM is about 10 ms
	fadeMenu->addAction(tr("Kurze Fades gegen Knackser (1 Tick)"), this,
		[setFadesForSelection] { setFadesForSelection(1, 1, false); });
	fadeMenu->addAction(tr("Fade-In: 1 Takt"), this,
		[setFadesForSelection] { setFadesForSelection(TimePos::ticksPerBar(), -1, false); });
	fadeMenu->addAction(tr("Fade-Out: 1 Takt"), this,
		[setFadesForSelection] { setFadesForSelection(-1, TimePos::ticksPerBar(), false); });
	fadeMenu->addSeparator();
	fadeMenu->addAction(tr("Fade-In entfernen"), this, [setFadesForSelection] { setFadesForSelection(0, -1, false); });
	fadeMenu->addAction(tr("Fade-Out entfernen"), this, [setFadesForSelection] { setFadesForSelection(-1, 0, false); });
	fadeMenu->addAction(tr("Alle Fades entfernen"), this, [setFadesForSelection] { setFadesForSelection(0, 0, false); });
	fadeMenu->addSeparator();
	auto hint = fadeMenu->addAction(tr("Tipp: an den kleinen Griffen oben links/rechts ziehen"));
	hint->setEnabled(false);

	cm->addAction(
		embed::getIconPixmap("automation_ghost_note"),
		tr("Set as ghost in automation editor"),
		this,
		SLOT(setAutomationGhost())
	);

}




void SampleClipView::dragEnterEvent( QDragEnterEvent * _dee )
{
	if( StringPairDrag::processDragEnterEvent( _dee,
					"samplefile,sampledata" ) == false )
	{
		ClipView::dragEnterEvent( _dee );
	}
}






void SampleClipView::dropEvent( QDropEvent * _de )
{
	if( StringPairDrag::decodeKey( _de ) == "samplefile" )
	{
		m_clip->setSampleFile( StringPairDrag::decodeValue( _de ) );
		_de->accept();
	}
	else if( StringPairDrag::decodeKey( _de ) == "sampledata" )
	{
		m_clip->setSampleBuffer(SampleBuffer::fromBase64(StringPairDrag::decodeValue(_de)));
		m_clip->updateLength();
		update();
		_de->accept();
	}
	else
	{
		ClipView::dropEvent( _de );
	}
}




void SampleClipView::mousePressEvent( QMouseEvent * _me )
{
	// Custom: drag a fade handle
	if (_me->button() == Qt::LeftButton && !fixedClips() && !(_me->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
	{
		if (const int handle = fadeHandleAt(_me->pos()))
		{
			m_fadeDrag = handle;
			m_clip->addJournalCheckPoint();
			setCursor(Qt::SizeHorCursor);
			textFloat()->setTitle(handle == 1 ? tr("Fade-In") : tr("Fade-Out"));
			textFloat()->setText(QString());
			textFloat()->moveGlobal(this, QPoint(width() + 2, height() + 2));
			textFloat()->show();
			return;
		}
	}
	if( _me->button() == Qt::LeftButton &&
		_me->modifiers() & Qt::ControlModifier &&
		_me->modifiers() & Qt::ShiftModifier )
	{
		m_clip->toggleRecord();
	}
	else
	{
		if( _me->button() == Qt::MiddleButton && _me->modifiers() == Qt::ControlModifier )
		{
			auto sClip = dynamic_cast<SampleClip*>(getClip());
			if( sClip )
			{
				sClip->updateTrackClips();
			}
		}
		ClipView::mousePressEvent( _me );
	}
}




void SampleClipView::mouseReleaseEvent(QMouseEvent *_me)
{
	if (m_fadeDrag != 0)
	{
		m_fadeDrag = 0;
		textFloat()->hide();
		setCursor(Qt::ArrowCursor);
		Engine::getSong()->setModified();
		return;
	}
	if( _me->button() == Qt::MiddleButton && !_me->modifiers() )
	{
		auto sClip = dynamic_cast<SampleClip*>(getClip());
		if( sClip )
		{
			sClip->playbackPositionChanged();
		}
	}
	ClipView::mouseReleaseEvent( _me );
}



float SampleClipView::ticksToPixels(double ticks) const
{
	return static_cast<float>(ticks * pixelsPerBar() / TimePos::ticksPerBar());
}




int SampleClipView::fadeHandleAt(const QPoint& pos) const
{
	constexpr int HandleReach = 6;
	constexpr int HandleHeight = 10;
	if (pos.y() > HandleHeight) { return 0; }
	const float inX = ticksToPixels(m_clip->fadeIn());
	const float outX = width() - ticksToPixels(m_clip->fadeOut());
	const float dIn = std::abs(pos.x() - inX);
	const float dOut = std::abs(pos.x() - outX);
	if (dIn <= HandleReach && dIn <= dOut) { return 1; }
	if (dOut <= HandleReach) { return 2; }
	return 0;
}




void SampleClipView::mouseMoveEvent(QMouseEvent* me)
{
	if (m_fadeDrag != 0)
	{
		const double ticks = std::max(0.0, me->pos().x() * static_cast<double>(TimePos::ticksPerBar()) / pixelsPerBar());
		const tick_t length = m_clip->length();
		if (m_fadeDrag == 1)
		{
			m_clip->setFades(std::clamp<tick_t>(static_cast<tick_t>(std::lround(ticks)), 0, length - m_clip->fadeOut()),
				m_clip->fadeOut());
		}
		else
		{
			m_clip->setFades(m_clip->fadeIn(),
				std::clamp<tick_t>(length - static_cast<tick_t>(std::lround(ticks)), 0, length - m_clip->fadeIn()));
		}
		const tick_t fade = m_fadeDrag == 1 ? m_clip->fadeIn() : m_clip->fadeOut();
		const double ms = fade * 60000.0 / (Engine::getSong()->getTempo() * (DefaultTicksPerBar / 4.0));
		textFloat()->setText(tr("%1 ms  (%2 Ticks)").arg(std::lround(ms)).arg(fade));
		textFloat()->moveGlobal(this, QPoint(width() + 2, height() + 2));
		return;
	}
	ClipView::mouseMoveEvent(me);
	if (!me->buttons() && !fixedClips() && fadeHandleAt(me->pos()) != 0) { setCursor(Qt::SizeHorCursor); }
}




void SampleClipView::paintFades(QPainter& p, bool lightBackground)
{
	const int top = 1;
	const int bottom = height() - 2;
	const int h = bottom - top;
	const float x0 = -m_paintPixmapXPosition;
	const float w = width();
	const double length = m_clip->length();
	const double fadeIn = std::min<double>(m_clip->fadeIn(), length);
	const double fadeOut = std::min<double>(m_clip->fadeOut(), length - fadeIn);

	p.setRenderHint(QPainter::Antialiasing, true);
	const auto drawFade = [&](bool in, double fadeTicks) {
		if (fadeTicks <= 0) { return; }
		const float fw = ticksToPixels(fadeTicks);
		QPainterPath curve;
		QPainterPath shade;
		constexpr int Steps = 24;
		for (int i = 0; i <= Steps; ++i)
		{
			const double t = static_cast<double>(i) / Steps;
			const double pos = in ? t * fadeTicks : length - fadeTicks + t * fadeTicks;
			const float gain = SampleClip::fadeGain(pos, length, in ? fadeTicks : 0, in ? 0 : fadeTicks);
			const QPointF point(x0 + (in ? t * fw : w - fw + t * fw), bottom - gain * h);
			if (i == 0) { curve.moveTo(point); } else { curve.lineTo(point); }
		}
		shade = curve;
		if (in) { shade.lineTo(x0 + fw, top); shade.lineTo(x0, top); }
		else { shade.lineTo(x0 + w, top); shade.lineTo(x0 + w - fw, top); }
		shade.closeSubpath();
		p.fillPath(shade, lightBackground ? QColor(0, 0, 0, 90) : QColor(0, 0, 0, 130));
		p.setPen(QPen(lightBackground ? QColor(20, 20, 20, 230) : QColor(255, 255, 255, 210), 1.4));
		p.drawPath(curve);
	};
	drawFade(true, fadeIn);
	drawFade(false, fadeOut);

	// handles
	p.setPen(lightBackground ? QColor(20, 20, 20) : QColor(255, 255, 255));
	p.setBrush(lightBackground ? QColor(255, 255, 255) : QColor(40, 40, 40));
	const float inX = x0 + ticksToPixels(fadeIn);
	const float outX = x0 + w - ticksToPixels(fadeOut);
	p.drawRect(QRectF(std::clamp(inX - 3.f, x0 + 1.f, x0 + w - 7.f), top + 1, 6, 6));
	p.drawRect(QRectF(std::clamp(outX - 3.f, x0 + 1.f, x0 + w - 7.f), top + 1, 6, 6));
	p.setBrush(Qt::NoBrush);
	p.setRenderHint(QPainter::Antialiasing, false);
}




void SampleClipView::mouseDoubleClickEvent( QMouseEvent * )
{
	if (m_trackView->trackContainerView()->knifeMode()) { return; }

	const QString selectedAudioFile = FileDialog::openAudioFile();

	if (selectedAudioFile.isEmpty()) { return; }
	
	if (!m_clip->hasSampleFileLoaded(selectedAudioFile))
	{
		auto sampleBuffer = SampleBuffer::fromFile(selectedAudioFile);
		if (sampleBuffer != SampleBuffer::emptyBuffer())
		{
			m_clip->setSampleBuffer(sampleBuffer);
		}
	}
	m_clip->updateLength();
}




void SampleClipView::paintEvent( QPaintEvent * pe )
{
	QPainter painter( this );

	if( !needsUpdate() )
	{
		painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
		return;
	}

	setNeedsUpdate( false );

	const auto trackViewWidth = getTrackView()->rect().width();

	// Use the clip's height to avoid artifacts when rendering while something else is overlaying the clip.
	const auto viewPortRect = QRect(0, 0, trackViewWidth * 2, rect().height());

	m_paintPixmapXPosition = std::max(0, pe->rect().x() - trackViewWidth);

	if (m_paintPixmap.isNull() || m_paintPixmap.size() != viewPortRect.size())
	{
		m_paintPixmap = QPixmap(viewPortRect.size());
	}

	QPainter p( &m_paintPixmap );

	bool muted = m_clip->getTrack()->isMuted() || m_clip->isMuted();
	bool selected = isSelected();

	QLinearGradient lingrad(0, 0, 0, height());
	QColor c = painter.background().color();
	if (muted) { c = c.darker(150); }
	if (selected) { c = c.darker(150); }

	lingrad.setColorAt( 1, c.darker( 300 ) );
	lingrad.setColorAt( 0, c );

	// paint a black rectangle under the clip to prevent glitches with transparent backgrounds
	p.fillRect( rect(), QColor( 0, 0, 0 ) );

	if( gradient() )
	{
		p.fillRect( rect(), lingrad );
	}
	else
	{
		p.fillRect( rect(), c );
	}

	// Custom: waveform in a color that contrasts with the clip background
	auto clipColor = c.lightness() > 110 ? QColor(21, 24, 27) : QColor(235, 235, 235);

	p.setPen(clipColor);

	if (muted)
	{
		QColor penColor = p.pen().brush().color();
		penColor.setHsv(penColor.hsvHue(), penColor.hsvSaturation() / 4, penColor.value());
		p.setPen(penColor.darker(250));
	}
	if (selected)
	{
		p.setPen(p.pen().brush().color().darker(150));
	}

	const int spacing = BORDER_WIDTH + 1;
	const float ppb = fixedClips() ?
			( parentWidget()->width() - 2 * BORDER_WIDTH )
					/ (float) m_clip->length().getBar() :
								pixelsPerBar();

	float nom = Engine::getSong()->getTimeSigModel().getNumerator();
	float den = Engine::getSong()->getTimeSigModel().getDenominator();
	float ticksPerBar = DefaultTicksPerBar * nom / den;
	float offsetStart = m_clip->startTimeOffset() / ticksPerBar * pixelsPerBar();
	float sampleLength = m_clip->sampleLength() * ppb / ticksPerBar;

	const auto& sample = m_clip->m_sample;

	const auto sampleRextX = static_cast<int>(offsetStart) - m_paintPixmapXPosition;

	if (sample.sampleSize() > 0)
	{
		const auto param = SampleThumbnailRenderer::VisualizeParameters{
			.cache = m_clip->sampleThumbnailCache(),
			.rect = QRect(sampleRextX, spacing, sampleLength, height() - spacing),
			.amplification = sample.amplification(),
			.reversed = sample.reversed()
		};

		SampleThumbnailRenderer::visualize(param, p);
	}

	QString name = PathUtil::cleanName(m_clip->m_sample.sampleFile());
	paintTextLabel(name, p);
	paintLinkMarker(p);
	// Custom: on top of the label, so the handles are always reachable
	if (!fixedClips()) { paintFades(p, c.lightness() > 110); }

	// disable antialiasing for borders, since its not needed
	p.setRenderHint( QPainter::Antialiasing, false );

	// inner border
	p.setPen( c.lighter( 135 ) );
	p.drawRect(
		-m_paintPixmapXPosition + 1,
		1,
		rect().right() - BORDER_WIDTH,
		rect().bottom() - BORDER_WIDTH );

	// outer border
	p.setPen( c.darker( 200 ) );
	p.drawRect(-m_paintPixmapXPosition, 0, rect().right(), rect().bottom());

	// draw the 'muted' pixmap only if the clip was manually muted
	if( m_clip->isMuted() )
	{
		const int spacing = BORDER_WIDTH;
		const int size = 14;
		p.drawPixmap( spacing, height() - ( size + spacing ),
			embed::getIconPixmap( "muted", size, size ) );
	}

	if ( m_marker )
	{
		p.setPen(markerColor());
		p.drawLine(m_markerPos, rect().bottom(), m_markerPos, rect().top());
	}
	// recording sample tracks is not possible at the moment

	/* if( m_clip->isRecord() )
	{
		p.setFont( pointSize<7>( p.font() ) );

		p.setPen( textShadowColor() );
		p.drawText( 10, p.fontMetrics().height()+1, "Rec" );
		p.setPen( textColor() );
		p.drawText( 9, p.fontMetrics().height(), "Rec" );

		p.setBrush( QBrush( textColor() ) );
		p.drawEllipse( 4, 5, 4, 4 );
	}*/

	p.end();

	painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
}




void SampleClipView::reverseSample()
{
	// Custom: all selected sample clips, as one undo step
	for (const auto clipView : getClickedClips())
	{
		auto view = dynamic_cast<SampleClipView*>(clipView);
		if (!view) { continue; }
		auto clip = view->m_clip;
		clip->addJournalCheckPoint();
		clip->m_sample.setReversed(!clip->m_sample.reversed());
		clip->setStartTimeOffset(clip->length() - clip->startTimeOffset() - clip->sampleLength());
		// the fades stay at their clip borders
		emit clip->wasReversed();
		if (view != this) { view->update(); }
	}
	Engine::getSong()->setModified();
	update();
}



void SampleClipView::setAutomationGhost()
{
	auto aEditor = gui::getGUI()->automationEditor();
	aEditor->setGhostSample(m_clip);
	aEditor->parentWidget()->show();
	aEditor->show();
	aEditor->setFocus();
}

} // namespace lmms::gui
