#include "engine.h"

#include <windows.h>

#include <cstring>

#include "music_game.h"

namespace rr95::music::engine {

namespace {

void* g_hostModule = nullptr;

}  // namespace

void SetHostModule(void* module) {
    g_hostModule = module;
}

uintptr_t Rebase(uint32_t preferredVa) {
    return reinterpret_cast<uintptr_t>(g_hostModule) + (preferredVa - 0x00400000u);
}

bool ReadByte(uint32_t preferredVa, int& out) {
    __try {
        out = static_cast<int>(*GlobalPtr<const uint8_t>(preferredVa));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadInt(uint32_t preferredVa, int& out) {
    __try {
        out = *GlobalPtr<const int>(preferredVa);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteByte(uint32_t preferredVa, uint8_t value) {
    __try {
        *GlobalPtr<uint8_t>(preferredVa) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteInt(uint32_t preferredVa, int value) {
    __try {
        *GlobalPtr<int>(preferredVa) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BytesMatch(uint32_t preferredVa, const uint8_t* expected, size_t length) {
    if (expected == nullptr || length == 0 || length > 16) {
        return false;
    }
    __try {
        return std::memcmp(GlobalPtr<const void>(preferredVa), expected, length) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MusicThreadPresent() {
    __try {
        return *GlobalPtr<HANDLE>(kMusicThreadHandle) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PostMusicVolumeUpdate() {
    __try {
        if (*GlobalPtr<HANDLE>(kMusicThreadHandle) != nullptr) {
            const DWORD threadId = *GlobalPtr<DWORD>(kMusicThreadId);
            const UINT message = *GlobalPtr<UINT>(kMusicUpdateMessage);
            PostThreadMessageA(threadId, message, 0, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool ReadGameStringRaw(uint32_t stringId, char (&buffer)[65]) {
    __try {
        const uintptr_t table = *GlobalPtr<uintptr_t>(kGameStringTable);
        if (table == 0) {
            return false;
        }
        const char* text =
            *reinterpret_cast<const char* const*>(table + MusicGameStringOffset(stringId));
        if (text == nullptr) {
            return false;
        }
        size_t i = 0;
        for (; i < kMusicGameNameMaxBytes; ++i) {
            buffer[i] = text[i];
            if (buffer[i] == '\0') {
                break;
            }
        }
        buffer[kMusicGameNameMaxBytes] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::wstring GameString(uint32_t stringId, const std::wstring& fallback) {
    char buffer[kMusicGameNameMaxBytes + 1] = {};
    if (!ReadGameStringRaw(stringId, buffer)) {
        return fallback;
    }
    return DecodeMusicGameString(buffer, kMusicGameNameMaxBytes, fallback);
}

std::wstring GameFolder() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::wstring();
    }
    std::wstring folder(path, length);
    const size_t slash = folder.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    folder.resize(slash);
    return folder;
}

}  // namespace rr95::music::engine
