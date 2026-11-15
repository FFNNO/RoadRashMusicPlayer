#pragma once
#include <cstddef>
#include <string>
#include "shared/log.h"
#include "hotkey.h"

// [Music] in rr95music.ini. The music mod never reads the general mod's rr95gpu.ini.
//
// Enabled defaults to 1 (unlike the old in-ddraw player, whose default was 0): the user installs
// dsound.dll to get the player, so an installed proxy is an opt-in by itself.

enum class MusicOverlayMode {
    OnDemand = 0,
    Pinned = 1
};

enum class MusicCorner {
    BottomLeft = 0,
    TopLeft = 1,
    BottomRight = 2,
    TopRight = 3
};

struct MusicConfig {
    bool enabled = true;
    LogLevel logLevel = LogLevel::Info;
    std::wstring path = L"music";
    bool shuffle = false;
    int volume = 80;  // 0..100
    MusicOverlayMode overlay = MusicOverlayMode::OnDemand;
    MusicCorner corner = MusicCorner::BottomLeft;
    // Defaults are keys the game does not bind: its default Controls table holds w, s, a,
    // Space, f, d, Shift and the four arrows only.
    Hotkey nextKey = {0x22, false, false, false};          // PageDown
    Hotkey prevKey = {0x21, false, false, false};          // PageUp
    Hotkey playPauseKey = {0x23, false, false, false};     // End
    Hotkey nextCassetteKey = {0x2D, false, false, false};  // Insert
    Hotkey overlayKey = {0x24, false, false, false};       // Home
};

// Reads every key of iniPath into cfg (missing keys keep their defaults). MusicConfigInit applies
// it to the global config; tests call it on a local MusicConfig.
void ParseMusicConfigFile(const std::wstring& iniPath, MusicConfig& cfg);
void MusicConfigInit(const std::wstring& iniPath);
const MusicConfig& GetMusicConfig();

// S5 fix: the loader-lock safe probe DllMain uses before it installs the early DirectDrawCreate IAT
// hook. It reads only [Music] Enabled, from the bytes of the ini, with plain file APIs: no module is
// loaded, no log is opened and no API is resolved. Anything missing, unreadable or unparsable keeps
// defaultEnabled (an installed proxy is the opt-in, so a fresh install stays enabled).
bool ParseMusicEnabledIni(const char* data, size_t size, bool defaultEnabled);
bool ReadMusicEnabledIni(const wchar_t* iniPath, bool defaultEnabled);

// Accepts F1..F12, ScrollLock, Pause, or a full decimal/hex code in 1..255.
// Anything else (including trailing garbage such as "0x7Bzz") returns defaultKey.
uint32_t ParseVirtualKey(const wchar_t* str, uint32_t defaultKey = 0x91);

bool ParseMusicOverlayMode(const wchar_t* str, MusicOverlayMode& out);
bool ParseMusicCorner(const wchar_t* str, MusicCorner& out);

// Relative paths resolve against the game (exe) folder; absolute paths are kept as they are.
std::wstring ResolveMusicGamePath(const std::wstring& relOrAbsPath);
