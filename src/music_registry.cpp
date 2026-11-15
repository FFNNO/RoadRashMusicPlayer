#include "music_registry.h"

#include <windows.h>

#include <cwctype>

bool IsReservedMusicKey(uint32_t vk) {
    switch (vk) {
        case kMusicKeyReturn:
        case kMusicKeyEscape:
        case kMusicKeyTab:
        case kMusicKeyLeft:
        case kMusicKeyUp:
        case kMusicKeyRight:
        case kMusicKeyDown:
            return true;
        default:
            return false;
    }
}

bool IsMusicKeyModifier(uint32_t vk) {
    switch (vk) {
        case 0x10:  // VK_SHIFT
        case 0x11:  // VK_CONTROL
        case 0x12:  // VK_MENU
        case 0x5B:  // VK_LWIN
        case 0x5C:  // VK_RWIN
        case 0xA0:  // VK_LSHIFT
        case 0xA1:  // VK_RSHIFT
        case 0xA2:  // VK_LCONTROL
        case 0xA3:  // VK_RCONTROL
        case 0xA4:  // VK_LMENU
        case 0xA5:  // VK_RMENU
            return true;
        default:
            return false;
    }
}

bool MusicKeysAreValid(const MusicKeys& keys) {
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        const uint8_t key = keys.vk[i];
        if (key == 0 || IsReservedMusicKey(key) || IsMusicKeyModifier(key)) {
            return false;
        }
        for (int j = 0; j < i; ++j) {
            if (keys.vk[j] == key) {
                return false;
            }
        }
    }
    return true;
}

MusicKeysRegistryValue DecodeMusicKeysRegistryValue(uint32_t type, const uint8_t* data, uint32_t size) {
    MusicKeysRegistryValue value;
    if (data == nullptr && size == 0) {
        return value;
    }
    if (data == nullptr || type != REG_BINARY || size != kMusicKeysRegistrySize ||
        data[0] != kMusicKeysRegistryVersion) {
        value.status = MusicKeysRegistryStatus::Invalid;
        return value;
    }
    MusicKeys keys;
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        keys.vk[i] = data[1 + i];
    }
    if (!MusicKeysAreValid(keys)) {
        value.status = MusicKeysRegistryStatus::Invalid;
        return value;
    }
    value.status = MusicKeysRegistryStatus::Valid;
    value.keys = keys;
    return value;
}

std::string EncodeMusicKeysRegistryValue(const MusicKeys& keys) {
    if (!MusicKeysAreValid(keys)) {
        return {};
    }
    std::string out;
    out.reserve(kMusicKeysRegistrySize);
    out.push_back(static_cast<char>(kMusicKeysRegistryVersion));
    for (int i = 0; i < kMusicKeyActionCount; ++i) {
        out.push_back(static_cast<char>(keys.vk[i]));
    }
    return out;
}

EffectiveMusicKeys ResolveEffectiveMusicKeys(const MusicKeys& ini, const MusicKeysRegistryValue& value) {
    EffectiveMusicKeys effective;
    if (value.status == MusicKeysRegistryStatus::Valid) {
        effective.keys = value.keys;
        effective.fromRegistry = true;
    } else {
        effective.keys = ini;
    }
    return effective;
}

MusicKeysRegistryValue ReadMusicKeysRegistryValue() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return MusicKeysRegistryValue{};
    }
    DWORD type = 0;
    uint8_t data[16] = {};
    DWORD size = sizeof(data);
    const LSTATUS status = RegQueryValueExA(key, kMusicKeysRegistryValueName, nullptr, &type, data, &size);
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) {
        return MusicKeysRegistryValue{};
    }
    if (status != ERROR_SUCCESS) {
        MusicKeysRegistryValue invalid;
        invalid.status = MusicKeysRegistryStatus::Invalid;
        return invalid;
    }
    return DecodeMusicKeysRegistryValue(type, data, size);
}

bool WriteMusicKeysRegistryValue(const MusicKeys& keys) {
    const std::string value = EncodeMusicKeysRegistryValue(keys);
    if (value.empty()) {
        return false;
    }
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExA(key, kMusicKeysRegistryValueName, 0, REG_BINARY,
                                          reinterpret_cast<const BYTE*>(value.data()),
                                          static_cast<DWORD>(value.size()));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

int ParseMusicOverlayRegistryValue(uint32_t type, const uint8_t* data, uint32_t size) {
    if (data == nullptr || type != REG_BINARY || size != 1) {
        return kMusicOverlayRegistryInvalid;
    }
    if (data[0] == 0 || data[0] == 1) {
        return data[0];
    }
    return kMusicOverlayRegistryInvalid;
}

uint8_t EncodeMusicOverlayRegistryValue(MusicOverlayMode mode) {
    return mode == MusicOverlayMode::Pinned ? 1 : 0;
}

EffectiveMusicOverlay ResolveEffectiveMusicOverlay(MusicOverlayMode ini, int registryValue) {
    EffectiveMusicOverlay effective;
    if (registryValue == 0 || registryValue == 1) {
        effective.mode = registryValue == 1 ? MusicOverlayMode::Pinned : MusicOverlayMode::OnDemand;
        effective.fromRegistry = true;
    } else {
        effective.mode = ini;
        effective.fromRegistry = false;
    }
    return effective;
}

int ReadMusicOverlayRegistryValue() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return kMusicOverlayRegistryAbsent;
    }
    DWORD type = 0;
    uint8_t data[8] = {};
    DWORD size = sizeof(data);
    const LSTATUS status = RegQueryValueExA(key, kMusicOverlayRegistryValueName, nullptr, &type, data, &size);
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) {
        return kMusicOverlayRegistryAbsent;
    }
    if (status != ERROR_SUCCESS) {
        return kMusicOverlayRegistryInvalid;
    }
    return ParseMusicOverlayRegistryValue(type, data, size);
}

namespace {

std::string EncodeCp1252(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    BOOL usedDefault = FALSE;
    const int n = WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, text, -1, nullptr, 0, nullptr, &usedDefault);
    if (n <= 1 || usedDefault) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    usedDefault = FALSE;
    if (WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, text, -1, out.data(), n, nullptr, &usedDefault) != n ||
        usedDefault) {
        return {};
    }
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

}  // namespace

std::wstring DecodeMusicCassetteRegistryValue(const char* data, uint32_t size) {
    if (data == nullptr || size == 0 || data[size - 1] != '\0') {
        return {};
    }
    uint32_t length = 0;
    while (length + 1 < size && data[length] != '\0') {
        ++length;
    }
    if (length == 0) {
        return {};
    }
    const int n = MultiByteToWideChar(1252, 0, data, static_cast<int>(length), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(1252, 0, data, static_cast<int>(length), out.data(), n) != n) {
        return {};
    }
    return out;
}

std::string EncodeMusicCassetteRegistryValue(const std::wstring& label) {
    const std::string bytes = EncodeCp1252(label.c_str());
    if (bytes.empty()) {
        return {};
    }
    std::string out = bytes;
    out.push_back('\0');
    return out;
}

bool ReadMusicCassetteRegistryValue(std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    char data[512] = {};
    DWORD size = sizeof(data);
    const LSTATUS status = RegQueryValueExA(key, kMusicCassetteRegistryValueName, nullptr, &type,
                                            reinterpret_cast<BYTE*>(data), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }
    const std::wstring label = DecodeMusicCassetteRegistryValue(data, size);
    if (label.empty()) {
        return false;
    }
    out = label;
    return true;
}

bool WriteMusicCassetteRegistryValue(const std::wstring& label) {
    const std::string value = EncodeMusicCassetteRegistryValue(label);
    if (value.empty()) {
        return false;
    }
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExA(key, kMusicCassetteRegistryValueName, 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(value.data()),
                                          static_cast<DWORD>(value.size()));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool WriteMusicOverlayRegistryValue(MusicOverlayMode mode) {
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kGameRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const BYTE value = EncodeMusicOverlayRegistryValue(mode);
    const LSTATUS status = RegSetValueExA(key, kMusicOverlayRegistryValueName, 0, REG_BINARY, &value, 1);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

ResolvedMusicCassette ResolveSavedMusicCassette(const std::vector<std::wstring>& labels,
                                                const std::wstring& saved) {
    ResolvedMusicCassette resolved;
    if (labels.empty()) {
        resolved.index = 0;
        resolved.fallback = true;
        return resolved;
    }
    if (saved.empty()) {
        return resolved;
    }
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels[i].size() != saved.size()) {
            continue;
        }
        bool equal = true;
        for (size_t c = 0; c < saved.size(); ++c) {
            if (std::towlower(labels[i][c]) != std::towlower(saved[c])) {
                equal = false;
                break;
            }
        }
        if (equal) {
            resolved.index = static_cast<int>(i);
            return resolved;
        }
    }
    resolved.index = 0;
    resolved.fallback = true;
    return resolved;
}
