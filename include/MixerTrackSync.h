/*
 * MixerTrackSync.h - Custom (PtH-LMMS): every instrument/sample track and every group
 *                    owns its own mixer channel, ordered like the tracks
 */

#ifndef LMMS_GUI_MIXER_TRACK_SYNC_H
#define LMMS_GUI_MIXER_TRACK_SYNC_H

#include <QObject>
#include <QTimer>

namespace lmms
{
class Track;
class MixerChannel;
}

namespace lmms::gui
{

class MixerView;

/**
 * Keeps the mixer in line with the tracks:
 * - every instrument and sample track (song editor and pattern editor) and every group track
 *   owns exactly one mixer channel (MixerChannel::m_ownerUid); the track cannot be routed elsewhere
 * - the track channels come first, in the order of the tracks (top to bottom = left to right),
 *   independent channels follow
 * - group channels are real busses: the channels of the members send to the channel of their group
 *   (nested groups to the parent group, the top group to master)
 * - channel name and color follow the track
 * - old projects: a channel used by exactly one track becomes its channel; a channel shared by
 *   several tracks stays independent and the new track channels send to it (same sound)
 */
class MixerTrackSync : public QObject
{
	Q_OBJECT
public:
	explicit MixerTrackSync(MixerView* view);

	//! the group whose channel is the fixed bus of this track (nearest group above it), or nullptr
	static Track* busGroupOf(const Track* track);
	//! may channel `from` send to channel `to`? (track channels: only from the members of their group;
	//! group channels also from independent channels)
	static bool sendAllowed(const MixerChannel* from, const MixerChannel* to);
	//! the group whose bus this independent channel sends to (first one), or nullptr
	static Track* groupFedBy(const MixerChannel* ch);
	//! true for tracks that own a channel
	static bool ownsChannel(const Track* track);
	//! the track (song or pattern editor) with this uid, or nullptr
	static Track* trackByUid(const QString& uid);

public slots:
	void scheduleSync();
	void sync();

private:
	void watchTrack(Track* track);

	MixerView* m_view;
	QTimer m_timer;
	bool m_running = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_TRACK_SYNC_H
