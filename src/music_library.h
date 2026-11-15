#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Cassette and track model (feature rr95-music-player, task M2). Pure data and ordering only:
// no audio, no decoding, no Win32 inside the pure functions.
//
// A track is one file under the music folder. relativePath keeps the path as found under that
// folder (for example "Musica nueva 1\\song.mp3"); name is the file name without extension and is
// the display fallback until tags are read.
struct MusicTrack {
    std::wstring relativePath;
    std::wstring name;
    int roadRashIndex = -1;  // 0..13 on the built-in cassette, -1 otherwise
};

struct MusicCassette {
    std::wstring label;
    bool builtIn = false;
    std::vector<MusicTrack> tracks;
};

struct MusicLibrary {
    std::vector<MusicCassette> cassettes;
};

// Case-insensitive extension test for .wav, .mp3, .ogg and .flac.
bool IsMusicFileExtension(const std::wstring& path);

// Builds one cassette per first-level subfolder (labelled with the folder name) plus one cassette
// for files directly in the root (labelled "music"). Deeper subfolders belong to their first-level
// cassette. Unsupported extensions, empty names and cassettes without tracks are dropped.
// Cassettes and tracks are sorted by name, case-insensitively.
MusicLibrary BuildMusicLibrary(const std::vector<std::wstring>& relativePaths);

// Returns the play order for trackCount tracks: 0..trackCount-1 in order, or a deterministic
// permutation when shuffle is true. The generator is a local xorshift32 (a zero seed is replaced),
// so the same seed gives the same order on every run.
std::vector<int> BuildPlayOrder(int trackCount, bool shuffle, uint32_t seed);

// Move one position forward/back in a play order of orderSize entries, with wrap-around.
// Returns -1 when orderSize is not positive.
int NextPlayPosition(int position, int orderSize);
int PreviousPlayPosition(int position, int orderSize);

// Move to the next cassette, with wrap-around. Returns -1 when cassetteCount is not positive.
int NextCassetteIndex(int index, int cassetteCount);

// The built-in "Road Rash" cassette (the game's 14 AUDIO\MUSIC\*.RRA files, in game track order).
inline constexpr int kRoadRashTrackCount = 14;
extern const wchar_t* const kRoadRashTrackFiles[kRoadRashTrackCount];
extern const uint8_t kRoadRashBands[kRoadRashTrackCount];

int RoadRashBandOfTrack(int track);        // -1 outside 0..13
uint32_t RoadRashBandStringId(int track);  // 0xe1 + band, 0 outside 0..13
uint32_t RoadRashTrackStringId(int track); // 0xf1 + track, 0 outside 0..13
std::wstring RoadRashTrackPath(int track); // "AUDIO\\MUSIC\\<file>", empty outside 0..13
MusicCassette MakeRoadRashCassette();

// Thin non-pure helper: lists every file below root as a path relative to root (folder separators
// are '\\'). Returns an empty list for a missing folder and never throws.
std::vector<std::wstring> ScanMusicFolder(const std::wstring& root);
