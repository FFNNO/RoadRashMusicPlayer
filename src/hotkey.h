#pragma once
#include <cstdint>

// Keyboard hotkey with modifiers (feature rr95-music-player, task M1).
//
// The ini syntax is "Ctrl+Shift+Alt+Key", modifiers in any order, and the key last. The parser
// accepts the key names the mod needs beyond ParseVirtualKey: letters, digits, arrows,
// Home/End/PageUp/PageDown/Insert/Delete and the numpad, plus everything ParseVirtualKey already
// understands (F1..F12, the Scroll Lock aliases, Pause and a decimal/hex virtual-key code).
struct Hotkey {
    uint32_t vk = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// Parses str (case-insensitive, surrounding spaces trimmed). Returns false and leaves out
// untouched for anything invalid, so the caller keeps its own default.
bool ParseHotkey(const wchar_t* str, Hotkey& out);

// True when the key is set and the modifier state matches exactly.
bool HotkeyMatches(const Hotkey& key, bool ctrlDown, bool shiftDown, bool altDown);
