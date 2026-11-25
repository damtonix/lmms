/*
 * DuckDialog.h - settings of the beat-synced "Kick-Ducking" of a track (custom)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_DUCK_DIALOG_H
#define LMMS_GUI_DUCK_DIALOG_H

#include <QDialog>
#include <QList>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

class DuckCurveWidget;

//! Non-modal dialog, every change is applied (and audible) immediately
class DuckDialog : public QDialog
{
public:
	static void showFor(Track* track, QWidget* parent);

private:
	DuckDialog(Track* track, QWidget* parent);
	void apply();
	void updateLabels();
	QList<Track*> kickCandidates() const;
	void autoDetectKick();
	void fillKickTracks();
	void chooseKickTrack(int index);

	QPointer<Track> m_track;
	QCheckBox* m_enabled;
	QSlider* m_depth;
	QSlider* m_hold;
	QSlider* m_release;
	QComboBox* m_rate;
	QComboBox* m_shape;
	QComboBox* m_source;
	QComboBox* m_kickTrack;
	QLabel* m_kickInfo;
	QCheckBox* m_velocity;
	QString m_autoDetected;
	QLabel* m_depthLabel;
	QLabel* m_holdLabel;
	QLabel* m_releaseLabel;
	DuckCurveWidget* m_curve;
};

} // namespace lmms::gui

#endif // LMMS_GUI_DUCK_DIALOG_H
