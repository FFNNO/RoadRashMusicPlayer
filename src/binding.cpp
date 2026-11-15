#include "binding.h"

#include <cstddef>

namespace rr95::music {

namespace {

Binding DecideMenuRow(const BindingInputs& inputs) {
    switch (inputs.menuState) {
    case RR95_MENU_STATE_PENDING:
    case RR95_MENU_STATE_APPLIED:
        return Binding::UseApi;
    case RR95_MENU_STATE_NOT_USED:
    case RR95_MENU_STATE_FAILED:
        return Binding::OwnHooks;
    default:
        return Binding::Wait;
    }
}

Binding DecideItemText(const BindingInputs& inputs) {
    switch (inputs.menuState) {
    case RR95_MENU_STATE_PENDING:
        // The general mod will install its item-text detour when it builds the menu at the first
        // menu frame; nobody else may take that site before then.
        return Binding::UseApi;
    case RR95_MENU_STATE_APPLIED:
        return inputs.itemTextDetourInstalled ? Binding::UseApi : Binding::OwnHooks;
    case RR95_MENU_STATE_NOT_USED:
    case RR95_MENU_STATE_FAILED:
        return Binding::OwnHooks;
    default:
        return Binding::Wait;
    }
}

}  // namespace

Decisions DecideBindings(const BindingInputs& inputs) {
    Decisions decisions;
    if (!inputs.musicEnabled || !inputs.exeSupported) {
        return decisions;  // every capability stays Disabled: forward only
    }
    if (!inputs.apiAvailable) {
        decisions.raceFrames = Binding::OwnHooks;
        decisions.menuFrames = Binding::OwnHooks;
        decisions.graphicsMenuRow = Binding::OwnHooks;
        decisions.itemText = Binding::OwnHooks;
        decisions.overlayLayer = Binding::OwnHooks;
        return decisions;
    }
    switch (inputs.hookState) {
    case RR95_HOOK_STATE_ACTIVE:
        decisions.raceFrames = Binding::UseApi;
        decisions.menuFrames = Binding::UseApi;
        decisions.graphicsMenuRow = DecideMenuRow(inputs);
        decisions.itemText = DecideItemText(inputs);
        decisions.overlayLayer = Binding::UseApi;
        return decisions;
    case RR95_HOOK_STATE_FAILED:
        decisions.raceFrames = Binding::OwnHooks;
        decisions.menuFrames = Binding::OwnHooks;
        decisions.graphicsMenuRow = Binding::OwnHooks;
        decisions.itemText = Binding::OwnHooks;
        decisions.overlayLayer = Binding::OwnHooks;
        return decisions;
    case RR95_HOOK_STATE_NOT_INSTALLED:
    default:
        decisions.raceFrames = Binding::Wait;
        decisions.menuFrames = Binding::Wait;
        decisions.graphicsMenuRow = Binding::Wait;
        decisions.itemText = Binding::Wait;
        decisions.overlayLayer = Binding::Wait;
        return decisions;
    }
}

Binding BindingFor(const Decisions& decisions, Capability capability) {
    switch (capability) {
    case Capability::RaceFrames:
        return decisions.raceFrames;
    case Capability::MenuFrames:
        return decisions.menuFrames;
    case Capability::GraphicsMenuRow:
        return decisions.graphicsMenuRow;
    case Capability::ItemText:
        return decisions.itemText;
    case Capability::OverlayLayer:
        return decisions.overlayLayer;
    case Capability::Count:
    default:
        return Binding::Disabled;
    }
}

bool HasPendingDecision(const Decisions& decisions) {
    for (int i = 0; i < static_cast<int>(Capability::Count); ++i) {
        if (BindingFor(decisions, static_cast<Capability>(i)) == Binding::Wait) {
            return true;
        }
    }
    return false;
}

bool ShouldInstallFallbackSite(const Decisions& decisions, Capability capability) {
    return BindingFor(decisions, capability) == Binding::OwnHooks;
}

bool ShouldInstallGdiOverlayHook(const Decisions& decisions, bool apiAvailable) {
    return !apiAvailable && BindingFor(decisions, Capability::OverlayLayer) == Binding::OwnHooks;
}

const char* BindingToString(Binding binding) {
    switch (binding) {
    case Binding::Disabled:
        return "Disabled";
    case Binding::UseApi:
        return "UseApi";
    case Binding::OwnHooks:
        return "OwnHooks";
    case Binding::Wait:
        return "Wait";
    }
    return "Disabled";
}

const char* CapabilityToString(Capability capability) {
    switch (capability) {
    case Capability::RaceFrames:
        return "race frames";
    case Capability::MenuFrames:
        return "menu frames";
    case Capability::GraphicsMenuRow:
        return "graphics menu row";
    case Capability::ItemText:
        return "item text";
    case Capability::OverlayLayer:
        return "overlay layer";
    case Capability::Count:
        break;
    }
    return "unknown";
}

ApiTableCheck ValidateModApiTable(const RR95ModApi* api) {
    ApiTableCheck check;
    if (api == nullptr) {
        check.problem = ApiTableProblem::MissingTable;
        return check;
    }
    if (api->version != RR95_MOD_API_VERSION) {
        check.problem = ApiTableProblem::Version;
        return check;
    }
    // An older ddraw.dll reports the size it was built with, so a table that ends before the
    // appended fields is refused before any of them is read.
    const uint32_t required =
        static_cast<uint32_t>(offsetof(RR95ModApi, isItemTextDetourInstalled) + sizeof(api->isItemTextDetourInstalled));
    if (api->size < required) {
        check.problem = ApiTableProblem::Size;
        return check;
    }
    if (api->registerRaceFrameCallback == nullptr) {
        check.problem = ApiTableProblem::MissingRaceFrameCallback;
        return check;
    }
    if (api->registerMenuFrameCallback == nullptr) {
        check.problem = ApiTableProblem::MissingMenuFrameCallback;
        return check;
    }
    if (api->getHookState == nullptr) {
        check.problem = ApiTableProblem::MissingHookState;
        return check;
    }
    if (api->getMenuState == nullptr) {
        check.problem = ApiTableProblem::MissingMenuState;
        return check;
    }
    if (api->isItemTextDetourInstalled == nullptr) {
        check.problem = ApiTableProblem::MissingItemTextDetour;
        return check;
    }
    check.problem = ApiTableProblem::None;
    check.usable = true;
    return check;
}

const char* ApiTableProblemToString(ApiTableProblem problem) {
    switch (problem) {
    case ApiTableProblem::None:
        return "ok";
    case ApiTableProblem::MissingTable:
        return "no table";
    case ApiTableProblem::Version:
        return "version";
    case ApiTableProblem::Size:
        return "size";
    case ApiTableProblem::MissingRaceFrameCallback:
        return "race frame callback";
    case ApiTableProblem::MissingMenuFrameCallback:
        return "menu frame callback";
    case ApiTableProblem::MissingHookState:
        return "hook state";
    case ApiTableProblem::MissingMenuState:
        return "menu state";
    case ApiTableProblem::MissingItemTextDetour:
        return "item text detour";
    }
    return "no table";
}

EarlyRowAction DecideEarlyRowRegistration(const EarlyRowInputs& inputs) {
    if (!inputs.musicEnabled || !inputs.exeSupported) {
        return EarlyRowAction::Skip;  // forward only: no hook, no API calls
    }
    if (inputs.rowRegistered) {
        return EarlyRowAction::Skip;  // a registered row is never registered twice
    }
    if (!inputs.apiAvailable) {
        return EarlyRowAction::Defer;  // the DirectSoundCreate binding owns the fallbacks
    }
    if (inputs.hookState == RR95_HOOK_STATE_FAILED) {
        return EarlyRowAction::Defer;  // the general mod will never call the client
    }
    switch (inputs.menuState) {
    case RR95_MENU_STATE_UNKNOWN:
    case RR95_MENU_STATE_PENDING:
        // UNKNOWN: no install decision has run yet, so the row slot is still open.
        // PENDING: the relocated build is requested and runs at the first menu frame.
        return EarlyRowAction::Register;
    case RR95_MENU_STATE_NOT_USED:
    case RR95_MENU_STATE_FAILED:
        // The general mod will not relocate the menu: the in-place fallback row is the live one.
        return EarlyRowAction::Defer;
    case RR95_MENU_STATE_APPLIED:
    default:
        // The menu is already built: addGraphicsMenuRow would answer TOO_LATE.
        return EarlyRowAction::Defer;
    }
}

bool ShouldRunBindingDecision(const BindingSchedule& schedule, BindingTrigger trigger) {
    if (!schedule.decided) {
        // The first game DirectSoundCreate creates the sink, so it owns the first decision.
        return trigger == BindingTrigger::DirectSoundCreate;
    }
    if (!schedule.pending) {
        return false;
    }
    // A Wait cannot rely on another DirectSoundCreate: the game creates the sink only once. Retry
    // at the first player-only detour call or at a registered frame callback instead.
    return trigger == BindingTrigger::PlayerDetour || trigger == BindingTrigger::FrameCallback;
}

BindingSchedule AfterBindingDecision(BindingSchedule schedule, bool pending) {
    schedule.decided = true;
    schedule.pending = pending;
    return schedule;
}

}  // namespace rr95::music
