#include "music_game.h"
#include "music_menu.h"

#include <windows.h>

#include <utility>

// ---------------------------------------------------------------------------
// Race phase
// ---------------------------------------------------------------------------

MusicRaceEvent MusicRacePhaseTracker::OnRacePresent() {
    m_menuPresents = 0;
    if (m_inRace) {
        return MusicRaceEvent::None;
    }
    m_inRace = true;
    return MusicRaceEvent::Started;
}

MusicRacePresentCycle AdvanceMusicRacePresent(MusicRacePhaseTracker& tracker, MusicOverlayController& overlay) {
    MusicRacePresentCycle cycle;
    cycle.event = tracker.OnRacePresent();
    if (cycle.event == MusicRaceEvent::Started) {
        // A show/hide from the last race never carries over; this race starts from the configured
        // mode. The reset belongs to the transition, so every caller gets it.
        overlay.ResetForRace();
        cycle.overlayReset = true;
    }
    return cycle;
}

MusicRaceEvent MusicRacePhaseTracker::OnMenuPresent(bool gamePauseLoop) {
    if (!m_inRace) {
        m_menuPresents = 0;
        return MusicRaceEvent::None;
    }
    if (gamePauseLoop) {
        // WM_PAINT after a restore and WM_TIMER id 7 reach the menu present while the game sits in
        // its Esc pause or focus-loss loop; they say nothing about the race.
        m_menuPresents = 0;
        return MusicRaceEvent::None;
    }
    if (m_menuPresents < kMusicMenuPresentsToEndRace - 1) {
        ++m_menuPresents;
        return MusicRaceEvent::None;
    }
    m_menuPresents = 0;
    m_inRace = false;
    return MusicRaceEvent::Ended;
}

// ---------------------------------------------------------------------------
// Native race-music mute
// ---------------------------------------------------------------------------

MusicMutePlan PlanMusicMute(MusicMuteState& state, bool playerEnabled, bool playbackStarted,
                            bool raceActive, int currentMuteByte, bool musicThreadPresent) {
    MusicMutePlan plan;
    const bool shouldMute = raceActive && playerEnabled && playbackStarted;

    if (shouldMute) {
        if (state.originalValue >= 0) {
            // The mod already muted this race; never write the byte again.
            return plan;
        }
        if (currentMuteByte != 0 && currentMuteByte != 1) {
            if (!state.warnedInvalid) {
                state.warnedInvalid = true;
                plan.warnInvalid = true;
            }
            return plan;
        }
        state.originalValue = currentMuteByte;
        plan.write = true;
        plan.value = 1;
        plan.post = musicThreadPresent;
        return plan;
    }

    if (state.originalValue < 0) {
        return plan;
    }
    const int original = state.originalValue;
    state.originalValue = -1;
    // The remembered value is dropped even when no restore write follows, so the glue must clear
    // its native-resume block now: a player that stopped or was disabled mid-race must not leave
    // the game's Esc and focus-loss loops unable to resume its own music.
    plan.clearBlock = true;
    // Restore only the value the mod wrote. Anything else means the game or the player changed the
    // mute on purpose, and the mod must not overwrite that.
    if (currentMuteByte != 1) {
        return plan;
    }
    plan.write = true;
    plan.value = static_cast<uint8_t>(original);
    plan.post = musicThreadPresent;
    plan.restore = true;
    return plan;
}

MusicMuteApply PlanMusicMuteApply(const MusicMutePlan& plan) {
    MusicMuteApply apply;
    // R3-warninvalid-no-longer-stops-write: a warning means the engine byte is not 0 or 1, so no
    // byte write, volume post or block-set may follow from that plan. The block clear is a separate
    // concern and is still honoured (R3-clearblock-after-warninvalid).
    const bool write = plan.write && !plan.warnInvalid;
    apply.logInvalid = plan.warnInvalid;
    apply.clearBlock = plan.clearBlock;
    apply.write = write;
    apply.value = write ? plan.value : 0;
    apply.post = write && plan.post;
    return apply;
}

// ---------------------------------------------------------------------------
// Native MIDI
// ---------------------------------------------------------------------------
bool ShouldPauseNativeMidi(bool musicEnabled, bool raceActive, bool muteAppliedNow,
                           bool nativeMidiActive, bool routineVerified) {
    return musicEnabled && raceActive && muteAppliedNow && nativeMidiActive && routineVerified;
}

bool ShouldBlockNativeMusicResume(bool musicEnabled, bool modRaceMuteActive) {
    return musicEnabled && modRaceMuteActive;
}

// ---------------------------------------------------------------------------
// Native race music mode
// ---------------------------------------------------------------------------

MusicNativeMode DecideMusicNativeMode(bool streamFlagSet, bool midiFlagSet) {
    // FUN_004463a0 sets exactly one of the two flags. The stream flag is authoritative when both
    // are set (FUN_00411850 never leaves DAT_006163d3 behind).
    if (streamFlagSet) {
        return MusicNativeMode::Stream;
    }
    if (midiFlagSet) {
        return MusicNativeMode::Midi;
    }
    return MusicNativeMode::Unknown;
}

const char* MusicNativeModeName(MusicNativeMode mode) {
    switch (mode) {
        case MusicNativeMode::Stream:
            return "stream";
        case MusicNativeMode::Midi:
            return "MIDI";
        case MusicNativeMode::Unknown:
            break;
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Jukebox sync
// ---------------------------------------------------------------------------

MusicJukeboxRaceStart PlanMusicJukeboxRaceStart(MusicJukeboxState& state, bool roadRashCassette,
                                                bool raceStreamActive, int racePick, int menuPick) {
    MusicJukeboxRaceStart plan;
    if (!roadRashCassette) {
        return plan;
    }
    const int pick = raceStreamActive ? racePick : menuPick;
    if (pick >= 0 && pick < kRoadRashTrackCount) {
        plan.startAtTrack = true;
        plan.track = pick;
    }
    if (raceStreamActive) {
        // Only a race that actually started with a valid pick may write the jukebox.
        plan.syncWrites = plan.startAtTrack;
        return plan;
    }
    plan.syncWrites = false;
    if (!state.loggedInactiveStream) {
        state.loggedInactiveStream = true;
        plan.logInactiveStream = true;
    }
    return plan;
}

MusicJukeboxTrackWrite PlanMusicJukeboxTrackWrite(bool roadRashCassette, bool raceStreamActive,
                                                  int track) {
    MusicJukeboxTrackWrite plan;
    if (!roadRashCassette || !raceStreamActive || track < 0 || track >= kRoadRashTrackCount) {
        return plan;
    }
    plan.write = true;
    plan.track = track;
    plan.band = RoadRashBandOfTrack(track);
    return plan;
}

// ---------------------------------------------------------------------------
// Resume diagnostics
// ---------------------------------------------------------------------------

MusicResumeState ClassifyMusicResume(bool active, bool paused, bool autoPaused) {
    if (!active) {
        return MusicResumeState::Stopped;
    }
    if (!paused) {
        return MusicResumeState::Playing;
    }
    return autoPaused ? MusicResumeState::AutoPaused : MusicResumeState::UserPaused;
}

std::string DescribeMusicResume(const MusicResumeDiagnostics& diagnostics) {
    const char* state = "stopped";
    switch (diagnostics.state) {
        case MusicResumeState::Playing:
            state = "playing";
            break;
        case MusicResumeState::AutoPaused:
            state = "auto-paused";
            break;
        case MusicResumeState::UserPaused:
            state = "user-paused";
            break;
        case MusicResumeState::Stopped:
            break;
    }
    const auto describeByte = [](int value) {
        return value < 0 ? std::string("?") : std::to_string(value);
    };
    std::string text(state);
    text += (diagnostics.trackIndex >= 0)
                ? ", track " + std::to_string(diagnostics.trackIndex)
                : std::string(", no track");
    text += ", stream playing " + describeByte(diagnostics.streamPlaying);
    text += ", stream ready " + describeByte(diagnostics.streamReady);
    return text;
}

// ---------------------------------------------------------------------------
// Game metadata
// ---------------------------------------------------------------------------

std::wstring DecodeMusicGameString(const char* text, size_t maxBytes, const std::wstring& fallback) {
    if (text == nullptr || maxBytes == 0) {
        return fallback;
    }
    size_t length = 0;
    while (length < maxBytes && text[length] != '\0') {
        ++length;
    }
    if (length == 0) {
        return fallback;
    }
    const int sourceLength = static_cast<int>(length);
    const int wideLength = MultiByteToWideChar(1252, 0, text, sourceLength, nullptr, 0);
    if (wideLength <= 0) {
        return fallback;
    }
    std::wstring result(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(1252, 0, text, sourceLength, &result[0], wideLength) <= 0) {
        return fallback;
    }
    return result;
}

uint32_t MusicGameStringOffset(uint32_t stringId) {
    return stringId * static_cast<uint32_t>(sizeof(const char*));
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

bool SameHotkey(const Hotkey& a, const Hotkey& b) {
    return a.vk != 0 && a.vk == b.vk && a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt;
}

int ResolveMusicHotkeyConflicts(MusicHotkeyBindings& bindings, const Hotkey* reserved,
                                size_t reservedCount) {
    int disabled = 0;
    for (int i = 0; i < kMusicHotkeyCount; ++i) {
        if (!bindings.enabled[i]) {
            continue;
        }
        bool conflict = bindings.keys[i].vk == 0;
        for (size_t r = 0; r < reservedCount && !conflict; ++r) {
            conflict = SameHotkey(bindings.keys[i], reserved[r]);
        }
        if (conflict) {
            bindings.enabled[i] = false;
            ++disabled;
        }
    }
    return disabled;
}

MusicHotkeyCaptureApply ApplyMusicHotkeyCapture(const MusicHotkeyBindings& hotkeys, const MusicKeys& keys,
                                                MusicKeyAction action, uint32_t vk, const Hotkey* reserved,
                                                size_t reservedCount) {
    MusicHotkeyCaptureApply result;
    result.hotkeys = hotkeys;
    result.keys = keys;
    const int actionIndex = static_cast<int>(action);
    if (actionIndex < 0 || actionIndex >= kMusicHotkeyCount) {
        return result;
    }
    // Only a plain virtual-key byte can be stored; everything the conflict rule would disable is
    // rejected here, so the popup never shows a binding the race poll ignores and nothing is written.
    if (vk == 0 || vk > 0xFFu || IsReservedMusicKey(vk) || IsMusicKeyModifier(vk)) {
        return result;
    }
    const Hotkey captured{vk, false, false, false};
    for (size_t r = 0; r < reservedCount; ++r) {
        if (SameHotkey(captured, reserved[r])) {
            return result;
        }
    }

    // The plain-key view ModMusicKeys stores: a duplicate swaps the two keys there, which is the only
    // row besides the captured one that changes.
    MusicKeys nextKeys = rr95::music::ApplyMusicKeyCapture(keys, action, vk);
    // The captured row takes the pressed plain key. Every other row keeps its own hotkey, modifiers
    // included; a swap partner only takes the captured row's old plain key.
    MusicHotkeyBindings next = hotkeys;
    next.keys[actionIndex] = captured;
    next.enabled[actionIndex] = true;
    for (int i = 0; i < kMusicHotkeyCount; ++i) {
        if (i == actionIndex || nextKeys.vk[i] == keys.vk[i]) {
            continue;
        }
        next.keys[i].vk = nextKeys.vk[i];
        next.enabled[i] = true;
    }
    // S4 review R3-capture-skips-conflict-rule-on-swap: a swap partner takes the captured row's old
    // key with its own modifiers, a combination the capture itself never checked. The full conflict
    // rule runs on every row now: if any of them would be disabled as reserved, the whole capture is
    // rejected, so no binding changes and nothing is written to ModMusicKeys.
    for (int i = 0; i < kMusicHotkeyCount; ++i) {
        if (!next.enabled[i]) {
            continue;
        }
        if (next.keys[i].vk == 0) {
            return result;
        }
        for (size_t r = 0; r < reservedCount; ++r) {
            if (SameHotkey(next.keys[i], reserved[r])) {
                return result;
            }
        }
    }
    result.hotkeys = next;
    result.keys = nextKeys;
    result.accepted = true;
    return result;
}

bool MusicHotkeyBindingMatches(const MusicHotkeyBindings& bindings, int index, bool ctrlDown,
                               bool shiftDown, bool altDown) {
    if (index < 0 || index >= kMusicHotkeyCount || !bindings.enabled[index]) {
        return false;
    }
    const Hotkey& key = bindings.keys[index];
    if (key.vk == 0) {
        return false;
    }
    if (key.ctrl || key.shift || key.alt) {
        return HotkeyMatches(key, ctrlDown, shiftDown, altDown);
    }
    // A binding without modifiers accepts the press whatever is held, unless another enabled binding
    // on the same key wants exactly the held modifiers: then that binding wins. With no modifier
    // held no other binding can be "exact but different", so duplicate plain bindings keep the old
    // first-one-wins behaviour.
    for (int other = 0; other < kMusicHotkeyCount; ++other) {
        if (other == index || !bindings.enabled[other]) {
            continue;
        }
        const Hotkey& candidate = bindings.keys[other];
        const bool hasModifiers = candidate.ctrl || candidate.shift || candidate.alt;
        if (hasModifiers && candidate.vk == key.vk && candidate.ctrl == ctrlDown &&
            candidate.shift == shiftDown && candidate.alt == altDown) {
            return false;
        }
    }
    return true;
}

MusicHotkeyAction PollMusicHotkeys(MusicHotkeyState& state, const MusicHotkeyBindings& bindings,
                                   const MusicHotkeyInput& input) {
    static const MusicHotkeyAction kActions[kMusicHotkeyCount] = {
        MusicHotkeyAction::Next, MusicHotkeyAction::Previous, MusicHotkeyAction::PlayPause,
        MusicHotkeyAction::NextCassette, MusicHotkeyAction::Overlay};
    for (int i = 0; i < kMusicHotkeyCount; ++i) {
        const bool down = input.keyDown[i];
        const bool pressed = down && !state.held[i];
        state.held[i] = down;
        if (!pressed || !bindings.enabled[i]) {
            continue;
        }
        if (!MusicHotkeyBindingMatches(bindings, i, input.ctrlDown, input.shiftDown, input.altDown)) {
            continue;
        }
        return kActions[i];
    }
    return MusicHotkeyAction::None;
}

// ---------------------------------------------------------------------------
// Library loading
// ---------------------------------------------------------------------------

std::vector<MusicCassette> BuildMusicPlayerCassettes(const std::vector<std::wstring>& scanRelativePaths,
                                                     const std::wstring& musicRoot,
                                                     const std::wstring& gameFolder) {
    std::vector<MusicCassette> result;
    result.push_back(MakeRoadRashCassette());

    const auto trimSeparators = [](std::wstring value) {
        while (!value.empty() && (value.back() == L'\\' || value.back() == L'/')) {
            value.pop_back();
        }
        return value;
    };
    const std::wstring gameRoot = trimSeparators(gameFolder);
    if (!gameRoot.empty()) {
        for (MusicTrack& track : result.front().tracks) {
            track.relativePath = gameRoot + L"\\" + track.relativePath;
        }
    }

    const std::wstring root = trimSeparators(musicRoot);
    MusicLibrary library = BuildMusicLibrary(scanRelativePaths);
    for (MusicCassette& cassette : library.cassettes) {
        for (MusicTrack& track : cassette.tracks) {
            if (!root.empty()) {
                track.relativePath = root + L"\\" + track.relativePath;
            }
        }
        result.push_back(std::move(cassette));
    }
    return result;
}
