/*
 * PthDelay.h - Custom (PtH-LMMS): tempo synced (straight / dotted / triplet) ping pong delay
 *              with panning and a high-pass on the wet signal
 *
 * This file is part of LMMS - https://lmms.io (GPL v2 or later)
 */

#ifndef LMMS_PTH_DELAY_H
#define LMMS_PTH_DELAY_H

#include <vector>

#include "Effect.h"
#include "PthDelayControls.h"

namespace lmms
{

class PthDelayEffect : public Effect
{
public:
	PthDelayEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

	void changeSampleRate();

private:
	float read(const std::vector<float>& line, float delay) const;
	void updateHighpass(float cutoff);

	PthDelayControls m_controls;

	float m_sampleRate = 44100.f;
	std::vector<float> m_lineA; // echoes 1, 3, 5, ... (pan)
	std::vector<float> m_lineB; // echoes 2, 4, 6, ... (mirrored pan, bounce only)
	int m_write = 0;
	float m_delay = -1.f;       // smoothed delay in samples
	bool m_lastBounce = true;

	// high-pass (RBJ biquad, Q 0.707) on the signal going into the delay = on all echoes
	float m_hpCutoff = -1.f;
	float m_b0 = 1.f, m_b1 = 0.f, m_b2 = 0.f, m_a1 = 0.f, m_a2 = 0.f;
	float m_x1 = 0.f, m_x2 = 0.f, m_y1 = 0.f, m_y2 = 0.f;
};

} // namespace lmms

#endif // LMMS_PTH_DELAY_H
