/*
 * SampleClip.cpp
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
 
#include "SampleClip.h"

#include <cmath>

#include <algorithm>

#include <QDomElement>
#include <QFileInfo>

#include "PatternStore.h"
#include "PathUtil.h"
#include "SampleCache.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

SampleClip::SampleClip(Track* _track, Sample sample, bool isPlaying):
	Clip(_track),
	m_sample(std::move(sample)),
	m_isPlaying(false),
	m_startFrameOffset(0)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect(Engine::getSong(), &Song::timeSignatureChanged, this, &SampleClip::updateLength);

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}

SampleClip::SampleClip(Track* track)
	: SampleClip(track, Sample(), false)
{
}

SampleClip::SampleClip(const SampleClip& orig) :
	Clip(orig),
	m_sample(orig.m_sample),
	m_sampleThumbnailCache(orig.m_sampleThumbnailCache),
	m_isPlaying(orig.m_isPlaying),
	m_startFrameOffset(orig.m_startFrameOffset),
	m_fadeIn(orig.m_fadeIn),
	m_fadeOut(orig.m_fadeOut)
{
	m_missingSampleFile = orig.m_missingSampleFile;
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}




SampleClip::~SampleClip()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if ( sampletrack )
	{
		sampletrack->updateClips();
	}
}




void SampleClip::changeLength( const TimePos & _length )
{
	Clip::changeLength(std::max(static_cast<int>(_length), 1));
}



const QString& SampleClip::sampleFile() const
{
	return m_sample.sampleFile();
}

bool SampleClip::hasSampleFileLoaded(const QString & filename) const
{
	return m_sample.sampleFile() == filename;
}

void SampleClip::setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)
{
	if (!sb) { return; }

	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_sample = Sample(std::move(sb));
		m_sampleThumbnailCache = std::make_shared<const SampleThumbnailCache>(m_sample.buffer());
	}

	updateLength();
	emit sampleChanged();
	Engine::getSong()->setModified();
}

void SampleClip::setSampleFile(const QString& sf)
{
	if (sf.isEmpty())
	{
		// If there is no sample, make the clip a bar long
		float nom = Engine::getSong()->getTimeSigModel().getNumerator();
		float den = Engine::getSong()->getTimeSigModel().getDenominator();
		changeLength(DefaultTicksPerBar * (nom / den));
	}
	else
	{
		const auto path = PathUtil::stringToPath(sf);

		// TODO: Return an error value back to the caller (via std::expected or our own polyfill)
		auto buffer = SampleCache::instance().fetchBuffer(path);
		if (!buffer) { return; }

		// TODO: Return an error value back to the caller (via std::expected or our own polyfill)
		auto thumbnailCache = SampleCache::instance().fetchThumbnailCache(path);
		if (!thumbnailCache) { return; }

		// Remove any prior offset in the clip
		setStartTimeOffset(0);

		m_missingSampleFile.clear();
		m_sample = Sample{std::move(buffer)};
		m_sampleThumbnailCache = std::move(thumbnailCache);
		updateLength();
	}

	emit sampleChanged();
	emit playbackPositionChanged();
}




void SampleClip::toggleRecord()
{
	m_recordModel.setValue( !m_recordModel.value() );
	emit dataChanged();
}




void SampleClip::playbackPositionChanged()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( getTrack(), PlayHandle::Type::SamplePlayHandle );
	auto st = dynamic_cast<SampleTrack*>(getTrack());
	st->setPlayingClips( false );
}




void SampleClip::updateTrackClips()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if( sampletrack)
	{
		sampletrack->updateClips();
	}
}




bool SampleClip::isPlaying() const
{
	return m_isPlaying;
}




void SampleClip::setIsPlaying(bool isPlaying)
{
	m_isPlaying = isPlaying;
}




void SampleClip::updateLength()
{
	// If the clip has already been manually resized, don't automatically resize it.
	if (getAutoResize())
	{
		if (getTrack()->trackContainer() == Engine::patternStore())
		{
			changeLength(TimePos::ticksPerBar() * Engine::patternStore()->lengthOfPattern(getTrack()->getClipNum(this)));
			return;
		}
		changeLength(sampleLength());
		setStartTimeOffset(0);
	}

	emit sampleChanged();
}


void SampleClip::tempoChanged()
{
	Clip::setStartTimeOffset(std::round(1.0f * m_startFrameOffset / Engine::framesPerTick()));
	updateLength();
	emit sampleChanged();
}

void SampleClip::setStartTimeOffset(const TimePos& startTimeOffset)
{
	m_startFrameOffset = startTimeOffset * Engine::framesPerTick();
	Clip::setStartTimeOffset(startTimeOffset);
}


TimePos SampleClip::sampleLength() const
{
	return static_cast<int>(m_sample.sampleSize() / Engine::framesPerTick(m_sample.sampleRate()));
}




void SampleClip::setSampleStartFrame(f_cnt_t startFrame)
{
	m_sample.setStartFrame(startFrame);
}




void SampleClip::setSamplePlayLength(f_cnt_t length)
{
	m_sample.setEndFrame(length);
}




void SampleClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	if( _this.parentNode().nodeName() == "clipboard" )
	{
		_this.setAttribute( "pos", -1 );
	}
	else
	{
		_this.setAttribute( "pos", startPosition() );
	}
	_this.setAttribute( "len", length() );
	_this.setAttribute( "muted", isMuted() );
	// Custom: keep the path of a missing sample so it can be relinked later
	const bool keepMissing = sampleFile().isEmpty() && !m_missingSampleFile.isEmpty();
	_this.setAttribute( "src", keepMissing ? m_missingSampleFile : sampleFile() );
	_this.setAttribute( "off", startTimeOffset() );
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	if( sampleFile() == "" && !keepMissing )
	{
		QString s;
		_this.setAttribute("data", m_sample.toBase64());
	}

	_this.setAttribute( "sample_rate", m_sample.sampleRate());
	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}
	if (m_sample.reversed())
	{
		_this.setAttribute("reversed", "true");
	}
	if (isLinked()) { _this.setAttribute("link", linkId()); }
	if (m_fadeIn > 0) { _this.setAttribute("fadein", m_fadeIn); }
	if (m_fadeOut > 0) { _this.setAttribute("fadeout", m_fadeOut); }
	// TODO: start- and end-frame
}




void SampleClip::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "pos" ).toInt() >= 0 )
	{
		movePosition( _this.attribute( "pos" ).toInt() );
	}

	if (const auto srcFile = _this.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setSampleFile(srcFile);
		}
		else
		{
			m_missingSampleFile = srcFile;
			Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile));
		}
	}

	if( sampleFile().isEmpty() && _this.hasAttribute( "data" ) )
	{
		auto sampleRate = _this.hasAttribute("sample_rate") ? _this.attribute("sample_rate").toInt() :
			Engine::audioEngine()->outputSampleRate();
		setSampleBuffer(SampleBuffer::fromBase64(_this.attribute("data"), sampleRate));
	}

	changeLength( _this.attribute( "len" ).toInt() );
	setMuted( _this.attribute( "muted" ).toInt() );
	setStartTimeOffset( _this.attribute( "off" ).toInt() );
	setAutoResize(_this.attribute("autoresize", "1").toInt());

	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	// Custom: also un-reverse (undo of "reverse" restores a state without the attribute)
	const bool reversed = _this.hasAttribute("reversed");
	if (reversed != m_sample.reversed())
	{
		m_sample.setReversed(reversed);
		emit wasReversed(); // tell SampleClipView to update the view
	}
	setLinkId(_this.attribute("link"));
	setFades(_this.attribute("fadein", "0").toInt(), _this.attribute("fadeout", "0").toInt());
}




void SampleClip::setFades(tick_t fadeIn, tick_t fadeOut)
{
	const tick_t len = std::max<tick_t>(0, length());
	fadeIn = std::clamp<tick_t>(fadeIn, 0, len);
	fadeOut = std::clamp<tick_t>(fadeOut, 0, len - fadeIn);
	if (fadeIn == m_fadeIn && fadeOut == m_fadeOut) { return; }
	m_fadeIn = fadeIn;
	m_fadeOut = fadeOut;
	emit dataChanged();
}




float SampleClip::fadeGain(double posInClip, double clipLength, double fadeIn, double fadeOut)
{
	// equal-power curves (sin / cos quarter wave)
	constexpr double halfPi = 1.5707963267948966;
	double gain = 1.0;
	if (fadeIn > 0 && posInClip < fadeIn)
	{
		gain *= posInClip <= 0 ? 0.0 : std::sin(halfPi * posInClip / fadeIn);
	}
	const double toEnd = clipLength - posInClip;
	if (fadeOut > 0 && toEnd < fadeOut)
	{
		gain *= toEnd <= 0 ? 0.0 : std::sin(halfPi * toEnd / fadeOut);
	}
	return static_cast<float>(gain);
}




gui::ClipView * SampleClip::createView( gui::TrackView * _tv )
{
	return new gui::SampleClipView( this, _tv );
}


} // namespace lmms
