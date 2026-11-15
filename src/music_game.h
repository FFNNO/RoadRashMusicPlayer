#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "music_config.h"
#include "hotkey.h"
#include "music_library.h"
#include "music_overlay.h"
#include "music_registry.h"

// Wiring decisions for the in-game music player (feature rr95-music-player, task M5). Pure logic
// only: no Win32 calls here and no engine memory access. hook_sites.cpp performs the reads, writes
// and posts; every decision it applies is one of the helpers below, so the rules stay testable
// while hook_sites.cpp stays thin glue.

// ---------------------------------------------------------------------------
// Race phase, fed by the race and menu present detours
// ---------------------------------------------------------------------------
enum class MusicRaceEvent {
    None = 0,
    Started = 1,
    Ended = 2
};

// Consecutive menu presents, with no race present in between, that end a race (task M5c,
// finding C). A single menu present can come from the window procedure itself: WM_PAINT after a
// restore and WM_TIMER id 7 both reach the menu present FUN_0046ded0 while the game sits in its
// Esc pause or focus-loss loop.
inline constexpr int kMusicMenuPresentsToEndRace = 3;

// The first race present starts the race (not the race init detour: the order of race init and the
// game's race audio start is not proven). A race ends only after kMusicMenuPresentsToEndRace menu
// presents without a race present in between, and never while the game's pause or focus-loss loop
// runs (`gamePauseLoop`, byte 0x005cd4d4). Menu presents before any race do nothing.
class MusicRacePhaseTracker {
public:
    MusicRaceEvent OnRacePresent();
    MusicRaceEvent OnMenuPresent(bool gamePauseLoop = false);
    bool InRace() const { return m_inRace; }

private:
    bool m_inRace = false;
    int m_menuPresents = 0;
};

// S3b review R3-reset-for-race-wiring-unproved: the beeper's per-race show/hide ends on the same
// race-start transition the phase tracker reports. The reset rides on the transition itself, so the
// glue cannot apply it in one place and forget it in another.
struct MusicRacePresentCycle {
    MusicRaceEvent event = MusicRaceEvent::None;
    bool overlayReset = false;  // this present started a race and ResetForRace ran
};

MusicRacePresentCycle AdvanceMusicRacePresent(MusicRacePhaseTracker& tracker, MusicOverlayController& overlay);

// ---------------------------------------------------------------------------
// Native race-music mute (byte 0x0058e074)
// ---------------------------------------------------------------------------
struct MusicMuteState {
    int originalValue = -1;  // remembered native byte, -1 when there is nothing to restore
    bool warnedInvalid = false;
};

struct MusicMutePlan {
    bool write = false;
    uint8_t value = 0;
    bool post = false;     // request the game's "update the volume" thread message
    bool restore = false;  // the write puts the remembered original value back
    bool warnInvalid = false;
    // The remembered original value is dropped, with or without a restore write. The glue must
    // clear its native-resume block now, so a player that stops or is disabled mid-race cannot
    // leave the Esc and focus-loss loops unable to resume the game's own music.
    bool clearBlock = false;
};

// Evaluated on every race frame and at race end. Mute the native race music only while the race is
// active, the player is enabled (not in its terminal disabled state) and playback has really
// started. The first frame before playback starts does nothing; a later frame applies the mute.
// When the player stops, becomes disabled or the race ends, restore the remembered value only when
// the byte still holds the 1 the mod wrote. Any byte other than 0/1 is left alone and warns once per
// session. A disabled player never writes anything.
MusicMutePlan PlanMusicMute(MusicMuteState& state, bool playerEnabled, bool playbackStarted,
                            bool raceActive, int currentMuteByte, bool musicThreadPresent);

// What the glue does with a plan. A warned plan never writes the byte and never posts a volume
// update, whatever the plan carried. The native-resume block is still cleared whenever the plan
// drops the remembered value: the warning is a log, never a reason to keep the Esc and focus-loss
// loops from resuming the game's own music.
struct MusicMuteApply {
    bool logInvalid = false;
    bool clearBlock = false;
    bool write = false;
    uint8_t value = 0;
    bool post = false;
};

MusicMuteApply PlanMusicMuteApply(const MusicMutePlan& plan);

// ---------------------------------------------------------------------------
// Native MIDI (task M5b, bug A: the game resumes its MIDI over the player)
// ---------------------------------------------------------------------------
// The mute byte 0x0058e074 only stops FUN_004452a0 from starting the MIDI; it does not pause a
// stream that is already running, and the game's pause loops resume the MIDI through FUN_00445ba0
// without reading that byte. So the mod pauses the MIDI itself when it applies its race mute, and
// the FUN_00445ba0 detour skips the resume while the mute is in force. DAT_006163d3 is nonzero
// while the game plays race music through MIDI instead of the DirectSound stream.

// True when the glue must call the game's FUN_004459c0 (MIDI pause) now: the player is enabled, the
// race is active, the mod just wrote its race mute this frame, the game reports MIDI mode, and the
// routine's shipped bytes were verified. An unverified routine is never called.
bool ShouldPauseNativeMidi(bool musicEnabled, bool raceActive, bool muteAppliedNow,
                           bool nativeMidiActive, bool routineVerified);

// True when the FUN_00445ba0 (MIDI resume) or FUN_00411c00 (stream "Play the song") detour must
// return without calling the original: the player is enabled and the mod's race mute is still in
// force (the mod wrote the byte and has not restored it).
bool ShouldBlockNativeMusicResume(bool musicEnabled, bool modRaceMuteActive);

// ---------------------------------------------------------------------------
// Native race music mode (task M5c, finding B)
// ---------------------------------------------------------------------------
// The game starts its race music in FUN_004463a0, once per race and only from the race loop (the
// single call site is out/decomp.c:19975). After the original returns, DAT_005a6908 != 0 means the
// DirectSound stream started (FUN_00411850) and DAT_006163d3 != 0 means the MIDI path started
// (FUN_004452a0). Reading the stream flag earlier (on the first race frame) always sees 0, because
// the race sequence presents two frames before calling FUN_004463a0.
enum class MusicNativeMode {
    Unknown = 0,  // neither flag set: the master mute is on, or the game did not start race music
    Stream = 1,
    Midi = 2
};

MusicNativeMode DecideMusicNativeMode(bool streamFlagSet, bool midiFlagSet);

// "stream", "MIDI" or "unknown", for the once-per-race mode log.
const char* MusicNativeModeName(MusicNativeMode mode);

// ---------------------------------------------------------------------------
// Two-way jukebox sync ("Road Rash" cassette only)
// ---------------------------------------------------------------------------
struct MusicJukeboxState {
    bool loggedInactiveStream = false;
};

struct MusicJukeboxRaceStart {
    bool startAtTrack = false;
    int track = -1;
    bool syncWrites = false;     // write track and band on every track change this race
    bool logInactiveStream = false;  // MIDI mode: one info line per session
};

// At race start: with the race stream active the pick is the saved menu pick (DAT_005a6970),
// otherwise it is the menu track (DAT_005cd498). A pick outside 0..13 is ignored. Without the
// race stream the sync writes are skipped and one info line is requested per session.
MusicJukeboxRaceStart PlanMusicJukeboxRaceStart(MusicJukeboxState& state, bool roadRashCassette,
                                                bool raceStreamActive, int racePick, int menuPick);

struct MusicJukeboxTrackWrite {
    bool write = false;
    int track = -1;
    int band = -1;
};

// Track change on the "Road Rash" cassette while the race stream is active: write DAT_005a6970 and
// DAT_005cd490 (band of the track). User cassettes and MIDI mode never write.
MusicJukeboxTrackWrite PlanMusicJukeboxTrackWrite(bool roadRashCassette, bool raceStreamActive,
                                                  int track);

// ---------------------------------------------------------------------------
// Resume diagnostics (task M5c, finding E)
// ---------------------------------------------------------------------------
// When race frames come back after the worker auto-paused the player, the log must say what state
// the player is in and what the game's stream thread is doing, so a silent race can be told apart
// from a double soundtrack. Byte 0x005a6958 is nonzero while the game plays a stream buffer,
// 0x005a6909 is nonzero while the game's stream thread is ready to read.
enum class MusicResumeState {
    Stopped = 0,
    Playing = 1,
    AutoPaused = 2,
    UserPaused = 3
};

MusicResumeState ClassifyMusicResume(bool active, bool paused, bool autoPaused);

struct MusicResumeDiagnostics {
    MusicResumeState state = MusicResumeState::Stopped;
    int trackIndex = -1;      // -1 when no track is selected
    int streamPlaying = -1;   // 0x005a6958, -1 when the byte could not be read
    int streamReady = -1;     // 0x005a6909, -1 when the byte could not be read
};

// "playing, track 3, stream playing 1, stream ready 1"; unreadable bytes print "?".
std::string DescribeMusicResume(const MusicResumeDiagnostics& diagnostics);

// ---------------------------------------------------------------------------
// Game metadata strings (CP-1252)
// ---------------------------------------------------------------------------
inline constexpr size_t kMusicGameNameMaxBytes = 64;

// Converts up to maxBytes of text with MultiByteToWideChar(1252), stopping at a NUL. Returns
// fallback for a null buffer, a zero cap or an empty string. The caller reads the engine memory
// with structured exception handling; this conversion is pure and tested with fake buffers.
std::wstring DecodeMusicGameString(const char* text, size_t maxBytes, const std::wstring& fallback);

// Byte offset of one pointer inside the game string table: id * sizeof(char*).
uint32_t MusicGameStringOffset(uint32_t stringId);

// ---------------------------------------------------------------------------
// Hotkey edge detection
// ---------------------------------------------------------------------------
enum class MusicHotkeyAction {
    None = 0,
    Next = 1,
    Previous = 2,
    PlayPause = 3,
    NextCassette = 4,
    Overlay = 5
};

inline constexpr int kMusicHotkeyCount = 5;

// Index order used by the arrays below: next, previous, play/pause, next cassette, overlay.
struct MusicHotkeyBindings {
    Hotkey keys[kMusicHotkeyCount];
    bool enabled[kMusicHotkeyCount] = {true, true, true, true, true};
};

struct MusicHotkeyState {
    bool held[kMusicHotkeyCount] = {};
};

struct MusicHotkeyInput {
    bool keyDown[kMusicHotkeyCount] = {};
    bool ctrlDown = false;
    bool shiftDown = false;
    bool altDown = false;
};

// True when both describe the same virtual key with exactly the same modifiers.
bool SameHotkey(const Hotkey& a, const Hotkey& b);

// Disables empty bindings and every binding exactly equal to one of reserved[0..reservedCount),
// so a music hotkey can never steal an existing mod hotkey. Returns how many bindings it disabled.
int ResolveMusicHotkeyConflicts(MusicHotkeyBindings& bindings, const Hotkey* reserved,
                                size_t reservedCount);

// S3b review R3-capture-strips-ini-modifiers and R3-reserved-capture-shown-but-disabled. One capture
// changes the captured row only: it takes the pressed plain key, and every other row keeps its full
// hotkey, modifiers included. keys is the plain-key view ModMusicKeys stores; it keeps the value
// valid, so a key another row already uses swaps the two plain keys while the other row keeps its
// own modifiers. A key the conflict rule would disable (a reserved hotkey: the general mod's
// ToggleKey from rr95gpu.ini or F11), a reserved menu key, a modifier, an empty key or anything that
// is not a virtual-key byte is rejected: accepted stays false and both outputs are untouched.
struct MusicHotkeyCaptureApply {
    bool accepted = false;
    MusicHotkeyBindings hotkeys;
    MusicKeys keys;
};

MusicHotkeyCaptureApply ApplyMusicHotkeyCapture(const MusicHotkeyBindings& hotkeys, const MusicKeys& keys,
                                                MusicKeyAction action, uint32_t vk, const Hotkey* reserved,
                                                size_t reservedCount);

// One action per key press, never repeating while the key is held. A binding without modifiers
// matches whatever modifiers are held (Shift and Ctrl are game controls), unless another enabled
// binding on the same key matches the held modifiers exactly: that binding wins. A binding with
// modifiers keeps exact matching (HotkeyMatches). Disabled bindings are ignored.
MusicHotkeyAction PollMusicHotkeys(MusicHotkeyState& state, const MusicHotkeyBindings& bindings,
                                   const MusicHotkeyInput& input);

// True when the binding at index matches the held modifiers under the rule above. Exposed for the
// unit tests; PollMusicHotkeys uses it.
bool MusicHotkeyBindingMatches(const MusicHotkeyBindings& bindings, int index, bool ctrlDown,
                               bool shiftDown, bool altDown);

// ---------------------------------------------------------------------------
// Library loading
// ---------------------------------------------------------------------------
// The player's cassette list: the built-in "Road Rash" first, then the user cassettes built from
// scanRelativePaths. User track paths become musicRoot + "\\" + relative path and built-in track
// paths become gameFolder + "\\" + relative path, so the audio engine can open them from the game
// folder. An empty folder leaves that cassette's paths unchanged.
std::vector<MusicCassette> BuildMusicPlayerCassettes(const std::vector<std::wstring>& scanRelativePaths,
                                                     const std::wstring& musicRoot,
                                                     const std::wstring& gameFolder = std::wstring());
