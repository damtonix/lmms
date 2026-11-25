/*
 * DuckDialog.cpp - settings of the beat-synced "Kick-Ducking" of a track (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "DuckDialog.h"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QSlider>
#include <QVBoxLayout>

#include "Engine.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

//! Shows the volume over two grid periods, or one bar of the real kick hits
class DuckCurveWidget : public QWidget
{
public:
	explicit DuckCurveWidget(QWidget* parent) : QWidget(parent) { setMinimumSize(360, 100); }
	void setSettings(const Track::DuckSettings& settings, bool useKick)
	{
		m_settings = settings;
		m_useKick = useKick;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -6);
		p.fillRect(rect(), QColor(30, 33, 38));

		const auto song = Engine::getSong();
		const double bpm = song->getTempo();
		const double msPerTick = 60000.0 / (bpm * DefaultTicksPerBar / 4.0);
		const auto map = song->kickMap();
		const bool kick = m_useKick && map != nullptr && map->hasSource;

		// what is shown: [from, from + span) in ticks
		double from = 0;
		double span = 0;
		int divisions = 2;
		std::vector<Song::KickHit> hits;
		QString caption;
		if (kick)
		{
			const double barTicks = TimePos::ticksPerBar();
			span = barTicks;
			divisions = 4;
			const auto& all = map->song;
			double bar = std::floor(song->getPlayPos(Song::PlayMode::Song).getTicks() / barTicks) * barTicks;
			const auto firstFrom = [&all](double t) {
				return std::lower_bound(all.begin(), all.end(), t,
					[](const Song::KickHit& h, double v) { return h.tick < v; });
			};
			if (auto it = firstFrom(bar); it == all.end() || it->tick >= bar + barTicks)
			{
				if (it != all.end()) { bar = std::floor(it->tick / barTicks) * barTicks; }
				else if (!all.empty()) { bar = std::floor(all.front().tick / barTicks) * barTicks; }
			}
			from = bar;
			// include the hit before the bar (its release can reach into it)
			auto it = firstFrom(from);
			if (it != all.begin()) { --it; }
			for (; it != all.end() && it->tick < from + span + 1; ++it) { hits.push_back(*it); }
			caption = all.empty()
				? DuckDialog::tr("Kick-Spur hat (noch) keine Schl\xc3\xa4ge im Song")
				: DuckDialog::tr("Takt %1, echte Kick (%2 BPM)").arg(static_cast<int>(from / barTicks) + 1).arg(bpm);
		}
		else
		{
			const double period = Track::duckRateTicks(m_settings.rate);
			span = period * 2;
			for (int i = 0; i < 3; ++i) { hits.push_back({period * i, 1.f}); }
			caption = DuckDialog::tr("%1 BPM, ein Abschnitt = %2 ms").arg(bpm).arg(qRound(period * msPerTick));
		}

		// beat lines
		p.setPen(QColor(70, 76, 86));
		for (int i = 0; i <= divisions; ++i)
		{
			const double x = r.left() + r.width() * i / divisions;
			p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
		}
		p.drawLine(QPointF(r.left(), r.top()), QPointF(r.right(), r.top()));

		const double attackTicks = Track::DuckAttackMs / msPerTick;
		QPainterPath path;
		const int steps = static_cast<int>(r.width());
		for (int i = 0; i <= steps; ++i)
		{
			const double t = from + span * i / steps;
			double gain = 1.0;
			for (auto h = hits.rbegin(); h != hits.rend(); ++h)
			{
				if (h->tick <= t + attackTicks)
				{
					gain = Track::duckGain(m_settings, (t - h->tick) * msPerTick + Track::DuckAttackMs,
						m_settings.followVelocity ? h->strength : 1.f);
					break;
				}
			}
			const QPointF pt(r.left() + i, r.bottom() - gain * r.height());
			if (i == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
		}
		QPainterPath fill = path;
		fill.lineTo(r.bottomRight());
		fill.lineTo(r.bottomLeft());
		fill.closeSubpath();
		const QColor color = m_settings.enabled ? QColor(143, 166, 192) : QColor(110, 115, 122);
		QColor fillColor = color;
		fillColor.setAlpha(60);
		p.fillPath(fill, fillColor);
		p.setPen(QPen(color, 2));
		p.drawPath(path);

		if (kick)
		{
			// the hits themselves, height = strength
			p.setPen(QPen(QColor(230, 160, 90), 2));
			for (const auto& h : hits)
			{
				if (h.tick < from || h.tick >= from + span) { continue; }
				const double x = r.left() + r.width() * (h.tick - from) / span;
				p.drawLine(QPointF(x, r.top()), QPointF(x, r.top() + 4 + 10 * h.strength));
			}
		}

		p.setPen(QColor(150, 156, 164));
		QFont f = font();
		f.setPointSizeF(std::max(7.0, f.pointSizeF() * 0.85));
		p.setFont(f);
		p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom, caption);
	}

private:
	Track::DuckSettings m_settings;
	bool m_useKick = false;
};


void DuckDialog::showFor(Track* track, QWidget* parent)
{
	auto dialog = new DuckDialog(track, parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}


DuckDialog::DuckDialog(Track* track, QWidget* parent)
	: QDialog(parent)
	, m_track(track)
{
	setWindowTitle(tr("Kick-Ducking: %1").arg(track->name()));
	const auto settings = track->duckSettings();
	auto layout = new QVBoxLayout(this);

	auto intro = new QLabel(track->type() == Track::Type::Group
		? tr("Senkt die Lautst\xc3\xa4rke aller Spuren dieser Gruppe auf jedem Schlag kurz ab "
			"(wie Sidechain auf die Kick). Spuren mit eigenem Ducking nutzen ihr eigenes.")
		: tr("Senkt die Lautst\xc3\xa4rke dieser Spur auf jedem Schlag kurz ab (wie Sidechain auf die Kick). "
			"Wirkt nach den Spur-Effekten, nur w\xc3\xa4hrend der Wiedergabe."), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	m_enabled = new QCheckBox(tr("Ducking aktiv"), this);
	m_enabled->setChecked(settings.enabled);
	layout->addWidget(m_enabled);

	auto grid = new QGridLayout;
	int row = 0;
	auto addSlider = [&](const QString& name, int min, int max, int value, QSlider*& slider, QLabel*& label) {
		grid->addWidget(new QLabel(name, this), row, 0);
		slider = new QSlider(Qt::Horizontal, this);
		slider->setRange(min, max);
		slider->setValue(value);
		grid->addWidget(slider, row, 1);
		label = new QLabel(this);
		label->setMinimumWidth(60);
		grid->addWidget(label, row, 2);
		++row;
	};
	addSlider(tr("Tiefe"), 0, 100, qRound(settings.depth * 100), m_depth, m_depthLabel);
	addSlider(tr("Halten"), 0, 200, qRound(settings.holdMs), m_hold, m_holdLabel);
	addSlider(tr("Release"), 10, 1000, qRound(settings.releaseMs), m_release, m_releaseLabel);

	grid->addWidget(new QLabel(tr("Ausl\xc3\xb6ser"), this), row, 0);
	m_source = new QComboBox(this);
	m_source->addItems({tr("Echte Kick (Schl\xc3\xa4ge der Kick-Spur)"), tr("Festes Raster")});
	m_source->setCurrentIndex(settings.source);
	grid->addWidget(m_source, row++, 1, 1, 2);

	grid->addWidget(new QLabel(tr("Kick-Spur"), this), row, 0);
	m_kickTrack = new QComboBox(this);
	grid->addWidget(m_kickTrack, row++, 1, 1, 2);
	m_kickInfo = new QLabel(this);
	m_kickInfo->setWordWrap(true);
	grid->addWidget(m_kickInfo, row++, 1, 1, 2);
	m_velocity = new QCheckBox(tr("Leisere Kick-Schl\xc3\xa4ge ducken weniger"), this);
	m_velocity->setChecked(settings.followVelocity);
	grid->addWidget(m_velocity, row++, 1, 1, 2);

	grid->addWidget(new QLabel(tr("Rhythmus"), this), row, 0);
	m_rate = new QComboBox(this);
	m_rate->addItems({tr("1/4 (jeder Kick)"), tr("1/8"), tr("1/2"), tr("1/16")});
	m_rate->setCurrentIndex(settings.rate);
	grid->addWidget(m_rate, row++, 1, 1, 2);

	grid->addWidget(new QLabel(tr("Kurve"), this), row, 0);
	m_shape = new QComboBox(this);
	m_shape->addItems({tr("Weich"), tr("Linear"), tr("Pumpend")});
	m_shape->setCurrentIndex(settings.shape);
	grid->addWidget(m_shape, row++, 1, 1, 2);
	layout->addLayout(grid);

	m_curve = new DuckCurveWidget(this);
	layout->addWidget(m_curve);

	auto buttons = new QDialogButtonBox(this);
	auto reset = buttons->addButton(tr("Standard"), QDialogButtonBox::ResetRole);
	buttons->addButton(QDialogButtonBox::Close);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(reset, &QPushButton::clicked, this, [this] {
		const Track::DuckSettings defaults;
		m_source->setCurrentIndex(defaults.source);
		m_velocity->setChecked(defaults.followVelocity);
		m_depth->setValue(qRound(defaults.depth * 100));
		m_hold->setValue(qRound(defaults.holdMs));
		m_release->setValue(qRound(defaults.releaseMs));
		m_rate->setCurrentIndex(defaults.rate);
		m_shape->setCurrentIndex(defaults.shape);
	});

	connect(m_enabled, &QCheckBox::toggled, this, &DuckDialog::apply);
	for (auto slider : {m_depth, m_hold, m_release})
	{
		connect(slider, &QSlider::valueChanged, this, &DuckDialog::apply);
	}
	connect(m_rate, qOverload<int>(&QComboBox::currentIndexChanged), this, &DuckDialog::apply);
	connect(m_shape, qOverload<int>(&QComboBox::currentIndexChanged), this, &DuckDialog::apply);
	connect(m_source, qOverload<int>(&QComboBox::currentIndexChanged), this, &DuckDialog::apply);
	connect(m_velocity, &QCheckBox::toggled, this, &DuckDialog::apply);
	connect(m_kickTrack, qOverload<int>(&QComboBox::activated), this, &DuckDialog::chooseKickTrack);

	// follow the play position and edits of the kick
	auto refresh = new QTimer(this);
	refresh->setInterval(300);
	connect(refresh, &QTimer::timeout, m_curve, qOverload<>(&QWidget::update));
	refresh->start();
	connect(track, &Track::destroyedTrack, this, &QDialog::close);

	autoDetectKick();
	fillKickTracks();
	updateLabels();
	m_curve->setSettings(settings, settings.source == 0);
}


QList<Track*> DuckDialog::kickCandidates() const
{
	QList<Track*> result;
	for (const auto track : Engine::getSong()->tracks())
	{
		if (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample) { result.push_back(track); }
	}
	for (const auto track : Engine::patternStore()->tracks())
	{
		if (track->type() == Track::Type::Instrument) { result.push_back(track); }
	}
	return result;
}


void DuckDialog::autoDetectKick()
{
	m_autoDetected.clear();
	const auto candidates = kickCandidates();
	for (const auto track : candidates)
	{
		if (track->isKickSource()) { return; }
	}
	static const QRegularExpression kickName(QStringLiteral("kick|bassdrum|bass drum|\\bbd\\b|\\bkik\\b"),
		QRegularExpression::CaseInsensitiveOption);
	for (const auto track : candidates)
	{
		if (track != m_track.data() && kickName.match(track->name()).hasMatch())
		{
			track->setKickSource(true);
			Engine::getSong()->setModified();
			Engine::getSong()->updateKickMap();
			m_autoDetected = track->name();
			return;
		}
	}
}


void DuckDialog::fillKickTracks()
{
	const QSignalBlocker blocker(m_kickTrack);
	m_kickTrack->clear();
	m_kickTrack->addItem(tr("(keine festgelegt)"));
	QStringList marked;
	int current = 0;
	for (const auto track : kickCandidates())
	{
		const bool inPattern = track->trackContainer() == Engine::patternStore();
		const QString name = inPattern ? tr("%1 (Pattern-Editor)").arg(track->name()) : track->name();
		m_kickTrack->addItem(name, QVariant::fromValue<void*>(track));
		if (track->isKickSource())
		{
			marked << name;
			current = m_kickTrack->count() - 1;
		}
	}
	if (marked.size() > 1)
	{
		m_kickTrack->insertItem(1, tr("Mehrere: %1").arg(marked.join(", ")), QVariant::fromValue<void*>(nullptr));
		current = 1;
	}
	m_kickTrack->setCurrentIndex(current);

	if (marked.isEmpty())
	{
		m_kickInfo->setText(tr("Noch keine Kick-Spur: hier ausw\xc3\xa4hlen (oder Zahnrad-Men\xc3\xbc der Kick \xe2\x86\x92 "
			"\"Als Kick-Quelle\"). Bis dahin wird das feste Raster benutzt."));
	}
	else if (!m_autoDetected.isEmpty())
	{
		m_kickInfo->setText(tr("\"%1\" wurde am Namen als Kick erkannt, bitte pr\xc3\xbc" "fen.").arg(m_autoDetected));
	}
	else
	{
		m_kickInfo->setText(tr("Reagiert auf jeden Schlag dieser Spur, auch Rolls, Breaks und versetzte Kicks."));
	}
}


void DuckDialog::chooseKickTrack(int index)
{
	if (index < 0) { return; }
	const auto chosen = static_cast<Track*>(m_kickTrack->itemData(index).value<void*>());
	if (chosen == nullptr && index != 0) { return; } // "Mehrere": keep as it is
	for (const auto track : kickCandidates())
	{
		track->setKickSource(track == chosen);
	}
	m_autoDetected.clear();
	Engine::getSong()->setModified();
	Engine::getSong()->updateKickMap();
	fillKickTracks();
	updateLabels();
	m_curve->update();
}


void DuckDialog::updateLabels()
{
	m_depthLabel->setText(QString("%1 %").arg(m_depth->value()));
	m_holdLabel->setText(QString("%1 ms").arg(m_hold->value()));
	m_releaseLabel->setText(QString("%1 ms").arg(m_release->value()));
	const bool on = m_enabled->isChecked();
	for (auto w : std::initializer_list<QWidget*>{m_depth, m_hold, m_release, m_shape, m_source})
	{
		w->setEnabled(on);
	}
	const bool kick = m_source->currentIndex() == 0;
	const auto map = Engine::getSong()->kickMap();
	const bool hasKick = map != nullptr && map->hasSource;
	m_kickTrack->setEnabled(on && kick);
	m_kickInfo->setEnabled(on && kick);
	m_velocity->setEnabled(on && kick && hasKick);
	m_rate->setEnabled(on && (!kick || !hasKick));
}


void DuckDialog::apply()
{
	updateLabels();
	Track::DuckSettings settings;
	settings.enabled = m_enabled->isChecked();
	settings.depth = m_depth->value() / 100.f;
	settings.holdMs = static_cast<float>(m_hold->value());
	settings.releaseMs = static_cast<float>(m_release->value());
	settings.rate = m_rate->currentIndex();
	settings.shape = m_shape->currentIndex();
	settings.source = m_source->currentIndex();
	settings.followVelocity = m_velocity->isChecked();
	m_curve->setSettings(settings, settings.source == 0);
	if (m_track.isNull()) { return; }
	if (!(m_track->duckSettings() == settings))
	{
		m_track->setDuckSettings(settings);
		Engine::getSong()->setModified();
	}
}

} // namespace lmms::gui
