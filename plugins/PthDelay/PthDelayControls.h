/*
 * PthDelayControls.h - Custom (PtH-LMMS): controls + dialog of PtH Delay
 *
 * This file is part of LMMS - https://lmms.io (GPL v2 or later)
 */

#ifndef LMMS_PTH_DELAY_CONTROLS_H
#define LMMS_PTH_DELAY_CONTROLS_H

#include "ComboBoxModel.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"

namespace lmms
{

class PthDelayEffect;

namespace gui
{
class PthDelayDialog;
}

class PthDelayControls : public EffectControls
{
	Q_OBJECT
public:
	explicit PthDelayControls(PthDelayEffect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& elem) override;
	QString nodeName() const override { return "PthDelay"; }
	int controlCount() override { return 11; }
	gui::EffectControlDialog* createView() override;

	//! delay time in seconds for the current settings and tempo
	float delaySeconds() const;

	BoolModel m_syncModel;
	ComboBoxModel m_noteModel;   // 1/1 ... 1/32
	ComboBoxModel m_modeModel;   // straight, dotted, triplet
	FloatModel m_timeModel;      // ms, when not synced
	FloatModel m_feedbackModel;  // %
	FloatModel m_panModel;       // -100 (L) .. 100 (R): pan of the first echo
	BoolModel m_bounceModel;     // ping pong: every echo jumps to the other side
	FloatModel m_highpassModel;  // Hz, on the wet signal
	FloatModel m_dryModel;       // %
	FloatModel m_wetModel;       // %

private slots:
	void changeSampleRate();

private:
	PthDelayEffect* m_effect;
};


namespace gui
{

class PthDelayDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit PthDelayDialog(PthDelayControls* controls);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_PTH_DELAY_CONTROLS_H
