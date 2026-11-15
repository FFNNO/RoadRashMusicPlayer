#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Engine access for the standalone music mod (rr95-music-standalone S3a).
//
// Every game address below is a preferred VA from out/decomp.c; Rebase() turns it into the live
// address of the loaded game image. All reads and writes are guarded, so a missing or unexpected
// engine byte can never fault the game: the helper returns false and the caller skips the action.
// This file is the mod's own small copy of the shared hook helpers; it never includes the general
// mod's hook_sites.cpp.
namespace rr95::music::engine {

void SetHostModule(void* module);
uintptr_t Rebase(uint32_t preferredVa);

template <typename T>
T* GlobalPtr(uint32_t preferredVa) {
    return reinterpret_cast<T*>(Rebase(preferredVa));
}

bool ReadByte(uint32_t preferredVa, int& out);
bool ReadInt(uint32_t preferredVa, int& out);
bool WriteByte(uint32_t preferredVa, uint8_t value);
bool WriteInt(uint32_t preferredVa, int value);

// True when the live bytes at preferredVa equal expected (at most 16 bytes). A fault or a null
// expected buffer is false. Used to check a call target before calling it, like a hook site.
bool BytesMatch(uint32_t preferredVa, const uint8_t* expected, size_t length);

// Engine globals (preferred VAs verified in RoadRash95.exe):
// 0x0058e074: byte, race music mute (slot 4). The native mechanism sets 1 and posts the volume
//             update, and never touches the master mute at 0x0058e014.
// 0x005cd490: int, selected band (DAT_005cd490), restored by the game only through the menu paths.
// 0x005cd498: int, menu track (DAT_005cd498); never written during a race.
// 0x005a6970: int, saved menu pick for the race (DAT_005a6970).
// 0x005a6908: byte, nonzero when the race uses the DirectSound stream instead of MIDI.
// 0x005cd49c: char** game string table; band = [0xe1 + band], track = [0xf1 + track].
// 0x005cd48c: DWORD music thread id, 0x0061aff8: HANDLE music thread, 0x0061af8c: update message.
// 0x006163d3: byte, DAT_006163d3, nonzero while the game plays race music through MIDI.
// 0x005a6958: byte, DAT_005a6958, nonzero while the stream thread plays a buffer.
// 0x005a6909: byte, DAT_005a6909, nonzero while the stream thread is ready to read.
// 0x005cd4d4: byte, DAT_005cd4d4, set while the game's Esc pause or focus-loss loop runs.
inline constexpr uint32_t kRaceMusicMute = 0x0058e074;
inline constexpr uint32_t kNativeMidiActive = 0x006163d3;
inline constexpr uint32_t kJukeboxBand = 0x005cd490;
inline constexpr uint32_t kJukeboxTrack = 0x005cd498;
inline constexpr uint32_t kRacePick = 0x005a6970;
inline constexpr uint32_t kRaceStreamActive = 0x005a6908;
inline constexpr uint32_t kGameStringTable = 0x005cd49c;
inline constexpr uint32_t kMusicThreadId = 0x005cd48c;
inline constexpr uint32_t kMusicThreadHandle = 0x0061aff8;
inline constexpr uint32_t kMusicUpdateMessage = 0x0061af8c;
inline constexpr uint32_t kStreamPlaying = 0x005a6958;
inline constexpr uint32_t kStreamReady = 0x005a6909;
inline constexpr uint32_t kGamePauseLoop = 0x005cd4d4;

// Game music entry points:
// 0x004459c0: void __cdecl FUN_004459c0(void), pauses the native MIDI stream.
// 0x00445ba0: void __cdecl FUN_00445ba0(void), resumes it.
// 0x00411c00: void __cdecl FUN_00411c00(void), posts "Play the song" to the stream thread.
// 0x004463a0: void __cdecl FUN_004463a0(void), starts the race music.
inline constexpr uint32_t kMidiPause = 0x004459c0;
inline constexpr uint32_t kMidiResume = 0x00445ba0;
inline constexpr uint32_t kStreamResume = 0x00411c00;
inline constexpr uint32_t kRaceMusicStart = 0x004463a0;

// Menu 19 routines and globals (rr95-music-standalone S3b, the Reproductor row and its popup):
// 0x0043cea0: LRESULT __stdcall FUN_0043cea0(HWND, UINT, WPARAM, LPARAM), the RashButton window
//             procedure every menu button (native popup rows included) uses.
// 0x00439160: void __fastcall FUN_00439160(item, selected), redraws one item.
// 0x0043a4e0: void __fastcall FUN_0043a4e0(count, items, focus), opens a native popup.
// 0x0043a390: void __cdecl FUN_0043a390(void), closes and removes every open popup.
// 0x00618361: byte, nonzero while a popup is open.
// 0x005c3fc8: dword table (26 slots) of the open popup's items; 0x005c404c holds its first slot.
inline constexpr uint32_t kRashButtonProc = 0x0043cea0;
inline constexpr uint32_t kItemRedraw = 0x00439160;
inline constexpr uint32_t kPopupOpen = 0x0043a4e0;
inline constexpr uint32_t kPopupClose = 0x0043a390;
inline constexpr uint32_t kPopupOpenFlag = 0x00618361;
inline constexpr uint32_t kPopupItemTable = 0x005c3fc8;
inline constexpr uint32_t kPopupFirstSlot = 0x005c404c;

bool MusicThreadPresent();
// The native "Update the Volume and Pan" post. Only when the music thread exists.
void PostMusicVolumeUpdate();

// Fallback presentation sites (rr95-music-standalone S4). The music mod hooks them only when the
// binding says it owns the capability, so the general mod never patches the same bytes.
// 0x0046e630: void __fastcall FUN_0046e630(uint8_t* buffer, int pitch, int palette), race present.
// 0x0046ded0: void __cdecl FUN_0046ded0(void), menu present.
// 0x0045b930: const char* __fastcall FUN_0045b930(void* item), text of a flag-0x100000 menu item.
// 0x005ce858: HWND game window, 0x005ce85c: int buffer width, 0x005ce860: int buffer height,
// 0x005ce864: uint32_t* 32-bit DIB the original present blits through GDI32!StretchDIBits.
inline constexpr uint32_t kRacePresent = 0x0046e630;
inline constexpr uint32_t kMenuPresent = 0x0046ded0;
inline constexpr uint32_t kItemText = 0x0045b930;
inline constexpr uint32_t kGameHwnd = 0x005ce858;
inline constexpr uint32_t kBufferWidth = 0x005ce85c;
inline constexpr uint32_t kBufferHeight = 0x005ce860;
inline constexpr uint32_t kDibBuffer = 0x005ce864;

// Reads one game string table entry into a plain buffer. The caller converts it with
// DecodeMusicGameString; a fault, a null table or a null entry returns false.
bool ReadGameStringRaw(uint32_t stringId, char (&buffer)[65]);
std::wstring GameString(uint32_t stringId, const std::wstring& fallback);

// Folder that holds the game executable, empty when it cannot be resolved.
std::wstring GameFolder();

}  // namespace rr95::music::engine
