#include "early_hook.h"

#include <cstdint>

namespace rr95::music {

bool AsciiEqualsIgnoreCase(const char* left, const char* right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left;
        char b = *right;
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

// Walks the host's import descriptors by hand: no CRT string call, no module load, no allocation.
void** FindImportSlot(const void* moduleBase, const char* dllName, const char* functionName) {
    if (moduleBase == nullptr) {
        return nullptr;
    }
    const auto* base = static_cast<const uint8_t*>(moduleBase);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }
    const IMAGE_DATA_DIRECTORY& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) {
        return nullptr;
    }
    const uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
    const auto* descriptor =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        if (!AsciiEqualsIgnoreCase(reinterpret_cast<const char*>(base + descriptor->Name), dllName)) {
            continue;
        }
        // The same name table rule as tools/check_hook_sites.py. Without an
        // OriginalFirstThunk the FirstThunk array is the only name table; once the loader has bound
        // an entry it holds an address instead of a name RVA, and the bounds check below skips it.
        const uint32_t nameTable =
            descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
        if (nameTable == 0 || descriptor->FirstThunk == 0) {
            continue;
        }
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + nameTable);
        auto* slots = reinterpret_cast<void**>(const_cast<uint8_t*>(base) + descriptor->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData != 0; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) {
                continue;
            }
            if (names[i].u1.AddressOfData >= imageSize) {
                continue;  // not an in-image IMAGE_IMPORT_BY_NAME RVA: a bound address
            }
            const auto* import =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
            if (AsciiEqualsIgnoreCase(reinterpret_cast<const char*>(import->Name), functionName)) {
                return &slots[i];
            }
        }
    }
    return nullptr;
}

// Plain pointer store with the page protection lifted for the write only.
bool StoreImportPointer(void** slot, void* value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        return false;
    }
    *slot = value;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
    return true;
}

bool InstallEarlyImportHook(EarlyImportHook& hook, void** slot, void* detour, StoreImportPointerFn store) {
    if (slot == nullptr || *slot == nullptr || *slot == detour || store == nullptr || hook.installed.load()) {
        return false;
    }
    hook.previous.store(*slot);
    hook.slot = slot;
    if (!store(slot, detour)) {
        hook.previous.store(nullptr);
        hook.slot = nullptr;
        return false;
    }
    hook.installed.store(true);
    return true;
}

bool RestoreEarlyImportHook(EarlyImportHook& hook, void* detour, StoreImportPointerFn store) {
    if (!hook.installed.load()) {
        return false;
    }
    bool restored = false;
    // Restore the slot only when it still holds the detour: another chain may have wrapped it.
    if (hook.slot != nullptr && *hook.slot == detour && store != nullptr) {
        restored = store(hook.slot, hook.previous.load());
    }
    hook.slot = nullptr;
    hook.previous.store(nullptr);
    hook.installed.store(false);
    return restored;
}

bool TakeEarlyImportFirstCall(EarlyImportHook& hook) {
    bool expected = false;
    return hook.firstCallTaken.compare_exchange_strong(expected, true);
}

}  // namespace rr95::music
