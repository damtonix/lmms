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
#include <QSlider>
#include <QVBoxLayout>

#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

//! Shows the volume over two duck periods at the current tempo
class DuckCurveWidget : public QWidget
{
public:
	explicit DuckCurveWidget(QWidget* parent) : QWidget(parent) { setMinimumSize(320, 90); }
	void setSettings(const Track::DuckSettings& settings) { m_settings = settings; update(); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -6);
		p.fillRect(rect(), QColor(30, 33, 38));

		const double bpm = Engine::getSong()->getTempo();
		const double periodMs = Track::duckRateTicks(m_settings.rate) * 60000.0 / (bpm * DefaultTicksPerBar / 4.0);
		constexpr int Periods = 2;

		// beat lines
		p.setPen(QColor(70, 76, 86));
		for (int i = 0; i <= Periods; ++i)
		{
			const double x = r.left() + r.width() * i / Periods;
			p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
		}
		p.drawLine(QPointF(r.left(), r.top()), QPointF(r.right(), r.top()));

		QPainterPath path;
		const int steps = static_cast<int>(r.width());
		for (int i = 0; i <= steps; ++i)
		{
			const double t = periodMs * Periods * i / steps;
			const double gain = Track::duckGain(m_settings, std::fmod(t, periodMs));
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

		p.setPen(QColor(150, 156, 164));
		QFont f = font();
		f.setPointSizeF(std::max(7.0, f.pointSizeF() * 0.85));
		p.setFont(f);
		p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom,
			DuckDialog::tr("%1 BPM, ein Abschnitt = %2 ms").arg(bpm).arg(qRound(periodMs)));
	}

private:
	Track::DuckSettings m_settings;
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
	connect(track, &Track::destroyedTrack, this, &QDialog::close);

	updateLabels();
	m_curve->setSettings(settings);
}


void DuckDialog::updateLabels()
{
	m_depthLabel->setText(QString("%1 %").arg(m_depth->value()));
	m_holdLabel->setText(QString("%1 ms").arg(m_hold->value()));
	m_releaseLabel->setText(QString("%1 ms").arg(m_release->value()));
	for (auto w : std::initializer_list<QWidget*>{m_depth, m_hold, m_release, m_rate, m_shape})
	{
		w->setEnabled(m_enabled->isChecked());
	}
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
	m_curve->setSettings(settings);
	if (m_track.isNull()) { return; }
	if (!(m_track->duckSettings() == settings))
	{
		m_track->setDuckSettings(settings);
		Engine::getSong()->setModified();
	}
}

} // namespace lmms::gui
