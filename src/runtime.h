#pragma once
#include <windows.h>

namespace rr95::music {

extern HMODULE g_module;
extern DWORD g_mainThreadId;

// S5 fix: loader-lock safe attach/detach of the chained DDRAW.dll!DirectDrawCreate IAT hook. Attach
// reads [Music] Enabled with plain file APIs and, only when music is enabled, writes the exe's import
// slot with a pointer store; it loads no module, resolves no API and writes no log. The detour's first
// call registers the Reproductor row on the game thread, before the general mod installs its hooks.
void MusicEarlyHookAttach(HMODULE module);
void MusicEarlyHookDetach();

}  // namespace rr95::music
