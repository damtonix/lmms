/*
 * CpuMeter.h - Custom: small CPU load display for a track or mixer channel
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License v2 or later.
 */

#ifndef LMMS_GUI_CPU_METER_H
#define LMMS_GUI_CPU_METER_H

#include <cstdint>
#include <functional>

#include <QElapsedTimer>
#include <QWidget>

namespace lmms::gui
{

class CpuMeter : public QWidget
{
	Q_OBJECT
public:
	//! sampler returns the CPU time (ns) collected since its last call
	CpuMeter(std::function<std::int64_t()> sampler, const QString& what, QWidget* parent);

	static QColor loadColor(float percent);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	void poll();

	std::function<std::int64_t()> m_sampler;
	QString m_what;
	QElapsedTimer m_clock;
	std::int64_t m_collected = 0;
	float m_load = 0.f; //!< smoothed, percent of real time
	int m_shown = -1;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CPU_METER_H
