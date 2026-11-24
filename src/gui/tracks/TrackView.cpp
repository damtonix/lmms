/*
 * TrackView.cpp - implementation of TrackView class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "TrackView.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QLabel>
#include <QTimer>
#include <QToolButton>


#include "AudioEngine.h"
#include "AutomatableButton.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "DeprecationHelper.h"
#include "Engine.h"
#include "FadeButton.h"
#include "GroupTrack.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "TrackGrip.h"
#include "SampleThumbnailRenderer.h"
#include "SampleClip.h"
#include "TrackLabelButton.h"
#include "TrackContainerView.h"
#include "ClipView.h"


namespace lmms::gui
{

TrackView::TrackView(Track* track, TrackContainerView* tcv)
	: QWidget(tcv->contentWidget())
	, ModelView(nullptr, this)
	, m_track(track)
	, m_trackContainerView(tcv)
	, m_trackOperationsWidget(this)
	, m_trackSettingsWidget(this)
	, m_trackContentWidget(this)
{
	setAutoFillBackground( true );
	QPalette pal;
	pal.setColor( backgroundRole(), QColor( 32, 36, 40 ) );
	setPalette( pal );

	m_trackSettingsWidget.setAutoFillBackground( true );

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing( 0 );
	layout->addWidget( &m_trackOperationsWidget );
	layout->addWidget( &m_trackSettingsWidget );
	layout->addWidget( &m_trackContentWidget, 1 );
	setFixedHeight( m_track->getHeight() );

	resizeEvent( nullptr );

	setAcceptDrops( true );
	setAttribute( Qt::WA_DeleteOnClose, true );


	connect( m_track, SIGNAL(destroyedTrack()), this, SLOT(close()));
	connect( m_track,
		SIGNAL(clipAdded(lmms::Clip*)),
			this, SLOT(createClipView(lmms::Clip*)),
			Qt::QueuedConnection );

	connect( &m_track->m_mutedModel, SIGNAL(dataChanged()),
			&m_trackContentWidget, SLOT(update()));

	connect(&m_track->m_mutedModel, SIGNAL(dataChanged()),
			this, SLOT(muteChanged()));

	connect( &m_track->m_soloModel, SIGNAL(dataChanged()),
			m_track, SLOT(toggleSolo()), Qt::DirectConnection );

	connect(m_track, &Track::groupChanged, this, [this] {
		m_trackContainerView->realignTracks();
		groupStateChanged();
	});
	connect(m_track, &Track::collapsedChanged, this, [this] {
		m_trackContainerView->realignTracks();
		applyGroupIndent();
	});
	connect(m_track, &Track::frozenChanged, this, &TrackView::updateFrozenState);
	
	auto trackGrip = m_trackOperationsWidget.getTrackGrip();
	connect(trackGrip, &TrackGrip::grabbed, this, &TrackView::onTrackGripGrabbed);
	connect(trackGrip, &TrackGrip::released, this, &TrackView::onTrackGripReleased);

	// create views for already existing clips
	for (const auto& clip : m_track->m_clips)
	{
		createClipView(clip);
	}

	m_trackContainerView->addTrackView( this );

	// Custom: apply the group indent / frozen state once the derived view has created its widgets
	QTimer::singleShot(0, this, &TrackView::applyGroupIndent);
	QTimer::singleShot(0, this, &TrackView::updateFrozenState);
}




void TrackView::resizeEvent( QResizeEvent * re )
{
	if( ConfigManager::inst()->value( "ui",
					  "compacttrackbuttons" ).toInt() )
	{
		m_trackOperationsWidget.setFixedSize( TRACK_OP_WIDTH_COMPACT, height() - 1 );
		m_trackSettingsWidget.setFixedSize( DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT, height() - 1 );
	}
	else
	{
		m_trackOperationsWidget.setFixedSize( TRACK_OP_WIDTH, height() - 1 );
		m_trackSettingsWidget.setFixedSize( DEFAULT_SETTINGS_WIDGET_WIDTH, height() - 1 );
	}

	m_trackContentWidget.setFixedHeight( height() );
	if (m_groupIndentBar) { m_groupIndentBar->setFixedHeight(m_trackSettingsWidget.height()); }
	if (m_frozenOverlay) { m_frozenOverlay->setGeometry(0, 0, m_trackContentWidget.width(), height()); }
}




void TrackView::update()
{
	m_trackContentWidget.update();
	if( !m_trackContainerView->fixedClips() )
	{
		m_trackContentWidget.changePosition();
	}
	QWidget::update();
}




QMenu * TrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	Q_UNUSED(title)
	Q_UNUSED(newMixerLabel)
	return nullptr;
}




bool TrackView::close()
{
	m_trackContainerView->removeTrackView( this );
	return QWidget::close();
}




void TrackView::modelChanged()
{
	m_track = castModel<Track>();
	Q_ASSERT( m_track != nullptr );
	connect( m_track, SIGNAL(destroyedTrack()), this, SLOT(close()));
	m_trackOperationsWidget.m_muteBtn->setModel( &m_track->m_mutedModel );
	m_trackOperationsWidget.m_soloBtn->setModel( &m_track->m_soloModel );
	ModelView::modelChanged();
	setFixedHeight( m_track->getHeight() );
}




void TrackView::dragEnterEvent( QDragEnterEvent * dee )
{
	StringPairDrag::processDragEnterEvent( dee, "track_" +
					QString::number( static_cast<int>(m_track->type()) ) );
}




void TrackView::dropEvent( QDropEvent * de )
{
	QString type = StringPairDrag::decodeKey( de );
	QString value = StringPairDrag::decodeValue( de );
	if( type == ( "track_" + QString::number( static_cast<int>(m_track->type()) ) ) )
	{
		// value contains our XML-data so simply create a
		// DataFile which does the rest for us...
		DataFile dataFile( value.toUtf8() );
		Engine::audioEngine()->requestChangeInModel();
		m_track->restoreState( dataFile.content().firstChild().toElement() );
		Engine::audioEngine()->doneChangeInModel();
		de->accept();
	}
}




void TrackView::mousePressEvent( QMouseEvent * me )
{
	const auto pos = position(me);

	// If previously dragged too small, restore on shift-leftclick
	if( height() < DEFAULT_TRACK_HEIGHT &&
		me->modifiers() & Qt::ShiftModifier &&
		me->button() == Qt::LeftButton )
	{
		setFixedHeight( DEFAULT_TRACK_HEIGHT );
		m_track->setHeight( DEFAULT_TRACK_HEIGHT );
	}


	int widgetTotal = ConfigManager::inst()->value( "ui",
							"compacttrackbuttons" ).toInt()==1 ?
		DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT :
		DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;
	if (m_trackContainerView->allowRubberband() == true  && pos.x() > widgetTotal)
	{
		QWidget::mousePressEvent( me );
	}
	else if( me->button() == Qt::LeftButton )
	{
		if( me->modifiers() & Qt::ShiftModifier )
		{
			m_action = Action::Resize;
			QCursor::setPos(mapToGlobal(QPoint(pos.x(), height())));
			QApplication::setOverrideCursor(Qt::SizeVerCursor);
		}

		me->accept();
	}
	else
	{
		QWidget::mousePressEvent( me );
	}
}




void TrackView::mouseMoveEvent( QMouseEvent * me )
{
	const auto pos = position(me);

	int widgetTotal = ConfigManager::inst()->value( "ui",
							"compacttrackbuttons" ).toInt()==1 ?
		DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT :
		DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;
	if (m_trackContainerView->allowRubberband() == true && pos.x() > widgetTotal)
	{
		QWidget::mouseMoveEvent( me );
	}
	else if( m_action == Action::Move )
	{
		// look which track-widget the mouse-cursor is over
		const int yPos =
			m_trackContainerView->contentWidget()->mapFromGlobal(globalPosition(me)).y();
		const TrackView * trackAtY = m_trackContainerView->trackViewAt( yPos );

		// debug code
		//	qDebug( "y position %d", yPos );

		// a track-widget not equal to ourself?
		if( trackAtY != nullptr && trackAtY != this )
		{
			// then move us up/down there!
			if (pos.y() < 0)
			{
				m_trackContainerView->moveTrackViewUp( this );
			}
			else
			{
				m_trackContainerView->moveTrackViewDown( this );
			}
		}
	}
	else if( m_action == Action::Resize )
	{
		resizeToHeight(pos.y());
	}

	if( height() < DEFAULT_TRACK_HEIGHT )
	{
		setToolTip(m_track->m_name);
	}
}



void TrackView::mouseReleaseEvent( QMouseEvent * me )
{
	m_action = Action::None;
	while( QApplication::overrideCursor() != nullptr )
	{
		QApplication::restoreOverrideCursor();
	}
	m_trackOperationsWidget.update();

	QWidget::mouseReleaseEvent( me );
}

void TrackView::wheelEvent(QWheelEvent* we)
{
	// Note: we add the values because one of them will be 0. If the alt modifier
	// is pressed x is non-zero and otherwise y.
	const int deltaY = we->angleDelta().x() + we->angleDelta().y();
	int const direction = deltaY < 0 ? -1 : 1;

	auto const modKeys = we->modifiers();
	int stepSize = modKeys == (Qt::ControlModifier | Qt::AltModifier) ? 1 : modKeys == (Qt::ShiftModifier | Qt::AltModifier) ? 5 : 0;

	if (stepSize != 0)
	{
		resizeToHeight(height() + stepSize * direction);
		we->accept();
		return;
	}
	we->ignore();
}




void TrackView::paintEvent( QPaintEvent * pe )
{
	QStyleOption opt;
	opt.initFrom( this );
	QPainter p( this );
	style()->drawPrimitive( QStyle::PE_Widget, &opt, &p, this );
}




void TrackView::createClipView( Clip * clip )
{
	ClipView * tv = clip->createView( this );
	if( clip->getSelectViewOnCreate() == true )
	{
		tv->setSelected( true );
	}
	clip->selectViewOnCreate( false );
	// Custom: keep the frozen/muted overlay above new clips
	if (m_frozenOverlay && m_frozenOverlay->isVisible()) { m_frozenOverlay->raise(); }
}




void TrackView::muteChanged()
{
	FadeButton * indicator = getActivityIndicator();
	if (indicator) { setIndicatorMute(indicator, m_track->isMuted()); }
	updateFrozenState();
}




void TrackView::applyGroupIndent()
{
	constexpr int IndentStep = 18;
	const auto parent = m_track->parentTrack();
	const int depth = m_track->treeDepth();
	// tracks with children (e.g. instruments with automation tracks) get a fold arrow; groups have their own
	const bool showFold = m_track->type() != Track::Type::Group && m_track->hasChildTracks();
	const int indent = depth * IndentStep + (showFold ? IndentStep : 0);
	auto settings = &m_trackSettingsWidget;

	// The derived view builds its widgets after the TrackView constructor; wait until they exist
	auto children = settings->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
	children.removeAll(m_groupIndentBar);
	children.removeAll(m_foldButton);
	const bool settingsBuilt = settings->layout() != nullptr || !children.isEmpty();
	if (indent != m_groupIndent && settingsBuilt)
	{
		const int delta = indent - m_groupIndent;
		if (auto layout = settings->layout())
		{
			auto margins = layout->contentsMargins();
			margins.setLeft(margins.left() + delta);
			layout->setContentsMargins(margins);
		}
		else
		{
			for (auto child : children) { child->move(child->x() + delta, child->y()); }
		}
		// keep everything inside the fixed settings width: shrink the name button
		if (auto label = settings->findChild<TrackLabelButton*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (label->width() - delta > 2 * IndentStep) { label->setFixedWidth(label->width() - delta); }
		}
		m_groupIndent = indent;
	}

	if (parent != nullptr)
	{
		if (m_groupIndentBar == nullptr)
		{
			m_groupIndentBar = new QWidget(settings);
			m_groupIndentBar->setAutoFillBackground(true);
		}
		QColor color = parent->color().value_or(palette().color(QPalette::Highlight));
		if (parent->isMuted()) { color = color.darker(200); }
		QPalette pal = m_groupIndentBar->palette();
		pal.setColor(QPalette::Window, color);
		m_groupIndentBar->setPalette(pal);
		m_groupIndentBar->setGeometry(depth * IndentStep - 8, 0, 4, settings->height());
		m_groupIndentBar->show();
		m_groupIndentBar->raise();
	}
	else if (m_groupIndentBar != nullptr)
	{
		m_groupIndentBar->hide();
	}

	if (showFold)
	{
		if (m_foldButton == nullptr)
		{
			m_foldButton = new QToolButton(settings);
			m_foldButton->setAutoRaise(true);
			m_foldButton->setFocusPolicy(Qt::NoFocus);
			m_foldButton->setToolTip(tr("Automationsspuren ein-/ausklappen"));
			connect(m_foldButton, &QToolButton::clicked, this, [this] { m_track->setCollapsed(!m_track->isCollapsed()); });
		}
		m_foldButton->setArrowType(m_track->isCollapsed() ? Qt::RightArrow : Qt::DownArrow);
		m_foldButton->setGeometry(depth * IndentStep, (settings->height() - 22) / 2, 16, 22);
		m_foldButton->show();
		m_foldButton->raise();
	}
	else if (m_foldButton != nullptr)
	{
		m_foldButton->hide();
	}
}




namespace
{
//! Custom: drawn over the clips – frozen tracks show their rendered audio, muted tracks are struck through
class TrackStateOverlay : public QWidget
{
public:
	TrackStateOverlay(TrackView* trackView, QWidget* parent) : QWidget(parent), m_trackView(trackView)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		auto track = m_trackView->getTrack();

		if (track->isFrozen())
		{
			p.fillRect(rect(), QColor(40, 70, 110, 200));
			// waveform of the frozen audio
			if (auto proxy = track->freezeProxy(); proxy != nullptr && !proxy->getClips().empty())
			{
				if (auto clip = dynamic_cast<SampleClip*>(proxy->getClips().front());
					clip != nullptr && clip->sample().sampleSize() > 0 && clip->sampleThumbnailCache() != nullptr)
				{
					auto tcv = m_trackView->trackContainerView();
					const double pxPerTick = tcv->pixelsPerBar() / TimePos::ticksPerBar();
					const double x = (clip->startPosition() - tcv->currentPosition()) * pxPerTick;
					const double w = clip->sampleLength() * pxPerTick;
					p.setPen(QColor(160, 205, 255));
					const auto param = SampleThumbnailRenderer::VisualizeParameters{
						.cache = clip->sampleThumbnailCache(),
						.rect = QRectF(x, 3, w, height() - 6),
						.amplification = clip->sample().amplification(),
						.reversed = clip->sample().reversed()
					};
					SampleThumbnailRenderer::visualize(param, p);
				}
			}
			p.setPen(QColor(216, 236, 255));
			QFont f = font();
			f.setBold(true);
			f.setPointSizeF(std::max(7.0, f.pointSizeF() * 0.85));
			p.setFont(f);
			p.drawText(rect().adjusted(6, 2, -6, -2), Qt::AlignLeft | Qt::AlignTop, TrackView::tr("EINGEFROREN"));
		}

		if (track->isMuted())
		{
			p.fillRect(rect(), QColor(0, 0, 0, 110));
			p.setPen(QPen(QColor(212, 212, 212, 190), 2));
			p.drawLine(0, height() / 2, width(), height() / 2);
		}
	}

private:
	TrackView* m_trackView;
};
} // namespace




void TrackView::updateFrozenState()
{
	const bool frozen = m_track->isFrozen();
	const bool muted = m_track->isMuted();

	// no clip editing while frozen
	m_trackContentWidget.setEnabled(!frozen);

	// the whole track row is tinted when frozen
	QPalette pal = palette();
	if (frozen) { pal.setColor(QPalette::Window, QColor(44, 74, 110)); }
	if (m_track->type() != Track::Type::Group) { m_trackSettingsWidget.setPalette(pal); }
	m_trackOperationsWidget.setPalette(pal);
	m_trackOperationsWidget.update();

	// muted tracks: struck-through name
	if (auto label = m_trackSettingsWidget.findChild<TrackLabelButton*>(QString(), Qt::FindDirectChildrenOnly))
	{
		QFont f = label->font();
		if (f.strikeOut() != muted)
		{
			f.setStrikeOut(muted);
			label->setFont(f);
		}
	}

	if (frozen || muted)
	{
		if (m_frozenOverlay == nullptr)
		{
			m_frozenOverlay = new TrackStateOverlay(this, &m_trackContentWidget);
			connect(m_trackContainerView, &TrackContainerView::positionChanged, m_frozenOverlay, qOverload<>(&QWidget::update));
		}
		m_frozenOverlay->setGeometry(0, 0, m_trackContentWidget.width(), height());
		m_frozenOverlay->show();
		m_frozenOverlay->raise();
		m_frozenOverlay->update();
	}
	else if (m_frozenOverlay != nullptr)
	{
		m_frozenOverlay->hide();
	}
}




void TrackView::groupStateChanged()
{
	applyGroupIndent();
	muteChanged();
	m_trackContentWidget.update();
	m_trackOperationsWidget.update();
	for (auto w : m_trackOperationsWidget.findChildren<QWidget*>()) { w->update(); }
}


void TrackView::onTrackGripGrabbed()
{
	m_action = Action::Move;
}

void TrackView::onTrackGripReleased()
{
	m_action = Action::None;
	// Custom: keep group members directly below their group
	m_trackContainerView->regroupTrackViews();
}



void TrackView::setIndicatorMute(FadeButton* indicator, bool muted)
{
	indicator->setMuted(muted);
}


void TrackView::resizeToHeight(int h)
{
	setFixedHeight(qMax<int>(h, MINIMAL_TRACK_HEIGHT));
	m_trackContainerView->realignTracks();
	m_track->setHeight(height());
}


} // namespace lmms::gui
