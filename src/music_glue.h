#pragma once
#include "binding.h"

// Game-side glue of the standalone music mod (rr95-music-standalone S3a).
//
// Every decision lives in music_game.h/.cpp (pure and unit tested); this glue only reads engine
// memory, applies the writes and posts, and drives the player. Race and menu frames arrive through
// the general mod's API callbacks when it owns them; otherwise the fallback detours the race
// present (0x0046e630), the menu present (0x0046ded0) and the item text function (0x0045b930).
// The menu row goes through the API, or into the unused shipped slot 7 in place, and the beeper
// goes through the API overlay, or through a chained GDI32!StretchDIBits IAT hook.
namespace rr95::music {

// Installs the player-only hooks (native MIDI resume 0x00445ba0, native stream resume 0x00411c00,
// race music start 0x004463a0). Only called when music is enabled and the exe is supported; each
// site is installed only when its shipped bytes match. Returns how many sites were installed.
int InstallMusicPlayerHooks(void* hostModule);
void RemoveMusicPlayerHooks(void* hostModule);

// Called after every binding decision with the answer, the inputs it was computed from and the API
// table (null when absent). Clears a stale API item text provider from those inputs, installs the
// fallback detours, then registers the frame callbacks, the menu row (API or in place) and the
// beeper overlay (API layer or the GDI DIB blend) according to the decisions.
void MusicGlueApplyDecision(const Decisions& decisions, const BindingInputs& inputs, const RR95ModApi* api);

// Registers the Reproductor row and its item text provider at the game's first call to the
// chained DDRAW.dll!DirectDrawCreate hook, before the general mod installs its hooks and builds the
// relocated graphics menu. Never registers twice, and records a failure cause for the gate the
// DirectSoundCreate binding runs later. Returns true when the row and the provider are registered.
bool MusicGlueRegisterEarlyRow(const RR95ModApi* api);
// True once a row is registered through the API or written in place.
bool MusicGlueMenuRowRegistered();

// API frame callbacks.
void MusicGlueOnRaceFrame();
void MusicGlueOnMenuFrame();

}  // namespace rr95::music
