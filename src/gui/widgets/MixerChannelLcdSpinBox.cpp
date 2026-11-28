/*
 * MixerChannelLcdSpinBox.cpp - a specialization of LcdSpnBox for setting mixer channels
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "MixerChannelLcdSpinBox.h"

#include <QInputDialog>
#include <QMouseEvent>

#include "CaptionMenu.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "TrackView.h"

namespace lmms::gui
{


void MixerChannelLcdSpinBox::setTrackView(TrackView * tv)
{
	m_tv = tv;
}

void MixerChannelLcdSpinBox::mouseDoubleClickEvent(QMouseEvent* event)
{
	event->accept();
}

void MixerChannelLcdSpinBox::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		// show the track's own channel in the mixer
		auto mixer = getGUI()->mixerView();
		mixer->setCurrentMixerChannel(model()->value());
		mixer->parentWidget()->show();
		mixer->show();
		mixer->setFocus();
	}
	event->accept();
}

void MixerChannelLcdSpinBox::mouseMoveEvent(QMouseEvent* event)
{
	event->accept();
}

void MixerChannelLcdSpinBox::mouseReleaseEvent(QMouseEvent* event)
{
	if (event != nullptr) { event->accept(); }
}

void MixerChannelLcdSpinBox::wheelEvent(QWheelEvent* event)
{
	event->accept();
}

void MixerChannelLcdSpinBox::contextMenuEvent(QContextMenuEvent* event)
{
	QPointer<CaptionMenu> contextMenu = new CaptionMenu(model()->displayName(), this);
	contextMenu->addAction(tr("Im Mixer zeigen"), this, [this] {
		auto mixer = getGUI()->mixerView();
		mixer->setCurrentMixerChannel(model()->value());
		mixer->parentWidget()->show();
		mixer->show();
	});
	contextMenu->exec(QCursor::pos());
	delete contextMenu;
}

void MixerChannelLcdSpinBox::enterValue()
{
	const auto val = model()->value();
	const auto min = model()->minValue();
	const auto max = model()->maxValue();
	const auto step = model()->step<int>();
	const auto label = tr("Please enter a new value between %1 and %2:").arg(min).arg(max);

	auto ok = false;
	const auto newVal = QInputDialog::getInt(this, tr("Set value"), label, val, min, max, step, &ok);

	if (ok) { model()->setValue(newVal); }
}

} // namespace lmms::gui
