#include "music_glue.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "shared/hook.h"
#include "shared/log.h"
#include "engine.h"
#include "music_config.h"
#include "music_dsound.h"
#include "music_game.h"
#include "music_menu.h"
#include "music_overlay.h"
#include "music_player.h"
#include "music_registry.h"

namespace rr95::music {

namespace {

struct MusicGlueState {
    bool initialized = false;
    bool initWarningLogged = false;
    bool frameWarningLogged = false;
    bool writeWarningLogged = false;
    bool overlayWarningLogged = false;
    bool overlayRegistered = false;
    bool raceActive = false;
    bool pendingRaceStart = false;
    bool syncWrites = false;
    int lastSyncedTrack = -1;
    // Frames come only from the general mod's API in S3a. Without them the player stays inactive.
    bool framesAvailable = false;
    bool raceCallbackRegistered = false;
    bool menuCallbackRegistered = false;
    bool callbackRejectedLogged = false;
    // The mod's race mute is in force, so the MIDI and stream resume detours must skip the original.
    std::atomic<bool> nativeMusicBlocked{false};
    // -1 unchecked, 0 the routine's bytes do not match either shipped exe, 1 verified.
    int midiPauseBytesVerified = -1;
    bool midiPauseWarningLogged = false;
    MusicNativeMode nativeMode = MusicNativeMode::Unknown;
    bool pendingModeSync = false;
    bool loggedModeThisRace = false;
    uint32_t lastAutoResumes = 0;
    MusicRacePhaseTracker tracker;
    MusicMuteState mute;
    MusicJukeboxState jukebox;
    MusicHotkeyBindings hotkeys;
    MusicHotkeyState hotkeyState;
    // The general mod's toggle key and F11, read once; a captured key that equals either is
    // disabled by the race poll, like an ini binding.
    Hotkey reservedHotkeys[2] = {};
    // S3b: the beeper panel. The controller (pinned or on demand, plus the per-race hotkey) and the
    // rasterized frame live in this mod; the general mod only draws the layer.
    MusicOverlayController overlay{MusicOverlayMode::OnDemand};
    MusicOverlayFrameState overlayFrame;
    RR95OverlayFrame overlayApiFrame{};
    MusicOverlayMode overlayMode = MusicOverlayMode::OnDemand;
    std::vector<std::wstring> cassetteLabels;
    std::wstring savedCassetteLabel;
    int selectedCassette = 0;
    bool cassetteResolved = false;
    // S3b: the graphics menu row registered through the API, the Reproductor popup it opens, and the
    // key capture that only the menu input hook makes safe.
    bool menuRowRegistered = false;
    // earlyRowRegistered: the row reached the general mod before it installed its hooks, at the first
    // DirectDrawCreate. apiRowRegistered is the API row whichever point registered it, so the owner
    // decision can tell it from the in-place fallback row.
    bool apiRowRegistered = false;
    bool earlyRowRegistered = false;
    // The item text provider registered with the API row; it stays registered until the general
    // mod accepts the clear.
    MusicItemTextProviderState itemTextProvider;
    MusicMenuRowOwner menuRowOwner = MusicMenuRowOwner::None;
    // The in-place row's own "shipped bytes do not match" line; the API gate has its two flags below.
    bool menuRowWarningLogged = false;
    // S4 review R3-menu-row-register-ignores-logged: the cause of a failed registration attempt and
    // the status the API answered, so the gate stops retrying and logs once.
    MusicMenuRowFailure menuRowFailure = MusicMenuRowFailure::None;
    int32_t menuRowStatus = 0;
    // S5 review R3-menu-row-final-cause-after-transient-never-logged: one flag per kind of line.
    MusicMenuRowLogState menuRowLog;
    bool keyCaptureSafe = false;
    bool keyCaptureWarningLogged = false;
    bool popupWarningLogged = false;
    // -1 unchecked, 0 the popup routines do not match the shipped bytes, 1 verified.
    int popupRoutinesVerified = -1;
    MusicKeys musicKeys;
    MusicKeyCaptureState keyCapture;
    // S4: the fallback without the general mod. Own present and item-text hooks, the in-place menu
    // row and the GDI DIB blend are installed only when the binding says this mod owns them.
    bool fallbackRacePresentInstalled = false;
    bool fallbackMenuPresentInstalled = false;
    bool fallbackItemTextInstalled = false;
    bool menuInPlaceInstalled = false;
    bool gdiOverlayInstalled = false;
    bool fallbackMenuWarningLogged = false;
    bool dibWarningLogged = false;
    uint32_t frameSequence = 0;
    MusicDibBlitTracker dibBlit;
    // The eight shipped menu items, kept before the in-place write so unloading restores them.
    uint8_t shippedMenuSlots[kGraphicsMenuSlotsSize] = {};
};

MusicGlueState g_music;

void* g_hostModule = nullptr;

// The Reproductor popup is this mod's own item array, so the engine can write the item HWNDs and the
// general mod's item text detour compares the pointers the provider sees. The two C-string buffers
// follow the API contract: the returned text stays valid until the next call.
uint8_t g_reproductorPopupItems[kReproductorPopupItemCount * kPopupItemSize] = {};
char g_reproductorRowText[kReproductorTextBufferSize] = {};
char g_reproductorPopupText[kReproductorTextBufferSize] = {};
inline constexpr int kPopupItemTableSlots = 26;

// Player-only sites. The bytes and decoding are the same for the original and the patched exe.
rr95::HookSite g_siteMidiResume{
    "FUN_00445ba0",
    engine::kMidiResume,
    {0x8B, 0x0D, 0xF0, 0x53, 0x5C, 0x00},
    6,
    {6},
    nullptr,
    nullptr,
    false,
    {2}
};

rr95::HookSite g_siteStreamResume{
    "FUN_00411c00",
    engine::kStreamResume,
    {0x80, 0x3D, 0x08, 0x69, 0x5A, 0x00, 0x00},
    7,
    {7},
    nullptr,
    nullptr,
    false,
    {2}
};

rr95::HookSite g_siteRaceMusicStart{
    "FUN_004463a0",
    engine::kRaceMusicStart,
    {0x83, 0x3D, 0x98, 0xBC, 0x5A, 0x00, 0x00},
    7,
    {7},
    nullptr,
    nullptr,
    false,
    {2}
};

// S3b: the RashButton window procedure (every menu button, popup rows included). It is a stdcall
// window procedure with no absolute operand in the hooked span, and the engine's menu loop has no key
// filter, so every menu key and mouse message reaches it. While a key capture runs, this detour
// swallows that input so the menu cannot move focus or close the popup.
rr95::HookSite g_siteRashButton{
    "FUN_0043cea0",
    engine::kRashButtonProc,
    {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70},
    6,
    {1, 2, 3},
    nullptr,
    nullptr,
    false
};

using MidiPauseFn = void(__cdecl*)();

// FUN_004459c0 is called through a rebased pointer, so its shipped bytes are checked once, the same
// way a hook site is checked. The first 16 bytes are identical in the original and the patched exe;
// both variants are kept so a future patch that changes them fails closed instead of calling an
// unknown routine. Offset 1 is the moffs32 absolute operand, rebased like a hook site's.
struct MusicCallTargetSite {
    const char* name;
    uint32_t preferredVa;
    uint8_t expectedOriginal[16];
    uint8_t expectedPatched[16];
    size_t length;
    size_t absoluteOperandOffset;
};

const MusicCallTargetSite g_midiPauseTarget{
    "FUN_004459c0",
    engine::kMidiPause,
    {0xA1, 0xF0, 0x53, 0x5C, 0x00, 0x85, 0xC0, 0x74, 0x36,
     0x56, 0x8B, 0x30, 0x81, 0x3E, 0x4D, 0x44},
    {0xA1, 0xF0, 0x53, 0x5C, 0x00, 0x85, 0xC0, 0x74, 0x36,
     0x56, 0x8B, 0x30, 0x81, 0x3E, 0x4D, 0x44},
    16,
    1
};

void(__cdecl* g_origMidiResume)() = nullptr;
void(__cdecl* g_origStreamResume)() = nullptr;
void(__cdecl* g_origRaceMusicStart)() = nullptr;
LRESULT(__stdcall* g_origRashButton)(HWND, UINT, WPARAM, LPARAM) = nullptr;

void MusicOnNativeRaceAudioStart();
void MusicOnRacePresent();
void MusicOnMenuPresent();
void MusicGlueOverlayHotkey();
const RR95OverlayFrame* MusicGlueOverlayFrame();
const RR95OverlayFrame* OverlayFrameSource(void* /*ctx*/);

// M5b (bug A): the game's Esc pause loop and focus-loss loop resume the race MIDI through this
// function without checking the mute byte. While the player owns the soundtrack, skip the resume.
// The first call into either player-only detour also retries a pending binding decision.
void __cdecl DetourMidiResume() {
    MusicProxyBind(BindingTrigger::PlayerDetour);
    if (ShouldBlockNativeMusicResume(GetMusicConfig().enabled, g_music.nativeMusicBlocked.load())) {
        return;
    }
    if (g_origMidiResume) {
        g_origMidiResume();
    }
}

// M5c (finding A): both pause loops also restart the DirectSound stream through FUN_00411c00,
// which does not read the mute byte.
void __cdecl DetourStreamResume() {
    MusicProxyBind(BindingTrigger::PlayerDetour);
    if (ShouldBlockNativeMusicResume(GetMusicConfig().enabled, g_music.nativeMusicBlocked.load())) {
        return;
    }
    if (g_origStreamResume) {
        g_origStreamResume();
    }
}

// M5c (finding B): FUN_004463a0 starts the race music; its only call site is the race loop. The
// real mode (stream or MIDI) is readable only after the original returns.
void __cdecl DetourRaceMusicStart() {
    MusicProxyBind(BindingTrigger::PlayerDetour);
    if (g_origRaceMusicStart) {
        g_origRaceMusicStart();
    }
    MusicOnNativeRaceAudioStart();
}

bool VerifyMidiPauseBytes() {
    const intptr_t delta =
        static_cast<intptr_t>(engine::Rebase(0x00400000u)) - static_cast<intptr_t>(0x00400000u);
    const uint8_t* variants[2] = {g_midiPauseTarget.expectedOriginal,
                                  g_midiPauseTarget.expectedPatched};
    for (const uint8_t* variant : variants) {
        uint8_t expected[16] = {};
        std::memcpy(expected, variant, g_midiPauseTarget.length);
        uint32_t operand = 0;
        std::memcpy(&operand, expected + g_midiPauseTarget.absoluteOperandOffset, sizeof(operand));
        operand = static_cast<uint32_t>(static_cast<intptr_t>(operand) + delta);
        std::memcpy(expected + g_midiPauseTarget.absoluteOperandOffset, &operand, sizeof(operand));
        if (engine::BytesMatch(g_midiPauseTarget.preferredVa, expected, g_midiPauseTarget.length)) {
            return true;
        }
    }
    return false;
}

// The check runs once per session, on the first mute that would pause the MIDI.
bool MidiPauseRoutineVerified() {
    if (g_music.midiPauseBytesVerified < 0) {
        g_music.midiPauseBytesVerified = VerifyMidiPauseBytes() ? 1 : 0;
    }
    return g_music.midiPauseBytesVerified == 1;
}

void ApplyMusicMutePlan(const MusicMutePlan& plan) {
    const MusicMuteApply apply = PlanMusicMuteApply(plan);
    if (apply.logInvalid) {
        LOG_WARN("Music: engine byte 0x0058E074 is not 0 or 1; the game's race music is left alone");
    }
    // R3-native-block-stuck-midrace: the plan drops the remembered value with or without a restore
    // write, so the block is cleared here, before the write can fail or be skipped.
    if (apply.clearBlock) {
        g_music.nativeMusicBlocked.store(false);
    }
    if (!apply.write) {
        return;
    }
    if (!engine::WriteByte(engine::kRaceMusicMute, apply.value)) {
        if (!g_music.writeWarningLogged) {
            g_music.writeWarningLogged = true;
            LOG_WARN("Music: cannot write the engine race-music mute; the mute is skipped for this session");
        }
        return;
    }
    if (apply.post) {
        engine::PostMusicVolumeUpdate();
    }
    if (!plan.restore && apply.value == 1) {
        g_music.nativeMusicBlocked.store(true);
        int midiActive = 0;
        const bool midiKnown = engine::ReadByte(engine::kNativeMidiActive, midiActive);
        const bool midiActiveNow = midiKnown && midiActive != 0;
        const bool verified = MidiPauseRoutineVerified();
        if (ShouldPauseNativeMidi(GetMusicConfig().enabled, g_music.raceActive, true, midiActiveNow,
                                  verified)) {
            auto pauseMidi = reinterpret_cast<MidiPauseFn>(engine::Rebase(engine::kMidiPause));
            pauseMidi();
            LOG_INFO("Music: the native MIDI race music is paused while the player plays");
        } else if (midiActiveNow && !verified && !g_music.midiPauseWarningLogged) {
            // R3-unguarded-midi-pause-call: never call a routine whose shipped bytes do not match.
            g_music.midiPauseWarningLogged = true;
            LOG_WARN("Music: FUN_004459c0 does not match the original or patched exe; the native MIDI is "
                     "not paused and may play over the player");
        }
    }
    LOG_INFO("Music: race music %s (mute byte %u)", plan.restore ? "restored" : "muted",
             static_cast<unsigned>(plan.value));
}

// Runs on every race frame and at race end. The pure plan decides; this glue only reads the byte
// and the player state, then applies the write and the post.
void UpdateMusicMute() {
    int muteByte = -1;
    engine::ReadByte(engine::kRaceMusicMute, muteByte);
    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    const MusicMutePlan plan =
        PlanMusicMute(g_music.mute, MusicPlayerAudioEnabled(), now.playbackStarted, g_music.raceActive,
                      muteByte, engine::MusicThreadPresent());
    if (plan.write || plan.warnInvalid || plan.clearBlock) {
        ApplyMusicMutePlan(plan);
    }
}

void ApplyMusicJukeboxWrite(const MusicJukeboxTrackWrite& plan) {
    if (!plan.write) {
        return;
    }
    if (!engine::WriteInt(engine::kRacePick, plan.track) ||
        !engine::WriteInt(engine::kJukeboxBand, plan.band)) {
        if (!g_music.writeWarningLogged) {
            g_music.writeWarningLogged = true;
            LOG_WARN("Music: cannot write the engine jukebox; the sync is skipped for this session");
        }
    }
}

// Applies the race-start jukebox decision with the real mode. playerAlreadyStarted is true when the
// player already began playing before FUN_004463a0 reported the mode (the race loop presents two
// frames before that call), so the start command must not be sent twice.
void ApplyMusicRaceStartPlan(bool playerAlreadyStarted) {
    int racePick = -1;
    int menuPick = -1;
    engine::ReadInt(engine::kRacePick, racePick);
    engine::ReadInt(engine::kJukeboxTrack, menuPick);

    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    // Before the first start the player is stopped and will begin on the selected cassette (the
    // built-in one when nothing was picked).
    const bool roadRash = MusicPlayerSelectedCassette() <= 0 &&
                          ((now.cassetteIndex < 0) ? true : now.builtInCassette);
    const bool streamActive = g_music.nativeMode == MusicNativeMode::Stream;
    const MusicJukeboxRaceStart jukebox =
        PlanMusicJukeboxRaceStart(g_music.jukebox, roadRash, streamActive, racePick, menuPick);
    if (jukebox.logInactiveStream) {
        LOG_INFO("Music: the race uses MIDI, not the music stream; jukebox sync is off for this session");
    }
    if (jukebox.startAtTrack) {
        MusicPlayerRaceStartedAtTrack(jukebox.track);
    } else if (!playerAlreadyStarted) {
        MusicPlayerRaceStarted();
    }
    g_music.syncWrites = jukebox.syncWrites;
    g_music.lastSyncedTrack = -1;
    g_music.pendingModeSync = false;
}

// Resolves the saved cassette label against the real library (one log line when the saved label no
// longer exists). Runs on the game thread once the worker scan is ready.
void SyncSelectedCassetteFromLibrary() {
    if (!g_music.initialized || g_music.cassetteResolved || !MusicPlayerLibraryReady()) {
        return;
    }
    g_music.cassetteLabels = MusicPlayerCassetteLabels();
    const ResolvedMusicCassette resolved =
        ResolveSavedMusicCassette(g_music.cassetteLabels, g_music.savedCassetteLabel);
    g_music.selectedCassette = resolved.index;
    g_music.cassetteResolved = true;
    MusicPlayerSelectCassette(resolved.index);
    if (g_music.cassetteLabels.empty()) {
        return;
    }
    if (resolved.fallback) {
        LOG_WARN("Music: saved cassette '%ls' was not found; using '%ls'",
                 g_music.savedCassetteLabel.c_str(), g_music.cassetteLabels[0].c_str());
    } else if (!g_music.savedCassetteLabel.empty()) {
        LOG_INFO("Music: selected cassette '%ls' from the registry",
                 g_music.cassetteLabels[static_cast<size_t>(resolved.index)].c_str());
    }
}

void CompleteMusicRaceStart() {
    g_music.pendingRaceStart = false;
    SyncSelectedCassetteFromLibrary();
    if (g_music.nativeMode == MusicNativeMode::Unknown) {
        // M5c (finding B): the race loop presents two frames before FUN_004463a0 starts the race
        // music, so the mode is not known yet. Start the player now and let the FUN_004463a0
        // detour apply the jukebox plan when the real mode arrives.
        MusicPlayerRaceStarted();
        g_music.syncWrites = false;
        g_music.lastSyncedTrack = -1;
        g_music.pendingModeSync = true;
        return;
    }
    ApplyMusicRaceStartPlan(false);
}

// Runs on the game thread right after FUN_004463a0 started the race music. Reports the real mode
// once per race and, when the player started before the mode was known, applies the jukebox plan now.
void MusicOnNativeRaceAudioStart() {
    if (!GetMusicConfig().enabled || !g_music.initialized || !g_music.raceActive) {
        return;
    }
    int streamFlag = 0;
    int midiFlag = 0;
    if (!engine::ReadByte(engine::kRaceStreamActive, streamFlag) ||
        !engine::ReadByte(engine::kNativeMidiActive, midiFlag)) {
        return;
    }
    const MusicNativeMode mode = DecideMusicNativeMode(streamFlag != 0, midiFlag != 0);
    g_music.nativeMode = mode;
    if (!g_music.loggedModeThisRace) {
        g_music.loggedModeThisRace = true;
        LOG_INFO("Music: race music mode: %s (stream byte 0x005A6908=%d, MIDI byte 0x006163D3=%d)",
                 MusicNativeModeName(mode), streamFlag, midiFlag);
    }
    if (g_music.pendingModeSync && !g_music.pendingRaceStart) {
        ApplyMusicRaceStartPlan(true);
    }
}

void SyncMusicJukebox() {
    if (!g_music.syncWrites) {
        return;
    }
    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    if (now.trackIndex == g_music.lastSyncedTrack) {
        return;
    }
    const MusicJukeboxTrackWrite plan = PlanMusicJukeboxTrackWrite(
        now.builtInCassette, g_music.nativeMode == MusicNativeMode::Stream, now.trackIndex);
    ApplyMusicJukeboxWrite(plan);
    g_music.lastSyncedTrack = now.trackIndex;
}

// M5c (finding E): race frames came back after the worker auto-paused the player. Say what the
// player is doing and what the game's stream thread holds.
void LogMusicResumeDiagnostics() {
    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    MusicResumeDiagnostics info;
    info.state = ClassifyMusicResume(now.active, now.paused, MusicPlayerAutoPaused());
    info.trackIndex = now.trackIndex;
    int value = -1;
    info.streamPlaying = engine::ReadByte(engine::kStreamPlaying, value) ? value : -1;
    value = -1;
    info.streamReady = engine::ReadByte(engine::kStreamReady, value) ? value : -1;
    LOG_INFO("Music: race frames are back; player %s", DescribeMusicResume(info).c_str());
}

void MusicOnRacePresent() {
    if (!GetMusicConfig().enabled || !g_music.framesAvailable) {
        return;
    }
    if (!g_music.initialized) {
        return;
    }
    const MusicRacePresentCycle cycle = AdvanceMusicRacePresent(g_music.tracker, g_music.overlay);
    if (cycle.event == MusicRaceEvent::Started) {
        g_music.raceActive = true;
        g_music.pendingRaceStart = true;
        g_music.syncWrites = false;
        g_music.lastSyncedTrack = -1;
        // The mode of the previous race says nothing about this one.
        g_music.nativeMode = MusicNativeMode::Unknown;
        g_music.pendingModeSync = false;
        g_music.loggedModeThisRace = false;
        g_music.lastAutoResumes = MusicPlayerAutoResumeCount();
    }
    if (g_music.pendingRaceStart && MusicPlayerLibraryReady()) {
        CompleteMusicRaceStart();
    }
    if (g_music.raceActive && !g_music.pendingRaceStart) {
        SyncMusicJukebox();
    }
    if (g_music.raceActive) {
        // M5b (bug B): every race frame reports a heartbeat; the worker pauses the player when the
        // game stops presenting race frames (Esc pause, end-of-race video, focus loss).
        MusicPlayerRaceHeartbeat(GetTickCount64());
        UpdateMusicMute();
        const uint32_t resumes = MusicPlayerAutoResumeCount();
        if (resumes != g_music.lastAutoResumes) {
            g_music.lastAutoResumes = resumes;
            LogMusicResumeDiagnostics();
        }
    }
}

void MusicOnMenuPresent() {
    if (!GetMusicConfig().enabled || !g_music.framesAvailable || !g_music.initialized) {
        return;
    }
    // M5c (finding C): WM_PAINT after a restore and WM_TIMER id 7 reach the menu present while the
    // game sits in its Esc pause or focus-loss loop; those presents must not end the race.
    int pauseLoop = 0;
    const bool pauseLoopKnown = engine::ReadByte(engine::kGamePauseLoop, pauseLoop);
    if (g_music.tracker.OnMenuPresent(pauseLoopKnown && pauseLoop != 0) != MusicRaceEvent::Ended) {
        return;
    }
    g_music.raceActive = false;
    g_music.pendingRaceStart = false;
    g_music.syncWrites = false;
    g_music.pendingModeSync = false;
    g_music.nativeMode = MusicNativeMode::Unknown;
    MusicPlayerRaceEnded();
    UpdateMusicMute();
    // The restore is skipped when the game changed the byte itself; the native resume block must
    // still end with the race, or it would keep silencing menu music after a focus loss.
    g_music.nativeMusicBlocked.store(false);
}

void PollMusicHotkeysGlue() {
    if (!g_music.initialized) {
        return;
    }
    MusicHotkeyInput input;
    for (int i = 0; i < kMusicHotkeyCount; ++i) {
        const uint32_t vk = g_music.hotkeys.keys[i].vk;
        input.keyDown[i] = g_music.hotkeys.enabled[i] && vk != 0 &&
                           (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
    }
    input.ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    input.shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    input.altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    switch (PollMusicHotkeys(g_music.hotkeyState, g_music.hotkeys, input)) {
        case MusicHotkeyAction::Next:
            MusicPlayerNext();
            break;
        case MusicHotkeyAction::Previous:
            MusicPlayerPrevious();
            break;
        case MusicHotkeyAction::PlayPause:
            MusicPlayerTogglePause();
            break;
        case MusicHotkeyAction::NextCassette:
            MusicPlayerNextCassette();
            break;
        case MusicHotkeyAction::Overlay:
            // R3-002: the hotkey is a per-race show/hide of the panel. The configured mode
            // (registry/ini, shown and toggled by the Beeper row) stays untouched.
            MusicGlueOverlayHotkey();
            break;
        case MusicHotkeyAction::None:
            break;
    }
}

// The reserved hotkeys: the general mod's toggle key read from rr95gpu.ini (read only, never
// written), or its defaults when the file or the key is absent, plus F11 (the capture key).
void ReadReservedHotkeys(Hotkey (&reserved)[2]) {
    uint32_t toggleKey = VK_SCROLL;
    if (g_hostModule != nullptr) {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(g_hostModule), path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            std::wstring ini(path, length);
            const size_t slash = ini.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                ini.resize(slash + 1);
                ini += L"rr95gpu.ini";
                if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    wchar_t value[64] = {};
                    GetPrivateProfileStringW(L"World", L"ToggleKey", L"ScrollLock", value, 64, ini.c_str());
                    toggleKey = ParseVirtualKey(value, VK_SCROLL);
                    LOG_INFO("Music: reserved hotkeys follow rr95gpu.ini ToggleKey=0x%02X (read only)",
                             static_cast<unsigned>(toggleKey));
                }
            }
        }
    }
    reserved[0] = Hotkey{toggleKey, false, false, false};
    reserved[1] = Hotkey{VK_F11, false, false, false};
}

bool EnsureMusicInitialized() {
    const MusicConfig& music = GetMusicConfig();
    if (!music.enabled) {
        return false;
    }
    if (g_music.initialized) {
        return true;
    }
    const std::wstring gameFolder = engine::GameFolder();
    if (gameFolder.empty()) {
        if (!g_music.initWarningLogged) {
            g_music.initWarningLogged = true;
            LOG_WARN("Music: cannot resolve the game folder; the music player is disabled for this session");
        }
        return false;
    }
    const std::wstring musicRoot = ResolveMusicGamePath(music.path);
    // A fresh install has no music folder yet: create it so the user knows where to put cassettes.
    if (!musicRoot.empty() && GetFileAttributesW(musicRoot.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (CreateDirectoryW(musicRoot.c_str(), nullptr)) {
            LOG_INFO("Music: created the music folder %ls", musicRoot.c_str());
        } else {
            LOG_WARN("Music: cannot create the music folder %ls (error %lu)", musicRoot.c_str(),
                     GetLastError());
        }
    }

    MusicPlayerInitWithPaths(music, musicRoot, gameFolder);

    g_music.hotkeys.keys[0] = music.nextKey;
    g_music.hotkeys.keys[1] = music.prevKey;
    g_music.hotkeys.keys[2] = music.playPauseKey;
    g_music.hotkeys.keys[3] = music.nextCassetteKey;
    g_music.hotkeys.keys[4] = music.overlayKey;
    // A valid ModMusicKeys blob (read at startup) wins over the ini. The blob has no modifiers, so
    // a registry binding is always a plain key; ini bindings keep their modifiers.
    MusicKeys iniKeys;
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        iniKeys.vk[i] = static_cast<uint8_t>(g_music.hotkeys.keys[i].vk & 0xFFu);
    }
    const MusicKeysRegistryValue keyRegistry = ReadMusicKeysRegistryValue();
    const EffectiveMusicKeys effectiveKeys = ResolveEffectiveMusicKeys(iniKeys, keyRegistry);
    LOG_INFO("Music: hotkeys from %s", effectiveKeys.fromRegistry ? "the registry (ModMusicKeys)" : "the ini");
    if (effectiveKeys.fromRegistry) {
        for (int i = 0; i < kMusicKeyActionCount; ++i) {
            g_music.hotkeys.keys[i] = Hotkey{effectiveKeys.keys.vk[i], false, false, false};
        }
    }
    if (keyRegistry.status == MusicKeysRegistryStatus::Invalid) {
        LOG_WARN("Music: HKCU\\%s\\%s is not a valid key set (REG_BINARY, version %u, %u bytes, no zero, "
                 "reserved or duplicate key); the ini hotkeys stay",
                 kGameRegistryKey, kMusicKeysRegistryValueName,
                 static_cast<unsigned>(kMusicKeysRegistryVersion),
                 static_cast<unsigned>(kMusicKeysRegistrySize));
    }

    Hotkey reserved[2];
    ReadReservedHotkeys(reserved);
    g_music.reservedHotkeys[0] = reserved[0];
    g_music.reservedHotkeys[1] = reserved[1];
    const int disabled = ResolveMusicHotkeyConflicts(g_music.hotkeys, reserved, 2);
    if (disabled > 0) {
        LOG_WARN("Music: %d hotkey(s) are empty or equal an existing mod hotkey; they are ignored", disabled);
    }
    // The key rows show the same plain keys the race poll uses.
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        g_music.musicKeys.vk[i] = static_cast<uint8_t>(g_music.hotkeys.keys[i].vk & 0xFFu);
    }

    if (ReadMusicCassetteRegistryValue(g_music.savedCassetteLabel)) {
        LOG_INFO("Music: saved cassette '%ls' from the registry", g_music.savedCassetteLabel.c_str());
    }
    // M6a: a valid ModMusicOverlay value wins over the ini Overlay setting; the Beeper row later
    // toggles and rewrites the registry value.
    const EffectiveMusicOverlay effectiveOverlay =
        ResolveEffectiveMusicOverlay(music.overlay, ReadMusicOverlayRegistryValue());
    g_music.overlayMode = effectiveOverlay.mode;
    g_music.overlay.SetMode(effectiveOverlay.mode);
    LOG_INFO("Music: beeper from %s (%s)", effectiveOverlay.fromRegistry ? "the registry (ModMusicOverlay)" : "the ini",
             effectiveOverlay.mode == MusicOverlayMode::Pinned ? "Fijo" : "A demanda");

    g_music.initialized = true;
    LOG_INFO("Music: player enabled (folder %ls, game folder %ls)", musicRoot.c_str(), gameFolder.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Beeper overlay (S3b)
// ---------------------------------------------------------------------------
// Content for one frame. The built-in cassette reads the game's band and track names, like the
// in-ddraw player did, so the panel shows the original labels instead of the file stem.
MusicOverlayContent OverlayContentFromPlayer() {
    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    MusicOverlayContent content;
    content.cassetteLabel = now.cassetteLabel;
    content.title = now.title;
    content.artist = now.artist;
    content.playing = !now.paused;
    content.raceActive = g_music.raceActive;
    content.positionMs = now.positionMs;
    content.durationMs = now.durationMs;
    content.changeCounter = now.changeCounter;
    if (now.builtInCassette && now.trackIndex >= 0) {
        content.artist = engine::GameString(RoadRashBandStringId(now.trackIndex), content.artist);
        content.title = engine::GameString(RoadRashTrackStringId(now.trackIndex), content.title);
    }
    // R3-marquee-glue-fill-unproved: the marquee offset is not filled here. UpdateMusicOverlayFrame
    // builds the drawn content and the key from the same time and LCD width, so one cannot follow the
    // clock without the other.
    return content;
}

const RR95OverlayFrame* MusicGlueOverlayFrame() {
    if (!g_music.initialized || !GetMusicConfig().enabled) {
        return nullptr;
    }
    const MusicPlayerNowPlaying now = MusicPlayerSnapshot();
    if (!now.active) {
        return nullptr;
    }
    const uint64_t nowMs = GetTickCount64();
    const MusicOverlayContent content = OverlayContentFromPlayer();
    g_music.overlay.OnChange(now.changeCounter, nowMs);
    const float alpha = g_music.overlay.Alpha(nowMs, g_music.raceActive);
    if (alpha <= 0.0f) {
        return nullptr;
    }
    // The layer is handed over in 640x480 game coordinates: the presenter scales and letterboxes
    // it like the game image, so the panel keeps its place in any window.
    const MusicOverlayPlacement placement =
        ComputeMusicOverlayPlacement(640, 480, MusicOverlayRect{0, 0, 640, 480}, 0, 0, kMusicOverlayCaseW,
                                     kMusicOverlayCaseH, GetMusicConfig().corner);
    if (!placement.draw) {
        return nullptr;
    }
    UpdateMusicOverlayFrame(content, placement.scale, placement.x, placement.y, nowMs, alpha,
                            g_music.overlayFrame);
    MusicOverlayBitmap& bitmap = g_music.overlayFrame.bitmap;
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.pixels.empty()) {
        return nullptr;
    }
    RR95OverlayFrame& frame = g_music.overlayApiFrame;
    frame.pixels = bitmap.pixels.data();
    frame.width = bitmap.width;
    frame.height = bitmap.height;
    frame.x = g_music.overlayFrame.x;
    frame.y = g_music.overlayFrame.y;
    frame.changeCounter = g_music.overlayFrame.changeCounter;
    return &frame;
}

const RR95OverlayFrame* OverlayFrameSource(void* /*ctx*/) {
    return MusicGlueOverlayFrame();
}

// Per-race show/hide of the panel, from the overlay hotkey. It never changes the configured mode.
void MusicGlueOverlayHotkey() {
    if (!g_music.initialized) {
        return;
    }
    g_music.overlay.OnOverlayHotkey(GetTickCount64(), g_music.raceActive);
    LOG_INFO("Music: beeper %s by the overlay hotkey for this race (configured mode stays %s)",
             g_music.overlay.Visibility() == MusicOverlayVisibility::Hidden ? "hidden" : "shown",
             g_music.overlayMode == MusicOverlayMode::Pinned ? "Fijo" : "A demanda");
}

// ---------------------------------------------------------------------------
// Reproductor menu row, popup and key capture (S3b)
// ---------------------------------------------------------------------------
// Shipped prologues of the three routines the popup calls through rebased pointers. The redraw and
// the popup open have no absolute operand in their span; the close starts with `mov eax, ds:[..]`,
// whose operand the loader relocates (offset 1).
constexpr uint8_t kItemRedrawPrologue[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00};
constexpr uint8_t kPopupOpenPrologue[8] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x53, 0x56};
constexpr uint8_t kPopupClosePrologue[10] = {0xA1, 0x34, 0x40, 0x5C, 0x00,
                                              0x53, 0x8B, 0x1D, 0xA8, 0xC6};
constexpr size_t kPopupCloseAbsoluteOperandOffset = 1;

using ItemRedrawFn = void(__fastcall*)(void* item, void* selected);
using PopupOpenFn = void(__fastcall*)(int count, void* items, int focus);
using PopupCloseFn = void(__cdecl*)();

// The shipped bytes of all three routines, checked once before the row is registered. A routine
// that does not match is never called; the row is then left out instead.
bool VerifyPopupRoutines() {
    if (!engine::BytesMatch(engine::kItemRedraw, kItemRedrawPrologue, sizeof(kItemRedrawPrologue))) {
        return false;
    }
    if (!engine::BytesMatch(engine::kPopupOpen, kPopupOpenPrologue, sizeof(kPopupOpenPrologue))) {
        return false;
    }
    uint8_t expected[sizeof(kPopupClosePrologue)];
    std::memcpy(expected, kPopupClosePrologue, sizeof(expected));
    const intptr_t delta =
        static_cast<intptr_t>(engine::Rebase(0x00400000u)) - static_cast<intptr_t>(0x00400000u);
    uint32_t operand = 0;
    std::memcpy(&operand, expected + kPopupCloseAbsoluteOperandOffset, sizeof(operand));
    operand = static_cast<uint32_t>(static_cast<intptr_t>(operand) + delta);
    std::memcpy(expected + kPopupCloseAbsoluteOperandOffset, &operand, sizeof(operand));
    return engine::BytesMatch(engine::kPopupClose, expected, sizeof(expected));
}

bool PopupRoutinesVerified() {
    if (g_music.popupRoutinesVerified < 0) {
        g_music.popupRoutinesVerified = VerifyPopupRoutines() ? 1 : 0;
    }
    return g_music.popupRoutinesVerified == 1;
}

bool ReproductorPopupIsOpen() {
    int first = -1;
    if (!engine::ReadInt(engine::kPopupFirstSlot, first)) {
        return false;
    }
    if (first < 0 || first >= kPopupItemTableSlots) {
        return false;
    }
    int slot = 0;
    if (!engine::ReadInt(engine::kPopupItemTable + static_cast<uint32_t>(first) * 4u, slot)) {
        return false;
    }
    int open = 0;
    if (!engine::ReadByte(engine::kPopupOpenFlag, open)) {
        return false;
    }
    return ReproductorPopupStillOpen(static_cast<uint8_t>(open), static_cast<uintptr_t>(static_cast<uint32_t>(slot)),
                                     reinterpret_cast<uintptr_t>(g_reproductorPopupItems));
}

// True when any keyboard key other than exceptVk is still held. Virtual keys 0..7 are the mouse
// buttons and VK_CANCEL, so they are skipped. On a GetKeyboardState failure the answer is "yes",
// which only keeps the capture waiting or draining a little longer.
bool AnyKeyboardKeyDown(int exceptVk) {
    BYTE state[256] = {};
    if (!GetKeyboardState(state)) {
        return true;
    }
    for (int vk = 8; vk < 256; ++vk) {
        if (vk == exceptVk) {
            continue;
        }
        if ((state[vk] & 0x80) != 0) {
            return true;
        }
    }
    return false;
}

MusicCassetteMenuInput CassetteMenuInput() {
    MusicCassetteMenuInput input;
    input.libraryReady = MusicPlayerLibraryReady();
    input.selectionResolved = g_music.cassetteResolved;
    input.savedLabel = g_music.savedCassetteLabel;
    input.labels = g_music.cassetteLabels;
    input.currentIndex = g_music.selectedCassette;
    return input;
}

MusicPopupTextInput PopupTextInput() {
    MusicPopupTextInput input;
    input.libraryReady = MusicPlayerLibraryReady();
    input.cassetteResolved = g_music.cassetteResolved;
    input.savedCassetteLabel = g_music.savedCassetteLabel;
    input.cassetteLabels = g_music.cassetteLabels;
    input.selectedCassette = g_music.selectedCassette;
    input.overlayMode = g_music.overlayMode;
    input.capture = g_music.keyCapture;
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        input.keyVk[i] = g_music.musicKeys.vk[i];
    }
    return input;
}

// Row text of the graphics menu row. Always the header; the popup rows come through the provider.
const char* ReproductorRowText(void* /*ctx*/) {
    CopyReproductorText(g_reproductorRowText, sizeof(g_reproductorRowText), ReproductorHeaderText());
    return g_reproductorRowText;
}

// Item text for this mod's own popup array only: the general mod asks the provider for every item
// it does not own, so anything outside the array returns null and the game's own text stays.
const char* ReproductorItemText(const void* item, void* /*ctx*/) {
    if (!g_music.menuRowRegistered || item == nullptr) {
        return nullptr;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(item);
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_reproductorPopupItems);
    const uintptr_t end = base + sizeof(g_reproductorPopupItems);
    if (address < base || address >= end) {
        return nullptr;
    }
    const size_t offset = static_cast<size_t>(address - base);
    if (offset % kPopupItemSize != 0) {
        return nullptr;
    }
    const int row = static_cast<int>(offset / kPopupItemSize);
    // R3-003/R3-004: the row resolves the saved cassette on the first request after the worker scan
    // finishes and reads the loading text without a wait before that.
    SyncSelectedCassetteFromLibrary();
    CopyReproductorText(g_reproductorPopupText, sizeof(g_reproductorPopupText),
                        ReproductorPopupRowText(row, PopupTextInput()));
    return g_reproductorPopupText;
}

// Redraws one hotkey row, only while the popup is open: a closed popup's items keep a stale HWND.
void RedrawReproductorKeyRow(int action) {
    const int row = kReproductorKeyRowFirst + action;
    if (row < kReproductorKeyRowFirst || row >= kReproductorKeyRowFirst + kMusicKeyActionCount ||
        !ReproductorPopupIsOpen()) {
        return;
    }
    uint8_t* item = g_reproductorPopupItems + static_cast<size_t>(row) * kPopupItemSize;
    auto fnRedraw = reinterpret_cast<ItemRedrawFn>(engine::Rebase(engine::kItemRedraw));
    fnRedraw(item, item);
}

// Stores one captured key (swapping a duplicate binding) and writes ModMusicKeys. The race poll
// reads g_music.hotkeys, so the next race frame already uses it. A key the conflict rule would
// disable is rejected before anything changes or is written (S3b review
// R3-reserved-capture-shown-but-disabled), and the other rows keep their ini modifiers (S3b review
// R3-capture-strips-ini-modifiers).
void ApplyMusicKeyBinding(int actionIndex, uint8_t vk) {
    const char* label = MusicKeyActionLabel(static_cast<MusicKeyAction>(actionIndex));
    const std::string keyName = MusicKeyName(vk);
    const MusicHotkeyCaptureApply applied =
        ApplyMusicHotkeyCapture(g_music.hotkeys, g_music.musicKeys,
                                static_cast<MusicKeyAction>(actionIndex), vk, g_music.reservedHotkeys, 2);
    if (!applied.accepted) {
        LOG_INFO("Options menu: %s key %s (0x%02X) is reserved or disabled by the conflict rule; the "
                 "binding is unchanged",
                 label, keyName.c_str(), vk);
        return;
    }
    const MusicHotkeyBindings previous = g_music.hotkeys;
    g_music.hotkeys = applied.hotkeys;
    g_music.musicKeys = applied.keys;
    g_music.hotkeyState = MusicHotkeyState{};
    if (WriteMusicKeysRegistryValue(applied.keys)) {
        LOG_INFO("Options menu: %s key set to %s (0x%02X); HKCU\\%s\\%s written (REG_BINARY)",
                 label, keyName.c_str(), vk, kGameRegistryKey, kMusicKeysRegistryValueName);
    } else {
        LOG_WARN("Options menu: %s key set to %s (0x%02X) for this session only; could not write HKCU\\%s\\%s",
                 label, keyName.c_str(), vk, kGameRegistryKey, kMusicKeysRegistryValueName);
    }
    // A swap changes a second row too.
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        if (i == actionIndex || applied.keys.vk[i] != previous.keys[i].vk) {
            RedrawReproductorKeyRow(i);
        }
    }
}

// Ends a capture (or its drain) with no binding change, e.g. when the popup closed or a race
// started. Redraws the row only when it still exists on screen.
void EndMusicKeyCaptureGlue(const char* reason) {
    if (!MusicKeyCaptureSwallowsInput(g_music.keyCapture)) {
        return;
    }
    const bool wasActive = g_music.keyCapture.active;
    const int action = g_music.keyCapture.action;
    g_music.keyCapture = EndMusicKeyCapture();
    if (wasActive) {
        LOG_INFO("Options menu: key capture for %s ended (%s); the binding is unchanged",
                 MusicKeyActionLabel(static_cast<MusicKeyAction>(action)), reason);
        RedrawReproductorKeyRow(action);
    }
}

// Feeds one RashButton input message to the capture state machine. Returns true when the menu must
// not see the message. The swallow rule itself is pure (DecideMusicKeyCaptureMessage) and tested.
bool HandleMusicKeyCaptureMessage(UINT msg, WPARAM wParam) {
    const uint8_t vk = static_cast<uint8_t>(wParam & 0xFFu);
    const MusicKeyCaptureMessageDecision decision =
        DecideMusicKeyCaptureMessage(g_music.keyCapture, ReproductorPopupIsOpen(), msg, vk);
    if (decision.endCapture) {
        EndMusicKeyCaptureGlue("the popup closed");
        return false;
    }
    if (!decision.feed) {
        return false;
    }
    MusicKeyCaptureInput input;
    input.key = MusicKeyCaptureKeyForMessage(msg);
    if (input.key == MusicKeyCaptureKey::Down || input.key == MusicKeyCaptureKey::Up) {
        input.vk = vk;
        input.otherKeysDown = AnyKeyboardKeyDown(input.vk);
    } else if (input.key == MusicKeyCaptureKey::None) {
        input.otherKeysDown = AnyKeyboardKeyDown(-1);
    }
    const int action = g_music.keyCapture.action;
    const MusicKeyCaptureUpdate update = FeedMusicKeyCapture(g_music.keyCapture, input);
    g_music.keyCapture = update.state;
    if (update.result == MusicKeyCaptureResult::Captured) {
        ApplyMusicKeyBinding(action, update.capturedVk);
    } else if (update.result == MusicKeyCaptureResult::Cancelled) {
        LOG_INFO("Options menu: key capture for %s cancelled (%s); the binding is unchanged",
                 MusicKeyActionLabel(static_cast<MusicKeyAction>(action)),
                 input.vk == kMusicKeyReturn ? "Enter" : "Esc");
        RedrawReproductorKeyRow(action);
    }
    // A Shift key-up is fed to the state machine but still reaches the engine (tests cover the
    // exception in DecideMusicKeyCaptureMessage).
    return decision.swallow;
}

// RashButton window procedure (every menu button, popup rows included). While a key capture is
// active or draining, keyboard and mouse messages go to the capture and never reach the engine, so
// the menu cannot move focus, close the popup or activate a row. Everything else passes through.
LRESULT __stdcall DetourRashButton(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (HandleMusicKeyCaptureMessage(msg, wParam)) {
        return 0;
    }
    if (g_origRashButton) {
        return g_origRashButton(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Reproductor popup rows. Each row redraws itself and keeps the popup open; "Volver" closes through
// the engine's own FUN_0043a390, the same call the native popup handler uses.
int __fastcall ReproductorPopupSelect(void* item) {
    uint32_t id = 0;
    std::memcpy(&id, static_cast<uint8_t*>(item) + kMenuItemId, sizeof(id));
    auto fnRedraw = reinterpret_cast<ItemRedrawFn>(engine::Rebase(engine::kItemRedraw));
    switch (static_cast<ReproductorPopupRow>(id)) {
        case ReproductorPopupRow::Cassette: {
            SyncSelectedCassetteFromLibrary();
            const MusicCassetteMenuDecision decision = DecideMusicCassetteMenu(CassetteMenuInput());
            if (!decision.canSelect || decision.nextIndex < 0) {
                // R3-003: before the library is ready the placeholder row does nothing, so no
                // ModMusicCassette value is ever written from it.
                break;
            }
            const int next = decision.nextIndex;
            g_music.selectedCassette = next;
            g_music.cassetteResolved = true;
            MusicPlayerSelectCassette(next);
            const std::wstring& label = g_music.cassetteLabels[static_cast<size_t>(next)];
            if (WriteMusicCassetteRegistryValue(label)) {
                LOG_INFO("Options menu: cassette '%ls' selected (HKCU\\%s\\%s, REG_SZ); used at the next race",
                         label.c_str(), kGameRegistryKey, kMusicCassetteRegistryValueName);
            } else {
                LOG_WARN("Options menu: could not write HKCU\\%s\\%s; the cassette is used for this session only",
                         kGameRegistryKey, kMusicCassetteRegistryValueName);
            }
            fnRedraw(item, item);
            break;
        }
        case ReproductorPopupRow::Overlay: {
            // R3-002: always toggles the configured mode, independent of the hotkey's show/hide.
            const MusicOverlayMode mode = DecideMusicBeeperMenu(g_music.overlayMode, true).next;
            g_music.overlayMode = mode;
            g_music.overlay.SetMode(mode);
            if (WriteMusicOverlayRegistryValue(mode)) {
                LOG_INFO("Options menu: beeper set to %s (HKCU\\%s\\%s, REG_BINARY)",
                         mode == MusicOverlayMode::Pinned ? "Fijo" : "A demanda", kGameRegistryKey,
                         kMusicOverlayRegistryValueName);
            } else {
                LOG_WARN("Options menu: could not write HKCU\\%s\\%s; the beeper mode is used for this session only",
                         kGameRegistryKey, kMusicOverlayRegistryValueName);
            }
            fnRedraw(item, item);
            break;
        }
        case ReproductorPopupRow::NextKey:
        case ReproductorPopupRow::PreviousKey:
        case ReproductorPopupRow::PlayPauseKey:
        case ReproductorPopupRow::NextCassetteKey:
        case ReproductorPopupRow::OverlayKey: {
            // M6b: Enter (activated on its key-up) puts the row in capture mode. Without the
            // RashButton hook the menu would act on the captured keys, so the row does nothing.
            const int action = static_cast<int>(id) - kReproductorKeyRowFirst;
            if (!g_music.keyCaptureSafe) {
                if (!g_music.keyCaptureWarningLogged) {
                    g_music.keyCaptureWarningLogged = true;
                    LOG_WARN("Options menu: the RashButton hook on FUN_0043cea0 is not installed; key rows are "
                             "read-only");
                }
                break;
            }
            g_music.keyCapture = StartMusicKeyCapture(static_cast<MusicKeyAction>(action));
            LOG_INFO("Options menu: key capture started for %s",
                     MusicKeyActionLabel(static_cast<MusicKeyAction>(action)));
            fnRedraw(item, item);
            break;
        }
        case ReproductorPopupRow::Back:
            reinterpret_cast<PopupCloseFn>(engine::Rebase(engine::kPopupClose))();
            break;
        case ReproductorPopupRow::Header:
        default:
            break;
    }
    return 0;
}

// Select handler of the menu row: opens the native popup through FUN_0043a4e0 and stays in menu 19.
int __fastcall ReproductorMenuSelect(void* /*item*/) {
    if (!EnsureMusicInitialized()) {
        return 0;
    }
    // R3-004: open at once, never wait on the worker scan. The row text resolves the cassette on
    // the first request after the library is ready (R3-003).
    SyncSelectedCassetteFromLibrary();
    const MusicCassetteMenuInput input = CassetteMenuInput();
    const MusicCassetteMenuDecision decision = DecideMusicCassetteMenu(input);
    const uint32_t handler = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ReproductorPopupSelect));
    // M6b: a capture never outlives its popup.
    g_music.keyCapture = EndMusicKeyCapture();
    BuildReproductorPopupItems(handler, g_reproductorPopupItems);
    auto fnOpen = reinterpret_cast<PopupOpenFn>(engine::Rebase(engine::kPopupOpen));
    fnOpen(kReproductorPopupItemCount, g_reproductorPopupItems, 0);
    if (decision.loading) {
        LOG_INFO("Options menu: Reproductor popup opened (cassette loading, beeper %s)",
                 g_music.overlayMode == MusicOverlayMode::Pinned ? "Fijo" : "A demanda");
    } else {
        LOG_INFO("Options menu: Reproductor popup opened (cassette '%ls', beeper %s)",
                 input.labels[static_cast<size_t>(decision.index)].c_str(),
                 g_music.overlayMode == MusicOverlayMode::Pinned ? "Fijo" : "A demanda");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Fallback without the general mod (S4)
// ---------------------------------------------------------------------------
// The general mod detours the same three engine sites; the music mod installs its own only when the
// binding decision says it owns that capability, so two DLLs never patch the same bytes. The bytes
// are the same as the general mod's table (renderer/src/hook_sites.cpp), and check_hook_sites.py
// cross-checks both tables.
rr95::HookSite g_siteFallbackRacePresent{
    "FUN_0046e630",
    engine::kRacePresent,
    {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x2C, 0x04, 0x00, 0x00},
    9,
    {1, 2, 6},
    nullptr,
    nullptr,
    false
};

rr95::HookSite g_siteFallbackMenuPresent{
    "FUN_0046ded0",
    engine::kMenuPresent,
    {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24},
    6,
    {1, 2, 3},
    nullptr,
    nullptr,
    false
};

rr95::HookSite g_siteFallbackItemText{
    "FUN_0045b930",
    engine::kItemText,
    {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20},
    6,
    {1, 2, 3},
    nullptr,
    nullptr,
    false
};

using RacePresentFn = void(__fastcall*)(uint8_t* buffer, int pitch, int palette);
using MenuPresentFn = void(__cdecl*)();
using ItemTextFn = const char*(__fastcall*)(void* item);
using StretchDIBitsFn = int(WINAPI*)(HDC hdc, int xDest, int yDest, int destWidth, int destHeight, int xSrc,
                                     int ySrc, int srcWidth, int srcHeight, const VOID* bits,
                                     const BITMAPINFO* info, UINT usage, DWORD rop);

RacePresentFn g_origFallbackRacePresent = nullptr;
MenuPresentFn g_origFallbackMenuPresent = nullptr;
ItemTextFn g_origFallbackItemText = nullptr;
StretchDIBitsFn g_origStretchDIBits = nullptr;

// S4 marking: this table is the install guard. check_hook_sites.py parses it and fails unless every
// fallback site is paired with a capability and the install loop calls ShouldInstallFallbackSite.
struct MusicFallbackSite {
    rr95::HookSite* site;
    Capability capability;
    void* detour;
    void** trampoline;
};

bool BlendMusicOverlayIntoPresentDib(const VOID* bits, const BITMAPINFO* info, MusicDibSavedRegion& saved);
void __fastcall DetourFallbackRacePresent(uint8_t* buffer, int pitch, int palette);
void __cdecl DetourFallbackMenuPresent();
const char* __fastcall DetourFallbackItemText(void* item);
int WINAPI DetourStretchDIBits(HDC hdc, int xDest, int yDest, int destWidth, int destHeight, int xSrc, int ySrc,
                               int srcWidth, int srcHeight, const VOID* bits, const BITMAPINFO* info,
                               UINT usage, DWORD rop);

const MusicFallbackSite kMusicFallbackSites[] = {
    {&g_siteFallbackRacePresent, Capability::RaceFrames, reinterpret_cast<void*>(&DetourFallbackRacePresent),
     reinterpret_cast<void**>(&g_origFallbackRacePresent)},
    {&g_siteFallbackMenuPresent, Capability::MenuFrames, reinterpret_cast<void*>(&DetourFallbackMenuPresent),
     reinterpret_cast<void**>(&g_origFallbackMenuPresent)},
    {&g_siteFallbackItemText, Capability::ItemText, reinterpret_cast<void*>(&DetourFallbackItemText),
     reinterpret_cast<void**>(&g_origFallbackItemText)},
};

// The GDI IAT hook chains: it saves whatever slot value the game already has, so an older general
// mod without the API keeps its own counter.
rr95::IatHook g_iatDibBlend{"GDI32.dll", "StretchDIBits", reinterpret_cast<void*>(&DetourStretchDIBits),
                            reinterpret_cast<void**>(&g_origStretchDIBits), nullptr};

void __fastcall DetourFallbackRacePresent(uint8_t* buffer, int pitch, int palette) {
    // The frame callback runs first so the panel is ready when the original present blits the DIB.
    ++g_music.frameSequence;
    MusicGlueOnRaceFrame();
    if (g_origFallbackRacePresent) {
        g_origFallbackRacePresent(buffer, pitch, palette);
    }
}

void __cdecl DetourFallbackMenuPresent() {
    ++g_music.frameSequence;
    MusicGlueOnMenuFrame();
    if (g_origFallbackMenuPresent) {
        g_origFallbackMenuPresent();
    }
}

const char* __fastcall DetourFallbackItemText(void* item) {
    const MusicItemTextTarget target = DecideMusicItemTextOwner(
        reinterpret_cast<uintptr_t>(item), g_music.menuInPlaceInstalled,
        static_cast<uintptr_t>(engine::Rebase(MusicGraphicsMenuItemVa(kGraphicsMenuReproductorSlot))),
        reinterpret_cast<uintptr_t>(g_reproductorPopupItems), sizeof(g_reproductorPopupItems));
    if (target.owner == MusicItemTextOwner::MenuRow) {
        return ReproductorRowText(nullptr);
    }
    if (target.owner == MusicItemTextOwner::PopupRow) {
        const char* text = ReproductorItemText(item, nullptr);
        if (text != nullptr) {
            return text;
        }
    }
    return g_origFallbackItemText ? g_origFallbackItemText(item) : nullptr;
}

// Blends the panel into the game's DIB for the original blit, only for the buffer the present fills.
// Returns true and fills saved when the panel was blended, so the caller can put the original pixels
// back right after the blit.
//
// S3b review R3-dib-blend-persistence: the game's DIB is never left modified. Blending in place and
// leaving it would blend again on the next present of the same DIB and would keep the panel after it
// is hidden. Saving the panel's rectangle (not the whole frame) and restoring it is the cheaper of
// the two correct options.
//
// S4 review R3-dib-sequence-guard-drops-reblit: because the DIB is restored, the next blit of the
// same frame must blend again (a repaint, a pause loop or a focus-loss loop). There is no per-frame
// sequence guard here; BeginMusicDibBlit only stops a reentrant blend inside one blit.
bool BlendMusicOverlayIntoPresentDib(const VOID* bits, const BITMAPINFO* info, MusicDibSavedRegion& saved) {
    if (bits == nullptr || info == nullptr || !g_music.gdiOverlayInstalled) {
        return false;
    }
    int dibPointer = 0;
    int width = 0;
    int height = 0;
    if (!engine::ReadInt(engine::kDibBuffer, dibPointer) || dibPointer == 0 ||
        !engine::ReadInt(engine::kBufferWidth, width) || !engine::ReadInt(engine::kBufferHeight, height) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return false;
    }
    if (reinterpret_cast<const VOID*>(static_cast<uintptr_t>(static_cast<uint32_t>(dibPointer))) != bits) {
        return false;  // another DIB the game blits: never touched
    }
    // S3b review R3-dib-orientation-unchecked: the header decides the row order, and a frame that is
    // not the engine's 32-bit frame is skipped instead of written at a guessed offset.
    const MusicDibFramePlan plan = PlanMusicDibFrame(info->bmiHeader.biBitCount, info->bmiHeader.biWidth,
                                                    info->bmiHeader.biHeight, width, height);
    if (!plan.blend) {
        if (!g_music.dibWarningLogged) {
            g_music.dibWarningLogged = true;
            LOG_WARN("%s", plan.logMessage);
        }
        return false;
    }
    const RR95OverlayFrame* frame = MusicGlueOverlayFrame();
    if (frame == nullptr) {
        return false;
    }
    const MusicOverlayBitmap& bitmap = g_music.overlayFrame.bitmap;
    if (frame->pixels != bitmap.pixels.data()) {
        return false;  // never blend a buffer this mod does not own
    }
    auto* dib = reinterpret_cast<uint32_t*>(const_cast<VOID*>(bits));
    const int x = static_cast<int>(frame->x);
    const int y = static_cast<int>(frame->y);
    if (!SaveMusicDibRegion(dib, width, height, plan.orientation, bitmap, x, y, saved)) {
        return false;
    }
    BlendMusicOverlayIntoDib(dib, width, height, plan.orientation, bitmap, x, y);
    return true;
}

int WINAPI DetourStretchDIBits(HDC hdc, int xDest, int yDest, int destWidth, int destHeight, int xSrc, int ySrc,
                               int srcWidth, int srcHeight, const VOID* bits, const BITMAPINFO* info,
                               UINT usage, DWORD rop) {
    if (g_origStretchDIBits == nullptr) {
        return 0;
    }
    MusicDibSavedRegion saved;
    const bool canBlend = BeginMusicDibBlit(g_music.dibBlit, g_music.frameSequence);
    const bool blended = canBlend && BlendMusicOverlayIntoPresentDib(bits, info, saved);
    const int result = g_origStretchDIBits(hdc, xDest, yDest, destWidth, destHeight, xSrc, ySrc, srcWidth,
                                           srcHeight, bits, info, usage, rop);
    if (blended) {
        RestoreMusicDibRegion(reinterpret_cast<uint32_t*>(const_cast<VOID*>(bits)), saved);
    }
    EndMusicDibBlit(g_music.dibBlit, canBlend);
    return result;
}

bool MenuSlotsAreShipped() {
    const auto* slots = reinterpret_cast<const uint8_t*>(engine::Rebase(kVaGraphicsMenuItems));
    const intptr_t delta =
        static_cast<intptr_t>(engine::Rebase(0x00400000u)) - static_cast<intptr_t>(0x00400000u);
    return GraphicsMenuSlotsAreShipped(slots, delta);
}

// Writes the Reproductor row into the shipped eight-item graphics menu (slot 7, the 20-pixel
// filler). The engine's own HWND dwords are kept, and the shipped snapshot is kept for restore.
bool InstallMusicMenuInPlace(const Decisions& decisions) {
    const MusicMenuInPlacePlan plan =
        PlanMusicMenuInPlace(ShouldInstallFallbackSite(decisions, Capability::GraphicsMenuRow),
                             ShouldInstallFallbackSite(decisions, Capability::ItemText),
                             MenuSlotsAreShipped(), g_music.menuInPlaceInstalled);
    if (plan.action != MusicMenuInPlaceAction::Install) {
        // Only an explicit skip is worth a line; the API case (NotOwned) is the normal path.
        if (!g_music.fallbackMenuWarningLogged &&
            (plan.action == MusicMenuInPlaceAction::AlreadyInstalled ||
             plan.action == MusicMenuInPlaceAction::NotShipped)) {
            g_music.fallbackMenuWarningLogged = true;
            LOG_INFO("%s", plan.logMessage);
        }
        return false;
    }
    if (!PopupRoutinesVerified()) {
        if (!g_music.menuRowWarningLogged) {
            g_music.menuRowWarningLogged = true;
            LOG_WARN("Music: the shipped bytes of FUN_00439160, FUN_0043a4e0 or FUN_0043a390 do not match; "
                     "the Reproductor row is not added");
        }
        return false;
    }
    auto* slots = reinterpret_cast<uint8_t*>(engine::Rebase(kVaGraphicsMenuItems));
    std::memcpy(g_music.shippedMenuSlots, slots, kGraphicsMenuSlotsSize);
    GraphicsMenuModRows rows;
    rows.reproductor = true;
    rows.reproductorHandler = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ReproductorMenuSelect));
    uint8_t next[kGraphicsMenuSlotsSize];
    BuildGraphicsMenuSlots(g_music.shippedMenuSlots, ComputeGraphicsMenuLayout(false, true), rows, next);
    for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
        const size_t offset = static_cast<size_t>(slot) * kMenuItemSize;
        std::memcpy(next + offset, slots + offset, 4);  // the button HWND the engine writes
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(slots, kGraphicsMenuSlotsSize, PAGE_READWRITE, &oldProtect)) {
        LOG_WARN("Music: cannot make graphics menu 19 writable (error %lu); the Reproductor row is not "
                 "added", GetLastError());
        return false;
    }
    for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
        // One derivation for the row address: the item-text owner check uses the same helper.
        auto* item = reinterpret_cast<uint8_t*>(engine::Rebase(MusicGraphicsMenuItemVa(slot)));
        std::memcpy(item, next + static_cast<size_t>(slot) * kMenuItemSize, kMenuItemSize);
    }
    VirtualProtect(slots, kGraphicsMenuSlotsSize, oldProtect, &oldProtect);
    g_music.menuInPlaceInstalled = true;
    g_music.menuRowRegistered = true;
    LOG_INFO("%s", plan.logMessage);
    return true;
}

void RestoreMusicMenuInPlace() {
    if (!g_music.menuInPlaceInstalled) {
        return;
    }
    auto* slots = reinterpret_cast<uint8_t*>(engine::Rebase(kVaGraphicsMenuItems));
    DWORD oldProtect = 0;
    if (VirtualProtect(slots, kGraphicsMenuSlotsSize, PAGE_READWRITE, &oldProtect)) {
        uint8_t restored[kGraphicsMenuSlotsSize];
        std::memcpy(restored, g_music.shippedMenuSlots, kGraphicsMenuSlotsSize);
        for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
            const size_t offset = static_cast<size_t>(slot) * kMenuItemSize;
            // Keep the window handles the engine wrote, exactly like the install does.
            std::memcpy(restored + offset, slots + offset, 4);
        }
        for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
            auto* item = reinterpret_cast<uint8_t*>(engine::Rebase(MusicGraphicsMenuItemVa(slot)));
            std::memcpy(item, restored + static_cast<size_t>(slot) * kMenuItemSize, kMenuItemSize);
        }
        VirtualProtect(slots, kGraphicsMenuSlotsSize, oldProtect, &oldProtect);
        LOG_INFO("Music: the shipped graphics menu is restored");
    }
    g_music.menuInPlaceInstalled = false;
    g_music.menuRowRegistered = false;
}

// Installs every fallback site the decision owns, then the in-place menu row and the GDI blend.
void InstallMusicFallbackHooks(const Decisions& decisions, const RR95ModApi* api) {
    HMODULE module = static_cast<HMODULE>(g_hostModule);
    for (const MusicFallbackSite& fallback : kMusicFallbackSites) {
        if (!ShouldInstallFallbackSite(decisions, fallback.capability)) {
            continue;
        }
        if (fallback.site->installed) {
            continue;
        }
        fallback.site->detour = fallback.detour;
        fallback.site->originalTrampoline = fallback.trampoline;
        if (rr95::InstallHook(*fallback.site, module)) {
            LOG_INFO("Music: fallback hook on %s installed (%s)", fallback.site->name,
                     CapabilityToString(fallback.capability));
        } else {
            LOG_WARN("Music: failed to install the fallback hook on %s; that capability stays off for this "
                     "session", fallback.site->name);
        }
    }
    g_music.fallbackRacePresentInstalled = g_siteFallbackRacePresent.installed;
    g_music.fallbackMenuPresentInstalled = g_siteFallbackMenuPresent.installed;
    g_music.fallbackItemTextInstalled = g_siteFallbackItemText.installed;
    InstallMusicMenuInPlace(decisions);
    if (!ShouldInstallGdiOverlayHook(decisions, api != nullptr)) {
        return;
    }
    if (g_music.gdiOverlayInstalled) {
        return;
    }
    g_music.gdiOverlayInstalled = rr95::InstallIatHook(g_iatDibBlend, module);
    if (g_music.gdiOverlayInstalled) {
        LOG_INFO("Music: GDI StretchDIBits hook installed; the beeper is blended into the game DIB");
    } else {
        LOG_WARN("Music: GDI32!StretchDIBits is not imported by name; the beeper is not drawn without the "
                 "general mod");
    }
}

void RemoveMusicFallbackHooks() {
    HMODULE module = static_cast<HMODULE>(g_hostModule);
    rr95::RemoveIatHook(g_iatDibBlend);
    g_music.gdiOverlayInstalled = false;
    for (const MusicFallbackSite& fallback : kMusicFallbackSites) {
        rr95::RemoveHook(*fallback.site, module);
    }
    g_origFallbackRacePresent = nullptr;
    g_origFallbackMenuPresent = nullptr;
    g_origFallbackItemText = nullptr;
    g_music.fallbackRacePresentInstalled = false;
    g_music.fallbackMenuPresentInstalled = false;
    g_music.fallbackItemTextInstalled = false;
    RestoreMusicMenuInPlace();
}

}  // namespace

int InstallMusicPlayerHooks(void* hostModule) {
    g_hostModule = hostModule;
    engine::SetHostModule(hostModule);
    HMODULE module = static_cast<HMODULE>(hostModule);
    int installed = 0;

    g_siteMidiResume.detour = reinterpret_cast<void*>(&DetourMidiResume);
    g_siteMidiResume.originalTrampoline = reinterpret_cast<void**>(&g_origMidiResume);
    if (rr95::InstallHook(g_siteMidiResume, module)) {
        ++installed;
        LOG_INFO("Music: native MIDI resume hook on FUN_00445ba0 installed");
    } else {
        LOG_WARN("Music: failed to install the native MIDI resume hook on FUN_00445ba0; the game's MIDI can "
                 "resume over the player after an Esc pause or focus loss");
    }

    g_siteStreamResume.detour = reinterpret_cast<void*>(&DetourStreamResume);
    g_siteStreamResume.originalTrampoline = reinterpret_cast<void**>(&g_origStreamResume);
    if (rr95::InstallHook(g_siteStreamResume, module)) {
        ++installed;
        LOG_INFO("Music: native stream resume hook on FUN_00411c00 installed");
    } else {
        LOG_WARN("Music: failed to install the native stream resume hook on FUN_00411c00; the game's stream "
                 "can restart over the player after an Esc pause or focus loss");
    }

    g_siteRaceMusicStart.detour = reinterpret_cast<void*>(&DetourRaceMusicStart);
    g_siteRaceMusicStart.originalTrampoline = reinterpret_cast<void**>(&g_origRaceMusicStart);
    if (rr95::InstallHook(g_siteRaceMusicStart, module)) {
        ++installed;
        LOG_INFO("Music: race music start hook on FUN_004463a0 installed");
    } else {
        LOG_WARN("Music: failed to install the race music start hook on FUN_004463a0; the race music mode is "
                 "not reported and the jukebox sync is skipped for this session");
    }

    // S3b: key capture needs the menu to ignore input while it runs. The row itself is registered
    // later through the general mod's API; without this hook the key rows stay read-only.
    g_siteRashButton.detour = reinterpret_cast<void*>(&DetourRashButton);
    g_siteRashButton.originalTrampoline = reinterpret_cast<void**>(&g_origRashButton);
    g_music.keyCaptureSafe = rr95::InstallHook(g_siteRashButton, module);
    if (g_music.keyCaptureSafe) {
        ++installed;
        LOG_INFO("Music: RashButton hook on FUN_0043cea0 installed; the Reproductor key rows can capture");
    } else {
        g_origRashButton = nullptr;
        LOG_WARN("Music: failed to install the RashButton hook on FUN_0043cea0; the Reproductor key rows are "
                 "read-only");
    }
    return installed;
}

void RemoveMusicPlayerHooks(void* hostModule) {
    HMODULE module = static_cast<HMODULE>(hostModule);
    RemoveMusicFallbackHooks();
    rr95::RemoveHook(g_siteMidiResume, module);
    rr95::RemoveHook(g_siteStreamResume, module);
    rr95::RemoveHook(g_siteRaceMusicStart, module);
    rr95::RemoveHook(g_siteRashButton, module);
    g_origMidiResume = nullptr;
    g_origStreamResume = nullptr;
    g_origRaceMusicStart = nullptr;
    g_origRashButton = nullptr;
    g_music.keyCaptureSafe = false;
    g_music.keyCapture = EndMusicKeyCapture();
}

// One API row registration attempt: the row and its item text provider. Records the cause on
// failure so the gate decides whether another attempt can succeed; never registers twice. Both the
// early DirectDrawCreate path and the DirectSoundCreate binding go through it.
bool TryRegisterMusicMenuRow(const RR95ModApi* api) {
    if (api == nullptr || g_music.menuRowRegistered) {
        return false;
    }
    if (!PopupRoutinesVerified()) {
        g_music.menuRowFailure = MusicMenuRowFailure::RoutinesUnverified;
        g_music.menuRowStatus = 0;
        return false;
    }
    RR95MenuRowDesc row{};
    row.selectHandler = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ReproductorMenuSelect));
    row.text = &ReproductorRowText;
    row.ctx = nullptr;
    const int32_t rowStatus = api->addGraphicsMenuRow(&row);
    if (rowStatus != RR95_STATUS_OK) {
        g_music.menuRowFailure = MusicMenuRowFailureFromApiStatus(rowStatus);
        g_music.menuRowStatus = rowStatus;
        return false;
    }
    const int32_t textStatus = api->registerItemTextProvider(&ReproductorItemText, nullptr);
    if (textStatus != RR95_STATUS_OK) {
        api->addGraphicsMenuRow(nullptr);
        g_music.menuRowFailure = MusicMenuRowFailure::ItemTextRejected;
        g_music.menuRowStatus = textStatus;
        return false;
    }
    g_music.menuRowRegistered = true;
    g_music.apiRowRegistered = true;
    g_music.itemTextProvider.registered = true;
    g_music.menuRowFailure = MusicMenuRowFailure::None;
    g_music.menuRowStatus = 0;
    return true;
}

void MusicGlueApplyDecision(const Decisions& decisions, const BindingInputs& inputs, const RR95ModApi* api) {
    if (!MusicProxyEnabled()) {
        return;
    }
    // The API row registration (early at DirectDrawCreate, or here) also registered the item text
    // provider. When the general mod reports NOT_USED or FAILED, or APPLIED without its item-text
    // detour, that provider is cleared before the in-place fallback takes the row and the item text,
    // so no stale provider stays live in the general mod. The clear reads the same inputs this
    // decision was computed from; a refused clear keeps the provider registered and is retried at
    // the next binding decision.
    const MusicItemTextProviderClearResult providerClear = ClearStaleMusicItemTextProvider(
        g_music.itemTextProvider, api, inputs.menuState, inputs.itemTextDetourInstalled);
    if (providerClear.log && providerClear.outcome == MusicItemTextProviderClear::Cleared) {
        LOG_INFO("Music: the API item text provider is cleared; the mod's own item text takes over");
    } else if (providerClear.log && providerClear.outcome == MusicItemTextProviderClear::Refused) {
        LOG_WARN("Music: the general mod refused to clear the item text provider (status %d); the clear is "
                 "retried at the next binding decision",
                 providerClear.status);
    }
    // S4: the fallback sites, the in-place menu row and the GDI blend exist before the rest of the
    // plan runs, so the overlay and the row blocks below can report what actually happened.
    InstallMusicFallbackHooks(decisions, api);
    // S3b: the mod rasterizes the beeper and the general mod draws it. Without the general mod the
    // panel is blended into the game DIB by the GDI hook (S4); when neither path exists the panel
    // stays hidden and the player keeps working.
    if (decisions.overlayLayer == Binding::UseApi && api != nullptr) {
        if (!g_music.overlayRegistered) {
            const int32_t status = api->setOverlayLayer(&OverlayFrameSource, nullptr);
            g_music.overlayRegistered = status == RR95_STATUS_OK;
            if (g_music.overlayRegistered) {
                LOG_INFO("Music: beeper overlay layer registered with the general mod");
            } else {
                LOG_WARN("Music: the general mod rejected the overlay layer (status %d); the beeper is "
                         "hidden for this session",
                         status);
            }
        }
    } else if (decisions.overlayLayer == Binding::OwnHooks && !g_music.overlayWarningLogged) {
        g_music.overlayWarningLogged = true;
        if (g_music.gdiOverlayInstalled) {
            LOG_INFO("Music: the beeper is drawn into the game DIB through the mod's own GDI hook");
        } else {
            LOG_WARN("Music: the beeper is not drawn: neither the general mod's presenter nor the GDI "
                     "StretchDIBits hook is available");
        }
    }
    // S3b: the Reproductor row arrives through the general mod's API, before it builds the menu. The
    // pure plan reads the menu state the API reports. S3b review R3-menu-row-unknown-never-retried:
    // an unknown state is not a failure; the gate retries at the next binding decision and only the
    // final states stop the row for the session (S4 adds the in-place row for those).
    //
    // An early registration at DirectDrawCreate already set menuRowRegistered, so this block never
    // registers a second row. After NOT_USED or FAILED the early API row is dead and the in-place
    // fallback row installed above is the live one.
    if (!g_music.menuRowRegistered && decisions.graphicsMenuRow == Binding::UseApi &&
        decisions.itemText == Binding::UseApi && api != nullptr) {
        const MusicMenuRowPlan plan = PlanMusicMenuRow(api->getMenuState());
        MusicMenuRowGate gate = DecideMusicMenuRowGate(plan.action, g_music.menuRowRegistered,
                                                      g_music.menuRowLog, g_music.menuRowFailure);
        if (gate.attempt) {
            if (TryRegisterMusicMenuRow(api)) {
                LOG_INFO("%s", plan.logMessage);
            } else {
                // The attempt changed the cause: decide again with it, so a final cause ends the
                // retries and a transient one is retried at the next binding decision.
                gate = DecideMusicMenuRowGate(plan.action, g_music.menuRowRegistered,
                                              g_music.menuRowLog, g_music.menuRowFailure);
            }
        }
        // S5 review R3-menu-row-final-cause-after-transient-never-logged: each kind of line has its
        // own flag, so a final cause after a logged transient cause is still written once.
        if (gate.logTransient) {
            g_music.menuRowLog.transientLogged = true;
        } else if (gate.logFinal) {
            g_music.menuRowLog.finalLogged = true;
        }
        if (gate.logTransient || gate.logFinal) {
            if (g_music.menuRowFailure != MusicMenuRowFailure::None) {
                const std::string message =
                    MusicMenuRowFailureMessage(g_music.menuRowFailure, g_music.menuRowStatus);
                if (MusicMenuRowFailureIsTransient(g_music.menuRowFailure) ||
                    g_music.menuRowFailure == MusicMenuRowFailure::ApiTooLate) {
                    LOG_INFO("%s", message.c_str());
                } else {
                    LOG_WARN("%s", message.c_str());
                }
            } else {
                LOG_INFO("%s", plan.logMessage);
            }
        }
    }
    // Say once which row draws the menu. When the general mod does not relocate it, a row
    // registered early is never used and only the in-place fallback row can appear.
    const int32_t menuState = api != nullptr ? api->getMenuState() : RR95_MENU_STATE_NOT_USED;
    const MusicMenuRowOwner owner =
        DecideMusicMenuRowOwner(menuState, g_music.apiRowRegistered, g_music.menuInPlaceInstalled);
    if (owner != g_music.menuRowOwner) {
        g_music.menuRowOwner = owner;
        if (owner == MusicMenuRowOwner::ApiRow) {
            LOG_INFO("Music: the Reproductor row is drawn by the general mod's relocated graphics menu");
        } else if (owner == MusicMenuRowOwner::InPlaceRow) {
            LOG_INFO("Music: the Reproductor row is drawn in the shipped graphics menu; the general "
                     "mod does not relocate it");
        }
    }
    const bool framesUseApi = decisions.raceFrames == Binding::UseApi &&
                              decisions.menuFrames == Binding::UseApi;
    if (!framesUseApi) {
        const bool ownBoth = ShouldInstallFallbackSite(decisions, Capability::RaceFrames) &&
                             ShouldInstallFallbackSite(decisions, Capability::MenuFrames);
        if (ownBoth) {
            g_music.framesAvailable =
                g_music.fallbackRacePresentInstalled && g_music.fallbackMenuPresentInstalled;
            if (g_music.framesAvailable) {
                if (!g_music.frameWarningLogged) {
                    g_music.frameWarningLogged = true;
                    LOG_INFO("Music: race and menu frames come from the mod's own present hooks; the general "
                             "mod is not needed for them");
                }
            } else if (!g_music.frameWarningLogged) {
                g_music.frameWarningLogged = true;
                LOG_WARN("Music: the mod's own present hooks are not installed; the player stays inactive for "
                         "this session");
            }
        } else {
            g_music.framesAvailable = false;
            if (!g_music.frameWarningLogged) {
                g_music.frameWarningLogged = true;
                LOG_INFO("Music: race and menu frames are not available yet (%s / %s); the player stays "
                         "inactive",
                         BindingToString(decisions.raceFrames), BindingToString(decisions.menuFrames));
            }
        }
        return;
    }
    g_music.framesAvailable = true;
    if (api != nullptr) {
        struct Callbacks {
            static void Race(void*) {
                MusicProxyBind(BindingTrigger::FrameCallback);
                MusicGlueOnRaceFrame();
            }
            static void Menu(void*) {
                MusicProxyBind(BindingTrigger::FrameCallback);
                MusicGlueOnMenuFrame();
            }
        };
        int32_t raceStatus = RR95_STATUS_OK;
        int32_t menuStatus = RR95_STATUS_OK;
        if (!g_music.raceCallbackRegistered) {
            raceStatus = api->registerRaceFrameCallback(&Callbacks::Race, nullptr);
            g_music.raceCallbackRegistered = raceStatus == RR95_STATUS_OK;
        }
        if (!g_music.menuCallbackRegistered) {
            menuStatus = api->registerMenuFrameCallback(&Callbacks::Menu, nullptr);
            g_music.menuCallbackRegistered = menuStatus == RR95_STATUS_OK;
        }
        if (!g_music.raceCallbackRegistered || !g_music.menuCallbackRegistered) {
            g_music.framesAvailable = false;
            if (!g_music.callbackRejectedLogged) {
                g_music.callbackRejectedLogged = true;
                LOG_WARN("Music: the general mod rejected the frame callbacks (race %d, menu %d); the player "
                         "stays inactive for this session",
                         raceStatus, menuStatus);
            }
        }
    }
}

bool MusicGlueRegisterEarlyRow(const RR95ModApi* api) {
    if (!MusicProxyEnabled() || api == nullptr || g_music.menuRowRegistered) {
        return false;
    }
    // The general mod installs its hooks at its own DirectDrawCreate, after this hook returns, and
    // builds the relocated menu at the first menu frame. Registering here is early enough for both.
    if (!TryRegisterMusicMenuRow(api)) {
        return false;
    }
    g_music.earlyRowRegistered = true;
    LOG_INFO("Music: the Reproductor row and the item text provider are registered before the general "
             "mod installs its hooks");
    return true;
}

bool MusicGlueMenuRowRegistered() {
    return g_music.menuRowRegistered;
}

void MusicGlueOnRaceFrame() {
    if (!EnsureMusicInitialized()) {
        return;
    }
    MusicOnRacePresent();
    // Hotkeys are polled from the race-frame path, like the in-ddraw player did.
    PollMusicHotkeysGlue();
}

void MusicGlueOnMenuFrame() {
    MusicOnMenuPresent();
}

}  // namespace rr95::music
