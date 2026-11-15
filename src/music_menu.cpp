#include "music_menu.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace rr95::music {

namespace {

constexpr uint32_t kItemExtra = 0x4852;  // +0x34 on every visible menu 19 item
constexpr uint32_t kVaPantallaSelectHandler = 0x0045b4b0;

// All eight menu 19 items as shipped in both exe builds (0x005a08a8..0x005a0a6f), HWND dwords zero,
// handlers at the preferred base.
constexpr uint8_t kShippedSlots[kGraphicsMenuSlotsSize] = {
    // slot 0: label 0x48, rect (0, 134, 180, 48), nav 00 00 06 01 01 06, flags 0xc, handler 0x45b3c0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x86, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01,
    0x01, 0x06, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xc0, 0xb3, 0x45, 0x00,
    0x49, 0x00, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 1: label 0x18c, rect (0, 182, 180, 48), nav 01 01 00 02 02 00, flags 0xc, handler 0x45b400
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb6, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x02,
    0x02, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xb4, 0x45, 0x00,
    0x8d, 0x01, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 2: id 20, label 0x52, rect (0, 230, 180, 48), nav 02 02 01 03 03 01, no handler
    0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xe6, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01, 0x03,
    0x03, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x53, 0x00, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 3: label 0x1af, rect (0, 278, 180, 48), nav 03 03 02 04 04 02, flags 0xc, handler 0x45b480
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaf, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x16, 0x01, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x04,
    0x04, 0x02, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x80, 0xb4, 0x45, 0x00,
    0xb0, 0x01, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 4 "Pantalla": label 0x3de, rect (0, 326, 180, 48), nav 04 04 03 06 06 03, flags 0xc,
    // enabled, handler 0x45b4b0, help 0x3df
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x46, 0x01, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x04, 0x04, 0x03, 0x06,
    0x06, 0x03, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xb0, 0xb4, 0x45, 0x00,
    0xdf, 0x03, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 5: empty, rect (0, 374, 180, 48), nav 05 05 04 06 06 04
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x76, 0x01, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x05, 0x05, 0x04, 0x06,
    0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // slot 6 "Ya esta": id 16, label 0x56, rect (0, 422, 180, 58), nav 06 06 05 00 00 05, flags
    // 0x800c, enabled, no handler, help 0x57
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xa6, 0x01, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x00, 0x00, 0x06, 0x06, 0x05, 0x00,
    0x00, 0x05, 0x00, 0x00, 0x0c, 0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x57, 0x00, 0x00, 0x00, 0x52, 0x48, 0x00, 0x00,
    // slot 7: unused disabled filler, rect (0, 114, 180, 20), no label, no handler
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x72, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Handler addresses at the preferred base, one per slot. Rebasing touches only these dwords.
constexpr uint32_t kSlotHandlers[kGraphicsMenuCount] = {
    0x0045b3c0, 0x0045b400, 0, 0x0045b480, kVaPantallaSelectHandler, 0, 0, 0,
};

void PutU32(uint8_t* p, uint32_t v) {
    std::memcpy(p, &v, sizeof(v));
}

uint32_t GetU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void PutI32(uint8_t* p, int v) {
    std::memcpy(p, &v, sizeof(v));
}

GraphicsMenuRect ReadShippedRect(int slot) {
    GraphicsMenuRect r;
    const uint8_t* item = kShippedSlots + static_cast<size_t>(slot) * kMenuItemSize;
    std::memcpy(&r.x, item + kMenuItemRect, 4);
    std::memcpy(&r.y, item + kMenuItemRect + 4, 4);
    std::memcpy(&r.width, item + kMenuItemRect + 8, 4);
    std::memcpy(&r.height, item + kMenuItemRect + 12, 4);
    return r;
}

GraphicsMenuNav ReadShippedNav(int slot) {
    GraphicsMenuNav n;
    const uint8_t* item = kShippedSlots + static_cast<size_t>(slot) * kMenuItemSize;
    n.left = item[kMenuItemNavLeft];
    n.right = item[kMenuItemNavRight];
    n.up = item[kMenuItemNavUp];
    n.down = item[kMenuItemNavDown];
    n.tab = item[kMenuItemNavTab];
    n.shiftTab = item[kMenuItemNavShiftTab];
    return n;
}

bool IsExtendedMusicKey(uint32_t vk) {
    switch (vk) {
        case 0x21:  // VK_PRIOR
        case 0x22:  // VK_NEXT
        case 0x23:  // VK_END
        case 0x24:  // VK_HOME
        case 0x25:  // VK_LEFT
        case 0x26:  // VK_UP
        case 0x27:  // VK_RIGHT
        case 0x28:  // VK_DOWN
        case 0x2D:  // VK_INSERT
        case 0x2E:  // VK_DELETE
        case 0x6F:  // VK_DIVIDE
        case 0x90:  // VK_NUMLOCK
        case 0xA3:  // VK_RCONTROL
        case 0xA5:  // VK_RMENU
            return true;
        default:
            return false;
    }
}

}  // namespace

void ShippedGraphicsMenuSlots(intptr_t rebaseDelta, uint8_t* out) {
    std::memcpy(out, kShippedSlots, kGraphicsMenuSlotsSize);
    for (uint32_t slot = 0; slot < kGraphicsMenuCount; ++slot) {
        const uint32_t handler = kSlotHandlers[slot];
        if (handler == 0) {
            continue;
        }
        PutU32(out + slot * kMenuItemSize + kMenuItemHandler,
               static_cast<uint32_t>(static_cast<intptr_t>(handler) + rebaseDelta));
    }
}

bool GraphicsMenuSlotsAreShipped(const uint8_t* slots, intptr_t rebaseDelta) {
    if (slots == nullptr) {
        return false;
    }
    uint8_t expected[kGraphicsMenuSlotsSize];
    ShippedGraphicsMenuSlots(rebaseDelta, expected);
    for (size_t i = 0; i < kGraphicsMenuSlotsSize; ++i) {
        if ((i % kMenuItemSize) < 4) {
            continue;  // HWND dword
        }
        if (slots[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

GraphicsMenuLayout ComputeGraphicsMenuLayout(bool widescreenItem, bool reproductorItem) {
    GraphicsMenuLayout layout;
    for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
        layout.rect[slot] = ReadShippedRect(slot);
        layout.nav[slot] = ReadShippedNav(slot);
    }
    layout.active = widescreenItem || reproductorItem;
    layout.regularCount = 5 + (widescreenItem ? 1 : 0) + (reproductorItem ? 1 : 0);
    layout.orderCount = layout.regularCount + 1;
    int index = 0;
    for (int slot = 0; slot <= kGraphicsMenuFirstSlot; ++slot) {
        layout.order[index++] = slot;
        layout.visible[slot] = true;
    }
    if (widescreenItem) {
        layout.order[index++] = kGraphicsMenuWidescreenSlot;
        layout.visible[kGraphicsMenuWidescreenSlot] = true;
    }
    if (reproductorItem) {
        layout.order[index++] = kGraphicsMenuReproductorSlot;
        layout.visible[kGraphicsMenuReproductorSlot] = true;
    }
    layout.order[index] = kGraphicsMenuLastSlot;
    layout.visible[kGraphicsMenuLastSlot] = true;
    if (!layout.active) {
        return layout;
    }

    const int height =
        layout.regularCount <= kGraphicsMenuRegularForWide ? kGraphicsMenuButtonH : kGraphicsMenuCompactH;
    const int start = kGraphicsMenuExitY - kGraphicsMenuExitGap - height * layout.regularCount;
    int y = start < kGraphicsMenuMinY ? kGraphicsMenuMinY : start;
    for (int i = 0; i < layout.regularCount; ++i) {
        GraphicsMenuRect& r = layout.rect[layout.order[i]];
        r.x = kGraphicsMenuButtonX;
        r.y = y;
        r.width = kGraphicsMenuButtonW;
        r.height = height;
        y += height;
    }
    layout.rect[kGraphicsMenuLastSlot] =
        GraphicsMenuRect{kGraphicsMenuButtonX, kGraphicsMenuExitY, kGraphicsMenuButtonW, kGraphicsMenuExitH};

    for (int i = 0; i < layout.orderCount; ++i) {
        const int slot = layout.order[i];
        GraphicsMenuNav& n = layout.nav[slot];
        n.left = static_cast<uint8_t>(slot);
        n.right = static_cast<uint8_t>(slot);
        n.up = static_cast<uint8_t>(layout.order[(i + layout.orderCount - 1) % layout.orderCount]);
        n.shiftTab = n.up;
        n.down = static_cast<uint8_t>(layout.order[(i + 1) % layout.orderCount]);
        n.tab = n.down;
    }
    return layout;
}

void BuildGraphicsMenuSlots(const uint8_t* shipped, const GraphicsMenuLayout& layout,
                            const GraphicsMenuModRows& rows, uint8_t* out) {
    std::memmove(out, shipped, kGraphicsMenuSlotsSize);
    if (!layout.active) {
        return;
    }
    for (int slot = 0; slot < static_cast<int>(kGraphicsMenuCount); ++slot) {
        if (!layout.visible[slot]) {
            continue;
        }
        uint8_t* item = out + static_cast<size_t>(slot) * kMenuItemSize;
        PutI32(item + kMenuItemRect, layout.rect[slot].x);
        PutI32(item + kMenuItemRect + 4, layout.rect[slot].y);
        PutI32(item + kMenuItemRect + 8, layout.rect[slot].width);
        PutI32(item + kMenuItemRect + 12, layout.rect[slot].height);
        item[kMenuItemNavLeft] = layout.nav[slot].left;
        item[kMenuItemNavRight] = layout.nav[slot].right;
        item[kMenuItemNavUp] = layout.nav[slot].up;
        item[kMenuItemNavDown] = layout.nav[slot].down;
        item[kMenuItemNavTab] = layout.nav[slot].tab;
        item[kMenuItemNavShiftTab] = layout.nav[slot].shiftTab;
    }
    const int modSlots[2] = {kGraphicsMenuWidescreenSlot, kGraphicsMenuReproductorSlot};
    const bool modInstalled[2] = {rows.widescreen, rows.reproductor};
    const uint32_t modHandlers[2] = {rows.widescreenHandler, rows.reproductorHandler};
    for (int m = 0; m < 2; ++m) {
        if (!modInstalled[m]) {
            continue;
        }
        uint8_t* item = out + static_cast<size_t>(modSlots[m]) * kMenuItemSize;
        PutU32(item + kMenuItemLabel, kWidescreenMenuLabelId);
        PutU32(item + kMenuItemFlags, kGraphicsMenuButtonFlags | kMenuFlagValueText);
        PutU32(item + kMenuItemEnabled, 1);
        PutU32(item + kMenuItemHandler, modHandlers[m]);
        PutU32(item + kMenuItemHelp, 0);
        PutU32(item + kMenuItemExtra, kItemExtra);
    }
}

void BuildReproductorPopupItems(uint32_t selectHandler, uint8_t* out) {
    std::memset(out, 0, static_cast<size_t>(kReproductorPopupItemCount) * kPopupItemSize);
    auto item = [out](int i) { return out + static_cast<size_t>(i) * kPopupItemSize; };

    PutI32(item(0) + kMenuItemRect, kReproductorPopupX);
    PutI32(item(0) + kMenuItemRect + 4, kReproductorPopupY);
    PutI32(item(0) + kMenuItemRect + 8, kReproductorPopupW);
    PutI32(item(0) + kMenuItemRect + 12, kReproductorPopupH);
    PutU32(item(0) + kMenuItemFlags, kReproductorPopupHeaderFlags | kMenuFlagValueText);
    PutU32(item(0) + kMenuItemExtra, kPopupItemExtra);

    for (int i = 1; i < kReproductorPopupItemCount; ++i) {
        uint8_t* it = item(i);
        PutU32(it + kMenuItemId, static_cast<uint32_t>(i));
        PutU32(it + kMenuItemLabel, kReproductorPopupLabelId);
        PutI32(it + kMenuItemRect, kReproductorPopupX);
        PutI32(it + kMenuItemRect + 4, kReproductorPopupY + kReproductorPopupPitch * i);
        PutI32(it + kMenuItemRect + 8, kReproductorPopupW);
        PutI32(it + kMenuItemRect + 12, kReproductorPopupH);
        it[kMenuItemNavLeft] = static_cast<uint8_t>(i);
        it[kMenuItemNavRight] = static_cast<uint8_t>(i);
        it[kMenuItemNavUp] = static_cast<uint8_t>(i == 1 ? kReproductorPopupItemCount - 1 : i - 1);
        it[kMenuItemNavShiftTab] = it[kMenuItemNavUp];
        it[kMenuItemNavDown] = static_cast<uint8_t>(i == kReproductorPopupItemCount - 1 ? 1 : i + 1);
        it[kMenuItemNavTab] = it[kMenuItemNavDown];
        PutU32(it + kMenuItemFlags, kReproductorPopupRowFlags | kMenuFlagValueText);
        PutU32(it + kMenuItemEnabled, 1);
        PutU32(it + kMenuItemHandler, selectHandler);
        PutU32(it + kMenuItemExtra, kPopupItemExtra);
    }
}

std::string TruncateReproductorText(const std::string& text) {
    if (text.size() <= kReproductorRowTextMaxBytes) {
        return text;
    }
    return text.substr(0, kReproductorRowTextMaxBytes);
}

std::string EncodeCp1252Lossy(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int n = WideCharToMultiByte(1252, 0, text, -1, nullptr, 0, "?", nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(1252, 0, text, -1, out.data(), n, "?", nullptr) != n) {
        return {};
    }
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::string ReproductorHeaderText() {
    return "Reproductor";
}

std::string ReproductorCassetteRowText(const std::wstring& label) {
    return TruncateReproductorText("Cassette: " + EncodeCp1252Lossy(label.c_str()));
}

std::string ReproductorOverlayRowText(MusicOverlayMode mode) {
    return mode == MusicOverlayMode::Pinned ? "Beeper: Fijo" : "Beeper: A demanda";
}

std::string ReproductorBackText() {
    return "Volver";
}

const char* MusicKeyActionLabel(MusicKeyAction action) {
    switch (action) {
        case MusicKeyAction::NextTrack:
            return "Siguiente";
        case MusicKeyAction::PreviousTrack:
            return "Anterior";
        case MusicKeyAction::PlayPause:
            return "Pausa";
        case MusicKeyAction::NextCassette:
            return "Cambiar cassette";
        case MusicKeyAction::Overlay:
            return "Mostrar beeper";
        default:
            return "";
    }
}

std::string MusicKeyName(uint32_t vk) {
    char fallback[8] = {};
    std::snprintf(fallback, sizeof(fallback), "0x%02X", static_cast<unsigned>(vk & 0xFFu));
    if (vk == 0 || vk > 0xFF) {
        return fallback;
    }
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan == 0) {
        return fallback;
    }
    LONG lparam = static_cast<LONG>((scan & 0xFFu) << 16);
    if (IsExtendedMusicKey(vk)) {
        lparam |= 1L << 24;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lparam, name, static_cast<int>(std::size(name))) <= 0) {
        return fallback;
    }
    const std::string converted = EncodeCp1252Lossy(name);
    return converted.empty() ? std::string(fallback) : converted;
}

std::string ReproductorKeyRowText(MusicKeyAction action, uint32_t vk) {
    return TruncateReproductorText(std::string(MusicKeyActionLabel(action)) + ": " + MusicKeyName(vk));
}

std::string ReproductorKeyCaptureText(MusicKeyAction action) {
    return TruncateReproductorText(std::string(MusicKeyActionLabel(action)) + ": presione una tecla");
}

MusicKeyCaptureState StartMusicKeyCapture(MusicKeyAction action) {
    const int index = static_cast<int>(action);
    MusicKeyCaptureState state;
    if (index < 0 || index >= kMusicKeyActionCount) {
        return state;
    }
    state.active = true;
    state.waitingRelease = true;
    state.action = static_cast<uint8_t>(index);
    return state;
}

MusicKeyCaptureState EndMusicKeyCapture() {
    return MusicKeyCaptureState{};
}

bool MusicKeyCaptureSwallowsInput(const MusicKeyCaptureState& state) {
    return state.active || state.draining;
}

bool IsMenuInputMessage(uint32_t msg) {
    return (msg >= WM_KEYFIRST && msg <= WM_SYSDEADCHAR) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSEHWHEEL);
}

MusicKeyCaptureKey MusicKeyCaptureKeyForMessage(uint32_t msg) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            return MusicKeyCaptureKey::Down;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            return MusicKeyCaptureKey::Up;
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
            return MusicKeyCaptureKey::Char;
        default:
            return MusicKeyCaptureKey::None;
    }
}

MusicKeyCaptureMessageDecision DecideMusicKeyCaptureMessage(const MusicKeyCaptureState& capture,
                                                            bool popupOpen, uint32_t msg, uint8_t vk) {
    MusicKeyCaptureMessageDecision decision;
    if (!MusicKeyCaptureSwallowsInput(capture) || !IsMenuInputMessage(msg)) {
        return decision;
    }
    if (!popupOpen) {
        // The popup the capture belongs to is gone; end it and let the menu see the message.
        decision.endCapture = true;
        return decision;
    }
    decision.feed = true;
    // A Shift key-up still reaches the engine: it only clears the Shift+Tab flag (DAT_005c3fc5),
    // which a Shift pressed before the capture set. Swallowing it would leave Tab moving backwards.
    decision.swallow =
        !(MusicKeyCaptureKeyForMessage(msg) == MusicKeyCaptureKey::Up && vk == kMusicKeyShift);
    return decision;
}

bool ReproductorPopupStillOpen(uint8_t popupOpenFlag, uintptr_t firstPopupItem,
                               uintptr_t reproductorItems) {
    return popupOpenFlag != 0 && reproductorItems != 0 && firstPopupItem == reproductorItems;
}

MusicKeyCaptureUpdate FeedMusicKeyCapture(const MusicKeyCaptureState& state,
                                          const MusicKeyCaptureInput& input) {
    MusicKeyCaptureUpdate update;
    update.state = state;
    if (!state.active) {
        // Draining: swallow the rest of the key press that ended the capture. Only a key-up or a
        // mouse message that finds every key released ends it.
        if (state.draining && !input.otherKeysDown &&
            (input.key == MusicKeyCaptureKey::Up || input.key == MusicKeyCaptureKey::None)) {
            update.state.draining = false;
        }
        return update;
    }
    update.result = MusicKeyCaptureResult::Waiting;
    if (input.key == MusicKeyCaptureKey::Char) {
        return update;
    }
    if (input.key == MusicKeyCaptureKey::None || input.key == MusicKeyCaptureKey::Up) {
        if (!input.otherKeysDown) {
            update.state.waitingRelease = false;
        }
        return update;
    }
    // Down. Enter and Esc cancel at any time, so a held key never traps the player in a capture.
    if (input.vk == kMusicKeyReturn || input.vk == kMusicKeyEscape) {
        update.state = EndMusicKeyCapture();
        update.state.draining = true;
        update.result = MusicKeyCaptureResult::Cancelled;
        return update;
    }
    if (input.vk == 0) {
        return update;
    }
    if (state.waitingRelease) {
        if (input.otherKeysDown) {
            return update;
        }
        update.state.waitingRelease = false;
    }
    if (IsReservedMusicKey(input.vk) || IsMusicKeyModifier(input.vk)) {
        return update;
    }
    update.state = EndMusicKeyCapture();
    update.state.draining = true;
    update.result = MusicKeyCaptureResult::Captured;
    update.capturedVk = input.vk;
    return update;
}

MusicKeys ApplyMusicKeyCapture(const MusicKeys& keys, MusicKeyAction action, uint32_t vk) {
    MusicKeys out = keys;
    const int target = static_cast<int>(action);
    if (target < 0 || target >= kMusicKeyActionCount || vk == 0 || vk > 0xFF || IsReservedMusicKey(vk) ||
        IsMusicKeyModifier(vk)) {
        return out;
    }
    const uint8_t key = static_cast<uint8_t>(vk);
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        if (i != target && out.vk[i] == key) {
            out.vk[i] = out.vk[target];
            break;
        }
    }
    out.vk[target] = key;
    return out;
}

std::string ReproductorPopupRowText(int row, const MusicPopupTextInput& input) {
    switch (static_cast<ReproductorPopupRow>(row)) {
        case ReproductorPopupRow::Cassette: {
            MusicCassetteMenuInput cassette;
            cassette.libraryReady = input.libraryReady;
            cassette.selectionResolved = input.cassetteResolved;
            cassette.savedLabel = input.savedCassetteLabel;
            cassette.labels = input.cassetteLabels;
            cassette.currentIndex = input.selectedCassette;
            const MusicCassetteMenuDecision decision = DecideMusicCassetteMenu(cassette);
            if (decision.loading) {
                return ReproductorCassetteLoadingText();
            }
            return ReproductorCassetteRowText(input.cassetteLabels[static_cast<size_t>(decision.index)]);
        }
        case ReproductorPopupRow::Overlay:
            return ReproductorOverlayRowText(DecideMusicBeeperMenu(input.overlayMode, false).mode);
        case ReproductorPopupRow::NextKey:
        case ReproductorPopupRow::PreviousKey:
        case ReproductorPopupRow::PlayPauseKey:
        case ReproductorPopupRow::NextCassetteKey:
        case ReproductorPopupRow::OverlayKey: {
            const MusicKeyAction action = static_cast<MusicKeyAction>(row - kReproductorKeyRowFirst);
            if (input.capture.active && input.capture.action == static_cast<uint8_t>(action)) {
                return ReproductorKeyCaptureText(action);
            }
            return ReproductorKeyRowText(action, input.keyVk[static_cast<int>(action)]);
        }
        case ReproductorPopupRow::Back:
            return ReproductorBackText();
        case ReproductorPopupRow::Header:
        default:
            return ReproductorHeaderText();
    }
}

void CopyReproductorText(char* buffer, size_t capacity, const std::string& text) {
    if (buffer == nullptr || capacity == 0) {
        return;
    }
    const size_t length = text.size() < capacity - 1 ? text.size() : capacity - 1;
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
}

MusicMenuRowPlan PlanMusicMenuRow(int32_t menuState) {
    MusicMenuRowPlan plan;
    switch (menuState) {
        case RR95_MENU_STATE_PENDING:
        case RR95_MENU_STATE_APPLIED:
            plan.action = MusicMenuRowAction::Register;
            plan.registerRow = true;
            plan.registerItemText = true;
            plan.logMessage =
                "Music: the Reproductor row and the item text are registered with the general mod";
            break;
        case RR95_MENU_STATE_NOT_USED:
            plan.action = MusicMenuRowAction::NotUsed;
            plan.logMessage = "Music: the general mod does not relocate the graphics menu; the Reproductor row "
                              "is not added (S4 adds it in place)";
            break;
        case RR95_MENU_STATE_FAILED:
            plan.action = MusicMenuRowAction::Failed;
            plan.logMessage = "Music: the general mod could not relocate the graphics menu; the Reproductor "
                              "row is not added (S4 adds it in place)";
            break;
        default:
            plan.action = MusicMenuRowAction::Unknown;
            plan.logMessage = "Music: the general mod has not decided on the graphics menu yet; the "
                              "Reproductor row is not added";
            break;
    }
    return plan;
}

MusicMenuRowFailure MusicMenuRowFailureFromApiStatus(int32_t status) {
    switch (status) {
        case RR95_STATUS_OK:
            return MusicMenuRowFailure::None;
        case RR95_STATUS_TOO_LATE:
            return MusicMenuRowFailure::ApiTooLate;
        case RR95_STATUS_REJECTED:
            return MusicMenuRowFailure::ApiRejected;
        case RR95_STATUS_INVALID_ARGUMENT:
            return MusicMenuRowFailure::ApiInvalidArgument;
        default:
            return MusicMenuRowFailure::ApiUnknownStatus;
    }
}

bool MusicMenuRowFailureIsTransient(MusicMenuRowFailure failure) {
    return failure == MusicMenuRowFailure::ApiUnknownStatus;
}

std::string MusicMenuRowFailureMessage(MusicMenuRowFailure failure, int32_t status) {
    switch (failure) {
        case MusicMenuRowFailure::RoutinesUnverified:
            return "Music: the shipped bytes of FUN_00439160, FUN_0043a4e0 or FUN_0043a390 do not match; "
                   "the Reproductor row is not added";
        case MusicMenuRowFailure::ApiTooLate:
            return "Music: the Reproductor row reached the general mod after the graphics menu was built; "
                   "the row is not added";
        case MusicMenuRowFailure::ApiRejected:
            return "Music: the general mod rejected the Reproductor row because the slot already has a "
                   "client (status " + std::to_string(status) + "); the row is not added";
        case MusicMenuRowFailure::ApiInvalidArgument:
            return "Music: the general mod refused the Reproductor row descriptor (status " +
                   std::to_string(status) + "); the row is not added";
        case MusicMenuRowFailure::ItemTextRejected:
            return "Music: the general mod rejected the music item text provider (status " +
                   std::to_string(status) + "); the Reproductor row is removed";
        case MusicMenuRowFailure::ApiUnknownStatus:
            return "Music: the general mod answered the Reproductor row with an undocumented status " +
                   std::to_string(status) + "; the registration is retried";
        case MusicMenuRowFailure::None:
        default:
            return {};
    }
}

MusicMenuRowGate DecideMusicMenuRowGate(MusicMenuRowAction action, bool registered,
                                       const MusicMenuRowLogState& log,
                                       MusicMenuRowFailure failure) {
    MusicMenuRowGate gate;
    if (registered) {
        return gate;  // nothing to register, and a registered row is never a failure
    }
    if (failure != MusicMenuRowFailure::None) {
        // A failed attempt: the gate only asks again for a cause that can still change. Each kind of
        // line has its own flag, so a final cause after a logged transient cause is still written.
        const bool transient = MusicMenuRowFailureIsTransient(failure);
        gate.attempt = transient;
        gate.finalSkip = !transient;
        gate.logTransient = transient && !log.transientLogged;
        gate.logFinal = !transient && !log.finalLogged;
        return gate;
    }
    switch (action) {
        case MusicMenuRowAction::Register:
            gate.attempt = true;
            return gate;
        case MusicMenuRowAction::NotUsed:
        case MusicMenuRowAction::Failed:
        case MusicMenuRowAction::TooLate:
            // A final answer: the row is not coming through this path. In S4 the in-place row takes
            // over when the binding says the mod owns the menu; either way there is nothing to retry.
            gate.finalSkip = true;
            gate.logFinal = !log.finalLogged;
            return gate;
        case MusicMenuRowAction::Unknown:
        default:
            // The general mod has not decided yet. This is not a failure: the glue asks again at its
            // next bind retry point and nothing is logged.
            return gate;
    }
}

MusicMenuRowOwner DecideMusicMenuRowOwner(int32_t menuState, bool apiRowRegistered,
                                          bool inPlaceInstalled) {
    switch (menuState) {
        case RR95_MENU_STATE_NOT_USED:
        case RR95_MENU_STATE_FAILED:
            // The general mod never draws its relocated menu, so a registered API row is dead even
            // though the slot still holds it. Only the shipped array can show a row.
            return inPlaceInstalled ? MusicMenuRowOwner::InPlaceRow : MusicMenuRowOwner::None;
        case RR95_MENU_STATE_PENDING:
        case RR95_MENU_STATE_APPLIED:
            return apiRowRegistered ? MusicMenuRowOwner::ApiRow : MusicMenuRowOwner::None;
        case RR95_MENU_STATE_UNKNOWN:
        default:
            return MusicMenuRowOwner::None;  // no decision has run yet
    }
}

bool ShouldClearMusicItemTextProvider(bool providerRegistered, int32_t menuState,
                                      bool itemTextDetourInstalled) {
    if (!providerRegistered) {
        return false;
    }
    switch (menuState) {
        case RR95_MENU_STATE_NOT_USED:
        case RR95_MENU_STATE_FAILED:
            return true;  // the relocated menu is never drawn: the in-place row takes over
        case RR95_MENU_STATE_APPLIED:
            return !itemTextDetourInstalled;  // the general mod never asks the provider
        case RR95_MENU_STATE_PENDING:
        case RR95_MENU_STATE_UNKNOWN:
        default:
            return false;  // the API row is still the live one
    }
}

MusicMenuInPlacePlan PlanMusicMenuInPlace(bool ownRow, bool ownItemText, bool shippedMatches,
                                         bool installed) {
    MusicMenuInPlacePlan plan;
    if (!ownRow || !ownItemText) {
        plan.action = MusicMenuInPlaceAction::NotOwned;
        plan.logMessage = "Music: the general mod owns the graphics menu row or the item text; the "
                          "Reproductor row is not written in place";
        return plan;
    }
    if (installed) {
        plan.action = MusicMenuInPlaceAction::AlreadyInstalled;
        plan.logMessage = "Music: the Reproductor row is already in the shipped graphics menu";
        return plan;
    }
    if (!shippedMatches) {
        // A patched or already rewritten array is never touched: the row is left out instead.
        plan.action = MusicMenuInPlaceAction::NotShipped;
        plan.logMessage = "Music: graphics menu 19 does not match the shipped bytes; the Reproductor row is "
                          "not written in place";
        return plan;
    }
    plan.action = MusicMenuInPlaceAction::Install;
    plan.write = true;
    plan.logMessage = "Music: the Reproductor row is written into the shipped graphics menu (slot 7)";
    return plan;
}

MusicItemTextTarget DecideMusicItemTextOwner(uintptr_t item, bool menuInstalled, uintptr_t menuRowItem,
                                             uintptr_t popupItems, size_t popupBytes) {
    MusicItemTextTarget target;
    if (item == 0) {
        return target;
    }
    if (menuInstalled && item == menuRowItem) {
        target.owner = MusicItemTextOwner::MenuRow;
        return target;
    }
    if (popupBytes == 0 || item < popupItems || item - popupItems >= popupBytes) {
        return target;
    }
    const size_t offset = static_cast<size_t>(item - popupItems);
    if (offset % kPopupItemSize != 0) {
        return target;
    }
    target.owner = MusicItemTextOwner::PopupRow;
    target.popupRow = static_cast<int>(offset / kPopupItemSize);
    return target;
}

MusicCassetteMenuDecision DecideMusicCassetteMenu(const MusicCassetteMenuInput& input) {
    MusicCassetteMenuDecision decision;
    if (!input.libraryReady || input.labels.empty()) {
        return decision;
    }
    int index = -1;
    if (input.selectionResolved && input.currentIndex >= 0 &&
        input.currentIndex < static_cast<int>(input.labels.size())) {
        index = input.currentIndex;
    } else {
        index = ResolveSavedMusicCassette(input.labels, input.savedLabel).index;
    }
    if (index < 0 || index >= static_cast<int>(input.labels.size())) {
        index = 0;
    }
    decision.loading = false;
    decision.index = index;
    decision.canSelect = true;
    decision.nextIndex = (index + 1) % static_cast<int>(input.labels.size());
    return decision;
}

std::string ReproductorCassetteLoadingText() {
    return "Cassette: cargando...";
}

MusicBeeperMenuDecision DecideMusicBeeperMenu(MusicOverlayMode configured, bool press) {
    MusicBeeperMenuDecision decision;
    decision.mode = configured;
    decision.next = configured;
    if (press) {
        decision.next = configured == MusicOverlayMode::Pinned ? MusicOverlayMode::OnDemand
                                                               : MusicOverlayMode::Pinned;
    }
    return decision;
}

}  // namespace rr95::music
