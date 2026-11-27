/*
 * CpuTime.h - Custom: per-track / per-mixer-channel CPU time counters
 *
 * Audio threads add the time they spend for a track, the GUI takes the sum periodically
 * and divides it by the wall-clock time since the last read.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License v2 or later.
 */

#ifndef LMMS_CPU_TIME_H
#define LMMS_CPU_TIME_H

#include <atomic>
#include <chrono>
#include <cstdint>

namespace lmms
{

class CpuTimeCounter
{
public:
	void add(std::int64_t ns) const { m_ns.fetch_add(ns, std::memory_order_relaxed); }
	//! returns the time collected since the last call and resets it
	std::int64_t take() const { return m_ns.exchange(0, std::memory_order_relaxed); }

private:
	mutable std::atomic<std::int64_t> m_ns{0};
};

//! Measures the time of its scope and adds it to the counter (if any)
class CpuTimeProbe
{
public:
	explicit CpuTimeProbe(const CpuTimeCounter* counter)
		: m_counter(counter)
	{
		if (m_counter) { m_start = std::chrono::steady_clock::now(); }
	}
	~CpuTimeProbe()
	{
		if (m_counter)
		{
			m_counter->add(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - m_start).count());
		}
	}
	CpuTimeProbe(const CpuTimeProbe&) = delete;
	CpuTimeProbe& operator=(const CpuTimeProbe&) = delete;

private:
	const CpuTimeCounter* m_counter;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace lmms

#endif // LMMS_CPU_TIME_H
