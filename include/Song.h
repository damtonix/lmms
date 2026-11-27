/*
 * Song.h - class song - the root of the model-tree
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

#ifndef LMMS_SONG_H
#define LMMS_SONG_H

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <QColor>
#include <QString>
#include <QTimer>
#include <QHash>  // IWYU pragma: keep

#include "AudioEngine.h"
#include "Controller.h"
#include "Metronome.h"
#include "lmms_constants.h"
#include "MeterModel.h"
#include "Timeline.h"
#include "TrackContainer.h"
#include "VstSyncController.h"

#include <filesystem> // QTBUG-73263

namespace lmms
{

class AutomationTrack;
class Keymap;
class MidiClip;
class Scale;

namespace gui
{
class SongEditor;
class ControllerRackView;
}


const bpm_t MinTempo = 10;
const bpm_t DefaultTempo = 140;
const bpm_t MaxTempo = 999;
const tick_t MaxSongLength = 9999 * DefaultTicksPerBar;


class LMMS_EXPORT Song : public TrackContainer
{
	Q_OBJECT
	mapPropertyFromModel( int,getTempo,setTempo,m_tempoModel );
	mapPropertyFromModel( int,masterPitch,setMasterPitch,m_masterPitchModel );
	mapPropertyFromModel( int,masterVolume,setMasterVolume, m_masterVolumeModel );
public:
	enum class PlayMode
	{
		None,
		Song,
		Pattern,
		MidiClip,
		AutomationClip,
		Count
	} ;
	constexpr static auto PlayModeCount = static_cast<std::size_t>(PlayMode::Count);

	struct SaveOptions {
		/**
		 * Should we discard MIDI ControllerConnections from project files?
		 */
		BoolModel discardMIDIConnections{false};
		/**
		 * Should we save the project as a project bundle? (with resources)
		 */
		BoolModel saveAsProjectBundle{false};

		void setDefaultOptions() {
			discardMIDIConnections.setValue(false);
			saveAsProjectBundle.setValue(false);
		}
	};

	void clearErrors();
	void collectError( const QString error );
	bool hasErrors();
	QString errorSummary();

	void processNextBuffer();

	inline int getLoadingTrackCount() const
	{
		return m_nLoadingTrack;
	}

	inline int getMilliseconds() const
	{
		return getMilliseconds(m_playMode);
	}

	//! Returns the elapsed milliseconds since the start of the song
	//! This function attempts to give the correct value even despite mid-song tempo changes
	inline int getMilliseconds(PlayMode playMode) const
	{
		return 1000 * getTimeline(playMode).getElapsedSeconds();
	}

	inline int getBars() const
	{
		return currentBar();
	}

	inline int ticksPerBar() const
	{
		return TimePos::ticksPerBar(m_timeSigModel);
	}

	// Returns the beat position inside the bar, 0-based
	inline int getBeat() const
	{
		return getPlayPos().getBeatWithinBar(m_timeSigModel);
	}
	// the remainder after bar and beat are removed
	inline int getBeatTicks() const
	{
		return getPlayPos().getTickWithinBeat(m_timeSigModel);
	}
	inline int getTicks() const
	{
		return currentTick();
	}
	inline f_cnt_t getFrames() const
	{
		return currentFrame();
	}
	inline bool isPaused() const
	{
		return m_paused;
	}

	inline bool isPlaying() const
	{
		return m_playing == true && m_exporting == false;
	}

	inline bool isStopped() const
	{
		return m_playing == false && m_paused == false;
	}

	inline bool isExporting() const
	{
		return m_exporting;
	}

	inline void setExportLoop( bool exportLoop )
	{
		m_exportLoop = exportLoop;
	}

	inline bool isRecording() const
	{
		return m_recording;
	}
	
	inline void setLoopRenderCount(int count)
	{
		if (count < 1)
			m_loopRenderCount = 1;
		else
			m_loopRenderCount = count;
		m_loopRenderRemaining = m_loopRenderCount;
	}
    
	inline int getLoopRenderCount() const
	{
		return m_loopRenderCount;
	}

	bool isExportDone() const;
	//! Custom: after the song end, the export goes on until the sound has died away (not for loop exports)
	bool exportWantsTail() const { return !m_exportLoop && !m_renderBetweenMarkers; }
	int getExportProgress() const;

	inline void setRenderBetweenMarkers( bool renderBetweenMarkers )
	{
		m_renderBetweenMarkers = renderBetweenMarkers;
	}

	inline PlayMode playMode() const
	{
		return m_playMode;
	}

	const TimePos& getPlayPos(PlayMode pm) const
	{
		return getTimeline(pm).pos();
	}
	const TimePos& getPlayPos() const
	{
		return getPlayPos(m_playMode);
	}

	void setPlayPos(tick_t ticks, PlayMode playMode)
	{
		getTimeline(playMode).setTicks(ticks);
	}
	void setPlayPos(tick_t ticks)
	{
		setPlayPos(ticks, m_playMode);
	}

	auto getTimeline(PlayMode mode) -> Timeline& { return m_timelines[static_cast<std::size_t>(mode)]; }
	auto getTimeline(PlayMode mode) const -> const Timeline& { return m_timelines[static_cast<std::size_t>(mode)]; }
	auto getTimeline() -> Timeline& { return getTimeline(m_playMode); }
	auto getTimeline() const -> const Timeline& { return getTimeline(m_playMode); }

	void updateLength();
	bar_t length() const
	{
		return m_length;
	}


	bpm_t getTempo();

	AutomationTrack * globalAutomationTrack()
	{
		return m_globalAutomationTrack;
	}

	//TODO: Add Q_DECL_OVERRIDE when Qt4 is dropped
	AutomatedValueMap automatedValuesAt(TimePos time, int clipNum = -1) const override;

	// file management
	void createNewProject();
	void createNewProjectFromTemplate( const QString & templ );
	void loadProject( const QString & filename );
	bool guiSaveProject();
	bool guiSaveProjectAs(const QString & filename);
	bool saveProjectFile(const QString & filename, bool withResources = false);
	//! Custom: load @p tempFile (a rewritten copy of the project) but keep @p realFileName as project file
	//! Custom: when exporting, muted tracks still count for the song length (stems of equal length)
	void setExportFullLength(bool on) { m_exportFullLength = on; }
	//! Custom: export only this range (plus the tail until silence), e.g. for bouncing one clip; end <= begin = off
	void setExportRange(TimePos begin, TimePos end) { m_exportRangeBegin = begin; m_exportRangeEnd = end; }
	void clearExportRange() { m_exportRangeBegin = m_exportRangeEnd = TimePos{0}; }

	//! Custom: position (in ticks, fractional) at the start of the audio period being rendered, for ducking
	double periodStartTick() const { return m_periodStartTick.load(std::memory_order_relaxed); }
	bool periodPlaying() const { return m_periodPlaying.load(std::memory_order_relaxed); }

	//! Custom: the real hits of the kick track(s) for Kick-Ducking, rebuilt in the main thread
	struct KickHit
	{
		double tick;
		float strength; //!< 0..1
		bool operator==(const KickHit&) const = default;
	};
	struct KickMap
	{
		bool hasSource = false;            //!< at least one track is marked as kick
		std::vector<KickHit> song;         //!< in song ticks, sorted
		std::vector<std::vector<KickHit>> patterns; //!< per pattern, in pattern ticks, repeated before/after
		std::vector<double> patternLength; //!< ticks
		bool operator==(const KickMap&) const = default;
	};
	//! map valid for the period that is rendered right now (audio threads), may be null
	const KickMap* periodKickMap() const { return m_periodKickMap.get(); }
	std::shared_ptr<const KickMap> kickMap() const;
	//! recompute the kick hits from the notes / samples of all kick tracks (main thread)
	void updateKickMap();

	//! Custom: linked clips. Number of clips in the song with this link id
	int linkGroupSize(const QString& linkId) const { return m_linkGroupSizes.value(linkId, 0); }
	//! copy the content of an edited linked clip to all clips of its group (main thread)
	void syncLinkedClips();
	//! sync soon (coalesced), e.g. after a clip changed
	void scheduleLinkSync();

	//! Custom: named arrangement markers; a section runs from a marker to the next one
	struct ArrangementMarker
	{
		tick_t tick = 0;
		QString name;
		QColor color;
	};
	const std::vector<ArrangementMarker>& arrangementMarkers() const { return m_arrangementMarkers; }
	void setArrangementMarkers(std::vector<ArrangementMarker> markers);
	void addArrangementMarker(tick_t tick, const QString& name, const QColor& color);
	tick_t sectionEnd(std::size_t index) const;
	void duplicateSection(std::size_t index);
	void deleteSection(std::size_t index);
	//! Custom: inserts @p length ticks of empty space at @p pos (clips, markers and loop points behind it move)
	void insertSpace(tick_t pos, tick_t length);
	//! Custom: removes the range [begin, end): clips starting inside are deleted, clips reaching into it are
	//! shortened, everything behind it moves forward
	void removeRange(tick_t begin, tick_t end);
	//! swaps section @p index with the following one (contents and markers)
	void swapSectionWithNext(std::size_t index);

	//! Custom: folder with the automatic versions ("snapshots") of a project file
	static QString snapshotDirectory(const QString& projectFile);
	static int maxSnapshots();
	//! Custom: copy a just saved project file into its snapshot folder (skips unchanged files)
	static void createSnapshot(const QString& savedFile);
	//! Custom: save the current (maybe unsaved) state as a snapshot, e.g. before a risky operation
	void snapshotCurrentState(const QString& reason);

	void reloadFromRewrittenFile(const QString& tempFile, const QString& realFileName);

	const QString & projectFileName() const
	{
		return m_fileName;
	}

	bool isLoadingProject() const
	{
		return m_loadingProject;
	}

	void loadingCancelled()
	{
		m_isCancelled = true;
		Engine::audioEngine()->clearNewPlayHandles();
	}

	bool isCancelled()
	{
		return m_isCancelled;
	}

	bool isModified() const
	{
		return m_modified;
	}

	QString nodeName() const override
	{
		return "song";
	}

	virtual bool fixedClips() const
	{
		return false;
	}

	void addController( Controller * c );
	void removeController( Controller * c );


	const ControllerVector & controllers() const
	{
		return m_controllers;
	}


	MeterModel & getTimeSigModel()
	{
		return m_timeSigModel;
	}

	IntModel& tempoModel()
	{
		return m_tempoModel;
	}

	void exportProjectMidi(const std::filesystem::path& filePath) const;

	inline void setLoadOnLaunch(bool value) { m_loadOnLaunch = value; }
	SaveOptions &getSaveOptions() {
		return m_saveOptions;
	}

	bool isSavingProject() const;

	std::shared_ptr<const Scale> getScale(unsigned int index) const;
	std::shared_ptr<const Keymap> getKeymap(unsigned int index) const;
	void setScale(unsigned int index, std::shared_ptr<Scale> newScale);
	void setKeymap(unsigned int index, std::shared_ptr<Keymap> newMap);

	const std::string& syncKey() const noexcept { return m_vstSyncController.sharedMemoryKey(); }

	Metronome& metronome() { return m_metronome; }

	void removeOldAutomatedValue(AutomatableModel* am);

public slots:
	void playSong();
	void record();
	void playAndRecord();
	void playPattern();
	void playMidiClip( const lmms::MidiClip * midiClipToPlay, bool loop = true );
	void togglePause();
	void stop();

	void startExport();
	void stopExport();


	void setModified();

	void clearProject();

	void addPatternTrack();


private slots:
	void insertBar();
	void removeBar();
	void addSampleTrack();
	void addAutomationTrack();
	void addGroupTrack(); //!< Custom

	void setTempo();
	void setTimeSignature();

	void masterVolumeChanged();

	void savePlayStartPosition();

	void updateFramesPerTick();



private:
	Song();
	Song( const Song & );
	~Song() override;


	inline bar_t currentBar() const
	{
		return getPlayPos(m_playMode).getBar();
	}

	inline tick_t currentTick() const
	{
		return getPlayPos(m_playMode).getTicks();
	}

	inline f_cnt_t currentFrame() const
	{
		return getTimeline(m_playMode).ticks() * Engine::framesPerTick() + getTimeline(m_playMode).frameOffset();
	}

	void saveControllerStates( QDomDocument & doc, QDomElement & element );
	void restoreControllerStates( const QDomElement & element );

	void removeAllControllers();

	void saveScaleStates(QDomDocument &doc, QDomElement &element);
	void restoreScaleStates(const QDomElement &element);

	void saveKeymapStates(QDomDocument &doc, QDomElement &element);
	void restoreKeymapStates(const QDomElement &element);

	void processAutomations(const TrackList& tracks, TimePos timeStart, f_cnt_t frames);
	void processMetronome(size_t bufferOffset);

	void setModified(bool value);

	void setProjectFileName(QString const & projectFileName);

	AutomationTrack * m_globalAutomationTrack;
	std::vector<ArrangementMarker> m_arrangementMarkers; //!< Custom, sorted by tick
	//! Custom: all tracks with clips on the song timeline (incl. the global automation track)
	std::vector<Track*> arrangementTracks() const;
	bool m_reloadingRewritten = false; //!< Custom

	IntModel m_tempoModel;
	MeterModel m_timeSigModel;
	int m_oldTicksPerBar;
	IntModel m_masterVolumeModel;
	IntModel m_masterPitchModel;

	ControllerVector m_controllers;

	int m_nLoadingTrack;

	QString m_fileName;
	QString m_oldFileName;
	bool m_modified;
	bool m_loadOnLaunch;

	volatile bool m_recording;
	volatile bool m_exporting;
	volatile bool m_exportLoop;
	volatile bool m_renderBetweenMarkers;
	volatile bool m_playing;
	std::atomic<double> m_periodStartTick{0.0}; //!< Custom: song position at the start of the current period
	std::atomic<bool> m_periodPlaying{false};
	bool m_exportFullLength = false; //!< Custom: stems all get the full song length
	TimePos m_exportRangeBegin{0}; //!< Custom: setExportRange
	TimePos m_exportRangeEnd{0};
	std::shared_ptr<const KickMap> m_kickMap;       //!< Custom: latest kick map (guarded by m_kickMapMutex)
	std::shared_ptr<const KickMap> m_periodKickMap; //!< copy for the current period (audio thread)
	mutable std::mutex m_kickMapMutex;
	QTimer* m_kickMapTimer = nullptr;
	QHash<QString, int> m_linkGroupSizes;      //!< Custom: linked clips per link id
	QHash<QString, QString> m_linkContent;     //!< last synced content per link id
	bool m_linkSyncScheduled = false;
	bool m_syncingLinks = false;
	volatile bool m_paused;

	bool m_savingProject;
	bool m_loadingProject;
	bool m_isCancelled;

	SaveOptions m_saveOptions;

	QHash<QString, int> m_errors;

	std::array<Timeline, PlayModeCount> m_timelines;

	PlayMode m_playMode;
	bar_t m_length;

	const MidiClip* m_midiClipToPlay;
	bool m_loopMidiClip;

	VstSyncController m_vstSyncController;
    
	int m_loopRenderCount;
	int m_loopRenderRemaining;
	TimePos m_exportSongBegin;
	TimePos m_exportLoopBegin;
	TimePos m_exportLoopEnd;
	TimePos m_exportSongEnd;
	TimePos m_exportEffectiveLength;

	std::shared_ptr<Scale> m_scales[MaxScaleCount];
	std::shared_ptr<Keymap> m_keymaps[MaxKeymapCount];

	AutomatedValueMap m_oldAutomatedValues;

	Metronome m_metronome;

	friend class Engine;
	friend class gui::SongEditor;
	friend class gui::ControllerRackView;

signals:
	void arrangementMarkersChanged();
	void projectLoaded();
	void playbackStateChanged();
	void playbackPositionJumped();
	void lengthChanged( int bars );
	void tempoChanged( lmms::bpm_t newBPM );
	void timeSignatureChanged( int oldTicksPerBar, int ticksPerBar );
	void controllerAdded( lmms::Controller * );
	void controllerRemoved( lmms::Controller * );
	void stopped();
	void modified();
	void projectFileNameChanged();
	void scaleListChanged(int index);
	void keymapListChanged(int index);
} ;


} // namespace lmms

#endif // LMMS_SONG_H
