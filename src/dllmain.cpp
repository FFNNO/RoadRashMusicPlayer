#include <windows.h>

#include "runtime.h"
#include "shared/log.h"

namespace rr95::music {

HMODULE g_module = nullptr;
DWORD g_mainThreadId = 0;

}  // namespace rr95::music

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        rr95::music::g_module = hinstDLL;
        rr95::music::g_mainThreadId = GetCurrentThreadId();
        // Chained DDRAW.dll!DirectDrawCreate hook so the Reproductor row exists before the general
        // mod installs its hooks and builds the relocated graphics menu.
        rr95::music::MusicEarlyHookAttach(hinstDLL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpvReserved == nullptr) {
            rr95::music::MusicEarlyHookDetach();
            LogClose();
        }
    }
    return TRUE;
}
