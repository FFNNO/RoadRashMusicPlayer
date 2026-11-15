#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "music_config.h"

// Registry persistence for the music mod (feature rr95-music-player, tasks M6a and M6b).
//
// HKCU\SOFTWARE\Electronic Arts\RoadRash 95, next to the game's own values:
//   ModMusicCassette: REG_SZ, the cassette label ("Road Rash" for the built-in one).
//   ModMusicOverlay:  REG_BINARY 1 byte, 1 = pinned, 0 = on demand.
//   ModMusicKeys:     REG_BINARY, version 1 + one virtual-key byte per action.
// Read at startup when [Music] Enabled=1; a valid registry value wins over the ini.

inline constexpr const char* kGameRegistryKey = "SOFTWARE\\Electronic Arts\\RoadRash 95";
inline constexpr const char* kMusicCassetteRegistryValueName = "ModMusicCassette";
inline constexpr const char* kMusicOverlayRegistryValueName = "ModMusicOverlay";
inline constexpr const char* kMusicKeysRegistryValueName = "ModMusicKeys";
inline constexpr int kMusicOverlayRegistryAbsent = -1;
inline constexpr int kMusicOverlayRegistryInvalid = -2;

// One binding per music action, in the order the race poll uses them.
enum class MusicKeyAction : uint8_t {
    NextTrack = 0,
    PreviousTrack = 1,
    PlayPause = 2,
    NextCassette = 3,
    Overlay = 4
};
inline constexpr int kMusicKeyActionCount = 5;

// VK_RETURN, VK_ESCAPE, VK_SHIFT, VK_TAB and the four arrow keys, as bytes so the header stays free
// of <windows.h>.
inline constexpr uint8_t kMusicKeyReturn = 0x0D;
inline constexpr uint8_t kMusicKeyEscape = 0x1B;
inline constexpr uint8_t kMusicKeyShift = 0x10;
inline constexpr uint8_t kMusicKeyTab = 0x09;
inline constexpr uint8_t kMusicKeyLeft = 0x25;
inline constexpr uint8_t kMusicKeyUp = 0x26;
inline constexpr uint8_t kMusicKeyRight = 0x27;
inline constexpr uint8_t kMusicKeyDown = 0x28;

bool IsReservedMusicKey(uint32_t vk);
bool IsMusicKeyModifier(uint32_t vk);

// The five bindings in row order. vk is a virtual-key code without modifiers.
struct MusicKeys {
    uint8_t vk[kMusicKeyActionCount] = {};
};

bool MusicKeysAreValid(const MusicKeys& keys);

// ModMusicKeys layout: byte 0 is the layout version (1), bytes 1..5 are the keys. The value stores
// one virtual key per action and has no room for modifiers: a captured row is always a plain key,
// and a row that keeps an ini hotkey with modifiers keeps them for the session only (the next start
// reads the registry value, which holds the key without them).
inline constexpr uint8_t kMusicKeysRegistryVersion = 1;
inline constexpr uint32_t kMusicKeysRegistrySize = 1 + kMusicKeyActionCount;

enum class MusicKeysRegistryStatus {
    Absent = 0,
    Invalid = 1,
    Valid = 2
};

struct MusicKeysRegistryValue {
    MusicKeysRegistryStatus status = MusicKeysRegistryStatus::Absent;
    MusicKeys keys;
};

// nullptr with size 0 is Absent; any other unusable value is Invalid.
MusicKeysRegistryValue DecodeMusicKeysRegistryValue(uint32_t type, const uint8_t* data, uint32_t size);
// The version byte and the five keys, empty when a key cannot be stored.
std::string EncodeMusicKeysRegistryValue(const MusicKeys& keys);

struct EffectiveMusicKeys {
    MusicKeys keys;
    bool fromRegistry = false;
};

EffectiveMusicKeys ResolveEffectiveMusicKeys(const MusicKeys& ini, const MusicKeysRegistryValue& value);

// Same key and API as the game. Read returns Absent when the key or value is missing.
MusicKeysRegistryValue ReadMusicKeysRegistryValue();
bool WriteMusicKeysRegistryValue(const MusicKeys& keys);

// 0 or 1 for REG_BINARY of 1 byte; kMusicOverlayRegistryInvalid for anything else.
int ParseMusicOverlayRegistryValue(uint32_t type, const uint8_t* data, uint32_t size);
uint8_t EncodeMusicOverlayRegistryValue(MusicOverlayMode mode);

struct EffectiveMusicOverlay {
    MusicOverlayMode mode = MusicOverlayMode::OnDemand;
    bool fromRegistry = false;
};

// A valid registry value wins over the ini Overlay setting.
EffectiveMusicOverlay ResolveEffectiveMusicOverlay(MusicOverlayMode ini, int registryValue);
int ReadMusicOverlayRegistryValue();

// REG_SZ with a trailing NUL; empty when the value is missing, not REG_SZ or not NUL-terminated.
std::wstring DecodeMusicCassetteRegistryValue(const char* data, uint32_t size);
// CP-1252 bytes with a trailing NUL (the REG_SZ form the game uses), empty for an empty label.
std::string EncodeMusicCassetteRegistryValue(const std::wstring& label);
bool ReadMusicCassetteRegistryValue(std::wstring& out);
bool WriteMusicCassetteRegistryValue(const std::wstring& label);
bool WriteMusicOverlayRegistryValue(MusicOverlayMode mode);

struct ResolvedMusicCassette {
    int index = 0;
    bool fallback = false;  // the saved label was not found and the first cassette is used
};

// Case-insensitive lookup of a saved label in the current library. A missing label falls back to
// index 0 (the built-in "Road Rash" cassette). An empty label is not a fallback.
ResolvedMusicCassette ResolveSavedMusicCassette(const std::vector<std::wstring>& labels,
                                                const std::wstring& saved);
