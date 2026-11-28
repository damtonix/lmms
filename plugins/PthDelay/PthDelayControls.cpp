/*
 * PthDelayControls.cpp - Custom (PtH-LMMS): controls + dialog of PtH Delay
 *
 * This file is part of LMMS - https://lmms.io (GPL v2 or later)
 */

#include "PthDelayControls.h"

#include <QLabel>

#include "ComboBox.h"
#include "Engine.h"
#include "FontHelper.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "PthDelay.h"
#include "Song.h"

namespace lmms
{

PthDelayControls::PthDelayControls(PthDelayEffect* effect)
	: EffectControls(effect)
	, m_syncModel(true, this, tr("Sync"))
	, m_noteModel(this, tr("Note value"))
	, m_modeModel(this, tr("Note mode"))
	, m_timeModel(300.f, 1.f, 2000.f, 1.f, this, tr("Time"))
	, m_feedbackModel(40.f, 0.f, 95.f, 0.1f, this, tr("Feedback"))
	, m_panModel(-50.f, -100.f, 100.f, 1.f, this, tr("Pan"))
	, m_bounceModel(true, this, tr("Bounce"))
	, m_highpassModel(250.f, 20.f, 2000.f, 1.f, this, tr("High-pass"))
	, m_dryModel(100.f, 0.f, 100.f, 0.1f, this, tr("Dry"))
	, m_wetModel(35.f, 0.f, 100.f, 0.1f, this, tr("Wet"))
	, m_effect(effect)
{
	for (const char* note : {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"}) { m_noteModel.addItem(note); }
	m_noteModel.setValue(3); // 1/8
	m_modeModel.addItem(tr("Gerade"));
	m_modeModel.addItem(tr("Punktiert"));
	m_modeModel.addItem(tr("Triole"));
	m_modeModel.setValue(1); // dotted
	m_highpassModel.setScaleLogarithmic(true);
	m_timeModel.setScaleLogarithmic(true);

	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, &PthDelayControls::changeSampleRate);
}


float PthDelayControls::delaySeconds() const
{
	if (!m_syncModel.value()) { return m_timeModel.value() / 1000.f; }
	static constexpr float wholes[] = {1.f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
	const int note = std::clamp(m_noteModel.value(), 0, 5);
	const float bpm = std::max(1.f, static_cast<float>(Engine::getSong()->getTempo()));
	float seconds = wholes[note] * 4.f * 60.f / bpm; // quarter note = one beat
	switch (m_modeModel.value())
	{
		case 1: seconds *= 1.5f; break;       // dotted
		case 2: seconds *= 2.f / 3.f; break;  // triplet
		default: break;
	}
	return seconds;
}


void PthDelayControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_syncModel.saveSettings(doc, parent, "sync");
	m_noteModel.saveSettings(doc, parent, "note");
	m_modeModel.saveSettings(doc, parent, "mode");
	m_timeModel.saveSettings(doc, parent, "time");
	m_feedbackModel.saveSettings(doc, parent, "feedback");
	m_panModel.saveSettings(doc, parent, "pan");
	m_bounceModel.saveSettings(doc, parent, "bounce");
	m_highpassModel.saveSettings(doc, parent, "highpass");
	m_dryModel.saveSettings(doc, parent, "dry");
	m_wetModel.saveSettings(doc, parent, "wet");
}


void PthDelayControls::loadSettings(const QDomElement& elem)
{
	m_syncModel.loadSettings(elem, "sync");
	m_noteModel.loadSettings(elem, "note");
	m_modeModel.loadSettings(elem, "mode");
	m_timeModel.loadSettings(elem, "time");
	m_feedbackModel.loadSettings(elem, "feedback");
	m_panModel.loadSettings(elem, "pan");
	m_bounceModel.loadSettings(elem, "bounce");
	m_highpassModel.loadSettings(elem, "highpass");
	m_dryModel.loadSettings(elem, "dry");
	m_wetModel.loadSettings(elem, "wet");
}


void PthDelayControls::changeSampleRate()
{
	m_effect->changeSampleRate();
}


gui::EffectControlDialog* PthDelayControls::createView()
{
	return new gui::PthDelayDialog(this);
}


namespace gui
{

PthDelayDialog::PthDelayDialog(PthDelayControls* controls)
	: EffectControlDialog(controls)
{
	setAutoFillBackground(true);
	QPalette pal = palette();
	pal.setColor(backgroundRole(), QColor(38, 42, 48));
	setPalette(pal);
	setFixedSize(330, 128);

	auto title = new QLabel(tr("PtH DELAY"), this);
	title->setStyleSheet("color: #9fb4c8; font-weight: bold; font-size: 11px;");
	title->move(10, 6);

	// row 1: timing
	auto sync = new LedCheckBox(tr("SYNC"), this, tr("Sync"), LedCheckBox::LedColor::Green);
	sync->setModel(&controls->m_syncModel);
	sync->setToolTip(tr("Delay-Zeit auf das Song-Tempo (BPM) synchronisieren"));
	sync->move(10, 30);

	auto note = new ComboBox(this);
	note->setGeometry(70, 26, 62, ComboBox::DEFAULT_HEIGHT);
	note->setModel(&controls->m_noteModel);
	note->setToolTip(tr("Notenwert"));

	auto mode = new ComboBox(this);
	mode->setGeometry(138, 26, 100, ComboBox::DEFAULT_HEIGHT);
	mode->setModel(&controls->m_modeModel);
	mode->setToolTip(tr("Gerade / punktiert (x1.5) / Triole (x2/3)"));

	auto bounce = new LedCheckBox(tr("BOUNCE"), this, tr("Bounce"), LedCheckBox::LedColor::Green);
	bounce->setModel(&controls->m_bounceModel);
	bounce->setToolTip(tr("Ping-Pong: Jedes Echo springt auf die Gegenseite (Pan L 60 % -> R 60 % -> L ...)"));
	bounce->move(250, 30);

	// row 2: knobs
	auto makeKnob = [this](int x, FloatModel* model, const QString& label, const QString& hint, const QString& unit) {
		auto knob = new Knob(KnobType::Bright26, label, SMALL_FONT_SIZE, this);
		knob->move(x, 70);
		knob->setModel(model);
		knob->setHintText(hint, unit);
		return knob;
	};
	auto time = makeKnob(10, &controls->m_timeModel, tr("TIME"), tr("Zeit (ohne Sync):"), " ms");
	makeKnob(60, &controls->m_feedbackModel, tr("FDBK"), tr("Feedback:"), " %");
	makeKnob(110, &controls->m_panModel, tr("PAN"), tr("Pan erstes Echo (-100 = L, 100 = R):"), "");
	makeKnob(160, &controls->m_highpassModel, tr("HPF"), tr("High-pass auf dem Wet-Signal:"), " Hz");
	makeKnob(222, &controls->m_dryModel, tr("DRY"), tr("Dry:"), " %");
	makeKnob(272, &controls->m_wetModel, tr("WET"), tr("Wet:"), " %");

	// the time knob only matters without sync
	auto updateTime = [time, controls] { time->setEnabled(!controls->m_syncModel.value()); };
	connect(&controls->m_syncModel, &BoolModel::dataChanged, time, updateTime);
	updateTime();
}

} // namespace gui

} // namespace lmms
