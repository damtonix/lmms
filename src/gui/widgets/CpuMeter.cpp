/*
 * CpuMeter.cpp - Custom: small CPU load display for a track or mixer channel
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License v2 or later.
 */

#include "CpuMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "FontHelper.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{
constexpr std::int64_t PollIntervalNs = 250'000'000; // 4 updates per second
}

CpuMeter::CpuMeter(std::function<std::int64_t()> sampler, const QString& what, QWidget* parent)
	: QWidget(parent)
	, m_sampler(std::move(sampler))
	, m_what(what)
{
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	m_clock.start();
	m_sampler(); // drop whatever was collected before
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, &CpuMeter::poll);
	setToolTip(tr("CPU: %1").arg(m_what));
}

QColor CpuMeter::loadColor(float percent)
{
	if (percent < 25.f) { return QColor(62, 195, 74); }   // green
	if (percent < 60.f) { return QColor(224, 192, 48); }  // yellow
	return QColor(224, 64, 42);                           // red
}

void CpuMeter::poll()
{
	m_collected += m_sampler();
	const auto elapsed = m_clock.nsecsElapsed();
	if (elapsed < PollIntervalNs) { return; }
	m_clock.restart();

	const float load = 100.f * static_cast<float>(m_collected) / static_cast<float>(elapsed);
	m_collected = 0;
	// rise fast, fall slowly
	m_load = load > m_load ? 0.5f * m_load + 0.5f * load : 0.75f * m_load + 0.25f * load;
	if (m_load < 0.05f) { m_load = 0.f; }

	const int shown = static_cast<int>(std::lround(m_load * 10.f));
	if (shown != m_shown)
	{
		m_shown = shown;
		setToolTip(tr("CPU: %1\n%2 % of the real time (smoothed)")
			.arg(m_what).arg(QString::number(m_load, 'f', 1)));
		update();
	}
}

void CpuMeter::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	p.setPen(QColor(255, 255, 255, 40));
	p.setBrush(QColor(0, 0, 0, 110));
	p.drawRoundedRect(r, 2, 2);

	const bool vertical = height() > 2 * width();
	if (vertical)
	{
		if (m_load > 0.f)
		{
			QRectF bar = r.adjusted(1, 1, -1, -1);
			const double h = std::max(1.0, bar.height() * std::min(m_load, 100.f) / 100.f);
			bar.setTop(bar.bottom() - h);
			p.setPen(Qt::NoPen);
			p.setBrush(loadColor(m_load));
			p.drawRect(bar);
		}
		return;
	}

	if (m_load > 0.f)
	{
		const float fraction = std::min(m_load, 100.f) / 100.f;
		QRectF bar = r.adjusted(1, 1, -1, -1);
		bar.setWidth(std::max(1.0, bar.width() * fraction));
		QColor c = loadColor(m_load);
		c.setAlpha(190);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawRoundedRect(bar, 1.5, 1.5);
	}

	p.setFont(adjustedToPixelSize(font(), 9));
	p.setPen(m_load > 0.f ? QColor(255, 255, 255) : QColor(255, 255, 255, 110));
	const QString text = m_load >= 10.f || m_load == 0.f
		? QString("%1%").arg(static_cast<int>(std::lround(m_load)))
		: QString("%1%").arg(QString::number(m_load, 'f', 1));
	p.drawText(rect(), Qt::AlignCenter, text);
}

} // namespace lmms::gui
