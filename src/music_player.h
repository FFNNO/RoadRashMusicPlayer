#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "music_config.h"
#include "music_library.h"

// In-game music player (feature rr95-music-player, task M3). Two layers:
//  - MusicPlayerStateMachine: pure, device-free logic (fully unit tested).
//  - The free MusicPlayer* functions: one worker thread that decodes ahead into XAudio2.
//
// Nothing here knows about the game: hook_sites.cpp drives these calls from the race and menu
// present detours (task M5). The public functions are thread-safe and never block on disk or
// audio; commands are queued to the worker.

enum class MusicPlayerState {
    Stopped = 0,
    Playing = 1,
    Paused = 2
};

// Copy of the player state for the overlay (task M4) and tests. positionMs and durationMs are
// written by the engine; the pure state machine leaves them at 0 unless SetProgress is called.
struct MusicPlayerNowPlaying {
    bool active = false;  // a cassette and track are selected (Playing or Paused)
    bool paused = false;
    // True once the engine opened a track and submitted at least one audio buffer in this session.
    // It stays true across ordinary track changes and a pause, and clears when the player stops or
    // the audio engine becomes disabled. The native-music mute waits for this signal (R3-001).
    bool playbackStarted = false;
    std::wstring cassetteLabel;
    std::wstring title;
    std::wstring artist;
    int trackIndex = -1;    // index in MusicCassette::tracks
    int cassetteIndex = -1; // index in the cassette list
    bool builtInCassette = false;  // the built-in "Road Rash" cassette
    uint32_t changeCounter = 0;  // increments on every track or cassette change
    uint64_t positionMs = 0;
    uint64_t durationMs = 0;
};

// ---------------------------------------------------------------------------
// Audio output seam (R3-001 .. R3-003). The engine reaches the device only through this interface,
// so the failure paths can be exercised without an audio device. The real implementation is
// XAudio2Sink in music_player.cpp; tests install a fake through MusicPlayerTestSetHooks.
// ---------------------------------------------------------------------------

// Per-track accounting shared with the audio backend. The engine owns the counters, hands one to
// BeginTrack/Submit, and reuses a counter only when no callback can still reach it (inFlight == 0),
// so memory stays bounded however many tracks play.
struct MusicAudioCounter {
    std::atomic<int> inFlight{0};
    std::atomic<uint64_t> framesRemaining{0};
};

// Voice state decisions of the XAudio2 sink (task M5c, finding D). A pause can land after
// BeginTrack and before the first Submit; the voice must not start then, or the pause flag and the
// voice state diverge and the next auto-pause does nothing. The sink follows these two booleans, so
// the rule is unit tested without an audio device.
// Start the voice on the first Submit only when no pause is pending.
bool MusicSinkStartsOnSubmit(bool paused);
// Start the voice on Resume only when a pause is pending and the voice has been prepared by Submit.
bool MusicSinkStartsOnResume(bool paused, bool voicePrepared);

class MusicAudioSink {
public:
    virtual ~MusicAudioSink() = default;

    // Creates the XAudio2 engine and the mastering voice. false is an engine-level failure: the
    // player becomes terminally disabled (R3-001).
    virtual bool Initialize(float volume) = 0;

    // Stops and flushes the previous track, then prepares the source voice for the format. false is
    // a track failure: the engine feeds TrackFailed and advances (R3-002).
    virtual bool BeginTrack(uint32_t sampleRate, uint32_t channels, float volume,
                            MusicAudioCounter* counter) = 0;

    virtual bool Submit(const float* samples, uint32_t frames, uint32_t channels,
                        MusicAudioCounter* counter) = 0;

    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void ResetTrack() = 0;
    virtual int FreeBuffers(const MusicAudioCounter* counter) const = 0;
    virtual bool Drained(const MusicAudioCounter* counter) const = 0;
    virtual uint64_t FramesRemaining(const MusicAudioCounter* counter) const = 0;
    virtual void Shutdown() = 0;
};

// Device-free player state machine. Cassettes are copied on Start; tracks are identified by their
// index inside the cassette and the play order is the seeded order from BuildPlayOrder.
class MusicPlayerStateMachine {
public:
    // Loads the cassette list and starts the first track of cassetteIndex. Empty lists or an
    // invalid index leave the machine Stopped. changeCounter increments once. startTrack selects
    // the track inside that cassette (for the jukebox pick) or -1 for the first play-order track.
    void Start(std::vector<MusicCassette> cassettes, int cassetteIndex, bool shuffle, uint32_t seed,
               int startTrack = -1);

    // Move forward/back in the play order, with wrap-around. Return false when stopped or when the
    // cassette has no tracks. Resuming a paused player also returns to Playing.
    bool Next();
    bool Previous();

    // Playing <-> Paused. Returns false when stopped. Does not change changeCounter.
    bool TogglePause();

    // Switch to the next cassette (wrap-around) and start its first play-order track. Respects
    // shuffle. Returns false when no cassettes are loaded.
    bool NextCassette();

    // Race lifecycle. RaceStarted resumes the current track, or starts the first track when the
    // machine is Stopped with cassettes loaded. RaceEnded pauses a playing machine.
    void RaceStarted();
    void RaceEnded();

    // Track playback events from the engine. TrackFinished advances with wrap-around.
    // TrackFailed records the current file as failed, advances to the next track that has not
    // failed yet and stops instead of looping when every track of the cassette has failed.
    void TrackFinished();
    void TrackFailed();

    // Terminal stop used when the audio engine itself fails (R3-001). Clears the selection, so the
    // snapshot is inactive, and leaves the machine Stopped.
    void Stop();

    // Display metadata (tags) for a track, keyed by relativePath, and progress in milliseconds.
    // SetTrackDisplay does not change changeCounter.
    void SetTrackDisplay(const std::wstring& relativePath, const std::wstring& title,
                         const std::wstring& artist);
    void SetProgress(uint64_t positionMs, uint64_t durationMs);

    // After TrackFailed: the relative path of the failed file the first time that file fails in
    // this session, or an empty string when it already failed. The engine logs it as one warning.
    std::wstring TakeFailureWarning();

    MusicPlayerState State() const { return m_state; }
    MusicPlayerNowPlaying Snapshot() const;

    const MusicCassette* CurrentCassette() const;
    const MusicTrack* CurrentTrack() const;

private:
    struct DisplayOverride {
        std::wstring relativePath;
        std::wstring title;
        std::wstring artist;
    };

    int CurrentTrackIndex() const;
    void RebuildPlayOrder();
    void SelectFirstTrack();
    void SelectTrack(int trackIndex);
    bool AdvanceSkippingFailed();
    void BumpChange();
    bool AllTracksFailed() const;
    bool IsTrackFailed(const std::wstring& relativePath) const;
    const DisplayOverride* FindDisplay(const std::wstring& relativePath) const;

    std::vector<MusicCassette> m_cassettes;
    std::vector<DisplayOverride> m_display;
    std::vector<int> m_order;
    std::vector<std::wstring> m_failedPaths;  // one entry per file that failed in this session
    int m_cassetteIndex = -1;
    int m_position = -1;
    bool m_shuffle = false;
    uint32_t m_seed = 0;
    uint64_t m_positionMs = 0;
    uint64_t m_durationMs = 0;
    MusicPlayerState m_state = MusicPlayerState::Stopped;
    uint32_t m_changeCounter = 0;
    std::wstring m_pendingFailure;
};

// Public engine API (task M3 / M5). MusicPlayerInit with config.enabled == false creates nothing
// and leaves every other call a no-op. XAudio2 is created lazily on the first race start, never in
// DllMain and never at DLL load; when xaudio2_9.dll is missing the player logs one warning and
// stays disabled while the game keeps working.
void MusicPlayerInit(const MusicConfig& config, std::vector<MusicCassette> cassettes);
// M5: scans the music folder and builds the cassette list on the worker thread, so the game never
// waits on disk. musicRoot is gameFolder + [Music] Path; gameFolder holds AUDIO\MUSIC\*.RRA for
// the built-in cassette. The built-in "Road Rash" cassette comes first, then the user cassettes.
void MusicPlayerInitWithPaths(const MusicConfig& config, const std::wstring& musicRoot,
                              const std::wstring& gameFolder);
void MusicPlayerRaceStarted();
// Starts (or resumes) the race and selects track on the built-in cassette when possible.
void MusicPlayerRaceStartedAtTrack(int track);
void MusicPlayerRaceEnded();
void MusicPlayerNext();
void MusicPlayerPrevious();
void MusicPlayerTogglePause();
void MusicPlayerNextCassette();

// Race heartbeat (task M5b, bug B). The race-frame path reports every race frame. The worker
// auto-pauses playback when the last heartbeat is older than kMusicHeartbeatTimeoutMs while a race
// is active (the game is frozen inside a modal loop, so no race frames run) and auto-resumes on the
// next heartbeat. A user pause (MusicPlayerTogglePause) is a separate flag and survives the gap.
inline constexpr uint64_t kMusicHeartbeatTimeoutMs = 250;
void MusicPlayerRaceHeartbeat(uint64_t nowMs);

// Copy of the current state, including position and duration in milliseconds. Never blocks on
// audio or disk. Returns an inactive snapshot when the player is not initialised.
MusicPlayerNowPlaying MusicPlayerSnapshot();

// True while the worker auto-paused playback because race frames stopped (task M5c, finding E).
bool MusicPlayerAutoPaused();
// Number of times the worker lifted an auto-pause since the player was created. The race-frame glue
// compares it with the last value it saw, so it logs the resume diagnostics once per auto-pause.
uint32_t MusicPlayerAutoResumeCount();

// Explicit in-session shutdown: stops the voice and joins the worker. Never called from a
// destructor at process exit; the singleton is leaked on purpose like the presenter.
void MusicPlayerShutdown();

// M5: true once the worker scanned the music folder, even when no cassette was found. The game
// waits for this before it starts the race player, so the decision uses the real library.
bool MusicPlayerLibraryReady();
// M6a: labels of the current cassette list in library order; the built-in "Road Rash" one is first.
// Empty until the worker finished the scan (or when the player is not initialized).
std::vector<std::wstring> MusicPlayerCassetteLabels();
// M6a: selects the cassette the next race starts with. -1 restores the default (first cassette).
// Safe to call from the game thread; the worker applies it at the next race start.
void MusicPlayerSelectCassette(int index);
// The selected cassette index, or -1 when nothing was selected.
int MusicPlayerSelectedCassette();
// True when at least one cassette lists tracks. Not enough to mute the native music: the mute also
// needs an enabled audio engine and a started playback (R3-001).
bool MusicPlayerHasPlayableCassette();
// True while the audio engine is alive and has not entered its terminal disabled state (missing
// XAudio2, engine or wake-event failure). The native-music mute uses it (R3-001).
bool MusicPlayerAudioEnabled();

// ---------------------------------------------------------------------------
// Test-only seam. Production code never calls these: they install the factories used by the next
// MusicPlayerInit, release the singleton so a test can start a fresh player, and expose small
// counters for the reliability tests (R3-001 .. R3-004).
// ---------------------------------------------------------------------------
struct MusicPlayerTestHooks {
    // Returns a sink owned by the engine. Null keeps the real XAudio2Sink.
    MusicAudioSink* (*createSink)(void* user) = nullptr;
    // Returns the worker wake event. Null keeps the real CreateEventW; returning NULL simulates a
    // creation failure (R3-004).
    HANDLE (*createWakeEvent)() = nullptr;
    // Clock used by the worker for the race-heartbeat timeout (M5b). Null keeps GetTickCount64.
    uint64_t (*nowMs)() = nullptr;
    void* user = nullptr;
};
void MusicPlayerTestSetHooks(const MusicPlayerTestHooks& hooks);
void MusicPlayerTestReset();
size_t MusicPlayerTestCounterPoolSize();
bool MusicPlayerTestAudioDisabled();
bool MusicPlayerTestWorkerRunning();
// M5b: worker-side auto-pause state and whether a race heartbeat window is open.
bool MusicPlayerTestAutoPaused();
bool MusicPlayerTestHeartbeatActive();
