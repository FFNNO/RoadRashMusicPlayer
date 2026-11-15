#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <ddraw.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <intrin.h>
#include <mutex>
#include <string>

#include "binding.h"
#include "dsound_exports.h"
#include "early_hook.h"
#include "music_config.h"
#include "music_dsound.h"
#include "music_glue.h"
#include "runtime.h"
#include "shared/exe_identity.h"
#include "shared/log.h"
#include "../include/rr95_mod_api.h"

namespace rr95::music {

namespace {

bool g_musicEnabled = false;
bool g_exeSupported = false;
BindingSchedule g_schedule;
std::atomic<uint32_t> g_missingLoggedMask{0};

HMODULE g_systemDsound = nullptr;
std::once_flag g_systemDsoundOnce;

using DirectSoundCreateFn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
using DirectSoundEnumerateFn = HRESULT(WINAPI*)(void*, LPVOID);
using DirectSoundCaptureCreateFn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUNDCAPTURE*, LPUNKNOWN);
using DirectSoundCreate8Fn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
using DirectSoundCaptureCreate8Fn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUNDCAPTURE8*, LPUNKNOWN);
using DirectSoundFullDuplexCreateFn = HRESULT(WINAPI*)(LPCGUID, LPCGUID, LPCDSCBUFFERDESC, LPCDSBUFFERDESC,
                                                       HWND, DWORD, LPDIRECTSOUNDFULLDUPLEX*,
                                                       LPDIRECTSOUNDCAPTUREBUFFER8*, LPDIRECTSOUNDBUFFER8*,
                                                       LPUNKNOWN);
using GetDeviceIDFn = HRESULT(WINAPI*)(LPCGUID, LPGUID);
using DllCanUnloadNowFn = HRESULT(WINAPI*)();
using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

constexpr HRESULT kAudioMissingFailure = DSERR_NODRIVER;
constexpr HRESULT kDeviceIdMissingFailure = DSERR_INVALIDPARAM;

struct ApiQuery {
    const RR95ModApi* api = nullptr;
    int32_t hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    int32_t menuState = -1;
    bool itemTextDetourInstalled = false;
};

std::wstring ModuleDir(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    std::wstring value(path);
    const size_t slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : value.substr(0, slash);
}

bool IsAddressInGameImage(const void* address) {
    const HMODULE exe = GetModuleHandleW(nullptr);
    if (exe == nullptr || address == nullptr) {
        return false;
    }
    const auto* base = reinterpret_cast<const uint8_t*>(exe);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const auto* value = reinterpret_cast<const uint8_t*>(address);
    return value >= base && value < base + nt->OptionalHeader.SizeOfImage;
}

void EnsureInitialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::wstring dir = ModuleDir(g_module);
        const std::wstring iniPath = dir.empty() ? L"rr95music.ini" : dir + L"\\rr95music.ini";
        const std::wstring logPath = dir.empty() ? L"rr95music.log" : dir + L"\\rr95music.log";

        MusicConfigInit(iniPath);
        const MusicConfig& cfg = GetMusicConfig();
        LogInit(logPath, cfg.logLevel);

        LOG_INFO("rr95music proxy dsound.dll loaded");
        LOG_INFO("Config: Music.Enabled=%d, Music.LogLevel=%d", cfg.enabled ? 1 : 0,
                 static_cast<int>(cfg.logLevel));
        g_musicEnabled = cfg.enabled;
        if (!cfg.enabled) {
            LOG_INFO("Music disabled by config: forwarding only, no binding");
            return;
        }

        const ExeIdentity id = IdentifyCurrentExe();
        if (id.build == ExeBuild::Unknown) {
            LOG_INFO("Host unknown (hash: %s), forwarding only", id.sha256Hex.c_str());
            return;
        }
        g_exeSupported = true;
        const char* buildName = (id.build == ExeBuild::Original) ? "Original" : "Patched";
        LOG_INFO("Detected supported game build: %s (%s)", buildName, id.sha256Hex.c_str());

        // Player-only sites: this mod always owns them, because the general mod never touches them.
        const int installed = InstallMusicPlayerHooks(GetModuleHandleW(nullptr));
        LOG_INFO("Music: player-only hooks installed: %d of 4", installed);
        if (installed > 0) {
            // A later FreeLibrary must not unload the trampolines that game code now calls.
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               reinterpret_cast<LPCWSTR>(&EnsureInitialized), &pinned);
        }
    });
}

void LogMissingExport(const char* name, HRESULT failure) {
    const int ordinal = DsoundExportOrdinal(name);
    const uint32_t bit = ordinal > 0 ? (uint32_t{1} << static_cast<uint32_t>(ordinal - 1)) : 0;
    if (bit != 0) {
        const uint32_t previous = g_missingLoggedMask.fetch_or(bit, std::memory_order_relaxed);
        if ((previous & bit) != 0) {
            return;
        }
    }
    LOG_ERROR("System dsound.dll does not export %s; returning 0x%08lX", name,
              static_cast<unsigned long>(failure));
}

HMODULE SystemDsound() {
    std::call_once(g_systemDsoundOnce, [] {
        wchar_t systemDir[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            LOG_ERROR("Cannot resolve the system directory (error %lu); dsound forwarding is unavailable",
                      GetLastError());
            return;
        }
        const std::wstring path = std::wstring(systemDir) + L"\\dsound.dll";
        g_systemDsound = LoadLibraryW(path.c_str());
        if (g_systemDsound == nullptr) {
            LOG_ERROR("Failed to load the system dsound.dll from %ls (error %lu)", path.c_str(), GetLastError());
        } else {
            LOG_INFO("System dsound.dll loaded from %ls", path.c_str());
        }
    });
    return g_systemDsound;
}

template <typename Fn>
Fn ResolveExport(const char* name) {
    const HMODULE module = SystemDsound();
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(GetProcAddress(module, name));
}

ApiQuery QueryApi() {
    ApiQuery query;
    const HMODULE ddraw = GetModuleHandleW(L"ddraw.dll");
    if (ddraw == nullptr) {
        LOG_INFO("Mod API: ddraw.dll is not loaded");
        return query;
    }
    using GetModApiFn = const RR95ModApi*(__cdecl*)(uint32_t);
    const auto getModApi = reinterpret_cast<GetModApiFn>(GetProcAddress(ddraw, "RR95_GetModApi"));
    if (getModApi == nullptr) {
        LOG_INFO("Mod API: the loaded ddraw.dll does not export RR95_GetModApi");
        return query;
    }
    const RR95ModApi* api = getModApi(RR95_MOD_API_VERSION);
    const ApiTableCheck check = ValidateModApiTable(api);
    if (!check.usable) {
        LOG_INFO("Mod API: no usable version 1 table (%s)", ApiTableProblemToString(check.problem));
        return query;
    }
    query.api = api;
    query.hookState = api->getHookState();
    query.menuState = api->getMenuState();
    query.itemTextDetourInstalled = api->isItemTextDetourInstalled() != 0;
    return query;
}

void LogDecision(const Decisions& decisions) {
    for (int i = 0; i < static_cast<int>(Capability::Count); ++i) {
        const Capability capability = static_cast<Capability>(i);
        LOG_INFO("Binding: %s = %s", CapabilityToString(capability),
                 BindingToString(BindingFor(decisions, capability)));
    }
}

void RunDecision(BindingTrigger trigger, const void* caller) {
    const ApiQuery query = QueryApi();
    BindingInputs inputs;
    inputs.musicEnabled = g_musicEnabled;
    inputs.exeSupported = g_exeSupported;
    inputs.apiAvailable = query.api != nullptr;
    inputs.hookState = query.hookState;
    inputs.menuState = query.menuState;
    inputs.itemTextDetourInstalled = query.itemTextDetourInstalled;

    const Decisions decisions = DecideBindings(inputs);
    LOG_INFO("Binding decision (%d, caller 0x%08X): api=%d, hook state=%d, menu state=%d, "
             "item text detour=%d",
             static_cast<int>(trigger), static_cast<unsigned>(reinterpret_cast<uintptr_t>(caller)),
             query.api != nullptr ? 1 : 0, query.hookState, query.menuState,
             query.itemTextDetourInstalled ? 1 : 0);
    LogDecision(decisions);

    MusicGlueApplyDecision(decisions, inputs, query.api);

    const bool pending = HasPendingDecision(decisions);
    g_schedule = AfterBindingDecision(g_schedule, pending);
    if (pending) {
        LOG_INFO("Binding: waiting for the general mod; the decision runs again at the first player-only "
                 "detour call or a registered frame callback");
    }
}

// Chained IAT hook on the exe's DDRAW.dll!DirectDrawCreate import.
//
// The general mod installs its hooks and opens its menu row slot inside its own DirectDrawCreate,
// which the game calls from WinMain about 11 s before the first DirectSoundCreate. A row added from
// the later DirectSoundCreate binding is refused with TOO_LATE. Attach runs from DllMain and does
// plain memory work only: it reads [Music] Enabled with the loader-lock safe probe and stores one
// pointer into the exe's import slot. VirtualProtect is the only kernel32 call, because the exe's
// .idata section is read-only. The detour's first call runs on the game thread after the loader
// lock, so it may resolve the API and register the row there, before the original DirectDrawCreate
// installs the general mod's hooks.
// The lookup, the store, the run-once flag and the conditional restore live in early_hook.cpp so the
// unit test runs them against a synthetic import table.
EarlyImportHook g_earlyHook;

// Runs once, at the game's first DirectDrawCreate call: resolves the API, validates it and lets the
// pure decision say whether the row may go in now. No API, a failed general mod, a menu it will not
// relocate or a row already registered all leave the DirectSoundCreate binding to its fallbacks.
void RunEarlyRowRegistration(void*) {
    EnsureInitialized();
    if (!g_musicEnabled || !g_exeSupported) {
        return;
    }
    const ApiQuery query = QueryApi();
    EarlyRowInputs inputs;
    inputs.musicEnabled = g_musicEnabled;
    inputs.exeSupported = g_exeSupported;
    inputs.apiAvailable = query.api != nullptr;
    inputs.hookState = query.hookState;
    inputs.menuState = query.menuState;
    inputs.rowRegistered = MusicGlueMenuRowRegistered();
    const EarlyRowAction action = DecideEarlyRowRegistration(inputs);
    LOG_INFO("Early row decision at DirectDrawCreate: api=%d, hook state=%d, menu state=%d, "
             "row registered=%d, action=%d",
             query.api != nullptr ? 1 : 0, query.hookState, query.menuState,
             inputs.rowRegistered ? 1 : 0, static_cast<int>(action));
    if (action != EarlyRowAction::Register) {
        LOG_INFO("Music: the Reproductor row is not registered here; the DirectSoundCreate binding "
                 "decides it");
        return;
    }
    MusicGlueRegisterEarlyRow(query.api);
}

HRESULT WINAPI DetourEarlyDirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer) {
    return ForwardEarlyImportCall<DirectDrawCreateFn>(g_earlyHook, &RunEarlyRowRegistration, nullptr,
                                                      static_cast<HRESULT>(DDERR_GENERIC), guid, out, outer);
}

}  // namespace

void MusicEarlyHookAttach(HMODULE module) {
    if (module == nullptr) {
        return;
    }
    const std::wstring dir = ModuleDir(module);
    const std::wstring iniPath = dir.empty() ? L"rr95music.ini" : dir + L"\\rr95music.ini";
    if (!ReadMusicEnabledIni(iniPath.c_str(), true)) {
        return;  // music off: forward only, and the slot is never touched
    }
    void** slot = FindImportSlot(GetModuleHandleW(nullptr), "DDRAW.dll", "DirectDrawCreate");
    InstallEarlyImportHook(g_earlyHook, slot, reinterpret_cast<void*>(&DetourEarlyDirectDrawCreate),
                           &StoreImportPointer);
}

void MusicEarlyHookDetach() {
    // The slot is restored only while it still holds the detour: another chain may have wrapped it.
    RestoreEarlyImportHook(g_earlyHook, reinterpret_cast<void*>(&DetourEarlyDirectDrawCreate),
                           &StoreImportPointer);
}

bool MusicProxyEnabled() {
    return g_musicEnabled && g_exeSupported;
}

void MusicProxyBind(BindingTrigger trigger) {
    EnsureInitialized();
    if (!g_musicEnabled || !g_exeSupported) {
        return;
    }
    if (ShouldRunBindingDecision(g_schedule, trigger)) {
        RunDecision(trigger, _ReturnAddress());
    }
}

}  // namespace rr95::music

extern "C" HRESULT WINAPI DirectSoundCreate(LPCGUID pcGuidDevice, LPDIRECTSOUND* ppDS, LPUNKNOWN pUnkOuter) {
    const void* caller = _ReturnAddress();
    rr95::music::EnsureInitialized();
    if (rr95::music::IsAddressInGameImage(caller)) {
        rr95::music::MusicProxyBind(rr95::music::BindingTrigger::DirectSoundCreate);
    }
    const auto real = rr95::music::ResolveExport<rr95::music::DirectSoundCreateFn>("DirectSoundCreate");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCreate", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(pcGuidDevice, ppDS, pUnkOuter);
}

extern "C" HRESULT WINAPI DirectSoundCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUND8* ppDS8, LPUNKNOWN pUnkOuter) {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::DirectSoundCreate8Fn>("DirectSoundCreate8");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCreate8", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(pcGuidDevice, ppDS8, pUnkOuter);
}

extern "C" HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context) {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::DirectSoundEnumerateFn>("DirectSoundEnumerateA");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundEnumerateA", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(reinterpret_cast<void*>(callback), context);
}

extern "C" HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context) {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::DirectSoundEnumerateFn>("DirectSoundEnumerateW");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundEnumerateW", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(reinterpret_cast<void*>(callback), context);
}

extern "C" HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE* ppDSC,
                                                   LPUNKNOWN pUnkOuter) {
    rr95::music::EnsureInitialized();
    const auto real =
        rr95::music::ResolveExport<rr95::music::DirectSoundCaptureCreateFn>("DirectSoundCaptureCreate");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCaptureCreate", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(pcGuidDevice, ppDSC, pUnkOuter);
}

extern "C" HRESULT WINAPI DirectSoundCaptureCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE8* ppDSC8,
                                                    LPUNKNOWN pUnkOuter) {
    rr95::music::EnsureInitialized();
    const auto real =
        rr95::music::ResolveExport<rr95::music::DirectSoundCaptureCreate8Fn>("DirectSoundCaptureCreate8");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCaptureCreate8", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(pcGuidDevice, ppDSC8, pUnkOuter);
}

extern "C" HRESULT WINAPI DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context) {
    rr95::music::EnsureInitialized();
    const auto real =
        rr95::music::ResolveExport<rr95::music::DirectSoundEnumerateFn>("DirectSoundCaptureEnumerateA");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCaptureEnumerateA", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(reinterpret_cast<void*>(callback), context);
}

extern "C" HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, LPVOID context) {
    rr95::music::EnsureInitialized();
    const auto real =
        rr95::music::ResolveExport<rr95::music::DirectSoundEnumerateFn>("DirectSoundCaptureEnumerateW");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundCaptureEnumerateW", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(reinterpret_cast<void*>(callback), context);
}

extern "C" HRESULT WINAPI DirectSoundFullDuplexCreate(LPCGUID pcGuidCaptureDevice, LPCGUID pcGuidRenderDevice,
                                                      LPCDSCBUFFERDESC pcDSCBufferDesc,
                                                      LPCDSBUFFERDESC pcDSBufferDesc, HWND hWnd, DWORD dwLevel,
                                                      LPDIRECTSOUNDFULLDUPLEX* ppDSFD,
                                                      LPDIRECTSOUNDCAPTUREBUFFER8* ppDSCBuffer8,
                                                      LPDIRECTSOUNDBUFFER8* ppDSBuffer8, LPUNKNOWN pUnkOuter) {
    rr95::music::EnsureInitialized();
    const auto real =
        rr95::music::ResolveExport<rr95::music::DirectSoundFullDuplexCreateFn>("DirectSoundFullDuplexCreate");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DirectSoundFullDuplexCreate", rr95::music::kAudioMissingFailure);
        return rr95::music::kAudioMissingFailure;
    }
    return real(pcGuidCaptureDevice, pcGuidRenderDevice, pcDSCBufferDesc, pcDSBufferDesc, hWnd, dwLevel,
                ppDSFD, ppDSCBuffer8, ppDSBuffer8, pUnkOuter);
}

extern "C" HRESULT WINAPI GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest) {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::GetDeviceIDFn>("GetDeviceID");
    if (real == nullptr) {
        rr95::music::LogMissingExport("GetDeviceID", rr95::music::kDeviceIdMissingFailure);
        return rr95::music::kDeviceIdMissingFailure;
    }
    return real(pGuidSrc, pGuidDest);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::DllCanUnloadNowFn>("DllCanUnloadNow");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DllCanUnloadNow", S_FALSE);
        return S_FALSE;
    }
    return real();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    rr95::music::EnsureInitialized();
    const auto real = rr95::music::ResolveExport<rr95::music::DllGetClassObjectFn>("DllGetClassObject");
    if (real == nullptr) {
        rr95::music::LogMissingExport("DllGetClassObject", CLASS_E_CLASSNOTAVAILABLE);
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return real(rclsid, riid, ppv);
}
