#pragma once
#include <cstdint>
#include "../include/rr95_mod_api.h"

// Pure binding decision for the standalone music mod (rr95-music-standalone S2).
//
// The music mod connects to the general mod through RR95_GetModApi when the general mod owns a
// site, and installs its own hooks only on sites the general mod does not hold. This header is
// free of Windows headers so music_test can exercise every rule in-process.
//
// The API path is dormant: no released mod exports RR95_GetModApi, so the decision is OwnHooks for
// every capability and the player runs standalone. Next to another mod that already detoured the
// same routines, the experimental [Music] OtherModsCompat=1 chains those OwnHooks sites onto that
// mod's hooks (src/shared/hook_chain.h) instead of refusing them.
namespace rr95::music {

enum class Capability : int {
    RaceFrames = 0,
    MenuFrames = 1,
    GraphicsMenuRow = 2,
    ItemText = 3,
    OverlayLayer = 4,
    Count = 5
};

enum class Binding {
    Disabled = 0,   // music is off or the exe is unsupported: forward only
    UseApi = 1,     // connect through the general mod
    OwnHooks = 2,   // the music mod owns the site
    Wait = 3        // the answer is not known yet; decide again later
};

// menuState is one of RR95_MENU_STATE_* or -1 when the API cannot answer yet.
struct BindingInputs {
    bool musicEnabled = true;
    bool exeSupported = false;
    bool apiAvailable = false;  // RR95_GetModApi(1) returns a table with every appended field
    int32_t hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    int32_t menuState = -1;
    bool itemTextDetourInstalled = false;
};

struct Decisions {
    Binding raceFrames = Binding::Disabled;
    Binding menuFrames = Binding::Disabled;
    Binding graphicsMenuRow = Binding::Disabled;
    Binding itemText = Binding::Disabled;
    Binding overlayLayer = Binding::Disabled;
};

Decisions DecideBindings(const BindingInputs& inputs);
Binding BindingFor(const Decisions& decisions, Capability capability);
bool HasPendingDecision(const Decisions& decisions);

// S4: the fallback install guard. The music mod installs an inline hook on an engine site only when
// the pure decision says it owns that capability. UseApi, Wait and Disabled never install anything,
// so a site the general mod holds is never patched twice.
bool ShouldInstallFallbackSite(const Decisions& decisions, Capability capability);

// S4: the beeper fallback. Without a usable general mod the mod blends the panel into the game's own
// 32-bit DIB inside a GDI32!StretchDIBits detour. With a general mod loaded (even when its hooks
// failed) the two never touch GDI together: the panel stays on the API path or is not drawn.
bool ShouldInstallGdiOverlayHook(const Decisions& decisions, bool apiAvailable);
const char* BindingToString(Binding binding);
const char* CapabilityToString(Capability capability);

// R3-003: validation of the version 1 table before any field is called. A table from an older
// ddraw.dll (for example one that ends before getMenuState) is not usable and the mod falls back.
enum class ApiTableProblem {
    None = 0,
    MissingTable,
    Version,
    Size,
    MissingRaceFrameCallback,
    MissingMenuFrameCallback,
    MissingHookState,
    MissingMenuState,
    MissingItemTextDetour
};

struct ApiTableCheck {
    ApiTableProblem problem = ApiTableProblem::None;
    bool usable = false;
};

ApiTableCheck ValidateModApiTable(const RR95ModApi* api);
const char* ApiTableProblemToString(ApiTableProblem problem);

// The Reproductor row must be registered before the general mod installs its hooks and
// builds the relocated graphics menu. That happens at the general mod's DirectDrawCreate, which the
// game calls during the intro about 11 s before the first DirectSoundCreate, so the binding point is
// the music mod's own chained IAT hook on the exe's DDRAW.dll!DirectDrawCreate import. The hook
// resolves the API, validates it and registers the row and the item text provider on its first call;
// every other capability still binds at DirectSoundCreate.
//
// This is the pure "register early or defer" decision. The row is registered early only when the API
// is usable and its menu row slot is still open (UNKNOWN: no install decision yet; PENDING: the
// deferred build is requested). Every other answer defers to the DirectSoundCreate binding, which
// owns the fallbacks: no API (own hooks), a failed general mod (own hooks) and a menu the general mod
// will not relocate (the in-place row). A row that is already registered is never registered twice.
enum class EarlyRowAction {
    Skip = 0,    // music off, unsupported exe, or the row is already registered
    Defer = 1,   // no usable API, or the general mod will not use the API row: DirectSoundCreate decides
    Register = 2 // the API row slot is still open: register now, before the general mod's hooks
};

struct EarlyRowInputs {
    bool musicEnabled = true;
    bool exeSupported = true;
    bool apiAvailable = false;
    int32_t hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    int32_t menuState = -1;  // -1 when no API answered; one of RR95_MENU_STATE_* otherwise
    bool rowRegistered = false;
};

EarlyRowAction DecideEarlyRowRegistration(const EarlyRowInputs& inputs);

// R3-001: when and where a binding decision runs. The first game DirectSoundCreate decides. A
// decision that comes back Wait cannot rely on another DirectSoundCreate, so it is retried at the
// next safe point on the game thread: the first call of one of the player-only detours (the race
// music start 0x004463A0 always runs on a race start) or, once one is registered, a frame callback.
enum class BindingTrigger { DirectSoundCreate = 0, PlayerDetour = 1, FrameCallback = 2 };

struct BindingSchedule {
    bool decided = false;
    bool pending = false;  // the last decision had at least one Wait
};

bool ShouldRunBindingDecision(const BindingSchedule& schedule, BindingTrigger trigger);
BindingSchedule AfterBindingDecision(BindingSchedule schedule, bool pending);

}  // namespace rr95::music
