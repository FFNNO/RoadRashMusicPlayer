#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <vector>

#include "hook_chain.h"

namespace rr95 {

constexpr uint32_t kPreferredImageBase = 0x00400000;

struct HookSite {
    const char* name;
    uint32_t preferredVa;
    uint8_t expected[16];
    size_t spanLength;
    std::vector<size_t> instructionLengths;
    void* detour;
    void** originalTrampoline;
    bool installed;
    // Offsets inside expected[] of 4-byte absolute addresses (e.g. A0 moffs32), written as preferred
    // VAs. The loader relocates them when the image is rebased (dynamic base), so InstallHook adds
    // the rebase delta before comparing, the trampoline copies the relocated bytes, and RemoveHook
    // restores them.
    std::vector<size_t> absoluteOperandOffsets = {};
    // Experimental OtherModsCompat=1: the site held another mod's jump and our hook is chained onto
    // it. chainSaved holds the other mod's first 5 bytes (put back on unload only while the site
    // still holds chainOurs, our E9), and chainWatch says whether another mod rewrote the site since.
    bool chained = false;
    uint8_t chainSaved[kJmpRel32Length] = {};
    uint8_t chainOurs[kJmpRel32Length] = {};
    ChainedSiteWatch chainWatch = {};
};

// expected[] of site with every absolute operand rebased by delta (module base - preferred base).
void BuildRelocatedExpected(const HookSite& site, intptr_t delta, uint8_t out[16]);

const char* ValidateHookDecoding(const HookSite& site);
// Installs site when its live bytes are the shipped bytes. With otherModsCompat (the experimental
// [Music] OtherModsCompat=1) a site that holds another mod's jump out of the game image is chained
// onto that jump instead (see hook_chain.h); anything else is refused and logged.
bool InstallHook(HookSite& site, HMODULE moduleBase = nullptr, bool otherModsCompat = false);
// A chained site gets the other mod's bytes back only while it still holds our E9; otherwise it is
// left alone, logged, and its trampoline is kept because another mod may still jump into it.
void RemoveHook(HookSite& site, HMODULE moduleBase = nullptr);

// True while an installed chained site still holds our E9. A fault reads as false.
bool ChainedHookStillOurs(const HookSite& site, HMODULE moduleBase = nullptr);
// One re-hook check of an installed chained site. Returns true only the first time the site is found
// rewritten by another mod: it is logged once and marked lost (never reinstalled).
bool MonitorChainedHook(HookSite& site, HMODULE moduleBase = nullptr);

// Import address table hook on one by-name import (dllName!functionName) of a module.
struct IatHook {
    const char* dllName;       // compared case-insensitively, e.g. "COMDLG32.dll"
    const char* functionName;  // e.g. "GetSaveFileNameA"
    void* detour;
    void** original;           // receives the previous slot value on install
    void** slot;               // patched IAT slot while installed, else nullptr
};

// Returns the IAT slot of dllName!functionName in moduleBase, or nullptr when it is not imported
// by name.
void** FindIatSlot(HMODULE moduleBase, const char* dllName, const char* functionName);
bool InstallIatHook(IatHook& hook, HMODULE moduleBase);
// Writes the saved original back into the slot.
void RemoveIatHook(IatHook& hook);

}  // namespace rr95
