#include "music_config.h"

#include <windows.h>

#include <cwctype>
#include <cstdlib>
#include <cstring>

namespace {

MusicConfig g_config;

std::wstring LowerTrimmed(const wchar_t* text) {
    std::wstring value = text != nullptr ? text : L"";
    size_t start = 0;
    while (start < value.size() && std::iswspace(value[start])) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::iswspace(value[end - 1])) {
        --end;
    }
    value = value.substr(start, end - start);
    for (auto& c : value) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return value;
}

}  // namespace

uint32_t ParseVirtualKey(const wchar_t* str, uint32_t defaultKey) {
    std::wstring s = LowerTrimmed(str);
    if (s.empty()) return defaultKey;
    static const struct {
        const wchar_t* name;
        uint32_t vk;
    } kNamedKeys[] = {
        {L"f1", VK_F1},   {L"f2", VK_F2},   {L"f3", VK_F3},   {L"f4", VK_F4},
        {L"f5", VK_F5},   {L"f6", VK_F6},   {L"f7", VK_F7},   {L"f8", VK_F8},
        {L"f9", VK_F9},   {L"f10", VK_F10}, {L"f11", VK_F11}, {L"f12", VK_F12},
        {L"scrolllock", VK_SCROLL}, {L"scroll_lock", VK_SCROLL}, {L"scroll", VK_SCROLL},
        {L"pause", VK_PAUSE},
    };
    for (const auto& k : kNamedKeys) {
        if (s == k.name) return k.vk;
    }
    // Numeric codes must consume the whole string: "0x7Bzz" or "12abc" fall back to the default.
    if (!std::iswdigit(s[0])) return defaultKey;
    wchar_t* end = nullptr;
    const unsigned long val = std::wcstoul(s.c_str(), &end, 0);
    if (end == nullptr || end == s.c_str() || *end != L'\0') return defaultKey;
    if (val > 0 && val < 256) {
        return static_cast<uint32_t>(val);
    }
    return defaultKey;
}

bool ParseMusicOverlayMode(const wchar_t* str, MusicOverlayMode& out) {
    const std::wstring s = LowerTrimmed(str);
    if (s == L"ondemand" || s == L"on-demand" || s == L"on demand") {
        out = MusicOverlayMode::OnDemand;
        return true;
    }
    if (s == L"pinned") {
        out = MusicOverlayMode::Pinned;
        return true;
    }
    return false;
}

bool ParseMusicCorner(const wchar_t* str, MusicCorner& out) {
    const std::wstring s = LowerTrimmed(str);
    if (s == L"bottomleft") {
        out = MusicCorner::BottomLeft;
        return true;
    }
    if (s == L"topleft") {
        out = MusicCorner::TopLeft;
        return true;
    }
    if (s == L"bottomright") {
        out = MusicCorner::BottomRight;
        return true;
    }
    if (s == L"topright") {
        out = MusicCorner::TopRight;
        return true;
    }
    return false;
}

std::wstring ResolveMusicGamePath(const std::wstring& relOrAbsPath) {
    if (relOrAbsPath.empty()) return L"";
    if (relOrAbsPath.size() >= 2 && relOrAbsPath[1] == L':') {
        return relOrAbsPath;
    }
    if (relOrAbsPath[0] == L'\\' || relOrAbsPath[0] == L'/') {
        return relOrAbsPath;
    }
    wchar_t exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return relOrAbsPath;
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash == nullptr) lastSlash = wcsrchr(exePath, L'/');
    if (lastSlash != nullptr) {
        *lastSlash = L'\0';
    }
    return std::wstring(exePath) + L"\\" + relOrAbsPath;
}

void ParseMusicConfigFile(const std::wstring& iniPath, MusicConfig& cfg) {
    cfg.enabled = GetPrivateProfileIntW(L"Music", L"Enabled", 1, iniPath.c_str()) != 0;
    // Experimental, off by default: anything but a nonzero integer keeps the standalone mode.
    cfg.otherModsCompat = GetPrivateProfileIntW(L"Music", L"OtherModsCompat", 0, iniPath.c_str()) != 0;

    wchar_t level[64] = {};
    GetPrivateProfileStringW(L"Music", L"LogLevel", L"info", level, 64, iniPath.c_str());
    cfg.logLevel = ParseLogLevelName(level);

    wchar_t path[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Music", L"Path", L"music", path, MAX_PATH, iniPath.c_str());
    cfg.path = path;

    UINT shuffle = GetPrivateProfileIntW(L"Music", L"Shuffle", 0, iniPath.c_str());
    cfg.shuffle = shuffle != 0;

    wchar_t volume[32] = {};
    GetPrivateProfileStringW(L"Music", L"Volume", L"80", volume, 32, iniPath.c_str());
    wchar_t* volumeEnd = nullptr;
    const long parsedVolume = std::wcstol(volume, &volumeEnd, 10);
    if (volumeEnd != volume && *volumeEnd == L'\0') {
        const long clamped = (parsedVolume < 0) ? 0 : ((parsedVolume > 100) ? 100 : parsedVolume);
        cfg.volume = static_cast<int>(clamped);
    }

    wchar_t overlay[32] = {};
    GetPrivateProfileStringW(L"Music", L"Overlay", L"ondemand", overlay, 32, iniPath.c_str());
    MusicOverlayMode overlayMode = cfg.overlay;
    if (ParseMusicOverlayMode(overlay, overlayMode)) {
        cfg.overlay = overlayMode;
    }

    wchar_t corner[32] = {};
    GetPrivateProfileStringW(L"Music", L"Corner", L"bottomleft", corner, 32, iniPath.c_str());
    MusicCorner cornerMode = cfg.corner;
    if (ParseMusicCorner(corner, cornerMode)) {
        cfg.corner = cornerMode;
    }

    static const struct {
        const wchar_t* key;
        const wchar_t* defaultName;
        Hotkey MusicConfig::* field;
    } kHotkeys[] = {
        {L"NextKey", L"PageDown", &MusicConfig::nextKey},
        {L"PrevKey", L"PageUp", &MusicConfig::prevKey},
        {L"PlayPauseKey", L"End", &MusicConfig::playPauseKey},
        {L"NextCassetteKey", L"Insert", &MusicConfig::nextCassetteKey},
        {L"OverlayKey", L"Home", &MusicConfig::overlayKey},
    };
    for (const auto& spec : kHotkeys) {
        wchar_t hotkey[64] = {};
        GetPrivateProfileStringW(L"Music", spec.key, spec.defaultName, hotkey, 64, iniPath.c_str());
        Hotkey parsedHotkey = cfg.*(spec.field);
        if (ParseHotkey(hotkey, parsedHotkey)) {
            cfg.*(spec.field) = parsedHotkey;
        }
    }
}

void MusicConfigInit(const std::wstring& iniPath) {
    ParseMusicConfigFile(iniPath, g_config);
}

const MusicConfig& GetMusicConfig() {
    return g_config;
}

namespace {

// Loader-lock safe helpers: no CRT string allocation, no Win32 call beyond the file read itself.
bool AsciiEqualsIgnoreCase(const char* text, size_t length, const char* expected) {
    const size_t expectedLength = std::strlen(expected);
    if (length != expectedLength) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        char a = text[i];
        char b = expected[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool IsIniSpace(char c) {
    return c == ' ' || c == '\t';
}

void TrimIniLine(const char*& text, size_t& length) {
    while (length > 0 && IsIniSpace(*text)) {
        ++text;
        --length;
    }
    while (length > 0 && IsIniSpace(text[length - 1])) {
        --length;
    }
}

// Decimal value of an ini line, following GetPrivateProfileInt: an optional sign, digits, and a
// trailing comment are accepted; anything else is not a number. out is untouched on failure.
bool ParseIniInteger(const char* text, size_t length, long& out) {
    TrimIniLine(text, length);
    bool negative = false;
    if (length > 0 && (text[0] == '-' || text[0] == '+')) {
        negative = text[0] == '-';
        ++text;
        --length;
    }
    if (length == 0 || text[0] < '0' || text[0] > '9') {
        return false;
    }
    long value = 0;
    size_t i = 0;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        const long digit = static_cast<long>(text[i] - '0');
        if (value > (2147483647L - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++i;
    }
    while (i < length && IsIniSpace(text[i])) {
        ++i;
    }
    if (i < length && text[i] != ';' && text[i] != '#') {
        return false;
    }
    out = negative ? -value : value;
    return true;
}

}  // namespace

bool ParseMusicEnabledIni(const char* data, size_t size, bool defaultEnabled) {
    if (data == nullptr || size == 0) {
        return defaultEnabled;
    }
    // A UTF-8 BOM is common in editors; UTF-16 would keep NUL bytes and fall back to the default,
    // which is safe because EnsureInitialized reads the full ini again.
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
        data += 3;
        size -= 3;
    }
    bool inMusicSection = false;
    size_t position = 0;
    while (position < size) {
        size_t end = position;
        while (end < size && data[end] != '\n' && data[end] != '\r') {
            ++end;
        }
        const char* line = data + position;
        size_t length = end - position;
        TrimIniLine(line, length);
        if (length > 0 && line[0] != ';' && line[0] != '#') {
            if (line[0] == '[') {
                inMusicSection = length >= 3 && line[length - 1] == ']' &&
                                 AsciiEqualsIgnoreCase(line + 1, length - 2, "Music");
            } else if (inMusicSection) {
                const char* equals = static_cast<const char*>(std::memchr(line, '=', length));
                if (equals != nullptr) {
                    const char* key = line;
                    size_t keyLength = static_cast<size_t>(equals - line);
                    TrimIniLine(key, keyLength);
                    if (AsciiEqualsIgnoreCase(key, keyLength, "Enabled")) {
                        long value = 0;
                        if (!ParseIniInteger(equals + 1, length - static_cast<size_t>(equals - line) - 1,
                                             value)) {
                            return defaultEnabled;
                        }
                        return value != 0;
                    }
                }
            }
        }
        // Skip the line break (CRLF or LF).
        if (end < size && data[end] == '\r') {
            ++end;
        }
        if (end < size && data[end] == '\n') {
            ++end;
        }
        position = end;
    }
    return defaultEnabled;
}

bool ReadMusicEnabledIni(const wchar_t* iniPath, bool defaultEnabled) {
    if (iniPath == nullptr || iniPath[0] == L'\0') {
        return defaultEnabled;
    }
    // CreateFileW/ReadFile load no module and take no loader lock, which is what makes this call safe
    // from DllMain. The probe reads the first 8 KiB, which is where every shipped ini keeps [Music];
    // a longer file keeps the default and EnsureInitialized reads it in full later.
    const HANDLE file = CreateFileW(iniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return defaultEnabled;
    }
    char buffer[8192];
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        return defaultEnabled;
    }
    return ParseMusicEnabledIni(buffer, static_cast<size_t>(read), defaultEnabled);
}
