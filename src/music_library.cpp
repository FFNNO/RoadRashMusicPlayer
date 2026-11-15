#include "music_library.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <utility>

const wchar_t* const kRoadRashTrackFiles[kRoadRashTrackCount] = {
    L"HB_Trip.RRA", L"HB_Pass.RRA", L"MM_Dino.RRA",  L"P_Pansy.RRA", L"P_Jessie.RRA",
    L"P_Bridge.RRA", L"SG_Rusty.RRA", L"SG_Outsh.RRA", L"SG_Kicks.RRA", L"SG_Super.RRA",
    L"SD_Train.RRA", L"SD_Duel.RRA", L"T_Teeth.RRA", L"T_Auto.RRA",
};

const uint8_t kRoadRashBands[kRoadRashTrackCount] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5};

namespace {

int CompareNamesIgnoreCase(const std::wstring& a, const std::wstring& b) {
    const size_t common = (a.size() < b.size()) ? a.size() : b.size();
    for (size_t i = 0; i < common; ++i) {
        const wint_t ca = std::towlower(static_cast<wint_t>(a[i]));
        const wint_t cb = std::towlower(static_cast<wint_t>(b[i]));
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
    }
    if (a.size() != b.size()) {
        return (a.size() < b.size()) ? -1 : 1;
    }
    const int raw = a.compare(b);
    if (raw < 0) return -1;
    if (raw > 0) return 1;
    return 0;
}

std::wstring NormalizeRelativePath(const std::wstring& path) {
    std::wstring result;
    result.reserve(path.size());
    for (wchar_t c : path) {
        result.push_back((c == L'/') ? L'\\' : c);
    }
    while (!result.empty() && result[0] == L'\\') {
        result.erase(0, 1);
    }
    while (result.size() >= 2 && result[0] == L'.' && result[1] == L'\\') {
        result.erase(0, 2);
    }
    return result;
}

std::wstring FileNamePart(const std::wstring& path) {
    const size_t sep = path.find_last_of(L'\\');
    return (sep == std::wstring::npos) ? path : path.substr(sep + 1);
}

std::wstring FileStem(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return name;
    }
    return name.substr(0, dot);
}

uint32_t NextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void ScanDirectory(const std::wstring& directory, const std::wstring& relative,
                   std::vector<std::wstring>& files, int depth) {
    if (depth > 32) {
        return;
    }
    WIN32_FIND_DATAW data = {};
    HANDLE handle = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    std::vector<std::wstring> subdirectories;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                subdirectories.push_back(name);
            }
        } else {
            files.push_back(relative.empty() ? name : relative + L"\\" + name);
        }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);

    for (const std::wstring& subdirectory : subdirectories) {
        ScanDirectory(directory + L"\\" + subdirectory,
                      relative.empty() ? subdirectory : relative + L"\\" + subdirectory, files, depth + 1);
    }
}

}  // namespace

bool IsMusicFileExtension(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring extension = path.substr(dot);
    for (auto& c : extension) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return extension == L".wav" || extension == L".mp3" || extension == L".ogg" || extension == L".flac";
}

MusicLibrary BuildMusicLibrary(const std::vector<std::wstring>& relativePaths) {
    MusicCassette rootCassette;
    rootCassette.label = L"music";
    std::vector<MusicCassette> folderCassettes;

    for (const std::wstring& rawPath : relativePaths) {
        const std::wstring path = NormalizeRelativePath(rawPath);
        if (path.empty() || !IsMusicFileExtension(path)) {
            continue;
        }

        MusicCassette* cassette = nullptr;
        std::wstring trackFileName;
        const size_t separator = path.find(L'\\');
        if (separator == std::wstring::npos) {
            cassette = &rootCassette;
            trackFileName = path;
        } else {
            const std::wstring folder = path.substr(0, separator);
            const std::wstring rest = path.substr(separator + 1);
            if (folder.empty() || rest.empty()) {
                continue;
            }
            trackFileName = FileNamePart(rest);
            if (trackFileName.empty()) {
                continue;
            }
            for (MusicCassette& existing : folderCassettes) {
                if (CompareNamesIgnoreCase(existing.label, folder) == 0) {
                    cassette = &existing;
                    break;
                }
            }
            if (cassette == nullptr) {
                MusicCassette created;
                created.label = folder;
                folderCassettes.push_back(std::move(created));
                cassette = &folderCassettes.back();
            }
        }

        MusicTrack track;
        track.relativePath = path;
        track.name = FileStem(trackFileName);
        if (track.name.empty()) {
            continue;
        }
        cassette->tracks.push_back(std::move(track));
    }

    MusicLibrary library;
    if (!rootCassette.tracks.empty()) {
        library.cassettes.push_back(std::move(rootCassette));
    }
    for (MusicCassette& cassette : folderCassettes) {
        if (!cassette.tracks.empty()) {
            library.cassettes.push_back(std::move(cassette));
        }
    }

    for (MusicCassette& cassette : library.cassettes) {
        std::sort(cassette.tracks.begin(), cassette.tracks.end(), [](const MusicTrack& a, const MusicTrack& b) {
            const int byName = CompareNamesIgnoreCase(a.name, b.name);
            if (byName != 0) {
                return byName < 0;
            }
            return CompareNamesIgnoreCase(a.relativePath, b.relativePath) < 0;
        });
    }
    std::stable_sort(library.cassettes.begin(), library.cassettes.end(),
                     [](const MusicCassette& a, const MusicCassette& b) {
                         return CompareNamesIgnoreCase(a.label, b.label) < 0;
                     });
    return library;
}

std::vector<int> BuildPlayOrder(int trackCount, bool shuffle, uint32_t seed) {
    std::vector<int> order;
    if (trackCount <= 0) {
        return order;
    }
    order.reserve(static_cast<size_t>(trackCount));
    for (int i = 0; i < trackCount; ++i) {
        order.push_back(i);
    }
    if (!shuffle) {
        return order;
    }

    uint32_t state = (seed != 0) ? seed : 0x9E3779B9u;
    for (int i = trackCount - 1; i > 0; --i) {
        const uint32_t bound = static_cast<uint32_t>(i) + 1u;
        const uint32_t j = NextRandom(state) % bound;
        std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(j)]);
    }
    return order;
}

int NextPlayPosition(int position, int orderSize) {
    if (orderSize <= 0) {
        return -1;
    }
    int normalized = position % orderSize;
    if (normalized < 0) {
        normalized += orderSize;
    }
    return (normalized + 1) % orderSize;
}

int PreviousPlayPosition(int position, int orderSize) {
    if (orderSize <= 0) {
        return -1;
    }
    int normalized = position % orderSize;
    if (normalized < 0) {
        normalized += orderSize;
    }
    return (normalized + orderSize - 1) % orderSize;
}

int NextCassetteIndex(int index, int cassetteCount) {
    return NextPlayPosition(index, cassetteCount);
}

int RoadRashBandOfTrack(int track) {
    if (track < 0 || track >= kRoadRashTrackCount) {
        return -1;
    }
    return static_cast<int>(kRoadRashBands[track]);
}

uint32_t RoadRashBandStringId(int track) {
    const int band = RoadRashBandOfTrack(track);
    if (band < 0) {
        return 0;
    }
    return static_cast<uint32_t>(0xE1 + band);
}

uint32_t RoadRashTrackStringId(int track) {
    if (track < 0 || track >= kRoadRashTrackCount) {
        return 0;
    }
    return static_cast<uint32_t>(0xF1 + track);
}

std::wstring RoadRashTrackPath(int track) {
    if (track < 0 || track >= kRoadRashTrackCount) {
        return L"";
    }
    return std::wstring(L"AUDIO\\MUSIC\\") + kRoadRashTrackFiles[track];
}

MusicCassette MakeRoadRashCassette() {
    MusicCassette cassette;
    cassette.label = L"Road Rash";
    cassette.builtIn = true;
    cassette.tracks.reserve(kRoadRashTrackCount);
    for (int track = 0; track < kRoadRashTrackCount; ++track) {
        MusicTrack entry;
        entry.relativePath = RoadRashTrackPath(track);
        entry.name = FileStem(kRoadRashTrackFiles[track]);
        entry.roadRashIndex = track;
        cassette.tracks.push_back(std::move(entry));
    }
    return cassette;
}

std::vector<std::wstring> ScanMusicFolder(const std::wstring& root) {
    std::vector<std::wstring> files;
    if (root.empty()) {
        return files;
    }
    try {
        ScanDirectory(root, L"", files, 0);
    } catch (...) {
        files.clear();
    }
    return files;
}
