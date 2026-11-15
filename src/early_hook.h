#pragma once
// The early Reproductor row hook: a chained IAT hook on the exe's own DDRAW.dll!DirectDrawCreate
// import. DllMain attaches it under the loader lock, so everything here is plain memory
// work: no module load, no export lookup, no allocation and no log call. The pieces are split out of
// dsound_proxy.cpp so the unit test can run them against a synthetic in-memory import table.
#include <windows.h>

#include <atomic>
#include <cstddef>

// Forward declarations instead of <ddraw.h>, which pulls in the RPC headers and their macros.
struct IDirectDraw;
struct IUnknown;

namespace rr95::music {

using DirectDrawCreateFn = HRESULT(WINAPI*)(GUID*, IDirectDraw**, IUnknown*);

// ASCII case-insensitive compare without the CRT, safe under the loader lock.
bool AsciiEqualsIgnoreCase(const char* left, const char* right);

// Walks the import descriptors of the PE image mapped at moduleBase by hand and returns the IAT
// slot of dllName!functionName, imported by name. Returns null when the image, the DLL or the name
// is missing. A descriptor whose OriginalFirstThunk is 0 has no separate name table: its FirstThunk
// array is read as the name table, and an entry that no longer holds an in-image name RVA (the
// loader already bound it to an address) is skipped. tools/check_hook_sites.py reads the exe files
// with the same rule.
void** FindImportSlot(const void* moduleBase, const char* dllName, const char* functionName);

// Writes one pointer into an import slot. The default lifts the page protection for the write only,
// because the exe's .idata section is read-only; the test injects a plain store.
using StoreImportPointerFn = bool (*)(void** slot, void* value);
bool StoreImportPointer(void** slot, void* value);

struct EarlyImportHook {
    void** slot = nullptr;                 // the import slot while the hook is installed
    std::atomic<void*> previous{nullptr};  // the value the slot held before the hook
    std::atomic<bool> installed{false};
    std::atomic<bool> firstCallTaken{false};
};

// Saves *slot and stores detour into it. Refuses a null slot, an empty slot and a slot that already
// holds the detour; on a failed store the hook stays uninstalled.
bool InstallEarlyImportHook(EarlyImportHook& hook, void** slot, void* detour, StoreImportPointerFn store);

// Puts the saved value back only while the slot still holds the detour: another chain may have
// wrapped it since. Clears the hook either way. Returns true when the slot was restored.
bool RestoreEarlyImportHook(EarlyImportHook& hook, void* detour, StoreImportPointerFn store);

// True for the first caller only: the early row registration runs once per process.
bool TakeEarlyImportFirstCall(EarlyImportHook& hook);

// The detour body: runs firstCall once, on the first call, then calls the saved previous function
// with the same arguments and returns its result unchanged. Without a saved function it returns
// missing.
template <typename Fn, typename Result, typename... Args>
Result ForwardEarlyImportCall(EarlyImportHook& hook, void (*firstCall)(void* ctx), void* ctx, Result missing,
                              Args... args) {
    if (TakeEarlyImportFirstCall(hook) && firstCall != nullptr) {
        firstCall(ctx);
    }
    const auto previous = reinterpret_cast<Fn>(hook.previous.load());
    if (previous == nullptr) {
        return missing;
    }
    return previous(args...);
}

}  // namespace rr95::music
