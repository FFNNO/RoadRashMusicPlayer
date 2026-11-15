#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../include/rr95_mod_api.h"
#include "music_config.h"
#include "music_registry.h"

namespace rr95::music {

// Graphics options menu 19 and its "Reproductor" popup (feature rr95-music-player tasks M6a and
// M6b), moved into the standalone music mod (rr95-music-standalone S3b). Every function here is
// pure: byte buffers, strings and decision structs only, so music_test exercises the same code the
// glue runs. Windows calls (key names, CP-1252 conversion) are the only external dependency and
// live in music_menu.cpp.
//
// Menu id 19 is a column of 0x38-byte items at 0x005a08a8 (count 8 at 0x005a0a70). Item layout:
//   +0x00 HWND of the button window (written by the engine when a menu opens)
//   +0x04 id, +0x08 label string id
//   +0x0c x, +0x10 y, +0x14 width, +0x18 height
//   +0x1c left, +0x1d right, +0x1e up, +0x1f down, +0x20 Tab, +0x21 Shift+Tab
//   +0x24 flags (0x100000 = text comes from FUN_0045b930(item))
//   +0x28 enabled byte, +0x2c select handler, +0x30 help string id, +0x34 0x4852
inline constexpr uint32_t kVaGraphicsMenuItems = 0x005a08a8;
inline constexpr uint32_t kVaGraphicsMenuCount = 0x005a0a70;
inline constexpr uint32_t kGraphicsMenuCount = 8;
inline constexpr size_t kMenuItemSize = 0x38;
inline constexpr int kGraphicsMenuFirstSlot = 4;
inline constexpr int kGraphicsMenuWidescreenSlot = 5;
inline constexpr int kGraphicsMenuLastSlot = 6;
inline constexpr int kGraphicsMenuReproductorSlot = 7;
// The address of one item of graphics menu 19. The in-place install writes the shipped array through
// it and the item-text owner check compares against it, so the two can never disagree about which
// item is the Reproductor row.
constexpr uint32_t MusicGraphicsMenuItemVa(int slot) {
    return kVaGraphicsMenuItems +
           static_cast<uint32_t>(slot) * static_cast<uint32_t>(kMenuItemSize);
}
inline constexpr size_t kGraphicsMenuSlotsSize = kGraphicsMenuCount * kMenuItemSize;

// Offsets used by the pure item builders.
inline constexpr size_t kMenuItemId = 0x04;
inline constexpr size_t kMenuItemLabel = 0x08;
inline constexpr size_t kMenuItemRect = 0x0c;
inline constexpr size_t kMenuItemNavLeft = 0x1c;
inline constexpr size_t kMenuItemNavRight = 0x1d;
inline constexpr size_t kMenuItemNavUp = 0x1e;
inline constexpr size_t kMenuItemNavDown = 0x1f;
inline constexpr size_t kMenuItemNavTab = 0x20;
inline constexpr size_t kMenuItemNavShiftTab = 0x21;
inline constexpr size_t kMenuItemFlags = 0x24;
inline constexpr size_t kMenuItemEnabled = 0x28;
inline constexpr size_t kMenuItemHandler = 0x2c;
inline constexpr size_t kMenuItemHelp = 0x30;
inline constexpr size_t kMenuItemExtra = 0x34;

inline constexpr uint32_t kWidescreenMenuLabelId = 0x3da;
inline constexpr uint32_t kMenuFlagValueText = 0x100000;
inline constexpr uint32_t kGraphicsMenuButtonFlags = 0x0c;

// Re-layout: every visible regular button shares a pitch and the last one ends a visible gap above
// the shipped "Ya esta" rect (0, 422, 180, 58).
inline constexpr int kGraphicsMenuButtonX = 0;
inline constexpr int kGraphicsMenuButtonW = 180;
inline constexpr int kGraphicsMenuButtonH = 48;
inline constexpr int kGraphicsMenuCompactH = 42;
inline constexpr int kGraphicsMenuExitY = 422;
inline constexpr int kGraphicsMenuExitH = 58;
inline constexpr int kGraphicsMenuExitGap = 16;
inline constexpr int kGraphicsMenuMinY = 112;
inline constexpr int kGraphicsMenuRegularForWide = 6;

struct GraphicsMenuRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct GraphicsMenuNav {
    uint8_t left = 0;
    uint8_t right = 0;
    uint8_t up = 0;
    uint8_t down = 0;
    uint8_t tab = 0;
    uint8_t shiftTab = 0;
};

// Visual layout of menu 19. active is false when no mod row is present: the shipped menu is then
// left exactly as it is.
struct GraphicsMenuLayout {
    bool active = false;
    int regularCount = 0;
    int orderCount = 0;
    int order[kGraphicsMenuCount] = {};
    bool visible[kGraphicsMenuCount] = {};
    GraphicsMenuRect rect[kGraphicsMenuCount] = {};
    GraphicsMenuNav nav[kGraphicsMenuCount] = {};
};

GraphicsMenuLayout ComputeGraphicsMenuLayout(bool widescreenItem, bool reproductorItem);

// Which mod rows the caller installs and the select handler address of each.
struct GraphicsMenuModRows {
    bool widescreen = false;
    bool reproductor = false;
    uint32_t widescreenHandler = 0;
    uint32_t reproductorHandler = 0;
};

// Copies the shipped slots into out and, when layout.active, writes the rects, navigation and the
// mod rows. With no mod row the output is the shipped snapshot byte for byte.
void BuildGraphicsMenuSlots(const uint8_t* shipped, const GraphicsMenuLayout& layout,
                            const GraphicsMenuModRows& rows, uint8_t* out);

// True when all eight shipped items match, with every absolute handler relocated by rebaseDelta.
// The HWND dwords at +0x00 are ignored (the engine fills them when a menu opens).
bool GraphicsMenuSlotsAreShipped(const uint8_t* slots, intptr_t rebaseDelta);

// The shipped bytes of all eight items (HWND dwords zero) with the handlers relocated.
void ShippedGraphicsMenuSlots(intptr_t rebaseDelta, uint8_t* out);

// ---------------------------------------------------------------------------
// "Reproductor" native popup
// ---------------------------------------------------------------------------
// Header, Cassette, Beeper, five hotkey rows and Volver at the native 36 px pitch. The column
// starts at y = 140 so the last row ends at y = 458, inside the 640 x 480 screen.
inline constexpr size_t kPopupItemSize = 0x38;
inline constexpr int kReproductorPopupItemCount = 9;
inline constexpr int kReproductorPopupX = 316;
inline constexpr int kReproductorPopupY = 140;
inline constexpr int kReproductorPopupW = 260;
inline constexpr int kReproductorPopupH = 30;
inline constexpr int kReproductorPopupPitch = 36;
inline constexpr int kReproductorPopupBottomLimit = 470;
inline constexpr uint32_t kReproductorPopupHeaderFlags = 0x8030;
inline constexpr uint32_t kReproductorPopupRowFlags = 0x10;
inline constexpr uint32_t kPopupItemExtra = 0x4850;
inline constexpr uint32_t kReproductorPopupLabelId = 0x3da;

enum class ReproductorPopupRow : uint32_t {
    Header = 0,
    Cassette = 1,
    Overlay = 2,
    NextKey = 3,
    PreviousKey = 4,
    PlayPauseKey = 5,
    NextCassetteKey = 6,
    OverlayKey = 7,
    Back = 8
};

inline constexpr int kReproductorKeyRowFirst = static_cast<int>(ReproductorPopupRow::NextKey);

// Writes the nine popup items. selectHandler goes into +0x2c of every selectable row; the header is
// disabled and has no handler. The selectable rows navigate in one ring. out must hold
// kReproductorPopupItemCount * kPopupItemSize bytes.
void BuildReproductorPopupItems(uint32_t selectHandler, uint8_t* out);

// ---------------------------------------------------------------------------
// Popup text
// ---------------------------------------------------------------------------
inline constexpr size_t kReproductorTextBufferSize = 96;
inline constexpr size_t kReproductorRowTextMaxBytes = 48;

std::string TruncateReproductorText(const std::string& text);
// Wide string to CP-1252 bytes, replacing characters outside the code page with '?'.
std::string EncodeCp1252Lossy(const wchar_t* text);
std::string ReproductorHeaderText();
std::string ReproductorCassetteRowText(const std::wstring& label);
std::string ReproductorOverlayRowText(MusicOverlayMode mode);
std::string ReproductorBackText();
// Copies text into the engine's fixed item-text buffer, which holds capacity bytes, and keeps the
// trailing NUL. An over-long text is truncated to capacity - 1 bytes (CP-1252 is one byte per
// character, so no character is split). A null buffer or a zero capacity writes nothing.
void CopyReproductorText(char* buffer, size_t capacity, const std::string& text);

// ---------------------------------------------------------------------------
// Music hotkey rows and key capture
// ---------------------------------------------------------------------------
// One popup row per music action, in the order the race poll uses them. The row shows
// "<label>: <Windows key name>" and switches to "<label>: presione una tecla" while it captures.
const char* MusicKeyActionLabel(MusicKeyAction action);
std::string MusicKeyName(uint32_t vk);
std::string ReproductorKeyRowText(MusicKeyAction action, uint32_t vk);
std::string ReproductorKeyCaptureText(MusicKeyAction action);

// Capture state machine. StartMusicKeyCapture enters WaitingForRelease so the Enter that started
// the capture is never bound. FeedMusicKeyCapture takes one message worth of input: Down/Up with
// the virtual key and whether any other key is still down, Char (never changes the state), or None
// (a mouse message) with the same "any key still down" flag. Enter and Esc cancel at any time; Tab,
// the arrows and modifiers alone are ignored. Captured and Cancelled leave the state draining until
// a key-up or mouse message finds every key released.
struct MusicKeyCaptureState {
    bool active = false;
    bool waitingRelease = false;
    bool draining = false;
    uint8_t action = 0;
};

enum class MusicKeyCaptureKey {
    None = 0,
    Down = 1,
    Up = 2,
    Char = 3
};

struct MusicKeyCaptureInput {
    MusicKeyCaptureKey key = MusicKeyCaptureKey::None;
    uint8_t vk = 0;
    bool otherKeysDown = false;
};

enum class MusicKeyCaptureResult {
    None = 0,
    Waiting = 1,
    Captured = 2,
    Cancelled = 3
};

struct MusicKeyCaptureUpdate {
    MusicKeyCaptureState state;
    MusicKeyCaptureResult result = MusicKeyCaptureResult::None;
    uint8_t capturedVk = 0;
};

bool MusicKeyCaptureSwallowsInput(const MusicKeyCaptureState& state);
// Keyboard (WM_KEYFIRST..WM_SYSDEADCHAR) and mouse (WM_MOUSEFIRST..WM_MOUSEHWHEEL) messages.
bool IsMenuInputMessage(uint32_t msg);
MusicKeyCaptureKey MusicKeyCaptureKeyForMessage(uint32_t msg);

// S3b review R3-glue-capture-untested: the swallow decision of the RashButton detour, in one pure
// function. While the capture is active or draining, keyboard and mouse menu messages are fed to
// the state machine and swallowed, so the menu cannot move focus or close the popup. A Shift key-up
// is fed but always reaches the engine: it only clears the Shift+Tab flag a Shift pressed before the
// capture set, and swallowing it would leave Tab moving backwards. A closed popup ends the capture
// and the message passes. Everything else passes untouched.
struct MusicKeyCaptureMessageDecision {
    bool feed = false;        // feed this message to FeedMusicKeyCapture
    bool swallow = false;     // the menu must not see the message
    bool endCapture = false;  // the popup closed: end the capture before anything else
};

MusicKeyCaptureMessageDecision DecideMusicKeyCaptureMessage(const MusicKeyCaptureState& capture,
                                                            bool popupOpen, uint32_t msg, uint8_t vk);

// True while the Reproductor popup is the open popup: the engine's popup-open byte is set and the
// first popup slot still points at the mod's items.
bool ReproductorPopupStillOpen(uint8_t popupOpenFlag, uintptr_t firstPopupItem, uintptr_t reproductorItems);

MusicKeyCaptureState StartMusicKeyCapture(MusicKeyAction action);
MusicKeyCaptureState EndMusicKeyCapture();
MusicKeyCaptureUpdate FeedMusicKeyCapture(const MusicKeyCaptureState& state,
                                          const MusicKeyCaptureInput& input);

// Binds vk to action. When another action already uses vk the two actions swap keys. Reserved
// keys, modifiers and zero are ignored.
MusicKeys ApplyMusicKeyCapture(const MusicKeys& keys, MusicKeyAction action, uint32_t vk);

// ---------------------------------------------------------------------------
// Popup row text from the mod state (S3b)
// ---------------------------------------------------------------------------
// The glue holds the state, the engine asks for one row at a time, and this pure function turns the
// state into the row string. Row 0 is the header, 1 the cassette, 2 the beeper, 3..7 the five hotkey
// rows and 8 "Volver"; an unknown row returns the header text, so a stale buffer is never drawn.
struct MusicPopupTextInput {
    bool libraryReady = false;
    bool cassetteResolved = false;
    std::wstring savedCassetteLabel;
    std::vector<std::wstring> cassetteLabels;
    int selectedCassette = -1;
    MusicOverlayMode overlayMode = MusicOverlayMode::OnDemand;
    MusicKeyCaptureState capture;
    uint8_t keyVk[kMusicKeyActionCount] = {};
};

std::string ReproductorPopupRowText(int row, const MusicPopupTextInput& input);

// ---------------------------------------------------------------------------
// Graphics menu row install (S3b)
// ---------------------------------------------------------------------------
// The general mod builds graphics menu 19 at the game's first menu frame, so a client that binds
// after that can no longer add a row. This plan is the one decision the glue takes from the menu
// state the API reports: the row and the item text provider are registered only when the general mod
// owns the menu, and every other answer leaves the row out with one log line (S4 adds the fallback).
enum class MusicMenuRowAction {
    Register = 0,  // PENDING or APPLIED: register the row and the item text provider
    TooLate = 1,   // the registration reached the general mod after it built the menu
    NotUsed = 2,   // the general mod never relocates the graphics menu
    Failed = 3,    // the relocation failed and the menu stays shipped
    Unknown = 4    // no install decision has run yet
};

struct MusicMenuRowPlan {
    MusicMenuRowAction action = MusicMenuRowAction::Unknown;
    bool registerRow = false;
    bool registerItemText = false;
    // English line for the mod log; never shown to the player.
    const char* logMessage = "";
};

// menuState is one of RR95_MENU_STATE_* as reported by the API at bind time.
MusicMenuRowPlan PlanMusicMenuRow(int32_t menuState);

// S3b review R3-menu-row-unknown-never-retried: the retry gate the glue runs at every binding
// decision. PENDING and APPLIED attempt the registration; UNKNOWN (the general mod has not decided
// yet) is not a failure and is retried at the next bind retry point without a log line; only the
// final states NOT_USED, FAILED and TOO_LATE stop the retries for this session, log their reason
// once, and leave the in-place row to S4. A registered row is never registered twice.
//
// S4 review R3-menu-row-register-ignores-logged: a registration attempt can fail. The failure is a
// cause, and the gate decides from it whether another attempt can still succeed:
//  - the shipped popup or redraw routines do not match their bytes (RoutinesUnverified);
//  - addGraphicsMenuRow answered TOO_LATE (the menu is already built), REJECTED (another client owns
//    the row), INVALID_ARGUMENT (the descriptor was refused) or an undocumented status;
//  - registerItemTextProvider was rejected (ItemTextRejected).
// Every documented cause is final for the session, so the gate logs it once and never retries. Only
// an undocumented status (ApiUnknownStatus) can still change with a different general mod answer, so
// only that one is retried, and its reason is still logged once.
enum class MusicMenuRowFailure {
    None = 0,               // no attempt has failed
    RoutinesUnverified = 1, // the popup or redraw routines do not match the shipped bytes
    ApiTooLate = 2,         // addGraphicsMenuRow: the graphics menu was already built
    ApiRejected = 3,        // addGraphicsMenuRow: the capability already has a client
    ApiInvalidArgument = 4, // addGraphicsMenuRow: missing handler, text provider or descriptor
    ItemTextRejected = 5,   // registerItemTextProvider: the capability already has a provider
    ApiUnknownStatus = 6    // a status this API version does not document
};

// Maps an RR95_STATUS_* answer of addGraphicsMenuRow to its cause.
MusicMenuRowFailure MusicMenuRowFailureFromApiStatus(int32_t status);

// True only for a cause that can still change: an undocumented status. A documented status, the
// unverified shipped bytes and a rejected item text provider never change in a running game.
bool MusicMenuRowFailureIsTransient(MusicMenuRowFailure failure);

// One English line for the mod log; never shown to the player. status is the API answer of the
// failed call, or 0 when the cause has none.
std::string MusicMenuRowFailureMessage(MusicMenuRowFailure failure, int32_t status);

// S5 review R3-menu-row-final-cause-after-transient-never-logged: the two kinds of line are tracked
// separately. The first transient cause (an undocumented status) is written once, and a later final
// cause is written once too, even after the transient line. One shared flag would drop the final
// cause, which is the answer the user needs.
struct MusicMenuRowLogState {
    bool transientLogged = false;
    bool finalLogged = false;
};

struct MusicMenuRowGate {
    bool attempt = false;      // register the row and its item text provider now
    bool finalSkip = false;    // a final state: never ask again for this session
    bool logTransient = false; // write the first transient cause now
    bool logFinal = false;     // write the final cause (or final action) now
};

MusicMenuRowGate DecideMusicMenuRowGate(MusicMenuRowAction action, bool registered,
                                       const MusicMenuRowLogState& log,
                                       MusicMenuRowFailure failure = MusicMenuRowFailure::None);

// Which row serves the graphics menu once the DirectSoundCreate binding has run. An early
// API row is live only while the general mod still relocates the menu (PENDING or APPLIED). When the
// general mod reports NOT_USED or FAILED, the API row is never used: the in-place fallback row takes
// over, and the early registration must not add a second row (the API row stays registered but dead).
enum class MusicMenuRowOwner {
    None = 0,      // neither row exists
    ApiRow = 1,    // the general mod's relocated menu draws the registered row
    InPlaceRow = 2 // the shipped menu 19 slot draws the fallback row
};

MusicMenuRowOwner DecideMusicMenuRowOwner(int32_t menuState, bool apiRowRegistered,
                                          bool inPlaceInstalled);

// The API row registration (early, at DirectDrawCreate, while the menu state is UNKNOWN or PENDING)
// also registers the item text provider. When the general mod later reports NOT_USED or FAILED, or
// APPLIED without its item-text detour, the provider must be cleared (registered as null) before the
// in-place fallback takes the row and the item text, so no stale provider stays live.
bool ShouldClearMusicItemTextProvider(bool providerRegistered, int32_t menuState,
                                      bool itemTextDetourInstalled);

// The item text provider registered with the API row. It counts as registered until the general mod
// accepts the clear, so a refused clear is retried at the next binding decision, and the refusal is
// logged once however many retries it takes.
struct MusicItemTextProviderState {
    bool registered = false;
    bool refusalLogged = false;
};

enum class MusicItemTextProviderClear {
    Kept = 0,    // nothing to clear: no provider, no API table, or the API row is still the live one
    Cleared = 1, // registerItemTextProvider(nullptr, nullptr) answered RR95_STATUS_OK
    Refused = 2  // the general mod refused the clear: the provider stays registered and is retried
};

struct MusicItemTextProviderClearResult {
    MusicItemTextProviderClear outcome = MusicItemTextProviderClear::Kept;
    int32_t status = RR95_STATUS_OK;  // the answer of the clear call
    bool log = false;                 // write this outcome now: the clear, or the first refusal
};

// Clears a stale provider. menuState and itemTextDetourInstalled are the BindingInputs the binding
// decision was computed from, never a fresh read of the API, so the clear always agrees with the
// fallback install that the same decision drives.
MusicItemTextProviderClearResult ClearStaleMusicItemTextProvider(MusicItemTextProviderState& state,
                                                                 const RR95ModApi* api, int32_t menuState,
                                                                 bool itemTextDetourInstalled);

// ---------------------------------------------------------------------------
// In-place graphics menu row (S4)
// ---------------------------------------------------------------------------
// Without the general mod the mod writes its row into the shipped eight-item array at 0x005a08a8:
// slot 7 is an unused 20-pixel disabled filler, so the row needs no count change and no relocation.
// The array is written only when the shipped bytes verify and restored on unload.
enum class MusicMenuInPlaceAction {
    Install = 0,           // the row and the item text are owned and the shipped bytes verify
    AlreadyInstalled = 1,  // nothing to do: the row is in place
    NotShipped = 2,        // the array does not match the shipped bytes: never write
    NotOwned = 3           // the API owns the row or the item text
};

struct MusicMenuInPlacePlan {
    MusicMenuInPlaceAction action = MusicMenuInPlaceAction::NotOwned;
    bool write = false;
    // English line for the mod log; never shown to the player.
    const char* logMessage = "";
};

// ownRow and ownItemText come from ShouldInstallFallbackSite for the two capabilities.
MusicMenuInPlacePlan PlanMusicMenuInPlace(bool ownRow, bool ownItemText, bool shippedMatches,
                                         bool installed);

// The engine asks FUN_0045b930 for the text of a value item. With the mod's own item-text detour the
// call is answered from the mod when the item is the in-place row or one of the popup rows.
enum class MusicItemTextOwner {
    Original = 0,  // the game's own routine
    MenuRow = 1,   // the Reproductor row text
    PopupRow = 2   // one of the Reproductor popup items
};

struct MusicItemTextTarget {
    MusicItemTextOwner owner = MusicItemTextOwner::Original;
    int popupRow = -1;
};

// menuInstalled is true once the in-place row is written; popupItems is the mod's own item array.
MusicItemTextTarget DecideMusicItemTextOwner(uintptr_t item, bool menuInstalled, uintptr_t menuRowItem,
                                             uintptr_t popupItems, size_t popupBytes);

// ---------------------------------------------------------------------------
// Cassette row and Beeper row decisions
// ---------------------------------------------------------------------------
// The popup opens immediately, without waiting for the worker scan. While the library is not ready
// the Cassette row says "Cassette: cargando..." and a press does nothing, so a placeholder label is
// never written to ModMusicCassette.
struct MusicCassetteMenuInput {
    bool libraryReady = false;
    bool selectionResolved = false;
    std::wstring savedLabel;
    std::vector<std::wstring> labels;
    int currentIndex = -1;
};

struct MusicCassetteMenuDecision {
    bool loading = true;
    int index = -1;
    bool canSelect = false;
    int nextIndex = -1;
};

MusicCassetteMenuDecision DecideMusicCassetteMenu(const MusicCassetteMenuInput& input);
std::string ReproductorCassetteLoadingText();

// The Beeper row always shows the configured mode. next is the mode written on a press.
struct MusicBeeperMenuDecision {
    MusicOverlayMode mode = MusicOverlayMode::OnDemand;
    MusicOverlayMode next = MusicOverlayMode::OnDemand;
};

MusicBeeperMenuDecision DecideMusicBeeperMenu(MusicOverlayMode configured, bool press);

}  // namespace rr95::music
