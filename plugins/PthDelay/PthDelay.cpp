/*
 * PthDelay.cpp - Custom (PtH-LMMS): tempo synced (straight / dotted / triplet) ping pong delay
 *                with panning and a high-pass on the wet signal
 *
 * This file is part of LMMS - https://lmms.io (GPL v2 or later)
 */

#include "PthDelay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Engine.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT pthdelay_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"PtH Delay",
	QT_TRANSLATE_NOOP("PluginBrowser", "Tempo-synced ping pong delay (dotted/triplet, pan, bounce, high-pass on wet)"),
	"PtH-LMMS",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}

namespace
{
constexpr float MaxDelaySeconds = 8.f;

// pan -1 (L) .. 1 (R): the opposite side is turned down
inline float gainLeft(float pan) { return pan <= 0.f ? 1.f : 1.f - pan; }
inline float gainRight(float pan) { return pan >= 0.f ? 1.f : 1.f + pan; }
}


PthDelayEffect::PthDelayEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key)
	: Effect(&pthdelay_plugin_descriptor, parent, key)
	, m_controls(this)
{
	changeSampleRate();
}


void PthDelayEffect::changeSampleRate()
{
	m_sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	const auto size = static_cast<std::size_t>(m_sampleRate * MaxDelaySeconds) + 4;
	m_lineA.assign(size, 0.f);
	m_lineB.assign(size, 0.f);
	m_write = 0;
	m_delay = -1.f;
	m_hpCutoff = -1.f;
	m_x1 = m_x2 = m_y1 = m_y2 = 0.f;
}


void PthDelayEffect::updateHighpass(float cutoff)
{
	if (cutoff == m_hpCutoff) { return; }
	m_hpCutoff = cutoff;
	const float w0 = 2.f * std::numbers::pi_v<float> * std::clamp(cutoff, 10.f, m_sampleRate * 0.45f) / m_sampleRate;
	const float cosw = std::cos(w0);
	const float alpha = std::sin(w0) / (2.f * 0.7071f);
	const float a0 = 1.f + alpha;
	m_b0 = (1.f + cosw) / 2.f / a0;
	m_b1 = -(1.f + cosw) / a0;
	m_b2 = m_b0;
	m_a1 = -2.f * cosw / a0;
	m_a2 = (1.f - alpha) / a0;
}


float PthDelayEffect::read(const std::vector<float>& line, float delay) const
{
	const int size = static_cast<int>(line.size());
	float pos = static_cast<float>(m_write) - delay;
	while (pos < 0.f) { pos += static_cast<float>(size); }
	const int i0 = static_cast<int>(pos);
	const float frac = pos - static_cast<float>(i0);
	const int i1 = (i0 + 1) % size;
	return line[i0 % size] * (1.f - frac) + line[i1] * frac;
}


Effect::ProcessStatus PthDelayEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (m_lineA.empty()) { return ProcessStatus::Continue; }

	const float target = std::clamp(m_controls.delaySeconds() * m_sampleRate, 1.f, static_cast<float>(m_lineA.size() - 2));
	if (m_delay < 0.f) { m_delay = target; }
	const float smooth = 1.f - std::exp(-1.f / (0.04f * m_sampleRate)); // glide ~40 ms on time changes

	const float feedback = m_controls.m_feedbackModel.value() / 100.f;
	const float pan = m_controls.m_panModel.value() / 100.f;
	const bool bounce = m_controls.m_bounceModel.value();
	const float dry = m_controls.m_dryModel.value() / 100.f;
	const float wet = m_controls.m_wetModel.value() / 100.f;
	const float d = dryLevel();
	const float w = wetLevel();
	updateHighpass(m_controls.m_highpassModel.value());

	if (bounce != m_lastBounce)
	{
		std::fill(m_lineB.begin(), m_lineB.end(), 0.f);
		m_lastBounce = bounce;
	}

	// first echo at `pan`, with bounce the next at the mirrored position, and so on
	const float aL = gainLeft(pan), aR = gainRight(pan);
	const float bL = gainLeft(-pan), bR = gainRight(-pan);
	const int size = static_cast<int>(m_lineA.size());

	for (f_cnt_t f = 0; f < frames; ++f)
	{
		m_delay += (target - m_delay) * smooth;

		const float inL = buf[f].left();
		const float inR = buf[f].right();

		// high-pass on what goes into the delay -> every echo (the wet signal) is filtered, the dry one is not
		const float x = 0.5f * (inL + inR);
		const float y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
		m_x2 = m_x1; m_x1 = x;
		m_y2 = m_y1; m_y1 = y;

		const float a = read(m_lineA, m_delay);
		const float b = bounce ? read(m_lineB, m_delay) : 0.f;

		if (bounce)
		{
			m_lineA[m_write] = y + feedback * b;
			m_lineB[m_write] = feedback * a;
		}
		else
		{
			m_lineA[m_write] = y + feedback * a;
		}
		if (++m_write >= size) { m_write = 0; }

		const float echoL = a * aL + b * bL;
		const float echoR = a * aR + b * bR;
		const float outL = dry * inL + wet * echoL;
		const float outR = dry * inR + wet * echoR;

		buf[f].left() = d * inL + w * outL;
		buf[f].right() = d * inR + w * outR;
	}

	// denormal guard for the filter state
	if (std::abs(m_y1) < 1e-20f) { m_y1 = 0.f; }
	if (std::abs(m_y2) < 1e-20f) { m_y2 = 0.f; }

	return ProcessStatus::ContinueIfNotQuiet;
}


extern "C"
{

// needed for getting plugin out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new PthDelayEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
