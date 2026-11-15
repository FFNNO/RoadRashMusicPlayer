// music_test: the pure code of the standalone music mod (rr95-music-standalone S2): the binding
// decision table, the [Music] ini parsing, and the dsound.dll export-forwarding name table. It also
// parses the built dsound.dll and checks its real export names and ordinals against the table.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../src/audio_decoders.h"
#include "../src/binding.h"
#include "../src/dsound_exports.h"
#include "../src/hotkey.h"
#include "../src/music_config.h"
#include "../src/music_game.h"
#include "../src/music_library.h"
#include "../src/music_menu.h"
#include "../src/music_player.h"
#include "../src/music_overlay.h"
#include "../src/music_registry.h"
#include "../src/music_tags.h"

#ifndef RR95_MUSIC_PROXY_PATH
#define RR95_MUSIC_PROXY_PATH L"dsound.dll"
#endif

namespace {

using namespace rr95::music;

int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!condition) {
        ++g_failures;
    }
}

std::wstring MakeTestTempPath(const wchar_t* name) {
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    return std::wstring(tempDir) + name + std::to_wstring(GetCurrentProcessId());
}

bool WriteTextFile(const std::wstring& path, const char* text) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
        return false;
    }
    const size_t len = std::strlen(text);
    const bool ok = std::fwrite(text, 1, len, fp) == len;
    std::fclose(fp);
    return ok;
}

bool ReadWholeFile(const wchar_t* path, std::vector<uint8_t>& bytes) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, L"rb") != 0 || fp == nullptr) {
        return false;
    }
    uint8_t buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + n);
    }
    std::fclose(fp);
    return true;
}

std::string ReadWholeTextFile(const std::wstring& path) {
    std::string content;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) {
        return content;
    }
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(fp);
    return content;
}

// R3 tag suffix: a seek that always fails, injected through ReadMusicTagSuffix's seek parameter.
int FailingMusicTagSeek(FILE* /*file*/, long long /*offset*/, int /*origin*/) {
    return -1;
}

// ---------------------------------------------------------------------------
// Music player helpers (ported from the in-ddraw hook_test with S3a adaptations)
// ---------------------------------------------------------------------------

std::wstring TempFilePath(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"rr95_audio_" + std::to_wstring(GetCurrentProcessId()) + L"_" + name;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
        return false;
    }
    const size_t written = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);
    return written == bytes.size();
}

std::vector<uint8_t> MakePcmWav(uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample,
                                const std::vector<uint8_t>& samples) {
    std::vector<uint8_t> out;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size());
    const auto put16 = [&out](uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    };
    const auto put32 = [&out](uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    };
    const auto putTag = [&out](const char* tag) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>(tag[i]));
        }
    };
    putTag("RIFF");
    put32(36 + dataBytes);
    putTag("WAVE");
    putTag("fmt ");
    put32(16);
    put16(1);  // PCM
    put16(channels);
    put32(sampleRate);
    put32(byteRate);
    put16(blockAlign);
    put16(bitsPerSample);
    putTag("data");
    put32(dataBytes);
    out.insert(out.end(), samples.begin(), samples.end());
    return out;
}

MusicCassette TestCassette(const wchar_t* label, std::initializer_list<const wchar_t*> files) {
    MusicCassette cassette;
    cassette.label = label;
    for (const wchar_t* file : files) {
        MusicTrack track;
        track.relativePath = file;
        const std::wstring path = file;
        const size_t slash = path.find_last_of(L'\\');
        const std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
        const size_t dot = base.find_last_of(L'.');
        track.name = (dot == std::wstring::npos) ? base : base.substr(0, dot);
        cassette.tracks.push_back(std::move(track));
    }
    return cassette;
}

MusicConfig TestPlayerConfig() {
    MusicConfig config;
    config.enabled = true;
    config.volume = 50;
    return config;
}

// Device-free audio backend for the reliability tests (R3-001 .. R3-004). It records what the
// engine asks of it and can fail engine creation, fail source-voice creation, or hold submitted
// buffers until the next track flushes them, like an asynchronous XAudio2 voice. The worker thread
// and the test thread share it, so the counters are atomics, the submitted list is behind a mutex
// and every predicate reads through the accessors (R3-001 of the S3a review).
class FakeAudioSink final : public MusicAudioSink {
public:
    std::atomic<bool> failInitialize{false};
    std::atomic<int> failBeginTrackCalls{0};  // fail the first N BeginTrack calls; -1 fails every call
    // Keep submitted buffers in flight across a track change, so their counters cannot be reused.
    std::atomic<bool> holdBuffersAcrossTracks{false};
    std::atomic<int> initializeCalls{0};
    std::atomic<int> beginTrackCalls{0};
    std::atomic<int> submitCalls{0};
    std::atomic<int> resetCalls{0};
    std::atomic<int> shutdownCalls{0};
    std::atomic<int> pauseCalls{0};
    std::atomic<int> resumeCalls{0};
    std::atomic<MusicAudioCounter*> lastBeginCounter{nullptr};

    int BeginTrackCalls() const { return beginTrackCalls.load(); }
    int SubmitCalls() const { return submitCalls.load(); }
    MusicAudioCounter* LastBeginCounter() const { return lastBeginCounter.load(); }

    bool Initialize(float) override {
        initializeCalls.fetch_add(1);
        return !failInitialize.load();
    }

    bool BeginTrack(uint32_t, uint32_t, float, MusicAudioCounter* counter) override {
        const int calls = beginTrackCalls.fetch_add(1) + 1;
        // Flushed callbacks of the previous track are delivered before the next voice is used.
        if (!holdBuffersAcrossTracks.load()) {
            CompletePending();
        }
        lastBeginCounter.store(counter);
        const int failCalls = failBeginTrackCalls.load();
        if (failCalls < 0 || calls <= failCalls) {
            return false;
        }
        if (counter != nullptr) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            counter->inFlight.store(0);
            counter->framesRemaining.store(0);
        }
        return true;
    }

    // Delivers the buffers the engine submitted so far, like an asynchronous voice callback.
    void ReleasePending() { CompletePending(); }

    bool Submit(const float*, uint32_t frames, uint32_t, MusicAudioCounter* counter) override {
        submitCalls.fetch_add(1);
        if (counter == nullptr || frames == 0) {
            return false;
        }
        Pending pending;
        pending.counter = counter;
        pending.frames = frames;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            // The counters and the pending entry change together, so a release from the test thread
            // can never decrement a buffer the worker has not counted yet.
            counter->inFlight.fetch_add(1);
            counter->framesRemaining.fetch_add(frames);
            m_pending.push_back(pending);
        }
        return true;
    }

    void Pause() override { pauseCalls.fetch_add(1); }
    void Resume() override { resumeCalls.fetch_add(1); }

    void ResetTrack() override {
        resetCalls.fetch_add(1);
        CompletePending();
    }

    int FreeBuffers(const MusicAudioCounter* counter) const override {
        if (counter == nullptr) {
            return kFakeQueuedBuffers;
        }
        const int inFlight = counter->inFlight.load();
        return (kFakeQueuedBuffers - inFlight > 0) ? kFakeQueuedBuffers - inFlight : 0;
    }

    bool Drained(const MusicAudioCounter* counter) const override {
        return counter == nullptr || counter->inFlight.load() <= 0;
    }

    uint64_t FramesRemaining(const MusicAudioCounter* counter) const override {
        return (counter != nullptr) ? counter->framesRemaining.load() : 0;
    }

    void Shutdown() override {
        shutdownCalls.fetch_add(1);
        CompletePending();
    }

private:
    static constexpr int kFakeQueuedBuffers = 4;

    struct Pending {
        MusicAudioCounter* counter = nullptr;
        uint32_t frames = 0;
    };

    void CompletePending() {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        CompletePendingLocked();
    }

    void CompletePendingLocked() {
        for (const Pending& pending : m_pending) {
            pending.counter->framesRemaining.fetch_sub(pending.frames);
            pending.counter->inFlight.fetch_sub(1);
        }
        m_pending.clear();
    }

    mutable std::mutex m_pendingMutex;
    std::vector<Pending> m_pending;
};

struct FakeSinkHolder {
    std::atomic<FakeAudioSink*> sink{nullptr};
    FakeAudioSink* Get() const { return sink.load(); }
};

MusicAudioSink* CreateFakeAudioSink(void* user) {
    FakeSinkHolder* holder = static_cast<FakeSinkHolder*>(user);
    holder->sink = new FakeAudioSink();
    return holder->sink;
}

HANDLE FailWakeEvent() {
    return nullptr;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, DWORD timeoutMs = 5000) {
    const ULONGLONG start = GetTickCount64();
    while (!predicate()) {
        if (GetTickCount64() - start >= timeoutMs) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

// M5b: injectable worker clock for the race-heartbeat tests.
std::atomic<uint64_t> g_fakeNowMs{1000};
uint64_t FakeNowMs() {
    return g_fakeNowMs.load();
}

int CountLogWarnings(const std::wstring& path, const char* needle) {
    const std::string content = ReadWholeTextFile(path);
    int count = 0;
    size_t position = 0;
    while ((position = content.find("[WARN]", position)) != std::string::npos) {
        const size_t end = content.find('\n', position);
        const size_t length = (end == std::string::npos) ? std::string::npos : end - position;
        const std::string line = content.substr(position, length);
        if (line.find(needle) != std::string::npos) {
            ++count;
        }
        position += 6;
    }
    return count;
}

void RemoveTree(const std::wstring& directory) {
    WIN32_FIND_DATAW data = {};
    HANDLE handle = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const std::wstring child = directory + L"\\" + name;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                RemoveTree(child);
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(handle, &data) != 0);
        FindClose(handle);
    }
    RemoveDirectoryW(directory.c_str());
}

struct ExportName {
    std::string name;
    int ordinal = 0;
};

// Minimal PE export parser, enough for the 32-bit proxy DLL.
bool ParsePeExports(const std::vector<uint8_t>& image, std::vector<ExportName>& out) {
    auto u16 = [&image](size_t offset) -> uint16_t {
        if (offset + 2 > image.size()) return 0;
        return static_cast<uint16_t>(image[offset]) |
               static_cast<uint16_t>(image[offset + 1] << 8);
    };
    auto u32 = [&image](size_t offset) -> uint32_t {
        if (offset + 4 > image.size()) return 0;
        return static_cast<uint32_t>(image[offset]) |
               (static_cast<uint32_t>(image[offset + 1]) << 8) |
               (static_cast<uint32_t>(image[offset + 2]) << 16) |
               (static_cast<uint32_t>(image[offset + 3]) << 24);
    };
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') {
        return false;
    }
    const size_t pe = u32(0x3c);
    if (pe + 24 > image.size() || image[pe] != 'P' || image[pe + 1] != 'E' ||
        image[pe + 2] != 0 || image[pe + 3] != 0) {
        return false;
    }
    const uint16_t sectionCount = u16(pe + 6);
    const uint16_t optionalSize = u16(pe + 20);
    const size_t optional = pe + 24;
    if (optional + optionalSize > image.size()) {
        return false;
    }
    const uint16_t magic = u16(optional);
    const size_t dataDir = optional + (magic == 0x10b ? 96 : 112);
    const uint32_t exportRva = u32(dataDir);
    const size_t sectionTable = optional + optionalSize;
    auto rvaToOffset = [&](uint32_t rva) -> size_t {
        for (uint16_t i = 0; i < sectionCount; ++i) {
            const size_t s = sectionTable + static_cast<size_t>(i) * 40;
            const uint32_t virtualSize = u32(s + 8);
            const uint32_t virtualAddress = u32(s + 12);
            const uint32_t rawSize = u32(s + 16);
            const uint32_t rawOffset = u32(s + 20);
            const uint32_t span = std::max(virtualSize, rawSize);
            if (rva >= virtualAddress && rva < virtualAddress + span) {
                return rawOffset + (rva - virtualAddress);
            }
        }
        return image.size();
    };
    const size_t exportOffset = rvaToOffset(exportRva);
    if (exportOffset == image.size()) {
        return false;
    }
    const uint32_t base = u32(exportOffset + 16);
    const uint32_t functionCount = u32(exportOffset + 20);
    const uint32_t nameCount = u32(exportOffset + 24);
    const uint32_t namesRva = u32(exportOffset + 32);
    const uint32_t ordinalsRva = u32(exportOffset + 36);
    const size_t namesOffset = rvaToOffset(namesRva);
    const size_t ordinalsOffset = rvaToOffset(ordinalsRva);
    if (namesOffset == image.size() || ordinalsOffset == image.size() ||
        functionCount > 4096 || nameCount > 4096) {
        return false;
    }
    for (uint32_t i = 0; i < nameCount; ++i) {
        const uint32_t nameRva = u32(namesOffset + static_cast<size_t>(i) * 4);
        const size_t nameOffset = rvaToOffset(nameRva);
        if (nameOffset == image.size()) {
            return false;
        }
        size_t end = nameOffset;
        while (end < image.size() && image[end] != 0) {
            ++end;
        }
        if (end == image.size()) {
            return false;
        }
        const uint16_t nameIndex = u16(ordinalsOffset + static_cast<size_t>(i) * 2);
        ExportName entry;
        entry.name.assign(reinterpret_cast<const char*>(image.data() + nameOffset), end - nameOffset);
        entry.ordinal = static_cast<int>(base + nameIndex);
        out.push_back(entry);
    }
    return true;
}

const char* const kExpectedExports[] = {
    "DirectSoundCreate",        "DirectSoundEnumerateA",   "DirectSoundEnumerateW",
    "DllCanUnloadNow",          "DllGetClassObject",       "DirectSoundCaptureCreate",
    "DirectSoundCaptureEnumerateA", "DirectSoundCaptureEnumerateW", "GetDeviceID",
    "DirectSoundFullDuplexCreate", "DirectSoundCreate8",   "DirectSoundCaptureCreate8"};

void TestExportTable() {
    size_t count = 0;
    const rr95::music::DsoundExport* table = rr95::music::DsoundExportTable(count);
    Check(table != nullptr && count == 12, "exports: the name table holds the documented 12 exports");
    if (table == nullptr || count != 12) {
        return;
    }
    bool namesMatch = true;
    bool ordinalsMatch = true;
    for (size_t i = 0; i < count; ++i) {
        namesMatch = namesMatch && std::strcmp(table[i].name, kExpectedExports[i]) == 0;
        ordinalsMatch = ordinalsMatch && table[i].ordinal == static_cast<int>(i + 1);
    }
    Check(namesMatch, "exports: the table lists the 12 names in ordinal order");
    Check(ordinalsMatch, "exports: the table keeps ordinals 1..12 like the system dsound.dll");
    Check(rr95::music::DsoundExportOrdinal("DirectSoundCreate") == 1 &&
              rr95::music::DsoundExportOrdinal("DirectSoundCaptureCreate8") == 12 &&
              rr95::music::DsoundExportOrdinal("NoSuchExport") == -1,
          "exports: ordinal lookup answers by name and rejects unknown names");

    std::vector<uint8_t> image;
    if (!ReadWholeFile(RR95_MUSIC_PROXY_PATH, image)) {
        Check(false, "exports: the built dsound.dll can be read");
        return;
    }
    std::vector<ExportName> actual;
    if (!ParsePeExports(image, actual)) {
        Check(false, "exports: the built dsound.dll has a readable PE export table");
        return;
    }
    bool actualNamesMatch = actual.size() == count;
    if (actualNamesMatch) {
        for (const ExportName& entry : actual) {
            const int expectedOrdinal = rr95::music::DsoundExportOrdinal(entry.name.c_str());
            if (expectedOrdinal != entry.ordinal) {
                actualNamesMatch = false;
            }
        }
        for (const char* name : kExpectedExports) {
            const bool found = std::any_of(actual.begin(), actual.end(), [name](const ExportName& entry) {
                return entry.name == name;
            });
            actualNamesMatch = actualNamesMatch && found;
        }
    }
    Check(actualNamesMatch,
          "exports: the built dsound.dll exports the table names with ordinals 1..12");
}

void TestMusicConfig() {
    const std::wstring base = MakeTestTempPath(L"rr95music-test-");
    const std::wstring file = base + L".ini";

    WriteTextFile(file, "[Music]\nEnabled=0\nLogLevel=debug\n");
    MusicConfig cfg;
    ParseMusicConfigFile(file, cfg);
    Check(!cfg.enabled && cfg.logLevel == LogLevel::Debug,
          "config: Enabled=0 and LogLevel=debug are read");

    WriteTextFile(file, "[Music]\nEnabled=1\nLogLevel=WARN\n");
    cfg = MusicConfig{};
    ParseMusicConfigFile(file, cfg);
    Check(cfg.enabled && cfg.logLevel == LogLevel::Warn,
          "config: Enabled=1 keeps music on and LogLevel is case-insensitive");

    WriteTextFile(file, "[Music]\nLogLevel=error\n");
    cfg = MusicConfig{};
    ParseMusicConfigFile(file, cfg);
    Check(cfg.enabled && cfg.logLevel == LogLevel::Error,
          "config: missing Enabled defaults to 1 and LogLevel=error is read");

    WriteTextFile(file, "[Music]\nEnabled=1\nLogLevel=nonsense\n");
    cfg = MusicConfig{};
    ParseMusicConfigFile(file, cfg);
    Check(cfg.enabled && cfg.logLevel == LogLevel::Info,
          "config: an unknown LogLevel falls back to info");

    DeleteFileW(file.c_str());
    cfg = MusicConfig{};
    ParseMusicConfigFile(file, cfg);
    Check(cfg.enabled && cfg.logLevel == LogLevel::Info,
          "config: a missing ini keeps Enabled=1 and LogLevel=info");

    WriteTextFile(file, "[Other]\nEnabled=0\nLogLevel=error\n");
    cfg = MusicConfig{};
    ParseMusicConfigFile(file, cfg);
    Check(cfg.enabled && cfg.logLevel == LogLevel::Info,
          "config: keys outside [Music] are ignored");
    DeleteFileW(file.c_str());
}

bool AllBindings(const rr95::music::Decisions& decisions, rr95::music::Binding expected) {
    using rr95::music::Capability;
    for (int i = 0; i < static_cast<int>(Capability::Count); ++i) {
        if (rr95::music::BindingFor(decisions, static_cast<Capability>(i)) != expected) {
            return false;
        }
    }
    return true;
}

void TestBindingDecision() {
    using namespace rr95::music;

    BindingInputs in;
    in.musicEnabled = false;
    in.exeSupported = true;
    in.apiAvailable = true;
    in.hookState = RR95_HOOK_STATE_ACTIVE;
    in.menuState = RR95_MENU_STATE_APPLIED;
    in.itemTextDetourInstalled = true;
    Check(AllBindings(DecideBindings(in), Binding::Disabled),
          "binding: music disabled -> every capability disabled");
    Check(!HasPendingDecision(DecideBindings(in)), "binding: disabled never waits");

    in = BindingInputs{};
    in.exeSupported = false;
    in.apiAvailable = true;
    in.hookState = RR95_HOOK_STATE_ACTIVE;
    Check(AllBindings(DecideBindings(in), Binding::Disabled),
          "binding: unsupported exe -> every capability disabled");

    in = BindingInputs{};
    in.exeSupported = true;
    in.apiAvailable = false;
    in.hookState = RR95_HOOK_STATE_ACTIVE;
    Check(AllBindings(DecideBindings(in), Binding::OwnHooks),
          "binding: no general mod API -> own hooks for every capability");
    in.hookState = RR95_HOOK_STATE_FAILED;
    Check(AllBindings(DecideBindings(in), Binding::OwnHooks),
          "binding: no API and failed general hooks -> own hooks for every capability");

    in = BindingInputs{};
    in.exeSupported = true;
    in.apiAvailable = true;
    in.hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    const Decisions notInstalled = DecideBindings(in);
    Check(AllBindings(notInstalled, Binding::Wait),
          "binding: general hooks not installed yet -> wait for every capability");
    Check(HasPendingDecision(notInstalled), "binding: waiting is pending a later decision");

    in.hookState = RR95_HOOK_STATE_FAILED;
    Check(AllBindings(DecideBindings(in), Binding::OwnHooks),
          "binding: failed general hooks -> own hooks for every capability");

    in = BindingInputs{};
    in.exeSupported = true;
    in.apiAvailable = true;
    in.hookState = RR95_HOOK_STATE_ACTIVE;
    in.menuState = RR95_MENU_STATE_PENDING;
    in.itemTextDetourInstalled = false;
    Decisions d = DecideBindings(in);
    Check(BindingFor(d, Capability::RaceFrames) == Binding::UseApi &&
              BindingFor(d, Capability::MenuFrames) == Binding::UseApi,
          "binding: active general hooks -> both frame callbacks use the API");
    Check(BindingFor(d, Capability::GraphicsMenuRow) == Binding::UseApi &&
              BindingFor(d, Capability::ItemText) == Binding::UseApi,
          "binding: pending menu relocation -> the API owns the row and the item text");
    Check(BindingFor(d, Capability::OverlayLayer) == Binding::UseApi,
          "binding: active general hooks -> the overlay layer uses the presenter");
    Check(!HasPendingDecision(d), "binding: a pending relocation is not a wait");

    in.menuState = RR95_MENU_STATE_APPLIED;
    in.itemTextDetourInstalled = true;
    d = DecideBindings(in);
    Check(AllBindings(d, Binding::UseApi),
          "binding: applied menu and item text detour -> every capability uses the API");

    in.itemTextDetourInstalled = false;
    d = DecideBindings(in);
    Check(BindingFor(d, Capability::GraphicsMenuRow) == Binding::UseApi &&
              BindingFor(d, Capability::ItemText) == Binding::OwnHooks,
          "binding: applied menu without the detour -> own hooks only for item text");

    for (int state : {static_cast<int>(RR95_MENU_STATE_NOT_USED),
                      static_cast<int>(RR95_MENU_STATE_FAILED)}) {
        in.menuState = state;
        in.itemTextDetourInstalled = false;
        d = DecideBindings(in);
        Check(BindingFor(d, Capability::GraphicsMenuRow) == Binding::OwnHooks &&
                  BindingFor(d, Capability::ItemText) == Binding::OwnHooks,
              "binding: no menu relocation -> own hooks for the row and item text");
        Check(BindingFor(d, Capability::RaceFrames) == Binding::UseApi &&
                  BindingFor(d, Capability::MenuFrames) == Binding::UseApi &&
                  BindingFor(d, Capability::OverlayLayer) == Binding::UseApi,
              "binding: no menu relocation keeps the API for frames and the overlay");
    }

    in.menuState = -1;
    in.itemTextDetourInstalled = false;
    d = DecideBindings(in);
    Check(BindingFor(d, Capability::GraphicsMenuRow) == Binding::Wait &&
              BindingFor(d, Capability::ItemText) == Binding::Wait,
          "binding: an unknown menu state waits for the row and item text");
    Check(BindingFor(d, Capability::RaceFrames) == Binding::UseApi &&
              BindingFor(d, Capability::MenuFrames) == Binding::UseApi &&
              BindingFor(d, Capability::OverlayLayer) == Binding::UseApi,
          "binding: an unknown menu state does not delay frames or the overlay");
    Check(HasPendingDecision(d), "binding: an unknown menu state needs a later decision");

    in.menuState = RR95_MENU_STATE_APPLIED;
    in.itemTextDetourInstalled = true;
    d = DecideBindings(in);
    Check(BindingFor(d, Capability::GraphicsMenuRow) != Binding::OwnHooks &&
              BindingFor(d, Capability::ItemText) != Binding::OwnHooks,
          "binding: never own hooks on a site the general mod holds");

    Check(std::strcmp(BindingToString(Binding::Disabled), "Disabled") == 0 &&
              std::strcmp(BindingToString(Binding::UseApi), "UseApi") == 0 &&
              std::strcmp(BindingToString(Binding::OwnHooks), "OwnHooks") == 0 &&
              std::strcmp(BindingToString(Binding::Wait), "Wait") == 0,
          "binding: every choice has a log name");
    Check(std::strcmp(CapabilityToString(Capability::RaceFrames), "race frames") == 0 &&
              std::strcmp(CapabilityToString(Capability::MenuFrames), "menu frames") == 0 &&
              std::strcmp(CapabilityToString(Capability::GraphicsMenuRow), "graphics menu row") == 0 &&
              std::strcmp(CapabilityToString(Capability::ItemText), "item text") == 0 &&
              std::strcmp(CapabilityToString(Capability::OverlayLayer), "overlay layer") == 0,
          "binding: every capability has a log name");
}

void TestApiTableValidation() {
    using namespace rr95::music;

    Check(!ValidateModApiTable(nullptr).usable &&
              ValidateModApiTable(nullptr).problem == ApiTableProblem::MissingTable,
          "api table: no table is not usable");

    RR95ModApi table = {};
    table.version = RR95_MOD_API_VERSION;
    table.size = static_cast<uint32_t>(sizeof(RR95ModApi));
    table.registerRaceFrameCallback = [](void (*)(void*), void*) -> int32_t { return 0; };
    table.registerMenuFrameCallback = [](void (*)(void*), void*) -> int32_t { return 0; };
    table.getHookState = []() -> int32_t { return RR95_HOOK_STATE_NOT_INSTALLED; };
    table.getMenuState = []() -> int32_t { return RR95_MENU_STATE_UNKNOWN; };
    table.isItemTextDetourInstalled = []() -> int32_t { return 0; };
    Check(ValidateModApiTable(&table).usable &&
              ValidateModApiTable(&table).problem == ApiTableProblem::None,
          "api table: a complete version 1 table is usable");

    // A ddraw.dll from S1 stops before getMenuState and reports that shorter size.
    RR95ModApi older = table;
    older.size = static_cast<uint32_t>(offsetof(RR95ModApi, getMenuState));
    older.getMenuState = nullptr;
    older.isItemTextDetourInstalled = nullptr;
    Check(!ValidateModApiTable(&older).usable &&
              ValidateModApiTable(&older).problem == ApiTableProblem::Size,
          "api table: a shorter table from an older ddraw.dll falls back");

    RR95ModApi wrongVersion = table;
    wrongVersion.version = RR95_MOD_API_VERSION + 1;
    Check(ValidateModApiTable(&wrongVersion).problem == ApiTableProblem::Version,
          "api table: a different version falls back");

    RR95ModApi missing = table;
    missing.registerRaceFrameCallback = nullptr;
    Check(ValidateModApiTable(&missing).problem == ApiTableProblem::MissingRaceFrameCallback,
          "api table: a missing race frame callback falls back");
    missing = table;
    missing.registerMenuFrameCallback = nullptr;
    Check(ValidateModApiTable(&missing).problem == ApiTableProblem::MissingMenuFrameCallback,
          "api table: a missing menu frame callback falls back");
    missing = table;
    missing.getHookState = nullptr;
    Check(ValidateModApiTable(&missing).problem == ApiTableProblem::MissingHookState,
          "api table: a missing hook state function falls back");
    missing = table;
    missing.getMenuState = nullptr;
    Check(ValidateModApiTable(&missing).problem == ApiTableProblem::MissingMenuState,
          "api table: a missing menu state function falls back");
    missing = table;
    missing.isItemTextDetourInstalled = nullptr;
    Check(ValidateModApiTable(&missing).problem == ApiTableProblem::MissingItemTextDetour,
          "api table: a missing item text flag falls back");

    Check(std::strcmp(ApiTableProblemToString(ApiTableProblem::None), "ok") == 0 &&
              std::strcmp(ApiTableProblemToString(ApiTableProblem::Size), "size") == 0,
          "api table: every problem has a log name");
}

void TestBindingRetry() {
    using namespace rr95::music;

    BindingSchedule schedule;
    Check(ShouldRunBindingDecision(schedule, BindingTrigger::DirectSoundCreate),
          "binding retry: the first game DirectSoundCreate runs the decision");
    Check(!ShouldRunBindingDecision(schedule, BindingTrigger::PlayerDetour) &&
              !ShouldRunBindingDecision(schedule, BindingTrigger::FrameCallback),
          "binding retry: an undecided schedule waits for the first game call");
    Check(!ShouldRunBindingDecision(schedule, BindingTrigger::DirectSoundCreate) == false,
          "binding retry: the first game call is the decision point");

    schedule = AfterBindingDecision(schedule, false);
    Check(schedule.decided && !schedule.pending,
          "binding retry: a decided schedule with no Wait is not pending");
    Check(!ShouldRunBindingDecision(schedule, BindingTrigger::DirectSoundCreate) &&
              !ShouldRunBindingDecision(schedule, BindingTrigger::PlayerDetour) &&
              !ShouldRunBindingDecision(schedule, BindingTrigger::FrameCallback),
          "binding retry: a complete decision never runs again");

    schedule = AfterBindingDecision(BindingSchedule{}, true);
    Check(schedule.decided && schedule.pending,
          "binding retry: a Wait decision marks the schedule pending");
    Check(!ShouldRunBindingDecision(schedule, BindingTrigger::DirectSoundCreate),
          "binding retry: Wait does not wait for another DirectSoundCreate");
    Check(ShouldRunBindingDecision(schedule, BindingTrigger::PlayerDetour),
          "binding retry: the first player-only detour call retries the decision");
    Check(ShouldRunBindingDecision(schedule, BindingTrigger::FrameCallback),
          "binding retry: a registered frame callback also retries the decision");

    schedule = AfterBindingDecision(schedule, false);
    Check(schedule.decided && !schedule.pending &&
              !ShouldRunBindingDecision(schedule, BindingTrigger::PlayerDetour),
          "binding retry: a retry that resolves the Wait stops the retries");
}

// S5 fix: the Reproductor row is registered at the game's first DirectDrawCreate, before the general
// mod installs its hooks and builds the relocated graphics menu. The pure decision says when that is
// safe, and the DirectSoundCreate binding must not register the row a second time.
void TestMusicEarlyRow() {
    using namespace rr95::music;

    EarlyRowInputs in;
    in.musicEnabled = true;
    in.exeSupported = true;
    in.apiAvailable = true;
    in.hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    in.menuState = RR95_MENU_STATE_UNKNOWN;
    in.rowRegistered = false;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Register,
          "binding S5: an API whose menu decision has not run registers the row early");
    in.menuState = RR95_MENU_STATE_PENDING;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Register,
          "binding S5: a requested menu relocation registers the row early");
    in.menuState = RR95_MENU_STATE_APPLIED;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Defer,
          "binding S5: a built menu defers to the DirectSoundCreate binding");
    in.menuState = RR95_MENU_STATE_NOT_USED;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Defer,
          "binding S5: a menu the general mod never relocates defers to the in-place fallback");
    in.menuState = RR95_MENU_STATE_FAILED;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Defer,
          "binding S5: a failed relocation defers to the in-place fallback");
    in.menuState = RR95_MENU_STATE_UNKNOWN;
    in.hookState = RR95_HOOK_STATE_FAILED;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Defer,
          "binding S5: a failed general mod never receives an early row");
    in.hookState = RR95_HOOK_STATE_NOT_INSTALLED;
    in.apiAvailable = false;
    in.menuState = -1;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Defer,
          "binding S5: no mod API defers to the DirectSoundCreate fallbacks");
    in.apiAvailable = true;
    in.menuState = RR95_MENU_STATE_UNKNOWN;
    in.rowRegistered = true;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Skip,
          "binding S5: a row registered early is never registered again");
    in.rowRegistered = false;
    in.musicEnabled = false;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Skip,
          "binding S5: music disabled installs no early row");
    in.musicEnabled = true;
    in.exeSupported = false;
    Check(DecideEarlyRowRegistration(in) == EarlyRowAction::Skip,
          "binding S5: an unsupported exe installs no early row");

    // The DirectSoundCreate binding reuses the gate: a registered row is not attempted again.
    const MusicMenuRowGate afterEarly =
        DecideMusicMenuRowGate(MusicMenuRowAction::Register, true, {}, MusicMenuRowFailure::None);
    Check(!afterEarly.attempt && !afterEarly.finalSkip && !afterEarly.logFinal,
          "binding S5: the DirectSoundCreate binding does not register an early row again");

    // NOT_USED after an early registration: the in-place row takes over and the API row stays unused.
    BindingInputs bind;
    bind.musicEnabled = true;
    bind.exeSupported = true;
    bind.apiAvailable = true;
    bind.hookState = RR95_HOOK_STATE_ACTIVE;
    bind.menuState = RR95_MENU_STATE_NOT_USED;
    bind.itemTextDetourInstalled = false;
    const Decisions decisions = DecideBindings(bind);
    Check(ShouldInstallFallbackSite(decisions, Capability::GraphicsMenuRow) &&
              ShouldInstallFallbackSite(decisions, Capability::ItemText),
          "binding S5: NOT_USED after an early row hands the row and item text to the fallback");
    const MusicMenuInPlacePlan inPlace = PlanMusicMenuInPlace(true, true, true, false);
    Check(inPlace.action == MusicMenuInPlaceAction::Install && inPlace.write,
          "binding S5: NOT_USED after an early row installs the in-place row");
    Check(DecideMusicMenuRowOwner(RR95_MENU_STATE_NOT_USED, true, true) ==
              MusicMenuRowOwner::InPlaceRow,
          "binding S5: the in-place row serves a menu the general mod does not relocate");
    Check(DecideMusicMenuRowOwner(RR95_MENU_STATE_NOT_USED, true, false) == MusicMenuRowOwner::None,
          "binding S5: a registered API row is never used when the menu is not relocated");
    Check(DecideMusicMenuRowOwner(RR95_MENU_STATE_PENDING, true, false) == MusicMenuRowOwner::ApiRow &&
              DecideMusicMenuRowOwner(RR95_MENU_STATE_APPLIED, true, false) ==
                  MusicMenuRowOwner::ApiRow,
          "binding S5: pending and applied menus use the early API row");
    Check(DecideMusicMenuRowOwner(RR95_MENU_STATE_NOT_USED, false, false) == MusicMenuRowOwner::None,
          "binding S5: no row exists when neither registration happened");
}

// S5 fix: the loader-lock safe [Music] Enabled probe used by DllMain before the early IAT hook.
void TestMusicEnabledIniProbe() {
    const auto probe = [](const char* text, bool defaultEnabled) {
        return ParseMusicEnabledIni(text, text != nullptr ? std::strlen(text) : 0, defaultEnabled);
    };
    Check(!probe("[Music]\nEnabled=0\n", true), "config S5: Enabled=0 disables the early hook");
    Check(probe("[Music]\nEnabled=1\n", false), "config S5: Enabled=1 enables the early hook");
    Check(!probe("[Music]\nEnabled = 0\r\n", true),
          "config S5: spaces around the value are ignored");
    Check(!probe("[music]\nENABLED=0\n", true),
          "config S5: the section and key names are case-insensitive");
    Check(probe("[Other]\nEnabled=0\n", true),
          "config S5: Enabled outside [Music] is ignored");
    Check(!probe("[Music]\nEnabled=0 ; comment\n", true),
          "config S5: a trailing comment keeps the value");
    Check(probe("[Music]\nEnabled=maybe\n", true) && probe("[Music]\nEnabled=0x10\n", true),
          "config S5: a value that is not a number keeps the default");
    Check(probe("[Music]\nEnabled=2\n", false) && probe("[Music]\nEnabled=-1\n", false),
          "config S5: a nonzero value enables music");
    Check(probe("", true) && probe(nullptr, false) == false,
          "config S5: empty input keeps the requested default");

    const std::wstring file = MakeTestTempPath(L"rr95music-enabled-") + L".ini";
    WriteTextFile(file, "; comment\n[Music]\nEnabled=0\n");
    Check(!ReadMusicEnabledIni(file.c_str(), true),
          "config S5: the file probe reads Enabled=0 from the ini");
    DeleteFileW(file.c_str());
    Check(ReadMusicEnabledIni(file.c_str(), true),
          "config S5: a missing ini keeps the default enabled state");
}

// ---------------------------------------------------------------------------
// Ported in-ddraw music tests (S3a). Names and assertions are preserved; the recorded adaptations
// are: MusicConfig replaces Config{}.music and ParseMusicConfigFile replaces ParseConfigFile, the
// default Enabled assertion flips to 1, and the overlay-hotkey, popup, menu and key-capture tests
// stay in S3b.
// ---------------------------------------------------------------------------

void TestMusicRegistryValues() {
    // Registry values: ModMusicOverlay is REG_BINARY 1 byte (1 pinned, 0 on demand).
    const uint8_t one = 1, zero = 0, two = 2;
    const uint32_t dwOne = 1;
    Check(ParseMusicOverlayRegistryValue(REG_BINARY, &one, 1) == 1 &&
              ParseMusicOverlayRegistryValue(REG_BINARY, &zero, 1) == 0,
          "menu M6a: ModMusicOverlay REG_BINARY 1/0 -> 1/0");
    Check(ParseMusicOverlayRegistryValue(REG_BINARY, &two, 1) == kMusicOverlayRegistryInvalid &&
              ParseMusicOverlayRegistryValue(REG_BINARY, &one, 0) == kMusicOverlayRegistryInvalid &&
              ParseMusicOverlayRegistryValue(REG_DWORD, reinterpret_cast<const uint8_t*>(&dwOne), 4) ==
                  kMusicOverlayRegistryInvalid &&
              ParseMusicOverlayRegistryValue(REG_SZ, &one, 1) == kMusicOverlayRegistryInvalid &&
              ParseMusicOverlayRegistryValue(REG_BINARY, nullptr, 1) == kMusicOverlayRegistryInvalid,
          "menu M6a: other ModMusicOverlay types, sizes or no data -> invalid");
    Check(EncodeMusicOverlayRegistryValue(MusicOverlayMode::Pinned) == 1 &&
              EncodeMusicOverlayRegistryValue(MusicOverlayMode::OnDemand) == 0,
          "menu M6a: the overlay mode encodes as one byte");

    Check(ResolveEffectiveMusicOverlay(MusicOverlayMode::Pinned, 0).mode == MusicOverlayMode::OnDemand &&
              ResolveEffectiveMusicOverlay(MusicOverlayMode::Pinned, 0).fromRegistry &&
              ResolveEffectiveMusicOverlay(MusicOverlayMode::OnDemand, 1).mode == MusicOverlayMode::Pinned &&
              ResolveEffectiveMusicOverlay(MusicOverlayMode::OnDemand, 1).fromRegistry,
          "menu M6a: a valid registry overlay wins over the ini");
    Check(ResolveEffectiveMusicOverlay(MusicOverlayMode::Pinned, kMusicOverlayRegistryAbsent).mode ==
                  MusicOverlayMode::Pinned &&
              !ResolveEffectiveMusicOverlay(MusicOverlayMode::Pinned, kMusicOverlayRegistryAbsent).fromRegistry &&
              ResolveEffectiveMusicOverlay(MusicOverlayMode::OnDemand, kMusicOverlayRegistryInvalid).mode ==
                  MusicOverlayMode::OnDemand &&
              !ResolveEffectiveMusicOverlay(MusicOverlayMode::OnDemand, kMusicOverlayRegistryInvalid).fromRegistry,
          "menu M6a: absent or invalid overlay value uses the ini");

    // ModMusicCassette is REG_SZ with a trailing NUL; a saved label that no longer exists falls
    // back to the first cassette (the built-in Road Rash one).
    const char cassetteValue[] = "Road Rash";
    Check(DecodeMusicCassetteRegistryValue(cassetteValue, sizeof(cassetteValue)) == L"Road Rash",
          "menu M6a: ModMusicCassette REG_SZ decodes to the label");
    Check(DecodeMusicCassetteRegistryValue(cassetteValue, 9).empty() &&
              DecodeMusicCassetteRegistryValue(nullptr, 0).empty(),
          "menu M6a: a REG_SZ without a trailing NUL or no data is invalid");
    const std::string encodedCassette = EncodeMusicCassetteRegistryValue(L"Road Rash");
    Check(encodedCassette.size() == sizeof(cassetteValue) &&
              encodedCassette.compare(0, 9, "Road Rash") == 0 && encodedCassette[9] == '\0',
          "menu M6a: ModMusicCassette encodes as CP-1252 with a trailing NUL");
    Check(EncodeMusicCassetteRegistryValue(L"").empty(),
          "menu M6a: an empty cassette label is not written");
    Check(EncodeMusicCassetteRegistryValue(L"M\u00FAsica") == std::string("M\xFAsica", 6) + '\0',
          "menu M6a: a cassette label with accents encodes in CP-1252");

    const std::vector<std::wstring> labels = {L"Road Rash", L"Musica nueva 1"};
    Check(ResolveSavedMusicCassette(labels, L"Road Rash").index == 0 &&
              !ResolveSavedMusicCassette(labels, L"Road Rash").fallback,
          "menu M6a: the saved Road Rash label selects cassette 0");
    Check(ResolveSavedMusicCassette(labels, L"musica NUEVA 1").index == 1 &&
              !ResolveSavedMusicCassette(labels, L"musica NUEVA 1").fallback,
          "menu M6a: the saved label lookup is case-insensitive");
    Check(ResolveSavedMusicCassette(labels, L"Deleted cassette").index == 0 &&
              ResolveSavedMusicCassette(labels, L"Deleted cassette").fallback,
          "menu M6a: a saved label that no longer exists falls back to the first cassette");
    Check(ResolveSavedMusicCassette(labels, L"").index == 0 &&
              !ResolveSavedMusicCassette(labels, L"").fallback,
          "menu M6a: no saved label is not a fallback");
    Check(ResolveSavedMusicCassette({}, L"Road Rash").fallback,
          "menu M6a: an empty library falls back before the scan finishes");

    // ModMusicKeys: version byte then one virtual key per action in row order.
    MusicKeys keys;
    keys.vk[0] = '2';
    keys.vk[1] = '1';
    keys.vk[2] = '3';
    keys.vk[3] = 0x2D;
    keys.vk[4] = 0x24;
    const std::string blob = EncodeMusicKeysRegistryValue(keys);
    Check(blob.size() == kMusicKeysRegistrySize && blob.size() == 6 &&
              static_cast<uint8_t>(blob[0]) == kMusicKeysRegistryVersion &&
              static_cast<uint8_t>(blob[1]) == '2' && static_cast<uint8_t>(blob[5]) == 0x24,
          "menu M6b: ModMusicKeys is version 1 plus five virtual-key bytes");
    const MusicKeysRegistryValue decoded = DecodeMusicKeysRegistryValue(
        REG_BINARY, reinterpret_cast<const uint8_t*>(blob.data()),
        static_cast<uint32_t>(blob.size()));
    Check(decoded.status == MusicKeysRegistryStatus::Valid &&
              std::memcmp(decoded.keys.vk, keys.vk, sizeof(keys.vk)) == 0,
          "menu M6b: a valid ModMusicKeys blob decodes to the five bindings");
    Check(DecodeMusicKeysRegistryValue(REG_BINARY, nullptr, 0).status ==
              MusicKeysRegistryStatus::Absent,
          "menu M6b: a missing ModMusicKeys value is absent, not invalid");

    uint8_t badSize[8] = {kMusicKeysRegistryVersion, '2', '1', '3', 0x2D, 0x24, 0, 0};
    uint8_t badVersion[6] = {2, '2', '1', '3', 0x2D, 0x24};
    uint8_t zeroKey[6] = {kMusicKeysRegistryVersion, '2', 0, '3', 0x2D, 0x24};
    uint8_t reservedKey[6] = {kMusicKeysRegistryVersion, '2', kMusicKeyEscape, '3', 0x2D, 0x24};
    uint8_t duplicateKey[6] = {kMusicKeysRegistryVersion, '2', '2', '3', 0x2D, 0x24};
    Check(DecodeMusicKeysRegistryValue(REG_SZ, reinterpret_cast<const uint8_t*>(blob.data()),
                                       static_cast<uint32_t>(blob.size())).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, badSize, 5).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, badSize, 7).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, badVersion, 6).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, zeroKey, 6).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, reservedKey, 6).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, duplicateKey, 6).status ==
                  MusicKeysRegistryStatus::Invalid,
          "menu M6b: wrong type or size, unknown version, zero, reserved or duplicate keys are invalid");
    Check(EncodeMusicKeysRegistryValue(MusicKeys{}).empty(),
          "menu M6b: an invalid binding set is never written");
    uint8_t modifierKey[6] = {kMusicKeysRegistryVersion, '2', 0x10, '3', 0x2D, 0x24};
    uint8_t ctrlKey[6] = {kMusicKeysRegistryVersion, '2', '1', 0xA2, 0x2D, 0x24};
    Check(DecodeMusicKeysRegistryValue(REG_BINARY, modifierKey, 6).status ==
                  MusicKeysRegistryStatus::Invalid &&
              DecodeMusicKeysRegistryValue(REG_BINARY, ctrlKey, 6).status ==
                  MusicKeysRegistryStatus::Invalid,
          "menu M6b: a modifier alone (which capture never binds) makes ModMusicKeys invalid");

    const EffectiveMusicKeys fromRegistry =
        ResolveEffectiveMusicKeys(MusicKeys{}, decoded);
    Check(fromRegistry.fromRegistry &&
              std::memcmp(fromRegistry.keys.vk, keys.vk, sizeof(keys.vk)) == 0,
          "menu M6b: a valid ModMusicKeys blob wins over the ini bindings");
    const MusicKeys iniKeys = keys;
    MusicKeysRegistryValue invalidValue;
    invalidValue.status = MusicKeysRegistryStatus::Invalid;
    const EffectiveMusicKeys fromIni = ResolveEffectiveMusicKeys(iniKeys, invalidValue);
    const EffectiveMusicKeys absent =
        ResolveEffectiveMusicKeys(iniKeys, MusicKeysRegistryValue{});
    Check(!fromIni.fromRegistry && std::memcmp(fromIni.keys.vk, iniKeys.vk, sizeof(iniKeys.vk)) == 0 &&
              !absent.fromRegistry && std::memcmp(absent.keys.vk, iniKeys.vk, sizeof(iniKeys.vk)) == 0,
          "menu M6b: absent or invalid ModMusicKeys keeps the ini bindings");
}

void TestMusicConfigAndHotkeys() {
    const MusicConfig defaults;
    Check(defaults.enabled && defaults.path == L"music" && !defaults.shuffle && defaults.volume == 80 &&
              defaults.overlay == MusicOverlayMode::OnDemand && defaults.corner == MusicCorner::BottomLeft,
          "[Music] defaults: Enabled=1, Path=music, Shuffle=0, Volume=80, Overlay=ondemand, Corner=bottomleft");
    const MusicConfig configDefaults = MusicConfig{};
    Check(configDefaults.nextKey.vk == 0x22 && configDefaults.prevKey.vk == 0x21 &&
              configDefaults.playPauseKey.vk == 0x23 && configDefaults.nextCassetteKey.vk == 0x2D &&
              configDefaults.overlayKey.vk == 0x24 && !configDefaults.nextKey.ctrl && !configDefaults.nextKey.shift &&
              !configDefaults.nextKey.alt,
          "[Music] default hotkeys: PageDown, PageUp, End, Insert, Home without modifiers");

    static const struct {
        const wchar_t* name;
        uint32_t vk;
    } kNamedKeys[] = {
        {L"F1", 0x70},   {L"F2", 0x71},   {L"F3", 0x72},   {L"F4", 0x73},
        {L"F5", 0x74},   {L"F6", 0x75},   {L"F7", 0x76},   {L"F8", 0x77},
        {L"F9", 0x78},   {L"F10", 0x79},  {L"F11", 0x7A},  {L"F12", 0x7B},
        {L"Left", 0x25}, {L"Right", 0x27}, {L"Up", 0x26},  {L"Down", 0x28},
        {L"Home", 0x24}, {L"End", 0x23},  {L"PageUp", 0x21}, {L"PageDown", 0x22},
        {L"Insert", 0x2D}, {L"Delete", 0x2E},
        {L"Num0", 0x60}, {L"Num1", 0x61}, {L"Num2", 0x62}, {L"Num3", 0x63}, {L"Num4", 0x64},
        {L"Num5", 0x65}, {L"Num6", 0x66}, {L"Num7", 0x67}, {L"Num8", 0x68}, {L"Num9", 0x69},
        {L"ScrollLock", 0x91}, {L"scroll_lock", 0x91}, {L"scroll", 0x91}, {L"Pause", 0x13},
    };
    for (const auto& named : kNamedKeys) {
        Hotkey parsed;
        char what[128];
        std::snprintf(what, sizeof(what), "ParseHotkey resolves %ls to vk 0x%02X", named.name, named.vk);
        Check(ParseHotkey(named.name, parsed) && parsed.vk == named.vk && !parsed.ctrl && !parsed.shift &&
                  !parsed.alt,
              what);
    }
    for (int i = 0; i < 26; ++i) {
        const wchar_t name[2] = {static_cast<wchar_t>(L'A' + i), 0};
        Hotkey parsed;
        char what[96];
        std::snprintf(what, sizeof(what), "ParseHotkey resolves letter %c", static_cast<char>('A' + i));
        Check(ParseHotkey(name, parsed) && parsed.vk == static_cast<uint32_t>(L'A' + i), what);
    }
    for (int i = 0; i < 10; ++i) {
        const wchar_t name[2] = {static_cast<wchar_t>(L'0' + i), 0};
        Hotkey parsed;
        char what[96];
        std::snprintf(what, sizeof(what), "ParseHotkey resolves digit %c", static_cast<char>('0' + i));
        Check(ParseHotkey(name, parsed) && parsed.vk == static_cast<uint32_t>(L'0' + i), what);
    }

    Hotkey hk;
    Check(ParseHotkey(L"ctrl+shift+alt+f5", hk) && hk.vk == 0x74 && hk.ctrl && hk.shift && hk.alt,
          "ParseHotkey accepts Ctrl+Shift+Alt+F5 in that order");
    Check(ParseHotkey(L"Alt+Ctrl+PageUp", hk) && hk.vk == 0x21 && hk.ctrl && hk.alt && !hk.shift,
          "ParseHotkey accepts modifiers in any order");
    Check(ParseHotkey(L"  Shift + Ctrl + A  ", hk) && hk.vk == L'A' && hk.ctrl && hk.shift && !hk.alt,
          "ParseHotkey trims spaces around modifiers and key");
    Check(ParseHotkey(L"CTRL+num9", hk) && hk.vk == 0x69 && hk.ctrl,
          "ParseHotkey is case-insensitive and accepts numpad names");
    Check(ParseHotkey(L"0x91", hk) && hk.vk == 0x91, "ParseHotkey accepts a hex virtual-key code");
    Check(ParseHotkey(L"145", hk) && hk.vk == 0x91, "ParseHotkey accepts a decimal virtual-key code");

    Hotkey unchanged = {0x5A, false, true, false};
    Check(!ParseHotkey(L"", unchanged) && !ParseHotkey(nullptr, unchanged) && !ParseHotkey(L"Ctrl+", unchanged) &&
              !ParseHotkey(L"+", unchanged) && !ParseHotkey(L"Ctrl+Ctrl+F1", unchanged) &&
              !ParseHotkey(L"F13", unchanged) && !ParseHotkey(L"Page Down", unchanged) &&
              !ParseHotkey(L"Num10", unchanged) && !ParseHotkey(L"Ctrl+A+B", unchanged) &&
              !ParseHotkey(L"0x7Bzz", unchanged),
          "ParseHotkey rejects empty, null, modifier-only, duplicate, unknown and trailing-garbage input");
    Check(unchanged.vk == 0x5A && !unchanged.ctrl && unchanged.shift && !unchanged.alt,
          "ParseHotkey leaves the caller's hotkey untouched on invalid input");

    const Hotkey ctrlF5 = {0x74, true, false, false};
    Check(HotkeyMatches(ctrlF5, true, false, false), "HotkeyMatches accepts the exact modifier state");
    Check(!HotkeyMatches(ctrlF5, false, false, false) && !HotkeyMatches(ctrlF5, true, true, false) &&
              !HotkeyMatches(ctrlF5, true, false, true),
          "HotkeyMatches rejects a missing or extra modifier");
    const Hotkey plain = {0x23, false, false, false};
    Check(HotkeyMatches(plain, false, false, false) && !HotkeyMatches(plain, false, true, false),
          "HotkeyMatches requires modifiers to match exactly");
    Check(!HotkeyMatches(Hotkey{0, false, false, false}, false, false, false),
          "HotkeyMatches never matches an unset key");

    // ParseVirtualKey behaviour is unchanged for the values it already accepted.
    Check(ParseVirtualKey(L"F12", 0x91) == 0x7B && ParseVirtualKey(L" scrolllock ", 0) == 0x91 &&
              ParseVirtualKey(L"0x7Bzz", 0x91) == 0x91 && ParseVirtualKey(L"123", 0) == 123,
          "ParseVirtualKey keeps its existing results next to the new hotkey parser");

    const std::wstring musicIni = MakeTestTempPath(L"rr95_music_cfg_") + L".ini";
    DeleteFileW(musicIni.c_str());
    MusicConfig missing;
    ParseMusicConfigFile(musicIni, missing);
    Check(missing.enabled && missing.path == L"music" && !missing.shuffle &&
              missing.volume == 80 && missing.overlay == MusicOverlayMode::OnDemand &&
              missing.corner == MusicCorner::BottomLeft && missing.nextKey.vk == 0x22,
          "[Music] keys missing from the ini keep their defaults");

    WritePrivateProfileStringW(L"Music", L"Enabled", L"1", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Path", L"my music", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Shuffle", L"1", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Volume", L"73", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Overlay", L"pinned", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Corner", L"topright", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"NextKey", L"Ctrl+PageDown", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"PrevKey", L"F5", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"PlayPauseKey", L"Num9", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"NextCassetteKey", L"Alt+X", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"OverlayKey", L"0x91", musicIni.c_str());
    MusicConfig parsed;
    ParseMusicConfigFile(musicIni, parsed);
    Check(parsed.enabled && parsed.path == L"my music" && parsed.shuffle &&
              parsed.volume == 73 && parsed.overlay == MusicOverlayMode::Pinned &&
              parsed.corner == MusicCorner::TopRight,
          "[Music] Enabled, Path, Shuffle, Volume, Overlay and Corner are parsed from the ini");
    Check(parsed.nextKey.vk == 0x22 && parsed.nextKey.ctrl && !parsed.nextKey.shift &&
              parsed.prevKey.vk == 0x74 && parsed.playPauseKey.vk == 0x69 &&
              parsed.nextCassetteKey.vk == L'X' && parsed.nextCassetteKey.alt &&
              parsed.overlayKey.vk == 0x91,
          "[Music] the five hotkeys are parsed from the ini");

    WritePrivateProfileStringW(L"Music", L"Volume", L"150", musicIni.c_str());
    MusicConfig highVolume;
    ParseMusicConfigFile(musicIni, highVolume);
    Check(highVolume.volume == 100, "[Music] Volume above 100 clamps to 100");
    WritePrivateProfileStringW(L"Music", L"Volume", L"-10", musicIni.c_str());
    MusicConfig lowVolume;
    ParseMusicConfigFile(musicIni, lowVolume);
    Check(lowVolume.volume == 0, "[Music] Volume below 0 clamps to 0");
    WritePrivateProfileStringW(L"Music", L"Volume", L"not a number", musicIni.c_str());
    MusicConfig badVolume;
    ParseMusicConfigFile(musicIni, badVolume);
    Check(badVolume.volume == 80, "[Music] a non-numeric Volume keeps the default 80");

    WritePrivateProfileStringW(L"Music", L"Overlay", L"bogus", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"Corner", L"middle", musicIni.c_str());
    WritePrivateProfileStringW(L"Music", L"NextKey", L"Ctrl+Ctrl+F1", musicIni.c_str());
    MusicConfig invalid;
    ParseMusicConfigFile(musicIni, invalid);
    Check(invalid.overlay == MusicOverlayMode::OnDemand && invalid.corner == MusicCorner::BottomLeft &&
              invalid.nextKey.vk == 0x22 && !invalid.nextKey.ctrl,
          "[Music] invalid Overlay, Corner and hotkey values keep their defaults");
    DeleteFileW(musicIni.c_str());
}

void TestMusicLibraryAndTags() {
    auto putLe32 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    };
    auto putBe24 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>(x & 0xFF));
    };
    auto putBe32 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>(x & 0xFF));
    };
    auto putSynchsafe = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>((x >> 21) & 0x7F));
        v.push_back(static_cast<uint8_t>((x >> 14) & 0x7F));
        v.push_back(static_cast<uint8_t>((x >> 7) & 0x7F));
        v.push_back(static_cast<uint8_t>(x & 0x7F));
    };
    auto appendAscii = [](std::vector<uint8_t>& v, const char* s) {
        while (*s != '\0') {
            v.push_back(static_cast<uint8_t>(*s));
            ++s;
        }
    };
    auto appendBytes = [](std::vector<uint8_t>& v, const std::vector<uint8_t>& bytes) {
        v.insert(v.end(), bytes.begin(), bytes.end());
    };

    const std::vector<std::wstring> files = {
        L"loose.OGG",
        L"Musica nueva 1\\song.mp3",
        L"Musica nueva 1/sub/deep.wav",
        L"zeta\\a.flac",
        L"Alpha\\b.mp3",
        L"Alpha\\c.MP3",
        L"Alpha\\notes.txt",
        L"empty\\readme.txt",
        L"",
        L"lonely",
        L"trailing\\",
    };
    const MusicLibrary library = BuildMusicLibrary(files);
    Check(library.cassettes.size() == 4, "BuildMusicLibrary builds four cassettes and drops the empty one");
    if (library.cassettes.size() == 4) {
        Check(library.cassettes[0].label == L"Alpha" && library.cassettes[0].tracks.size() == 2 &&
                  library.cassettes[1].label == L"music" && library.cassettes[1].tracks.size() == 1 &&
                  library.cassettes[2].label == L"Musica nueva 1" && library.cassettes[2].tracks.size() == 2 &&
                  library.cassettes[3].label == L"zeta" && library.cassettes[3].tracks.size() == 1,
              "cassettes sort case-insensitively and unsupported files are filtered");
        Check(library.cassettes[0].tracks[0].name == L"b" && library.cassettes[0].tracks[1].name == L"c" &&
                  library.cassettes[0].tracks[0].relativePath == L"Alpha\\b.mp3",
              "tracks sort by name case-insensitively");
        Check(library.cassettes[1].tracks[0].name == L"loose" &&
                  library.cassettes[1].tracks[0].relativePath == L"loose.OGG",
              "loose root files form the cassette labelled music");
        Check(library.cassettes[2].tracks[0].name == L"deep" &&
                  library.cassettes[2].tracks[0].relativePath == L"Musica nueva 1\\sub\\deep.wav" &&
                  library.cassettes[2].tracks[1].name == L"song",
              "deeper subfolders belong to their first-level cassette");
    }
    Check(BuildMusicLibrary({}).cassettes.empty(), "BuildMusicLibrary of no paths is empty");
    Check(IsMusicFileExtension(L"a.wav") && IsMusicFileExtension(L"b.MP3") && IsMusicFileExtension(L"c.Ogg") &&
              IsMusicFileExtension(L"d.FLAC") && !IsMusicFileExtension(L"e.txt") &&
              !IsMusicFileExtension(L"noext") && !IsMusicFileExtension(L"f.mp3.bak"),
          "only .wav, .mp3, .ogg and .flac count, case-insensitively");

    const std::vector<int> shuffled = BuildPlayOrder(20, true, 12345u);
    const std::vector<int> shuffledAgain = BuildPlayOrder(20, true, 12345u);
    const std::vector<int> shuffledOther = BuildPlayOrder(20, true, 54321u);
    bool isPermutation = shuffled.size() == 20;
    std::vector<bool> seen(20, false);
    for (int index : shuffled) {
        if (index < 0 || index >= 20 || seen[static_cast<size_t>(index)]) {
            isPermutation = false;
        } else {
            seen[static_cast<size_t>(index)] = true;
        }
    }
    Check(isPermutation, "shuffle order contains every track exactly once");
    Check(shuffled == shuffledAgain, "shuffle order is deterministic for a fixed seed");
    Check(shuffled != shuffledOther, "a different seed gives a different shuffle order");
    Check(BuildPlayOrder(5, false, 99u) == std::vector<int>({0, 1, 2, 3, 4}),
          "Shuffle=0 keeps the name order");
    Check(BuildPlayOrder(0, true, 1u).empty(), "an empty cassette has an empty play order");

    Check(NextPlayPosition(19, 20) == 0 && PreviousPlayPosition(0, 20) == 19 &&
              NextPlayPosition(0, 20) == 1 && PreviousPlayPosition(19, 20) == 18,
          "next/previous track positions wrap in both directions");
    Check(NextPlayPosition(-1, 4) == 0 && PreviousPlayPosition(-1, 4) == 2,
          "negative positions wrap into the order");
    Check(NextPlayPosition(0, 0) == -1 && PreviousPlayPosition(0, 0) == -1,
          "no track has no next or previous position");
    Check(NextCassetteIndex(3, 4) == 0 && NextCassetteIndex(0, 4) == 1 && NextCassetteIndex(0, 0) == -1,
          "next cassette wraps and handles an empty list");

    static const int kExpectedBands[14] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5};
    bool bandsOk = true;
    bool idsOk = true;
    bool pathsOk = true;
    for (int track = 0; track < kRoadRashTrackCount; ++track) {
        bandsOk = bandsOk && RoadRashBandOfTrack(track) == kExpectedBands[track];
        idsOk = idsOk && RoadRashBandStringId(track) == static_cast<uint32_t>(0xE1 + kExpectedBands[track]) &&
                RoadRashTrackStringId(track) == static_cast<uint32_t>(0xF1 + track);
        pathsOk = pathsOk && RoadRashTrackPath(track) ==
                                  std::wstring(L"AUDIO\\MUSIC\\") + kRoadRashTrackFiles[track];
    }
    Check(bandsOk, "Road Rash band of every one of the 14 tracks is 0,0,1,2,2,2,3,3,3,3,4,4,5,5");
    Check(idsOk, "Road Rash string ids are 0xe1 + band and 0xf1 + track");
    Check(pathsOk, "Road Rash track paths point at AUDIO\\MUSIC\\<file>");
    Check(RoadRashBandOfTrack(-1) == -1 && RoadRashBandOfTrack(14) == -1 &&
              RoadRashBandStringId(-1) == 0 && RoadRashTrackStringId(14) == 0 && RoadRashTrackPath(-1).empty(),
          "Road Rash track helpers reject indexes outside 0..13");
    const MusicCassette roadRash = MakeRoadRashCassette();
    Check(roadRash.label == L"Road Rash" && roadRash.builtIn && roadRash.tracks.size() == 14 &&
              roadRash.tracks[0].name == L"HB_Trip" && roadRash.tracks[13].name == L"T_Auto" &&
              roadRash.tracks[0].roadRashIndex == 0 && roadRash.tracks[13].roadRashIndex == 13,
          "the built-in Road Rash cassette holds the 14 game tracks in order");
    Check(ScanMusicFolder(MakeTestTempPath(L"rr95_no_such_music_")).empty(),
          "ScanMusicFolder returns an empty list for a missing folder");

    auto makeFrame = [&](uint8_t major, const char* id, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> frame;
        appendAscii(frame, id);
        if (major == 4) {
            putSynchsafe(frame, static_cast<uint32_t>(payload.size()));
        } else {
            putBe32(frame, static_cast<uint32_t>(payload.size()));
        }
        frame.push_back(0);
        frame.push_back(0);
        appendBytes(frame, payload);
        return frame;
    };
    auto makeId3v2 = [&](uint8_t major, uint8_t flags, const std::vector<std::vector<uint8_t>>& frames,
                         const std::vector<uint8_t>& extra) {
        std::vector<uint8_t> body = extra;
        for (const auto& frame : frames) {
            appendBytes(body, frame);
        }
        std::vector<uint8_t> tag;
        appendAscii(tag, "ID3");
        tag.push_back(major);
        tag.push_back(0);
        tag.push_back(flags);
        putSynchsafe(tag, static_cast<uint32_t>(body.size()));
        appendBytes(tag, body);
        return tag;
    };
    auto utf16LePayload = [](const std::wstring& text) {
        std::vector<uint8_t> payload;
        payload.push_back(1);
        payload.push_back(0xFF);
        payload.push_back(0xFE);
        for (wchar_t c : text) {
            payload.push_back(static_cast<uint8_t>(static_cast<uint32_t>(c) & 0xFF));
            payload.push_back(static_cast<uint8_t>((static_cast<uint32_t>(c) >> 8) & 0xFF));
        }
        return payload;
    };
    auto utf8Payload = [](const char* text) {
        std::vector<uint8_t> payload;
        payload.push_back(3);
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
            payload.push_back(*p);
        }
        return payload;
    };
    auto latin1Payload = [](const char* text) {
        std::vector<uint8_t> payload;
        payload.push_back(0);
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
            payload.push_back(*p);
        }
        return payload;
    };

    const std::vector<uint8_t> tag23 = makeId3v2(
        3, 0,
        {makeFrame(3, "TIT2", utf16LePayload(L"Café")), makeFrame(3, "TPE1", utf16LePayload(L"Ácido"))}, {});
    const MusicTags id3v23 = ParseId3Tags(tag23.data(), tag23.size());
    Check(id3v23.found && id3v23.title == L"Café" && id3v23.artist == L"Ácido",
          "ID3v2.3 reads TIT2 and TPE1 from UTF-16 with BOM");

    const std::vector<uint8_t> tag24 = makeId3v2(
        4, 0,
        {makeFrame(4, "TIT2", utf8Payload("M\xC3\xBAsica")),
         makeFrame(4, "TPE1", utf8Payload("Art\xC3\xADsta"))},
        {});
    const MusicTags id3v24 = ParseId3Tags(tag24.data(), tag24.size());
    Check(id3v24.found && id3v24.title == L"Música" && id3v24.artist == L"Artísta",
          "ID3v2.4 reads UTF-8 text frames with synchsafe frame sizes");

    std::vector<uint8_t> extendedHeader;
    putSynchsafe(extendedHeader, 6);
    extendedHeader.push_back(1);
    extendedHeader.push_back(0);
    const std::vector<uint8_t> tag24ext = makeId3v2(
        4, 0x40, {makeFrame(4, "TIT2", utf8Payload("Con cabecera"))}, extendedHeader);
    const MusicTags id3v24ext = ParseId3Tags(tag24ext.data(), tag24ext.size());
    Check(id3v24ext.found && id3v24ext.title == L"Con cabecera",
          "ID3v2.4 skips a synchsafe extended header safely");

    const std::vector<uint8_t> tagLatin = makeId3v2(
        3, 0,
        {makeFrame(3, "TIT2", latin1Payload("Canci\xF3n")), makeFrame(3, "TPE1", latin1Payload("Banda"))},
        {});
    const MusicTags id3Latin = ParseId3Tags(tagLatin.data(), tagLatin.size());
    Check(id3Latin.found && id3Latin.title == L"Canción" && id3Latin.artist == L"Banda",
          "ID3v2.3 reads ISO-8859-1 text (encoding 0)");

    std::vector<uint8_t> id3v1(128, 0);
    id3v1[0] = 'T';
    id3v1[1] = 'A';
    id3v1[2] = 'G';
    auto putFixed = [](std::vector<uint8_t>& v, size_t offset, const char* text, size_t limit) {
        for (size_t i = 0; i < limit && text[i] != '\0'; ++i) {
            v[offset + i] = static_cast<uint8_t>(text[i]);
        }
    };
    putFixed(id3v1, 3, "Viejo Tema", 30);
    putFixed(id3v1, 33, "Banda V1", 30);
    const MusicTags id3v1Tags = ParseId3Tags(id3v1.data(), id3v1.size());
    Check(id3v1Tags.found && id3v1Tags.title == L"Viejo Tema" && id3v1Tags.artist == L"Banda V1",
          "ID3v1 from the last 128 bytes fills a missing ID3v2 tag");

    Check(!ParseId3Tags(tag23.data(), 18).found && !ParseId3Tags(tag23.data(), 9).found &&
              !ParseId3Tags(nullptr, 0).found,
          "truncated or empty ID3 input is rejected");
    std::vector<uint8_t> lyingId3(10, 0);
    lyingId3[0] = 'I';
    lyingId3[1] = 'D';
    lyingId3[2] = '3';
    lyingId3[3] = 4;
    lyingId3[6] = 0x7F;
    lyingId3[7] = 0x7F;
    lyingId3[8] = 0x7F;
    lyingId3[9] = 0x7F;
    Check(!ParseId3Tags(lyingId3.data(), lyingId3.size()).found,
          "an ID3v2 size larger than the buffer does not read past it");

    auto makeVorbis = [&](std::initializer_list<std::pair<const char*, const char*>> fields) {
        std::vector<uint8_t> block;
        const char vendor[] = "test";
        putLe32(block, 4);
        appendAscii(block, vendor);
        putLe32(block, static_cast<uint32_t>(fields.size()));
        for (const auto& field : fields) {
            const std::string entry = std::string(field.first) + "=" + field.second;
            putLe32(block, static_cast<uint32_t>(entry.size()));
            for (char c : entry) {
                block.push_back(static_cast<uint8_t>(c));
            }
        }
        return block;
    };
    const std::vector<uint8_t> vorbis = makeVorbis(
        {{"title", "M\xC3\xBAsica"}, {"ARTIST", "Art\xC3\xADsta"}});
    const MusicTags vorbisTags = ParseVorbisCommentBlock(vorbis.data(), vorbis.size());
    Check(vorbisTags.found && vorbisTags.title == L"Música" && vorbisTags.artist == L"Artísta",
          "Vorbis comments read TITLE and ARTIST case-insensitively as UTF-8");
    Check(!ParseVorbisCommentBlock(vorbis.data(), 6).found && !ParseVorbisCommentBlock(nullptr, 0).found,
          "a truncated Vorbis comment block is rejected");
    const std::vector<uint8_t> lyingVorbis = {0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};
    Check(!ParseVorbisCommentBlock(lyingVorbis.data(), lyingVorbis.size()).found,
          "a Vorbis vendor length larger than the buffer is rejected");

    auto makeFlac = [&](const std::vector<uint8_t>& comment) {
        std::vector<uint8_t> flac;
        appendAscii(flac, "fLaC");
        flac.push_back(0x00);
        putBe24(flac, 34);
        flac.insert(flac.end(), 34, 0);
        flac.push_back(0x84);
        putBe24(flac, static_cast<uint32_t>(comment.size()));
        appendBytes(flac, comment);
        return flac;
    };
    const std::vector<uint8_t> flac = makeFlac(vorbis);
    std::vector<uint8_t> extracted;
    Check(ExtractFlacVorbisComment(flac.data(), flac.size(), extracted) && extracted == vorbis,
          "FLAC metadata block type 4 holds the Vorbis comment block");
    const MusicTags flacTags = ParseFlacTags(flac.data(), flac.size());
    Check(flacTags.found && flacTags.title == L"Música" && flacTags.artist == L"Artísta",
          "FLAC tags come from the Vorbis comment block");
    Check(!ParseFlacTags(flac.data(), 8).found && !ParseFlacTags(flac.data(), flac.size() - 3).found,
          "truncated FLAC input is rejected");

    auto makeOgg = [&](const std::vector<uint8_t>& comment) {
        std::vector<uint8_t> identification = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0};
        std::vector<uint8_t> commentPacket = {3, 'v', 'o', 'r', 'b', 'i', 's'};
        appendBytes(commentPacket, comment);
        std::vector<uint8_t> page;
        appendAscii(page, "OggS");
        page.push_back(0);
        page.push_back(0);
        for (int i = 0; i < 8; ++i) page.push_back(0);
        for (int i = 0; i < 4; ++i) page.push_back(0);
        for (int i = 0; i < 4; ++i) page.push_back(0);
        for (int i = 0; i < 4; ++i) page.push_back(0);
        page.push_back(2);
        page.push_back(static_cast<uint8_t>(identification.size()));
        page.push_back(static_cast<uint8_t>(commentPacket.size()));
        appendBytes(page, identification);
        appendBytes(page, commentPacket);
        return page;
    };
    const std::vector<uint8_t> ogg = makeOgg(vorbis);
    Check(ExtractOggVorbisComment(ogg.data(), ogg.size(), extracted) && extracted == vorbis,
          "Ogg Vorbis comment packet is found after the identification packet");
    const MusicTags oggTags = ParseOggTags(ogg.data(), ogg.size());
    Check(oggTags.found && oggTags.title == L"Música" && oggTags.artist == L"Artísta",
          "Ogg Vorbis tags come from the second header packet");
    Check(!ParseOggTags(ogg.data(), 40).found && !ParseOggTags(nullptr, 0).found,
          "truncated Ogg input is rejected");

    // R3-tag-prefix-regression-4-8mib: the read plan and the real file reader. A file up to 8 MiB
    // is read whole again (the old 4 MiB cap lost tags between 4 and 8 MiB); a larger file reads
    // the declared ID3v2 body, or a header prefix, plus the ID3v1 suffix parsed on its own.
    {
        const MusicTagReadPlan small = PlanMusicTagRead(4096, 0);
        Check(small.prefixBytes == 4096 && small.suffixBytes == 0,
              "R3 tags: a small file is read whole, so its last 128 bytes stay the ID3v1 tag");

        constexpr size_t kSixMib = 6u * 1024u * 1024u;
        const MusicTagReadPlan aboveFour = PlanMusicTagRead(kSixMib, 0);
        Check(aboveFour.prefixBytes == kSixMib && aboveFour.suffixBytes == 0,
              "R3 tags: a file between 4 and 8 MiB is read whole again, not cut to 4 MiB");

        constexpr size_t kNineMib = 9u * 1024u * 1024u;
        const uint32_t declared = DeclaredId3v2TagBytes(tag23.data(), tag23.size());
        Check(declared == tag23.size(),
              "R3 tags: the declared ID3v2 size includes the 10-byte header");
        const MusicTagReadPlan withTag = PlanMusicTagRead(kNineMib, declared);
        Check(withTag.prefixBytes == declared && withTag.suffixBytes == kMusicTagSuffixBytes,
              "R3 tags: a large ID3v2 file reads the declared tag body plus the ID3v1 suffix");

        const MusicTagReadPlan headerOnly = PlanMusicTagRead(kNineMib, 0);
        Check(headerOnly.prefixBytes == kMusicTagHeaderBytes && headerOnly.suffixBytes == kMusicTagSuffixBytes,
              "R3 tags: a large file without an ID3v2 tag reads a 1 MiB header prefix plus the suffix");

        std::vector<uint8_t> hugeHeader(10, 0);
        hugeHeader[0] = 'I';
        hugeHeader[1] = 'D';
        hugeHeader[2] = '3';
        hugeHeader[3] = 3;
        hugeHeader[6] = 0x7F;
        hugeHeader[7] = 0x7F;
        hugeHeader[8] = 0x7F;
        hugeHeader[9] = 0x7F;
        const MusicTagReadPlan capped = PlanMusicTagRead(32u * 1024u * 1024u,
                                                         DeclaredId3v2TagBytes(hugeHeader.data(), hugeHeader.size()));
        Check(capped.prefixBytes == kMusicTagId3v2MaxBytes && capped.suffixBytes == kMusicTagSuffixBytes,
              "R3 tags: a lying ID3v2 size is capped at 16 MiB");

        const MusicTagReadPlan empty = PlanMusicTagRead(0, 0);
        Check(empty.prefixBytes == 0 && empty.suffixBytes == 0, "R3 tags: an empty file requests no read");

        MusicTags primary;
        primary.title = L"Primary";
        MusicTags fallback;
        fallback.title = L"Fallback";
        fallback.artist = L"Fallback Artist";
        fallback.found = true;
        const MusicTags merged = MergeMusicTags(primary, fallback);
        Check(merged.title == L"Primary" && merged.artist == L"Fallback Artist" && merged.found,
              "R3 tags: the suffix merge keeps the primary title and fills the missing artist");
    }

    // R3-suffix-zero-on-seek-failure: the ID3v1 suffix is read into a buffer sized before the seek,
    // so a failed seek used to leave 128 zero bytes that ParseId3Tags then parsed as a tag. Every
    // failure path must leave the suffix empty.
    {
        const std::wstring suffixPath = MakeTestTempPath(L"rr95_tag_suffix_") + L".mp3";
        std::vector<uint8_t> suffixBytes(300, 0x5A);
        Check(WriteFileBytes(suffixPath, suffixBytes), "R3 tag suffix: the 300-byte test file is written");

        FILE* file = nullptr;
        if (_wfopen_s(&file, suffixPath.c_str(), L"rb") == 0 && file != nullptr) {
            std::vector<uint8_t> suffix;
            const bool read = ReadMusicTagSuffix(file, 300, kMusicTagSuffixBytes, suffix);
            std::fclose(file);
            Check(read && suffix.size() == kMusicTagSuffixBytes && suffix.front() == 0x5A &&
                      suffix.back() == 0x5A,
                  "R3 tag suffix: a successful seek reads the last 128 bytes");
        } else {
            Check(false, "R3 tag suffix: the test file opens for a successful seek");
        }

        FILE* shortFile = nullptr;
        if (_wfopen_s(&shortFile, suffixPath.c_str(), L"rb") == 0 && shortFile != nullptr) {
            std::vector<uint8_t> suffix(64, 0x11);
            const bool read = ReadMusicTagSuffix(shortFile, 100, kMusicTagSuffixBytes, suffix);
            std::fclose(shortFile);
            Check(!read && suffix.empty(),
                  "R3 tag suffix: a suffix that starts before the file leaves the buffer empty");
        } else {
            Check(false, "R3 tag suffix: the test file opens for the short-file case");
        }

        // The seek failure itself: a seek that returns nonzero must clear the suffix, never leave
        // the pre-sized zero buffer that ParseId3Tags would read as a tag.
        FILE* seekFile = nullptr;
        if (_wfopen_s(&seekFile, suffixPath.c_str(), L"rb") == 0 && seekFile != nullptr) {
            std::vector<uint8_t> suffix(kMusicTagSuffixBytes, 0x7F);
            const bool read =
                ReadMusicTagSuffix(seekFile, 300, kMusicTagSuffixBytes, suffix, &FailingMusicTagSeek);
            std::fclose(seekFile);
            Check(!read && suffix.empty(),
                  "R3 tag suffix: a failed seek clears the suffix instead of zero-filling it");
        } else {
            Check(false, "R3 tag suffix: the test file opens for the seek-failure case");
        }
        DeleteFileW(suffixPath.c_str());
    }

    // R3-tag-prefix-regression-4-8mib, real reader: three large MP3 files written to disk so
    // ReadMusicTrackTags, not a hand-cut buffer, exercises the plan.
    {
        const auto writeBytes = [](const std::wstring& path, const std::vector<uint8_t>& bytes) {
            FILE* fp = nullptr;
            if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
                return false;
            }
            const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size();
            std::fclose(fp);
            return ok;
        };

        // 6 MiB, read whole: the ID3v2 tag at the start and the ID3v1 tag at the end both count.
        const std::wstring wholePath = MakeTestTempPath(L"rr95_tags_whole_") + L".mp3";
        constexpr size_t kWholeBytes = 6u * 1024u * 1024u;
        std::vector<uint8_t> whole(kWholeBytes, 0);
        std::memcpy(whole.data(), tag23.data(), tag23.size());
        std::memcpy(whole.data() + kWholeBytes - id3v1.size(), id3v1.data(), id3v1.size());
        Check(writeBytes(wholePath, whole), "R3 tags: the 6 MiB test MP3 is written");
        const MusicTags wholeTags = ReadMusicTrackTags(wholePath);
        Check(wholeTags.found && wholeTags.title == L"Café" && wholeTags.artist == L"Ácido",
              "R3 tags: a 6 MiB file is read whole and keeps its ID3v2 tags");
        _wremove(wholePath.c_str());

        // 9 MiB with a large embedded picture: the declared tag body is read, and the ID3v1 artist
        // at the end must be parsed from the separate suffix, not from the tail of the audio.
        const std::wstring picturePath = MakeTestTempPath(L"rr95_tags_picture_") + L".mp3";
        constexpr size_t kPictureBytes = 9u * 1024u * 1024u;
        std::vector<uint8_t> apicPayload = {0};
        for (const char* p = "image/jpeg"; *p != 0; ++p) {
            apicPayload.push_back(static_cast<uint8_t>(*p));
        }
        apicPayload.push_back(0);
        apicPayload.push_back(3);
        apicPayload.push_back(0);
        apicPayload.insert(apicPayload.end(), 7u * 1024u * 1024u, 0x55);
        const std::vector<uint8_t> pictureTag = makeId3v2(
            3, 0,
            {makeFrame(3, "TIT2", utf16LePayload(L"Café")), makeFrame(3, "APIC", apicPayload)}, {});
        std::vector<uint8_t> picture(kPictureBytes, 0);
        std::memcpy(picture.data(), pictureTag.data(), pictureTag.size());
        std::memcpy(picture.data() + kPictureBytes - id3v1.size(), id3v1.data(), id3v1.size());
        // The tag body ends before the file does, so the suffix read stays separate.
        Check(pictureTag.size() < kPictureBytes - kMusicTagSuffixBytes,
              "R3 tags: the picture tag leaves room for the ID3v1 suffix");
        Check(writeBytes(picturePath, picture), "R3 tags: the 9 MiB test MP3 with a picture is written");
        const MusicTags pictureTags = ReadMusicTrackTags(picturePath);
        Check(pictureTags.found && pictureTags.title == L"Café" && pictureTags.artist == L"Banda V1",
              "R3 tags: a 9 MiB file keeps the ID3v2 title and the ID3v1 artist from the suffix");
        _wremove(picturePath.c_str());

        // 12 MiB with a truncated tag: the declared body ends in the middle of a frame, so only the
        // complete title survives; the ID3v1 artist still comes from the suffix.
        const std::wstring truncatedPath = MakeTestTempPath(L"rr95_tags_truncated_") + L".mp3";
        constexpr size_t kTruncatedBytes = 12u * 1024u * 1024u;
        std::vector<uint8_t> truncatedBody;
        appendBytes(truncatedBody, makeFrame(3, "TIT2", utf16LePayload(L"Café")));
        std::vector<uint8_t> cutFrame = makeFrame(3, "TPE1", utf16LePayload(L"Ácido"));
        cutFrame.resize(15);  // header plus part of the payload: the frame walker must stop here
        appendBytes(truncatedBody, cutFrame);
        std::vector<uint8_t> truncatedTag;
        appendAscii(truncatedTag, "ID3");
        truncatedTag.push_back(3);
        truncatedTag.push_back(0);
        truncatedTag.push_back(0);
        putSynchsafe(truncatedTag, static_cast<uint32_t>(truncatedBody.size()));
        appendBytes(truncatedTag, truncatedBody);
        std::vector<uint8_t> truncated(kTruncatedBytes, 0);
        std::memcpy(truncated.data(), truncatedTag.data(), truncatedTag.size());
        std::memcpy(truncated.data() + kTruncatedBytes - id3v1.size(), id3v1.data(), id3v1.size());
        Check(writeBytes(truncatedPath, truncated), "R3 tags: the 12 MiB test MP3 with a truncated tag is written");
        const MusicTags truncatedTags = ReadMusicTrackTags(truncatedPath);
        Check(truncatedTags.found && truncatedTags.title == L"Café" &&
                  truncatedTags.artist == L"Banda V1",
              "R3 tags: a truncated ID3v2 body keeps the complete frame and the separate ID3v1 suffix");
        _wremove(truncatedPath.c_str());
    }

    auto makeWav = [&](const char* title, const char* artist) {
        std::vector<uint8_t> list;
        appendAscii(list, "INFO");
        auto addInfo = [&](const char* id, const char* text) {
            appendAscii(list, id);
            const size_t len = std::strlen(text) + 1;
            putLe32(list, static_cast<uint32_t>(len));
            appendAscii(list, text);
            list.push_back(0);
            if ((len & 1) != 0) {
                list.push_back(0);
            }
        };
        addInfo("INAM", title);
        addInfo("IART", artist);
        std::vector<uint8_t> wav;
        appendAscii(wav, "RIFF");
        putLe32(wav, 0);
        appendAscii(wav, "WAVE");
        appendAscii(wav, "fmt ");
        putLe32(wav, 16);
        wav.insert(wav.end(), 16, 0);
        appendAscii(wav, "LIST");
        putLe32(wav, static_cast<uint32_t>(list.size()));
        appendBytes(wav, list);
        if ((list.size() & 1u) != 0) {
            wav.push_back(0);
        }
        return wav;
    };
    const std::vector<uint8_t> wav = makeWav("Wav Title", "Wav Artist");
    const MusicTags wavTags = ParseWaveInfoTags(wav.data(), wav.size());
    Check(wavTags.found && wavTags.title == L"Wav Title" && wavTags.artist == L"Wav Artist",
          "RIFF WAVE LIST/INFO reads INAM and IART");
    std::vector<uint8_t> badWav = wav;
    badWav[40] = 0xFF;
    badWav[41] = 0xFF;
    badWav[42] = 0xFF;
    badWav[43] = 0x7F;
    Check(!ParseWaveInfoTags(badWav.data(), badWav.size()).found &&
              !ParseWaveInfoTags(wav.data(), 30).found && !ParseWaveInfoTags(nullptr, 0).found,
          "truncated or lying RIFF chunks are rejected");

    const TrackDisplayInfo shown = TrackDisplay(L"C:\\Music\\My Song.mp3", id3v23);
    Check(shown.title == L"Café" && shown.artist == L"Ácido", "TrackDisplay uses the tags when present");
    const TrackDisplayInfo fallback = TrackDisplay(L"C:\\Music\\My Song.mp3", {});
    Check(fallback.title == L"My Song" && fallback.artist.empty(),
          "TrackDisplay falls back to the file name without extension");
    const TrackDisplayInfo fallback2 = TrackDisplay(L"folder/NoExt", {});
    Check(fallback2.title == L"NoExt" && fallback2.artist.empty(),
          "TrackDisplay handles forward slashes and files without extension");
}

void TestMusicStateMachineAndEngine() {
    // M3: pure music player state machine.
    {
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"Alpha", {L"a1.wav", L"a2.wav", L"a3.wav", L"a4.wav"}));
        cassettes.push_back(TestCassette(L"Beta", {L"b1.mp3", L"b2.mp3", L"b3.mp3"}));

        MusicPlayerStateMachine player;
        Check(player.State() == MusicPlayerState::Stopped && !player.Snapshot().active,
              "music state machine starts stopped and inactive");
        Check(!player.Next() && !player.Previous() && !player.NextCassette() && !player.TogglePause(),
              "a stopped machine rejects transport commands");

        player.Start(cassettes, 0, false, 0);
        MusicPlayerNowPlaying snap = player.Snapshot();
        Check(player.State() == MusicPlayerState::Playing && snap.active && !snap.paused,
              "Start plays the selected cassette");
        Check(snap.cassetteLabel == L"Alpha" && snap.cassetteIndex == 0 && snap.trackIndex == 0 &&
                  snap.title == L"a1",
              "Start exposes the first track in the snapshot");
        Check(snap.changeCounter == 1, "Start increments the change counter once");

        Check(player.Next() && player.Snapshot().trackIndex == 1, "Next moves forward");
        Check(player.Snapshot().changeCounter == 2, "Next increments the change counter");
        player.Next();
        player.Next();
        player.Next();
        Check(player.Snapshot().trackIndex == 0, "Next wraps past the last track");
        Check(player.Next() && player.Snapshot().trackIndex == 1, "Next continues after the wrap");
        Check(player.Previous() && player.Snapshot().trackIndex == 0, "Previous moves backward");
        Check(player.Previous() && player.Snapshot().trackIndex == 3,
              "Previous wraps before the first track");

        const uint32_t beforePause = player.Snapshot().changeCounter;
        Check(player.TogglePause() && player.State() == MusicPlayerState::Paused, "TogglePause pauses");
        Check(player.Snapshot().paused && player.Snapshot().changeCounter == beforePause,
              "pause keeps the counter and the track");
        Check(player.TogglePause() && player.State() == MusicPlayerState::Playing, "TogglePause resumes");
        Check(player.Snapshot().trackIndex == 3 && !player.Snapshot().paused,
              "resume keeps the current track");

        player.RaceEnded();
        Check(player.State() == MusicPlayerState::Paused, "RaceEnded pauses playback");
        const int raceTrack = player.Snapshot().trackIndex;
        const uint32_t raceCounter = player.Snapshot().changeCounter;
        player.RaceStarted();
        Check(player.State() == MusicPlayerState::Playing && player.Snapshot().trackIndex == raceTrack &&
                  player.Snapshot().changeCounter == raceCounter,
              "RaceStarted resumes the current track");

        const uint32_t beforeFinish = player.Snapshot().changeCounter;
        player.TrackFinished();
        Check(player.Snapshot().trackIndex == 0 && player.Snapshot().changeCounter == beforeFinish + 1,
              "TrackFinished advances with wrap and increments the counter");

        Check(player.NextCassette() && player.Snapshot().cassetteIndex == 1, "NextCassette switches cassette");
        snap = player.Snapshot();
        Check(snap.cassetteLabel == L"Beta" && snap.trackIndex == 0 && snap.title == L"b1" &&
                  snap.changeCounter == beforeFinish + 2,
              "NextCassette starts the first track of the next cassette");
        Check(player.NextCassette() && player.Snapshot().cassetteLabel == L"Alpha",
              "NextCassette wraps to the first cassette");

        player.SetTrackDisplay(L"a1.wav", L"Tagged Title", L"Tagged Artist");
        const uint32_t beforeDisplay = player.Snapshot().changeCounter;
        snap = player.Snapshot();
        Check(snap.title == L"Tagged Title" && snap.artist == L"Tagged Artist" &&
                  snap.changeCounter == beforeDisplay,
              "SetTrackDisplay overrides title and artist without a counter change");
        player.SetProgress(1234, 5678);
        Check(player.Snapshot().positionMs == 1234 && player.Snapshot().durationMs == 5678,
              "SetProgress is copied into the snapshot");

        // R3-001 (review): a consumer that refreshes on the change counter must see the engine
        // failure that Stop() represents.
        {
            MusicPlayerStateMachine stopped;
            std::vector<MusicCassette> stopCassettes;
            stopCassettes.push_back(TestCassette(L"Stop", {L"stop1.wav"}));
            stopped.Start(stopCassettes, 0, false, 0);
            const uint32_t playingCounter = stopped.Snapshot().changeCounter;
            stopped.Stop();
            Check(stopped.State() == MusicPlayerState::Stopped &&
                      stopped.Snapshot().changeCounter == playingCounter + 1,
                  "Stop bumps the change counter when it stops a playing machine");
            const uint32_t stoppedCounter = stopped.Snapshot().changeCounter;
            stopped.Stop();
            Check(stopped.State() == MusicPlayerState::Stopped &&
                      stopped.Snapshot().changeCounter == stoppedCounter,
                  "Stop on an already stopped machine keeps the change counter");
        }

        MusicPlayerStateMachine shuffled;
        const uint32_t seed = 0x1234ABCDu;
        shuffled.Start(cassettes, 0, true, seed);
        const std::vector<int> order = BuildPlayOrder(4, true, seed);
        std::vector<int> visited;
        for (int i = 0; i < 4; ++i) {
            visited.push_back(shuffled.Snapshot().trackIndex);
            if (i + 1 < 4) {
                shuffled.Next();
            }
        }
        std::vector<int> covered = visited;
        std::sort(covered.begin(), covered.end());
        Check(visited == order && covered == std::vector<int>({0, 1, 2, 3}),
              "shuffle uses BuildPlayOrder and visits every track");
        Check(shuffled.NextCassette(), "shuffle cassette switch works");
        const std::vector<int> betaOrder = BuildPlayOrder(3, true, seed);
        Check(shuffled.Snapshot().cassetteLabel == L"Beta" && shuffled.Snapshot().trackIndex == betaOrder[0],
              "NextCassette respects the shuffled order");
        shuffled.Next();
        Check(shuffled.Snapshot().trackIndex == betaOrder[1], "the shuffled order continues across tracks");

        MusicPlayerStateMachine failing;
        std::vector<MusicCassette> failingCassettes;
        failingCassettes.push_back(TestCassette(L"Solo", {L"f1.wav", L"f2.wav", L"f3.wav"}));
        failing.Start(failingCassettes, 0, false, 0);
        Check(failing.TakeFailureWarning().empty(), "no failure warning before a track fails");
        failing.TrackFailed();
        Check(failing.Snapshot().trackIndex == 1 && failing.State() == MusicPlayerState::Playing,
              "TrackFailed advances to the next track");
        Check(failing.TakeFailureWarning() == L"f1.wav", "TrackFailed reports the failed file once");
        Check(failing.TakeFailureWarning().empty(), "the failure warning is consumed once");
        failing.TrackFailed();
        Check(failing.Snapshot().trackIndex == 2, "a second failed track advances again");
        Check(failing.TakeFailureWarning() == L"f2.wav", "the second failed file reports once");
        Check(failing.Previous() && failing.Snapshot().trackIndex == 1,
              "Previous can select a previously failed track");
        failing.TrackFailed();
        Check(failing.TakeFailureWarning().empty(), "a file warns only once per session");
        Check(failing.Snapshot().trackIndex == 2 && failing.State() == MusicPlayerState::Playing,
              "TrackFailed skips files that already failed");
        failing.TrackFailed();
        Check(failing.TakeFailureWarning() == L"f3.wav", "the last failed file still reports once");
        Check(failing.State() == MusicPlayerState::Stopped && !failing.Snapshot().active,
              "the machine stops when every track of the cassette failed");
        Check(!failing.Next() && !failing.Previous(), "a stopped machine rejects commands after a full failure");
        failing.RaceStarted();
        Check(failing.State() == MusicPlayerState::Playing && failing.Snapshot().trackIndex == 0,
              "RaceStarted restarts the cassette after a full failure");
        failing.TrackFailed();
        Check(failing.TakeFailureWarning().empty(), "failed flags survive for the whole session");

        MusicPlayerStateMachine noCassettes;
        noCassettes.Start({}, 0, false, 0);
        Check(noCassettes.State() == MusicPlayerState::Stopped && noCassettes.Snapshot().trackIndex == -1,
              "Start with no cassettes stays stopped");
        noCassettes.RaceStarted();
        Check(noCassettes.State() == MusicPlayerState::Stopped, "RaceStarted without cassettes stays stopped");
        noCassettes.RaceEnded();
        Check(noCassettes.State() == MusicPlayerState::Stopped, "RaceEnded without playback stays stopped");

        std::vector<MusicCassette> emptyCassetteList;
        emptyCassetteList.push_back(TestCassette(L"Empty", {}));
        MusicPlayerStateMachine emptyCassette;
        emptyCassette.Start(emptyCassetteList, 0, false, 0);
        Check(emptyCassette.State() == MusicPlayerState::Stopped, "a cassette with no tracks stays stopped");
        MusicPlayerStateMachine badIndex;
        badIndex.Start(cassettes, 9, false, 0);
        Check(badIndex.State() == MusicPlayerState::Stopped, "an invalid cassette index stays stopped");
    }

    // M3: the public engine API honours Enabled=0 and never touches a device in tests.
    {
        MusicConfig disabled;
        disabled.enabled = false;
        MusicPlayerInit(disabled, {});
        MusicPlayerRaceStarted();
        MusicPlayerNext();
        MusicPlayerPrevious();
        MusicPlayerTogglePause();
        MusicPlayerNextCassette();
        MusicPlayerRaceEnded();
        const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
        Check(!snap.active && snap.title.empty() && snap.changeCounter == 0,
              "a disabled music player leaves every API call a no-op");
        MusicPlayerShutdown();

        MusicConfig enabled;
        enabled.enabled = true;
        enabled.volume = 50;
        MusicPlayerInit(enabled, {});
        Check(!MusicPlayerSnapshot().active, "an enabled player with no cassettes is inactive before a race");
        MusicPlayerShutdown();
        MusicPlayerShutdown();
        Check(!MusicPlayerSnapshot().active, "explicit shutdown is idempotent and leaves the player inactive");
    }

    // M3: decoder factory by extension (case-insensitive, .rra maps to WAV).
    {
        // R3-vorbis-int-overflow: stb_vorbis takes an int sample count, so the frame request is
        // bounded by INT_MAX / channels before the multiplication.
        Check(ClampVorbisFrameRequest(1000, 2) == 1000,
              "R3 vorbis: a request that fits the int sample count is unchanged");
        Check(ClampVorbisFrameRequest(0x7FFFFFFFull, 2) == 0x7FFFFFFFull / 2,
              "R3 vorbis: a huge stereo request is clamped to INT_MAX / 2 frames");
        Check(ClampVorbisFrameRequest(0xFFFFFFFFull, 4) == 0x7FFFFFFFull / 4,
              "R3 vorbis: the clamp follows the channel count");
        Check(ClampVorbisFrameRequest(100, 0) == 0,
              "R3 vorbis: zero channels request no samples");

        Check(CreateDecoderForPath(L"C:\\music\\song.wav") != nullptr, ".wav uses a decoder");        Check(CreateDecoderForPath(L"C:\\music\\song.WAV") != nullptr, ".wav is case-insensitive");
        Check(CreateDecoderForPath(L"C:\\music\\track.RrA") != nullptr, ".rra maps to the WAV decoder");
        Check(CreateDecoderForPath(L"C:\\music\\song.mp3") != nullptr, ".mp3 uses a decoder");
        Check(CreateDecoderForPath(L"C:\\music\\song.FLAC") != nullptr, ".flac uses a decoder");
        Check(CreateDecoderForPath(L"C:\\music\\song.Ogg") != nullptr, ".ogg uses a decoder");
        Check(CreateDecoderForPath(L"C:\\music\\cover.jpg") == nullptr, "an unknown extension is rejected");
        Check(CreateDecoderForPath(L"C:\\music\\no_extension") == nullptr, "a path without extension is rejected");
        Check(CreateDecoderForPath(L"dir.with.dot\\song") == nullptr, "dots in folder names are not extensions");
        Check(CreateDecoderForPath(L"") == nullptr, "an empty path is rejected");
        Check(CreateDecoderForPath(L"C:\\music\\song.") == nullptr, "a trailing dot is rejected");
    }

    // M3: decoding small PCM WAV files written by the test (no device, no game folder).
    {
        const std::wstring path8 = TempFilePath(L"pcm8_mono.wav");
        const std::vector<uint8_t> wav8 = MakePcmWav(1, 8000, 8, {128, 255, 0});
        Check(WriteFileBytes(path8, wav8), "the 8-bit mono test WAV was written");
        std::unique_ptr<AudioDecoder> decoder = CreateDecoderForPath(path8);
        Check(decoder != nullptr, "an 8-bit mono WAV gets a decoder");
        if (decoder != nullptr && decoder->Open(path8)) {
            Check(decoder->SampleRate() == 8000 && decoder->Channels() == 1 && decoder->TotalFrames() == 3,
                  "an 8-bit mono WAV reports rate, channels and frames");
            float samples[8] = {};
            Check(decoder->ReadFrames(samples, 8) == 3, "an 8-bit mono WAV decodes every frame");
            // dr_wav maps unsigned 8-bit with (value / 255) * 2 - 1.
            Check(std::fabs(samples[0] - (128.0f / 255.0f * 2.0f - 1.0f)) < 0.0001f &&
                      std::fabs(samples[1] - 1.0f) < 0.0001f && std::fabs(samples[2] + 1.0f) < 0.0001f,
                  "8-bit WAV samples convert to float32");
            Check(decoder->ReadFrames(samples, 8) == 0, "reading past the end returns zero frames");
        } else {
            Check(false, "an 8-bit mono WAV opens");
        }
        decoder.reset();

        const std::wstring path16 = TempFilePath(L"pcm16_stereo.wav");
        const std::vector<uint8_t> wav16 =
            MakePcmWav(2, 44100, 16, {0x00, 0x40, 0x00, 0xC0, 0xFF, 0x7F, 0x00, 0x80});
        Check(WriteFileBytes(path16, wav16), "the 16-bit stereo test WAV was written");
        decoder = CreateDecoderForPath(path16);
        Check(decoder != nullptr, "a 16-bit stereo WAV gets a decoder");
        if (decoder != nullptr && decoder->Open(path16)) {
            Check(decoder->SampleRate() == 44100 && decoder->Channels() == 2 && decoder->TotalFrames() == 2,
                  "a 16-bit stereo WAV reports rate, channels and frames");
            float stereo[8] = {};
            Check(decoder->ReadFrames(stereo, 8) == 2, "a 16-bit stereo WAV decodes every frame");
            Check(std::fabs(stereo[0] - 0.5f) < 0.0001f && std::fabs(stereo[1] + 0.5f) < 0.0001f &&
                      std::fabs(stereo[2] - 32767.0f / 32768.0f) < 0.0001f && std::fabs(stereo[3] + 1.0f) < 0.0001f,
                  "16-bit WAV samples convert to interleaved float32");
        } else {
            Check(false, "a 16-bit stereo WAV opens");
        }
        decoder.reset();

        const std::wstring rraPath = TempFilePath(L"game.RRA");
        Check(WriteFileBytes(rraPath, wav8), "a WAV body written with an .RRA name");
        decoder = CreateDecoderForPath(rraPath);
        Check(decoder != nullptr, ".RRA gets a decoder");
        if (decoder != nullptr && decoder->Open(rraPath)) {
            Check(decoder->Channels() == 1 && decoder->TotalFrames() == 3, ".rra files are decoded through dr_wav");
        } else {
            Check(false, ".rra files are decoded through dr_wav");
        }
        decoder.reset();

        const std::wstring garbagePath = TempFilePath(L"garbage.wav");
        const char* text = "this is not a wave file";
        Check(WriteFileBytes(garbagePath, std::vector<uint8_t>(text, text + std::strlen(text))),
              "the garbage test file was written");
        std::unique_ptr<AudioDecoder> garbage = CreateDecoderForPath(garbagePath);
        Check(garbage != nullptr && !garbage->Open(garbagePath),
              "a garbage .wav fails to open without crashing");
        garbage.reset();

        const std::wstring emptyPath = TempFilePath(L"empty.wav");
        Check(WriteFileBytes(emptyPath, {}), "the empty test file was written");
        std::unique_ptr<AudioDecoder> empty = CreateDecoderForPath(emptyPath);
        Check(empty != nullptr && !empty->Open(emptyPath), "an empty .wav fails to open without crashing");
        empty.reset();

        std::unique_ptr<AudioDecoder> missing = CreateDecoderForPath(TempFilePath(L"missing.wav"));
        Check(missing != nullptr && !missing->Open(TempFilePath(L"missing.wav")),
              "a missing .wav fails to open without crashing");

        DeleteFileW(path8.c_str());
        DeleteFileW(path16.c_str());
        DeleteFileW(rraPath.c_str());
        DeleteFileW(garbagePath.c_str());
        DeleteFileW(emptyPath.c_str());
    }

    // M3 / R3-001: ScanMusicFolder success path on a temporary folder tree.
    {
        const std::wstring root = TempFilePath(L"music_tree");
        RemoveTree(root);
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW((root + L"\\Alpha").c_str(), nullptr);
        CreateDirectoryW((root + L"\\Alpha\\Nested").c_str(), nullptr);
        CreateDirectoryW((root + L"\\Beta").c_str(), nullptr);
        const std::vector<std::wstring> files = {
            root + L"\\root.wav",
            root + L"\\note.txt",
            root + L"\\Alpha\\a1.mp3",
            root + L"\\Alpha\\Nested\\a2.ogg",
            root + L"\\Beta\\b1.flac",
        };
        bool wrote = true;
        for (const std::wstring& file : files) {
            wrote = WriteFileBytes(file, {0x52, 0x49, 0x46, 0x46}) && wrote;
        }
        Check(wrote, "the temporary music tree was written");

        std::vector<std::wstring> found = ScanMusicFolder(root);
        std::sort(found.begin(), found.end());
        const std::vector<std::wstring> expected = {
            L"Alpha\\Nested\\a2.ogg", L"Alpha\\a1.mp3", L"Beta\\b1.flac", L"note.txt", L"root.wav"};
        Check(found == expected, "ScanMusicFolder returns every relative path in the tree");
        Check(ScanMusicFolder(root + L"\\missing").empty(), "ScanMusicFolder returns empty for a missing folder");

        const MusicLibrary library = BuildMusicLibrary(found);
        Check(library.cassettes.size() == 3,
              "BuildMusicLibrary builds one cassette per folder plus the root");
        bool alphaOk = false;
        bool betaOk = false;
        bool musicOk = false;
        for (const MusicCassette& cassette : library.cassettes) {
            if (cassette.label == L"Alpha") {
                alphaOk = cassette.tracks.size() == 2 && cassette.tracks[0].name == L"a1" &&
                          cassette.tracks[1].name == L"a2";
            }
            if (cassette.label == L"Beta") {
                betaOk = cassette.tracks.size() == 1 && cassette.tracks[0].name == L"b1";
            }
            if (cassette.label == L"music") {
                musicOk = cassette.tracks.size() == 1 && cassette.tracks[0].name == L"root";
            }
        }
        Check(alphaOk && betaOk && musicOk && library.cassettes[0].label == L"Alpha" &&
                  library.cassettes[1].label == L"Beta" && library.cassettes[2].label == L"music",
              "BuildMusicLibrary turns the scanned paths into the expected cassettes");
        RemoveTree(root);
    }
}

void TestMusicPlayerReliability() {
    // M3 / R3-001: an engine-level audio failure disables the player for the session.
    {
        const std::wstring logPath = TempFilePath(L"engine_failure.log");
        DeleteFileW(logPath.c_str());
        LogInit(logPath, LogLevel::Debug);

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);

        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"Broken", {L"engine_a.wav", L"engine_b.wav"}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        Check(holder.sink != nullptr && MusicPlayerTestWorkerRunning(),
              "the music worker starts for an enabled player");
        if (holder.sink != nullptr) {
            holder.Get()->failInitialize = true;
        }
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] { return MusicPlayerTestAudioDisabled(); }),
              "an engine-creation failure disables the player");

        const MusicPlayerNowPlaying stopped = MusicPlayerSnapshot();
        Check(!stopped.active && stopped.trackIndex == -1 && stopped.title.empty(),
              "after an engine failure the snapshot reports no active track");
        Check(holder.sink != nullptr && holder.Get()->BeginTrackCalls() == 0,
              "no track is opened after an engine-level failure");

        const uint32_t counterBefore = stopped.changeCounter;
        MusicPlayerNext();
        MusicPlayerPrevious();
        MusicPlayerTogglePause();
        MusicPlayerNextCassette();
        MusicPlayerRaceEnded();
        Sleep(40);
        const MusicPlayerNowPlaying after = MusicPlayerSnapshot();
        Check(!after.active && after.changeCounter == counterBefore,
              "every command after an engine failure is a no-op");
        Check(MusicPlayerTestAudioDisabled(), "an engine failure is terminal for the session");
        Check(MusicPlayerTestCounterPoolSize() == 0u,
              "an engine failure allocates no per-track counter");

        MusicPlayerTestReset();
        LogClose();
        Check(CountLogWarnings(logPath, "music:") == 1,
              "an engine failure logs exactly one warning");
        DeleteFileW(logPath.c_str());
    }

    // M3 / R3-002: a source-voice failure is one track failure, not a session failure.
    {
        const std::wstring logPath = TempFilePath(L"voice_failure.log");
        DeleteFileW(logPath.c_str());
        LogInit(logPath, LogLevel::Debug);

        const std::wstring first = TempFilePath(L"voice_first.wav");
        const std::wstring second = TempFilePath(L"voice_second.wav");
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, {128, 255, 0});
        Check(WriteFileBytes(first, wav) && WriteFileBytes(second, wav),
              "the voice-failure test WAVs were written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"Voices", {first.c_str(), second.c_str()}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        if (holder.sink != nullptr) {
            holder.Get()->failBeginTrackCalls = 1;
        }
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return holder.sink != nullptr && holder.Get()->BeginTrackCalls() >= 2 &&
                         MusicPlayerSnapshot().trackIndex == 1;
              }),
              "a source-voice failure skips that track and plays the next one");
        Check(!MusicPlayerTestAudioDisabled(), "a source-voice failure keeps the player enabled");
        Check(MusicPlayerSnapshot().active, "the player stays active after one source-voice failure");

        MusicPlayerTestReset();
        LogClose();
        Check(CountLogWarnings(logPath, "cannot start the XAudio2 voice") == 1,
              "one source-voice failure logs one warning");
        DeleteFileW(logPath.c_str());
        DeleteFileW(first.c_str());
        DeleteFileW(second.c_str());
    }

    // M3 / R3-002: every track of the cassette failing still stops the player without disabling it.
    {
        const std::wstring logPath = TempFilePath(L"all_voices_fail.log");
        DeleteFileW(logPath.c_str());
        LogInit(logPath, LogLevel::Debug);

        const std::wstring first = TempFilePath(L"all_voice_a.wav");
        const std::wstring second = TempFilePath(L"all_voice_b.wav");
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, {128, 255, 0});
        Check(WriteFileBytes(first, wav) && WriteFileBytes(second, wav),
              "the all-voices-fail test WAVs were written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"Deaf", {first.c_str(), second.c_str()}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        if (holder.sink != nullptr) {
            holder.Get()->failBeginTrackCalls = -1;
        }
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return holder.sink != nullptr && holder.Get()->BeginTrackCalls() >= 2 &&
                         !MusicPlayerSnapshot().active;
              }),
              "when every track of the cassette fails the player stops");
        Check(!MusicPlayerTestAudioDisabled(), "track failures never disable the audio engine");
        MusicPlayerNext();
        Sleep(20);
        Check(!MusicPlayerSnapshot().active, "a player stopped by track failures rejects commands");

        MusicPlayerTestReset();
        LogClose();
        Check(CountLogWarnings(logPath, "cannot start the XAudio2 voice") == 2,
              "each failed file logs one source-voice warning");
        DeleteFileW(logPath.c_str());
        DeleteFileW(first.c_str());
        DeleteFileW(second.c_str());
    }

    // M3 / R3-003: per-track bookkeeping stays bounded however many tracks play.
    {
        const std::wstring path = TempFilePath(L"many_tracks.wav");
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, {128, 255, 0});
        Check(WriteFileBytes(path, wav), "the bounded-bookkeeping test WAV was written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);

        std::vector<MusicTrack> tracks;
        for (int i = 0; i < 1000; ++i) {
            MusicTrack track;
            track.relativePath = path;
            track.name = L"track" + std::to_wstring(i);
            tracks.push_back(std::move(track));
        }
        MusicCassette cassette;
        cassette.label = L"Many";
        cassette.tracks = std::move(tracks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(std::move(cassette));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] { return holder.sink != nullptr && holder.Get()->BeginTrackCalls() >= 1; }),
              "the first of the many tracks opens");

        bool allProcessed = true;
        for (int i = 0; i < 1000; ++i) {
            const uint32_t previousCounter = MusicPlayerSnapshot().changeCounter;
            MusicPlayerNext();
            allProcessed = WaitUntil([&] { return MusicPlayerSnapshot().changeCounter != previousCounter; });
            if (!allProcessed) {
                break;
            }
        }
        Check(allProcessed, "1000 track changes were processed");
        Check(MusicPlayerTestCounterPoolSize() <= 2u,
              "the per-track counter pool stays bounded after 1000 track changes");
        Check(holder.sink != nullptr && holder.Get()->BeginTrackCalls() == 1001,
              "every track change created one source voice");

        MusicPlayerTestReset();
        DeleteFileW(path.c_str());
    }

    // M3 / R3-003 (review): a counter whose buffers are still in flight across a track change is
    // never reused, so a late callback cannot reach a counter that has been handed out again.
    {
        const std::wstring path = TempFilePath(L"inflight_counter.wav");
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, {128, 255, 0});
        Check(WriteFileBytes(path, wav), "the in-flight counter test WAV was written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"InFlight", {path.c_str(), path.c_str(), path.c_str()}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        if (holder.sink != nullptr) {
            holder.Get()->holdBuffersAcrossTracks = true;
        }
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return holder.sink != nullptr && holder.Get()->BeginTrackCalls() >= 1 &&
                         holder.Get()->SubmitCalls() >= 1;
              }),
              "the first track of the in-flight test submits a buffer");

        MusicAudioCounter* firstCounter = holder.Get()->LastBeginCounter();
        Check(firstCounter != nullptr && firstCounter->inFlight.load() > 0,
              "the first track has a buffer in flight");

        MusicPlayerNext();
        Check(WaitUntil([&] { return holder.Get()->BeginTrackCalls() >= 2; }),
              "the track change opens the second track");
        Check(holder.Get()->LastBeginCounter() != firstCounter &&
                  MusicPlayerTestCounterPoolSize() == 2u,
              "a counter with a buffer in flight is not reused and the pool grows by one");
        Check(firstCounter->inFlight.load() > 0,
              "the flushed buffer is still in flight when the next track starts");

        holder.Get()->ReleasePending();
        Check(WaitUntil([&] { return firstCounter->inFlight.load() == 0; }),
              "releasing the buffers completes the in-flight callback");

        MusicPlayerNext();
        Check(WaitUntil([&] { return holder.Get()->BeginTrackCalls() >= 3; }),
              "the track change after the callback opens the third track");
        Check(holder.Get()->LastBeginCounter() == firstCounter,
              "the completed counter is reused once its buffer has finished");
        Check(MusicPlayerTestCounterPoolSize() == 2u,
              "the counter pool does not grow again once a counter is free");

        MusicPlayerTestReset();
        DeleteFileW(path.c_str());
    }

    // M3 / R3-004: a wake-event creation failure leaves the player disabled and starts no worker.
    {
        const std::wstring logPath = TempFilePath(L"wake_event_failure.log");
        DeleteFileW(logPath.c_str());
        LogInit(logPath, LogLevel::Debug);

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.createWakeEvent = FailWakeEvent;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"NoWake", {L"wake_a.wav", L"wake_b.wav"}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        Check(!MusicPlayerTestWorkerRunning(), "no worker starts when the wake event cannot be created");
        Check(MusicPlayerTestAudioDisabled(), "a wake-event failure keeps the player disabled");

        const uint32_t counterBefore = MusicPlayerSnapshot().changeCounter;
        MusicPlayerRaceStarted();
        MusicPlayerNext();
        MusicPlayerRaceEnded();
        Sleep(20);
        const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
        Check(!snap.active && snap.changeCounter == counterBefore,
              "commands are no-ops when no worker could start");
        Check(holder.sink != nullptr && holder.Get()->initializeCalls == 0,
              "no audio device is created without a worker");

        MusicPlayerTestReset();
        LogClose();
        Check(CountLogWarnings(logPath, "wake event") == 1,
              "the wake-event failure logs exactly one warning");
        DeleteFileW(logPath.c_str());
    }

    // M5 / R3-003: the RaceStartedAtTrack worker branch resumes or restarts the built-in cassette
    // depending on the saved pick.
    {
        const std::wstring first = TempFilePath(L"race_pick_a.wav");
        const std::wstring second = TempFilePath(L"race_pick_b.wav");
        const std::wstring third = TempFilePath(L"race_pick_c.wav");
        const std::vector<uint8_t> samples(8000, 128);
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, samples);
        Check(WriteFileBytes(first, wav) && WriteFileBytes(second, wav) && WriteFileBytes(third, wav),
              "the race-pick test WAVs were written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        MusicCassette builtIn =
            TestCassette(L"Road Rash", {first.c_str(), second.c_str(), third.c_str()});
        builtIn.builtIn = true;
        MusicPlayerInit(TestPlayerConfig(), {builtIn});
        MusicPlayerRaceStartedAtTrack(2);
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return holder.sink != nullptr && holder.Get()->SubmitCalls() >= 1 && snap.active &&
                         !snap.paused && snap.trackIndex == 2;
              }),
              "M5 pick: a race start at the saved pick plays that built-in track");
        Check(MusicPlayerSnapshot().playbackStarted,
              "M5 pick: the engine reports playback started after the first submitted buffer");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M5 pick: the race end pauses the built-in track");
        const uint32_t pausedCounter = MusicPlayerSnapshot().changeCounter;
        const int beginsBeforeResume = (holder.sink != nullptr) ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerRaceStartedAtTrack(2);
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return snap.active && !snap.paused;
              }),
              "M5 pick: the next race resumes the player");
        const MusicPlayerNowPlaying resumed = MusicPlayerSnapshot();
        Check(resumed.trackIndex == 2 && resumed.changeCounter == pausedCounter &&
                  holder.sink != nullptr && holder.Get()->BeginTrackCalls() == beginsBeforeResume,
              "M5 pick: the same saved pick resumes the paused built-in track");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M5 pick: the race end pauses the track again");
        const uint32_t beforeRestart = MusicPlayerSnapshot().changeCounter;
        const int beginsBeforeRestart = (holder.sink != nullptr) ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerRaceStartedAtTrack(0);
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return snap.active && !snap.paused && snap.trackIndex == 0;
              }),
              "M5 pick: a different saved pick restarts the built-in cassette at that track");
        Check(MusicPlayerSnapshot().changeCounter == beforeRestart + 1 &&
                  holder.sink != nullptr && holder.Get()->BeginTrackCalls() > beginsBeforeRestart,
              "M5 pick: the restart opens the picked track instead of resuming the old one");

        MusicPlayerTestReset();
        DeleteFileW(first.c_str());
        DeleteFileW(second.c_str());
        DeleteFileW(third.c_str());
    }

    // M6a review R3-001: the built-in cassette (index 0) is a real choice. Once a user cassette has
    // played, selecting index 0 and starting a race must switch back to the built-in cassette at the
    // jukebox pick; selecting the current cassette still only resumes it.
    {
        const std::wstring first = TempFilePath(L"switch_rr_a.wav");
        const std::wstring second = TempFilePath(L"switch_rr_b.wav");
        const std::wstring third = TempFilePath(L"switch_rr_c.wav");
        const std::vector<uint8_t> samples(8000, 128);
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, samples);
        Check(WriteFileBytes(first, wav) && WriteFileBytes(second, wav) && WriteFileBytes(third, wav),
              "the cassette-switch test WAVs were written");

        MusicPlayerTestReset();
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        MusicCassette builtIn =
            TestCassette(L"Road Rash", {first.c_str(), second.c_str(), third.c_str()});
        builtIn.builtIn = true;
        const MusicCassette user = TestCassette(L"User", {first.c_str(), second.c_str(), third.c_str()});
        MusicPlayerInit(TestPlayerConfig(), {builtIn, user});

        MusicPlayerSelectCassette(1);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return holder.sink != nullptr && holder.Get()->SubmitCalls() >= 1 && snap.active && !snap.paused &&
                         snap.cassetteIndex == 1;
              }),
              "M6a R3-001: the first race starts on the selected user cassette");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a R3-001: the race end pauses the user cassette");
        const uint32_t userPausedCounter = MusicPlayerSnapshot().changeCounter;
        const int beginsBeforeSwitch = holder.sink != nullptr ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerSelectCassette(0);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return snap.active && !snap.paused && snap.builtInCassette && snap.cassetteIndex == 0 &&
                         holder.sink != nullptr && holder.Get()->BeginTrackCalls() > beginsBeforeSwitch;
              }),
              "M6a R3-001: selecting index 0 after a user cassette switches to the built-in one");
        Check(MusicPlayerSnapshot().changeCounter == userPausedCounter + 1,
              "M6a R3-001: the switch to the built-in cassette is a real track change");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a R3-001: the race end pauses the built-in cassette again");
        MusicPlayerSelectCassette(1);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return MusicPlayerSnapshot().cassetteIndex == 1 && !MusicPlayerSnapshot().paused;
              }),
              "M6a R3-001: the user cassette can be selected again");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a R3-001: the race end pauses the user cassette for the pick test");
        const int beginsBeforePick = holder.sink != nullptr ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerSelectCassette(0);
        MusicPlayerRaceStartedAtTrack(2);
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return snap.active && !snap.paused && snap.builtInCassette && snap.cassetteIndex == 0 &&
                         snap.trackIndex == 2 && holder.sink != nullptr &&
                         holder.Get()->BeginTrackCalls() > beginsBeforePick;
              }),
              "M6a R3-001: RaceStartedAtTrack switches a paused user cassette to the built-in one at the pick");

        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a R3-001: the race end pauses the picked built-in track");
        const uint32_t pickedCounter = MusicPlayerSnapshot().changeCounter;
        const int beginsBeforeResume = holder.sink != nullptr ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerSelectCassette(0);
        MusicPlayerRaceStartedAtTrack(2);
        Check(WaitUntil([&] {
                  const MusicPlayerNowPlaying snap = MusicPlayerSnapshot();
                  return snap.active && !snap.paused && snap.trackIndex == 2;
              }),
              "M6a R3-001: selecting the current cassette still resumes it");
        Check(MusicPlayerSnapshot().changeCounter == pickedCounter && holder.sink != nullptr &&
                  holder.Get()->BeginTrackCalls() == beginsBeforeResume,
              "M6a R3-001: resuming the current cassette reopens no track");
        MusicPlayerSelectCassette(1);
        MusicPlayerRaceEnded();
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return MusicPlayerSnapshot().cassetteIndex == 1 && !MusicPlayerSnapshot().paused;
              }),
              "M6a R3-001: the user cassette plays before the resume check");
        // M6a fix review R3-001: a cassette reached with the next-cassette hotkey is the new choice,
        // so the next race keeps it instead of going back to the menu selection.
        MusicPlayerNextCassette();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().cassetteIndex == 0; }),
              "M6a hotkey: the next-cassette hotkey wraps from the user cassette to the built-in one");
        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a hotkey: the race end pauses the hotkey cassette");
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] { return !MusicPlayerSnapshot().paused; }) &&
                  MusicPlayerSnapshot().cassetteIndex == 0,
              "M6a hotkey: the next race keeps the cassette chosen with the hotkey");
        Check(MusicPlayerSelectedCassette() == 0,
              "M6a hotkey: the hotkey updates the selected cassette");
        MusicPlayerNextCassette();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().cassetteIndex == 1; }),
              "M6a hotkey: the hotkey wraps back to the user cassette");
        MusicPlayerRaceEnded();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused; }),
              "M6a R3-001: the user cassette pauses for the resume check");
        const uint32_t userCounter = MusicPlayerSnapshot().changeCounter;
        const int beginsBeforeUserResume = holder.sink != nullptr ? holder.Get()->BeginTrackCalls() : 0;
        MusicPlayerSelectCassette(1);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return MusicPlayerSnapshot().cassetteIndex == 1 && !MusicPlayerSnapshot().paused;
              }),
              "M6a R3-001: selecting the current user cassette resumes it");
        Check(MusicPlayerSnapshot().changeCounter == userCounter && holder.sink != nullptr &&
                  holder.Get()->BeginTrackCalls() == beginsBeforeUserResume,
              "M6a R3-001: resuming the current user cassette reopens no track");

        MusicPlayerTestReset();
        DeleteFileW(first.c_str());
        DeleteFileW(second.c_str());
        DeleteFileW(third.c_str());
    }

    // M5b (bug B): the race heartbeat. Every race frame reports the clock; when no report arrives for
    // more than kMusicHeartbeatTimeoutMs while a race is active (the game is frozen in its Esc pause
    // or an end-of-race video) the worker pauses playback on its own and resumes on the next
    // heartbeat. A user pause is a separate flag and survives the gap.
    {
        const std::wstring path = TempFilePath(L"heartbeat.wav");
        const std::vector<uint8_t> samples(8000, 128);
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, samples);
        Check(WriteFileBytes(path, wav), "the heartbeat test WAV was written");

        MusicPlayerTestReset();
        g_fakeNowMs.store(1000);
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.nowMs = FakeNowMs;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        std::vector<MusicCassette> cassettes;
        cassettes.push_back(TestCassette(L"Heartbeat", {path.c_str()}));
        MusicPlayerInit(TestPlayerConfig(), cassettes);
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return holder.sink != nullptr && MusicPlayerTestHeartbeatActive() &&
                         MusicPlayerSnapshot().active && holder.Get()->SubmitCalls() >= 1;
              }),
              "M5b heartbeat: the race start opens the heartbeat window and playback submits audio");
        Check(!MusicPlayerTestAutoPaused(), "M5b heartbeat: a fresh heartbeat never auto-pauses");

        MusicPlayerRaceHeartbeat(g_fakeNowMs.load());
        g_fakeNowMs.store(1400);
        Check(WaitUntil([&] { return MusicPlayerTestAutoPaused() && MusicPlayerSnapshot().paused; }),
              "M5b heartbeat: a 400 ms gap auto-pauses playback");
        Check(holder.Get()->pauseCalls >= 1, "M5b heartbeat: the audio sink is paused");
        const int submitsBeforeGap = holder.Get()->SubmitCalls();
        const uint64_t frozenPosition = MusicPlayerSnapshot().positionMs;
        Sleep(50);
        Check(holder.Get()->SubmitCalls() == submitsBeforeGap &&
                  MusicPlayerSnapshot().positionMs == frozenPosition,
              "M5b heartbeat: no audio is submitted and the shown position freezes while auto-paused");

        g_fakeNowMs.store(1500);
        MusicPlayerRaceHeartbeat(g_fakeNowMs.load());
        Check(WaitUntil([&] { return !MusicPlayerTestAutoPaused() && !MusicPlayerSnapshot().paused; }),
              "M5b heartbeat: the next heartbeat resumes playback");

        MusicPlayerTogglePause();
        Check(WaitUntil([&] { return MusicPlayerSnapshot().paused && !MusicPlayerTestAutoPaused(); }),
              "M5b heartbeat: the play/pause hotkey pauses the track");
        g_fakeNowMs.store(2000);
        Check(WaitUntil([&] { return MusicPlayerTestAutoPaused(); }),
              "M5b heartbeat: a gap also marks the auto-pause flag on a user pause");
        g_fakeNowMs.store(2100);
        MusicPlayerRaceHeartbeat(g_fakeNowMs.load());
        Check(WaitUntil([&] { return !MusicPlayerTestAutoPaused(); }),
              "M5b heartbeat: a heartbeat clears the auto-pause flag");
        Check(MusicPlayerSnapshot().paused,
              "M5b heartbeat: the user pause survives the heartbeat gap and its resume");
        MusicPlayerTogglePause();
        Check(WaitUntil([&] { return !MusicPlayerSnapshot().paused; }),
              "M5b heartbeat: the user resumes the track with the play/pause hotkey");

        g_fakeNowMs.store(3000);
        Check(WaitUntil([&] { return MusicPlayerTestAutoPaused(); }),
              "M5b heartbeat: the player auto-pauses again after a gap");
        MusicPlayerRaceEnded();
        Check(WaitUntil([&] {
                  return !MusicPlayerTestAutoPaused() && !MusicPlayerTestHeartbeatActive();
              }),
              "M5b heartbeat: race end clears the auto-pause and the heartbeat window");
        g_fakeNowMs.store(9000);
        Sleep(50);
        Check(!MusicPlayerTestAutoPaused(), "M5b heartbeat: no auto-pause after the race ended");
        MusicPlayerRaceHeartbeat(g_fakeNowMs.load());
        Sleep(50);
        Check(!MusicPlayerTestAutoPaused() && !MusicPlayerTestHeartbeatActive(),
              "M5b heartbeat: a heartbeat outside a race does not open the window again");

        MusicPlayerTestReset();
        DeleteFileW(path.c_str());
    }

    // M5c (finding E): the worker counts auto-pause lifts, so the race-frame glue can log the player
    // state and the game's stream bytes once when the frames come back.
    {
        const std::wstring path = TempFilePath(L"auto_resume_count.wav");
        const std::vector<uint8_t> samples(8000, 128);
        const std::vector<uint8_t> wav = MakePcmWav(1, 8000, 8, samples);
        Check(WriteFileBytes(path, wav), "the auto-resume count test WAV was written");

        MusicPlayerTestReset();
        g_fakeNowMs.store(1000);
        FakeSinkHolder holder;
        MusicPlayerTestHooks hooks;
        hooks.createSink = CreateFakeAudioSink;
        hooks.nowMs = FakeNowMs;
        hooks.user = &holder;
        MusicPlayerTestSetHooks(hooks);
        MusicPlayerInit(TestPlayerConfig(), {TestCassette(L"Resume", {path.c_str()})});
        MusicPlayerRaceStarted();
        Check(WaitUntil([&] {
                  return holder.sink != nullptr && holder.Get()->SubmitCalls() >= 1 &&
                         MusicPlayerTestHeartbeatActive();
              }),
              "M5c resume: the race start submits audio before the auto-pause count test");

        const uint32_t resumesBefore = MusicPlayerAutoResumeCount();
        g_fakeNowMs.store(1400);
        Check(WaitUntil([&] { return MusicPlayerTestAutoPaused(); }),
              "M5c resume: the heartbeat gap auto-pauses the player");
        Check(MusicPlayerAutoResumeCount() == resumesBefore,
              "M5c resume: an auto-pause alone does not bump the resume count");
        g_fakeNowMs.store(1500);
        MusicPlayerRaceHeartbeat(g_fakeNowMs.load());
        Check(WaitUntil([&] { return !MusicPlayerTestAutoPaused(); }),
              "M5c resume: the next heartbeat lifts the auto-pause");
        Check(WaitUntil([&] { return MusicPlayerAutoResumeCount() == resumesBefore + 1; }),
              "M5c resume: lifting the auto-pause bumps the resume count once");
        Sleep(50);
        Check(MusicPlayerAutoResumeCount() == resumesBefore + 1,
              "M5c resume: the resume count does not grow while the frames keep coming");

        MusicPlayerTestReset();
        DeleteFileW(path.c_str());
    }
}

void TestMusicOverlay() {
    // M4: beeper overlay timing, mode and fade (ported from hook_test).
    {
        MusicOverlayController onDemand(MusicOverlayMode::OnDemand);
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand, "the overlay starts in the configured mode");
        Check(onDemand.OnChange(5, 1000), "the overlay shows on a change counter change");
        Check(!onDemand.OnChange(5, 1200), "the overlay does not show again for the same counter");
        Check(std::fabs(onDemand.Alpha(1000, false) - 1.0f) < 1e-6f &&
                  std::fabs(onDemand.Alpha(5999, false) - 1.0f) < 1e-6f,
              "on-demand alpha is full for 5000 ms");
        Check(std::fabs(onDemand.Alpha(6000, false) - 1.0f) < 1e-6f, "the fade starts at 5000 ms");
        Check(std::fabs(onDemand.Alpha(6250, false) - 0.5f) < 1e-4f,
              "on-demand alpha is 0.5 halfway through the fade");
        Check(onDemand.Alpha(6500, false) == 0.0f && onDemand.Alpha(7000, false) == 0.0f,
              "on-demand alpha reaches 0 at 5500 ms");
        Check(onDemand.OnChange(6, 7000) && std::fabs(onDemand.Alpha(7000, false) - 1.0f) < 1e-6f,
              "a later change restarts the on-demand window");

        // R3-overlay-hotkey-persists-across-races: the hotkey is a per-race show/hide and never
        // rewrites the configured mode. ResetForRace returns the visibility to that mode.
        onDemand.OnOverlayHotkey(8000, true);
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Hidden &&
                  onDemand.Alpha(8000, true) == 0.0f,
              "R3 overlay key: a press while the on-demand window is visible hides it for this race only");
        Check(!onDemand.OnChange(7, 8100) && onDemand.Alpha(8100, true) == 0.0f &&
                  onDemand.Visibility() == MusicOverlayVisibility::Hidden,
              "R3 overlay key: a track change does not bring back a panel hidden for this race");
        onDemand.ResetForRace();
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Configured &&
                  onDemand.OnChange(8, 8100) && std::fabs(onDemand.Alpha(8100, true) - 1.0f) < 1e-6f,
              "R3 overlay key: the next race start shows the configured on-demand behaviour again");
        onDemand.OnOverlayHotkey(8200, true);
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Hidden && onDemand.Alpha(8200, true) == 0.0f,
              "R3 overlay key: a second press inside the flash hides it again");
        onDemand.OnOverlayHotkey(8300, true);
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Shown && onDemand.Alpha(8300, true) == 1.0f &&
                  onDemand.Alpha(8300, false) == 0.0f,
              "R3 overlay key: a press while hidden shows the panel for this race (race only)");
        onDemand.OnOverlayHotkey(8400, true);
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Hidden && onDemand.Alpha(8400, true) == 0.0f &&
                  onDemand.Alpha(9000, true) == 0.0f,
              "R3 overlay key: a press while shown hides the panel and leaves it hidden");
        onDemand.ResetForRace();
        Check(onDemand.Mode() == MusicOverlayMode::OnDemand &&
                  onDemand.Visibility() == MusicOverlayVisibility::Configured &&
                  onDemand.Alpha(9000, true) == 0.0f,
              "R3 overlay key: the reset also clears a panel shown by the hotkey");

        // The configured mode is what the Beeper popup row reports, so the hotkey must never write it.
        MusicOverlayController configuredPinned(MusicOverlayMode::Pinned);
        configuredPinned.OnOverlayHotkey(1000, true);
        Check(configuredPinned.Mode() == MusicOverlayMode::Pinned &&
                  configuredPinned.Visibility() == MusicOverlayVisibility::Hidden &&
                  configuredPinned.Alpha(1000, true) == 0.0f,
              "R3 overlay key: a pinned panel is hidden for the race without changing the configured mode");
        configuredPinned.ResetForRace();
        Check(configuredPinned.Mode() == MusicOverlayMode::Pinned &&
                  configuredPinned.Visibility() == MusicOverlayVisibility::Configured &&
                  configuredPinned.Alpha(2000, true) == 1.0f,
              "R3 overlay key: the next race shows the configured pinned mode again");

        MusicOverlayController pinned(MusicOverlayMode::Pinned);
        Check(pinned.Mode() == MusicOverlayMode::Pinned && pinned.Alpha(0, true) == 1.0f &&
                  pinned.Alpha(0, false) == 0.0f,
              "a pinned overlay is visible only during a race");
        Check(pinned.Alpha(100000, true) == 1.0f, "pinned mode never fades during a race");
    }

    // M5b (bug C): the overlay bitmap is uploaded as DXGI_FORMAT_B8G8R8A8_UNORM, so the packed
    // dword must hold the bytes B, G, R, A in memory order (byte 0 is blue).
    {
        const uint32_t packed = MusicOverlayBgra(10, 20, 30, 40);
        uint8_t bytes[4] = {};
        std::memcpy(bytes, &packed, sizeof(bytes));
        Check(bytes[0] == 30 && bytes[1] == 20 && bytes[2] == 10 && bytes[3] == 40,
              "M5b colour: MusicOverlayBgra packs B, G, R, A in memory order for B8G8R8A8_UNORM");
        Check(packed == 0x280A141Eu, "M5b colour: MusicOverlayBgra(10,20,30,40) is 0x280A141E");
        const uint32_t green = MusicOverlayBgra(0, 255, 0);
        Check(green == 0xFF00FF00u, "M5b colour: pure green is not swapped with blue");
    }

    // M4: beeper overlay placement.
    {
        Check(MusicOverlayMargin(1080) == 21, "the overlay margin is 2% of the client height");
        Check(MusicOverlayMargin(100) == 4, "the overlay margin has a 4-pixel floor");
        Check(MusicOverlayScale(720) == 2 && MusicOverlayScale(1080) == 3 && MusicOverlayScale(2160) == 6,
              "the overlay scale is about 15% of the client height");

        const int margin1080 = MusicOverlayMargin(1080);
        const int full1080W = kMusicOverlayCaseW * 3;
        const int full1080H = kMusicOverlayCaseH * 3;

        const MusicOverlayRect image43{240, 0, 1440, 1080};
        const MusicOverlayPlacement narrowPillarbox =
            ComputeMusicOverlayPlacement(1920, 1080, image43, 0, 0, kMusicOverlayCaseW, kMusicOverlayCaseH,
                                         MusicCorner::BottomLeft);
        Check(narrowPillarbox.draw && narrowPillarbox.scale == 3 && narrowPillarbox.x == 240 + margin1080 &&
                  narrowPillarbox.y == 1080 - margin1080 - full1080H,
              "a 4:3 frame in a 1920x1080 window uses the image corner at full size, not a shrunken pillarbox panel");

        const MusicOverlayRect image43Ultrawide{640, 0, 1440, 1080};
        const MusicOverlayPlacement pillarbox =
            ComputeMusicOverlayPlacement(2560, 1080, image43Ultrawide, 0, 0, kMusicOverlayCaseW,
                                         kMusicOverlayCaseH, MusicCorner::BottomLeft);
        Check(pillarbox.draw && pillarbox.scale == 3 && pillarbox.x == (640 - full1080W) / 2 &&
                  pillarbox.y == 1080 - margin1080 - full1080H,
              "a 4:3 frame in a 2560x1080 window puts the full-size panel in the left pillarbox");

        const MusicOverlayRect imageWide{0, 0, 1920, 1080};
        const MusicOverlayPlacement narrowBand =
            ComputeMusicOverlayPlacement(1920, 1080, imageWide, 240, 1680, kMusicOverlayCaseW,
                                         kMusicOverlayCaseH, MusicCorner::BottomLeft);
        Check(narrowBand.draw && narrowBand.scale == 3 && narrowBand.x == margin1080 &&
                  narrowBand.y == 1080 - margin1080 - full1080H,
              "a 16:9 widescreen race uses the image corner at full size when the side band is too narrow");

        const MusicOverlayRect imageUltrawide{0, 0, 2560, 1080};
        const MusicOverlayPlacement band =
            ComputeMusicOverlayPlacement(2560, 1080, imageUltrawide, 560, 2000, kMusicOverlayCaseW,
                                         kMusicOverlayCaseH, MusicCorner::BottomLeft);
        Check(band.draw && band.scale == 3 && band.x == margin1080 && band.y == 1080 - margin1080 - full1080H &&
                  band.x + full1080W <= 560,
              "an ultrawide widescreen race puts the full-size panel in the left band outside the HUD layer");

        const MusicOverlayRect image1024{0, 0, 1024, 768};
        const int margin768 = MusicOverlayMargin(768);
        const int panelW768 = kMusicOverlayCaseW * MusicOverlayScale(768);
        const int panelH768 = kMusicOverlayCaseH * MusicOverlayScale(768);
        const auto cornerPlacement = [&](MusicCorner corner) {
            return ComputeMusicOverlayPlacement(1024, 768, image1024, 0, 0, kMusicOverlayCaseW, kMusicOverlayCaseH,
                                                corner);
        };
        const MusicOverlayPlacement bottomLeft = cornerPlacement(MusicCorner::BottomLeft);
        const MusicOverlayPlacement topLeft = cornerPlacement(MusicCorner::TopLeft);
        const MusicOverlayPlacement bottomRight = cornerPlacement(MusicCorner::BottomRight);
        const MusicOverlayPlacement topRight = cornerPlacement(MusicCorner::TopRight);
        Check(bottomLeft.draw && bottomLeft.scale == 2 && bottomLeft.x == margin768 &&
                  bottomLeft.y == 768 - margin768 - panelH768,
              "the bottom-left image corner is inset by the margin");
        Check(topLeft.draw && topLeft.x == margin768 && topLeft.y == margin768,
              "the top-left image corner is inset by the margin");
        Check(bottomRight.draw && bottomRight.x == 1024 - margin768 - panelW768 &&
                  bottomRight.y == 768 - margin768 - panelH768,
              "the bottom-right image corner is inset by the margin");
        Check(topRight.draw && topRight.x == 1024 - margin768 - panelW768 && topRight.y == margin768,
              "the top-right image corner is inset by the margin");

        const MusicOverlayRect tiny{0, 0, 120, 80};
        const MusicOverlayPlacement none =
            ComputeMusicOverlayPlacement(120, 80, tiny, 0, 0, kMusicOverlayCaseW, kMusicOverlayCaseH,
                                         MusicCorner::BottomLeft);
        Check(!none.draw, "a window too small for the native panel draws nothing");

        // S3b: the standalone mod hands the panel over in 640x480 game coordinates, so the same
        // rules must place it inside that frame.
        const MusicOverlayPlacement game =
            ComputeMusicOverlayPlacement(640, 480, MusicOverlayRect{0, 0, 640, 480}, 0, 0, kMusicOverlayCaseW,
                                         kMusicOverlayCaseH, MusicCorner::BottomLeft);
        Check(game.draw && game.scale == MusicOverlayScale(480) &&
                  game.x == MusicOverlayMargin(480) && game.y == 480 - MusicOverlayMargin(480) -
                                                               kMusicOverlayCaseH * game.scale,
              "S3b placement: the 640x480 game frame gets a native-scale bottom-left panel");
    }

    // M4: marquee and text width.
    {
        MusicOverlayContent content;
        content.title = L"Song";
        Check(MusicOverlayMarqueeText(content) == L"Song", "row 2 is just the title without an artist");
        content.artist = L"Band";
        Check(MusicOverlayMarqueeText(content) == L"Band - Song", "row 2 is artist - title");

        Check(MusicOverlayTextWidthDots(std::wstring()) == 0 && MusicOverlayTextWidthDots(L"AB") == 11 &&
                  MusicOverlayTextWidthDots(L"\u00E1") == 5,
              "the 5x7 font advances 6 dots per glyph, with accents supported");

        Check(MarqueeOffset(100, kMusicOverlayLcdInnerW, 0) == 0 &&
                  MarqueeOffset(kMusicOverlayLcdInnerW, kMusicOverlayLcdInnerW, 5000) == 0,
              "the marquee does not scroll when the text fits");
        Check(MarqueeOffset(130, kMusicOverlayLcdInnerW, 0) == 0 &&
                  MarqueeOffset(130, kMusicOverlayLcdInnerW, kMusicOverlayMarqueePauseMs) == 0,
              "the marquee pauses at the start");
        Check(MarqueeOffset(130, kMusicOverlayLcdInnerW,
                            kMusicOverlayMarqueePauseMs + kMusicOverlayMarqueeStepMs) == 1,
              "the marquee scrolls one dot per step");
        const int overflow = 130 - kMusicOverlayLcdInnerW;
        const uint64_t endMs = kMusicOverlayMarqueePauseMs + overflow * kMusicOverlayMarqueeStepMs;
        const uint64_t cycle = endMs + kMusicOverlayMarqueePauseMs;
        Check(MarqueeOffset(130, kMusicOverlayLcdInnerW, endMs) == overflow &&
                  MarqueeOffset(130, kMusicOverlayLcdInnerW, cycle - 1) == overflow,
              "the marquee rests at the end");
        Check(MarqueeOffset(130, kMusicOverlayLcdInnerW, cycle) == 0 &&
                  MarqueeOffset(130, kMusicOverlayLcdInnerW, cycle * 3) == 0,
              "the marquee jumps back to the start and repeats");

        // R3-marquee-never-scrolls: the glue must copy the time-based offset into the content the
        // rasterizer draws, and the change counter must move only when that offset moves.
        MusicOverlayContent longContent;
        longContent.title = std::wstring(40, L'x');
        Check(MusicOverlayTextWidthDots(MusicOverlayMarqueeText(longContent)) > kMusicOverlayLcdInnerW,
              "R3 marquee: the long test line is wider than the LCD");
        ApplyMusicOverlayMarqueeOffset(longContent, 0);
        Check(longContent.marqueeOffsetDots == 0, "R3 marquee: the long line starts at offset 0");
        const uint64_t firstStep = kMusicOverlayMarqueePauseMs + kMusicOverlayMarqueeStepMs;
        ApplyMusicOverlayMarqueeOffset(longContent, firstStep);
        Check(longContent.marqueeOffsetDots == 1, "R3 marquee: the long line advances one dot per step");

        MusicOverlayContent shortContent;
        shortContent.title = L"Song";
        bool shortScrolls = false;
        for (uint64_t ms = 0; ms < 5000; ms += 120) {
            ApplyMusicOverlayMarqueeOffset(shortContent, ms);
            shortScrolls = shortScrolls || shortContent.marqueeOffsetDots != 0;
        }
        Check(!shortScrolls, "R3 marquee: a line that fits never scrolls");

        MusicOverlayFrameState marqueeState;
        MusicOverlayContent drawContent = longContent;
        ApplyMusicOverlayMarqueeOffset(drawContent, kMusicOverlayMarqueePauseMs);
        UpdateMusicOverlayFrame(drawContent, 1, 0, 0, kMusicOverlayMarqueePauseMs, 1.0f, marqueeState);
        const uint32_t counterBeforeStep = marqueeState.changeCounter;
        drawContent = longContent;
        ApplyMusicOverlayMarqueeOffset(drawContent, kMusicOverlayMarqueePauseMs + kMusicOverlayMarqueeStepMs);
        UpdateMusicOverlayFrame(drawContent, 1, 0, 0, firstStep, 1.0f, marqueeState);
        Check(marqueeState.changeCounter == counterBeforeStep + 1,
              "R3 marquee: a new marquee dot bumps the change counter once");
        const uint32_t counterAfterStep = marqueeState.changeCounter;
        drawContent = longContent;
        ApplyMusicOverlayMarqueeOffset(drawContent, firstStep + kMusicOverlayMarqueeStepMs / 2);
        UpdateMusicOverlayFrame(drawContent, 1, 0, 0, firstStep + kMusicOverlayMarqueeStepMs / 2, 1.0f,
                                marqueeState);
        Check(marqueeState.changeCounter == counterAfterStep,
              "R3 marquee: a frame between two dots does not bump the change counter");

        // S3b review R3-marquee-glue-fill-unproved: one pure call builds the drawn content and the
        // key from the same time and LCD width, so a long title moves both together.
        MusicOverlayContent marqueeContent;
        marqueeContent.title = std::wstring(40, L'x');
        const uint64_t secondStep = kMusicOverlayMarqueePauseMs + kMusicOverlayMarqueeStepMs;
        const MusicOverlayFrameInput beforeStep =
            BuildMusicOverlayFrameInput(marqueeContent, 1, 0, kMusicOverlayLcdInnerW);
        const MusicOverlayFrameInput afterStep =
            BuildMusicOverlayFrameInput(marqueeContent, 1, secondStep, kMusicOverlayLcdInnerW);
        Check(beforeStep.content.marqueeOffsetDots == 0 && beforeStep.key.marqueeOffsetDots == 0 &&
                  afterStep.content.marqueeOffsetDots == 1 && afterStep.key.marqueeOffsetDots == 1,
              "S3b marquee glue: a long title moves the drawn offset and the key together");
        Check(beforeStep.key == MakeMusicOverlayKey(marqueeContent, 1, 0) &&
                  afterStep.key == MakeMusicOverlayKey(marqueeContent, 1, secondStep),
              "S3b marquee glue: the builder key is the key the frame state compares");
        MusicOverlayContent shortTitle;
        shortTitle.title = L"Song";
        const MusicOverlayFrameInput shortBefore =
            BuildMusicOverlayFrameInput(shortTitle, 1, 0, kMusicOverlayLcdInnerW);
        const MusicOverlayFrameInput shortAfter =
            BuildMusicOverlayFrameInput(shortTitle, 1, secondStep, kMusicOverlayLcdInnerW);
        Check(shortBefore.content.marqueeOffsetDots == 0 && shortBefore.key.marqueeOffsetDots == 0 &&
                  shortAfter.content.marqueeOffsetDots == 0 && shortAfter.key.marqueeOffsetDots == 0,
              "S3b marquee glue: a title that fits keeps both offsets at zero");
    }

    // M4: beeper rasterizer.
    {
        MusicOverlayContent content;
        content.cassetteLabel = L"Road Rash";
        content.artist = L"Band";
        content.title = L"Song";
        content.playing = true;
        content.durationMs = 120000;
        content.positionMs = 30000;

        MusicOverlayBitmap panel;
        Check(RasterizeMusicOverlay(content, 2, panel), "the beeper rasterizer draws a panel");
        Check(panel.width == kMusicOverlayCaseW * 2 && panel.height == kMusicOverlayCaseH * 2 &&
                  panel.pixels.size() == static_cast<size_t>(panel.width) * panel.height,
              "the overlay bitmap is the native panel size times the scale");

        const auto pixelAt = [](const MusicOverlayBitmap& bmp, int nativeX, int nativeY, int scale) {
            const int x = nativeX * scale + scale / 2;
            const int y = nativeY * scale + scale / 2;
            return bmp.pixels[static_cast<size_t>(y) * static_cast<size_t>(bmp.width) + static_cast<size_t>(x)];
        };

        bool uniformBlocks = true;
        for (int y = 0; y < panel.height; y += 2) {
            for (int x = 0; x < panel.width; x += 2) {
                const uint32_t px = panel.pixels[static_cast<size_t>(y) * panel.width + x];
                uniformBlocks = uniformBlocks &&
                                panel.pixels[static_cast<size_t>(y) * panel.width + x + 1] == px &&
                                panel.pixels[static_cast<size_t>(y + 1) * panel.width + x] == px &&
                                panel.pixels[static_cast<size_t>(y + 1) * panel.width + x + 1] == px;
            }
        }
        Check(uniformBlocks, "every LCD pixel is an exact scale x scale block");
        Check(pixelAt(panel, 2, 25, 2) == kMusicOverlayCaseColor,
              "a pixel inside the plastic case has the case colour");
        Check(pixelAt(panel, 12, 17, 2) == kMusicOverlayLcdColor,
              "a pixel inside the LCD has the backlight colour");

        const uint32_t ledOn = pixelAt(panel, kMusicOverlayLedX, kMusicOverlayLedY, 2);
        content.playing = false;
        MusicOverlayBitmap paused;
        Check(RasterizeMusicOverlay(content, 2, paused), "the rasterizer draws a paused panel");
        const uint32_t ledOff = pixelAt(paused, kMusicOverlayLedX, kMusicOverlayLedY, 2);
        Check(ledOn == kMusicOverlayLedOnColor && ledOff == kMusicOverlayLedOffColor && ledOn != ledOff,
              "the LED is lit while playing and off while paused");

        MusicOverlayContent unsupported;
        unsupported.cassetteLabel = L"X";
        unsupported.title = L"A?";
        MusicOverlayContent substitution = unsupported;
        substitution.title = L"A\u4E00";
        MusicOverlayBitmap supportedBitmap;
        MusicOverlayBitmap substitutedBitmap;
        Check(RasterizeMusicOverlay(unsupported, 1, supportedBitmap) &&
                  RasterizeMusicOverlay(substitution, 1, substitutedBitmap) &&
                  supportedBitmap.pixels == substitutedBitmap.pixels,
              "an unsupported character renders like '?'");
    }

    // S3b: the rasterization key, premultiplied alpha and the frame state that keeps the presenter
    // from uploading frames with unchanged pixels.
    {
        MusicOverlayContent content;
        content.cassetteLabel = L"Road Rash";
        content.title = L"Song";
        content.playing = true;
        content.durationMs = 120000;
        content.positionMs = 30000;
        content.changeCounter = 1;

        const MusicOverlayKey key = MakeMusicOverlayKey(content, 2, 0);
        Check(key.changeCounter == 1 && key.playing && key.elapsedSeconds == 30 && key.scale == 2 &&
                  key.filledSegments == 4 && key.marqueeOffsetDots == 0,
              "the overlay key folds the change counter, play state, seconds and progress segments");

        MusicOverlayContent same = content;
        same.positionMs = 30800;  // same whole second and same segments
        Check(MakeMusicOverlayKey(same, 2, 0) == key,
              "a sub-second progress step does not change the overlay key");
        same.positionMs = 31000;
        Check(MakeMusicOverlayKey(same, 2, 0) != key, "the next whole second changes the overlay key");

        MusicOverlayBitmap bitmap;
        RasterizeMusicOverlay(content, 1, bitmap);
        const uint32_t casePixel = bitmap.pixels[static_cast<size_t>(25) * bitmap.width + 2];
        Check((casePixel >> 24) == 0xFFu, "the rasterizer draws opaque straight-alpha pixels");

        PremultiplyMusicOverlayAlpha(bitmap, 0.5f);
        const uint32_t halfPixel = bitmap.pixels[static_cast<size_t>(25) * bitmap.width + 2];
        Check(((halfPixel >> 24) & 0xFFu) == 128u && ((halfPixel >> 16) & 0xFFu) == 22u,
              "R3 alpha: 0.5 premultiplies case red 43 to 22, not 11");

        MusicOverlayBitmap zero;
        RasterizeMusicOverlay(content, 1, zero);
        PremultiplyMusicOverlayAlpha(zero, 0.0f);
        bool allClear = true;
        for (uint32_t px : zero.pixels) {
            allClear = allClear && px == 0;
        }
        Check(allClear, "R3 alpha: a zero fade premultiplies the whole panel to clear");

        // The frame state: the first frame changes, an identical frame does not bump the counter,
        // the fade changes pixels without a key change, and a position move reports a change
        // without an upload.
        MusicOverlayContent frameContent;
        frameContent.cassetteLabel = L"Road Rash";
        frameContent.title = L"Song";
        frameContent.playing = true;
        frameContent.durationMs = 120000;
        MusicOverlayFrameState state;
        Check(UpdateMusicOverlayFrame(frameContent, 1, 10, 20, 0, 1.0f, state),
              "R3 frame: the first frame is a change");
        const uint32_t firstCounter = state.changeCounter;
        Check(!UpdateMusicOverlayFrame(frameContent, 1, 10, 20, 0, 1.0f, state) &&
                  state.changeCounter == firstCounter,
              "R3 frame: an identical frame neither changes nor bumps the counter");
        Check(UpdateMusicOverlayFrame(frameContent, 1, 10, 20, 0, 0.5f, state) &&
                  state.changeCounter == firstCounter + 1,
              "R3 frame: the fade changes pixels and bumps the counter once");
        const uint32_t fadePixel =
            state.bitmap.pixels[static_cast<size_t>(25) * state.bitmap.width + 2];
        Check(((fadePixel >> 24) & 0xFFu) == 128u && ((fadePixel >> 16) & 0xFFu) == 22u,
              "R3 frame: the fade is applied to the straight pixels once, not to the faded result");
        Check(!UpdateMusicOverlayFrame(frameContent, 1, 10, 20, 0, 0.5f, state) &&
                  state.changeCounter == firstCounter + 1,
              "R3 frame: the same fade again does not bump the counter");
        Check(UpdateMusicOverlayFrame(frameContent, 1, 30, 40, 0, 0.5f, state) &&
                  state.changeCounter == firstCounter + 1 && state.x == 30 && state.y == 40,
              "R3 frame: a position move reports a change but no new pixels");
        Check(UpdateMusicOverlayFrame(frameContent, 1, 30, 40, 0, 0.25f, state) &&
                  state.changeCounter == firstCounter + 2,
              "R3 frame: another fade step bumps the counter from the moved frame");
        const uint32_t fadeAgainPixel =
            state.bitmap.pixels[static_cast<size_t>(25) * state.bitmap.width + 2];
        Check(((fadeAgainPixel >> 24) & 0xFFu) == 64u && ((fadeAgainPixel >> 16) & 0xFFu) == 11u,
              "R3 frame: a later fade step still premultiplies the straight pixels once");
    }
}

void TestMusicRaceWiring() {
    // M5: race phase tracker (race and menu present events).
    {
        MusicRacePhaseTracker menuOnly;
        Check(menuOnly.OnMenuPresent() == MusicRaceEvent::None && !menuOnly.InRace(),
              "M5 race phase: menu presents before any race do not start a race");

        MusicRacePhaseTracker tracker;
        Check(tracker.OnRacePresent() == MusicRaceEvent::Started && tracker.InRace(),
              "M5 race phase: the first race present starts the race");
        Check(tracker.OnRacePresent() == MusicRaceEvent::None && tracker.InRace(),
              "M5 race phase: repeated race presents do not restart it");
        Check(tracker.OnMenuPresent() == MusicRaceEvent::None &&
                  tracker.OnMenuPresent() == MusicRaceEvent::None && tracker.InRace(),
              "M5 race phase: one or two menu presents do not end the race");
        Check(tracker.OnMenuPresent() == MusicRaceEvent::Ended && !tracker.InRace(),
              "M5 race phase: three consecutive menu presents end the race");
        Check(tracker.OnMenuPresent() == MusicRaceEvent::None && !tracker.InRace(),
              "M5 race phase: further menu presents do nothing");
        Check(tracker.OnRacePresent() == MusicRaceEvent::Started && tracker.InRace(),
              "M5 race phase: a race after a menu starts again");
        Check(tracker.OnMenuPresent() == MusicRaceEvent::None &&
                  tracker.OnMenuPresent() == MusicRaceEvent::None &&
                  tracker.OnMenuPresent() == MusicRaceEvent::Ended,
              "M5 race phase: menu-race-menu reports start and end once each");
    }

    // S3b review R3-reset-for-race-wiring-unproved: the per-race beeper show/hide ends on the
    // race-start transition itself, so the glue cannot forget the reset.
    {
        MusicRacePhaseTracker race;
        MusicOverlayController overlay(MusicOverlayMode::Pinned);
        overlay.OnOverlayHotkey(1000, true);
        Check(overlay.Visibility() == MusicOverlayVisibility::Hidden && overlay.Alpha(1000, true) == 0.0f,
              "S3b reset for race: the hotkey hides the beeper for the current race");
        const MusicRacePresentCycle start = AdvanceMusicRacePresent(race, overlay);
        Check(start.event == MusicRaceEvent::Started && start.overlayReset &&
                  overlay.Visibility() == MusicOverlayVisibility::Configured &&
                  overlay.Alpha(1000, true) == 1.0f,
              "S3b reset for race: a race-start transition resets the beeper to the configured mode");
        const MusicRacePresentCycle again = AdvanceMusicRacePresent(race, overlay);
        Check(again.event == MusicRaceEvent::None && !again.overlayReset,
              "S3b reset for race: later race frames do not reset it again");
    }

    // M5c (finding C): the game's Esc pause and focus-loss loops set DAT_005cd4d4 (0x005cd4d4)
    // while they run. WM_PAINT after a restore and WM_TIMER id 7 reach the menu present inside
    // those loops, so a menu present with the flag set is not a race end, and a single menu present
    // is never enough: three consecutive presents, with no race present in between, end a race.
    {
        MusicRacePhaseTracker pausing;
        Check(pausing.OnRacePresent() == MusicRaceEvent::Started,
              "M5c false race end: the race starts on the first race present");
        Check(pausing.OnMenuPresent(true) == MusicRaceEvent::None && pausing.InRace(),
              "M5c false race end: a menu present inside the game pause loop is ignored");
        Check(pausing.OnMenuPresent(true) == MusicRaceEvent::None &&
                  pausing.OnMenuPresent(true) == MusicRaceEvent::None && pausing.InRace(),
              "M5c false race end: repeated menu presents inside the pause loop never end the race");
        Check(pausing.OnMenuPresent() == MusicRaceEvent::None && pausing.InRace(),
              "M5c false race end: the presents inside the loop do not count after it ends");
        Check(pausing.OnMenuPresent() == MusicRaceEvent::None && pausing.InRace(),
              "M5c false race end: two menu presents after the loop do not end the race yet");
        Check(pausing.OnMenuPresent() == MusicRaceEvent::Ended && !pausing.InRace(),
              "M5c false race end: the third menu present after the loop ends the race once");

        MusicRacePhaseTracker interrupted;
        interrupted.OnRacePresent();
        Check(interrupted.OnMenuPresent() == MusicRaceEvent::None &&
                  interrupted.OnMenuPresent() == MusicRaceEvent::None,
              "M5c false race end: two menu presents do not end the race");
        Check(interrupted.OnRacePresent() == MusicRaceEvent::None && interrupted.InRace(),
              "M5c false race end: the race present between menu presents keeps the race");
        Check(interrupted.OnMenuPresent() == MusicRaceEvent::None &&
                  interrupted.OnMenuPresent() == MusicRaceEvent::None && interrupted.InRace(),
              "M5c false race end: the menu count restarts after a race present");
        Check(interrupted.OnMenuPresent() == MusicRaceEvent::Ended && !interrupted.InRace(),
              "M5c false race end: the race still ends on three consecutive menu presents");
    }

    // M5 / R3-001: native race-music mute decisions (byte 0x0058e074). The mute is applied only
    // while the player is enabled and playback really started; a later stop or race end restores it.
    {
        MusicMuteState state;
        MusicMutePlan plan = PlanMusicMute(state, true, false, true, 0, true);
        Check(!plan.write && !plan.post && state.originalValue == -1,
              "M5 mute: the first race frame before playback starts does not mute");
        plan = PlanMusicMute(state, true, true, true, 0, true);
        Check(plan.write && plan.value == 1 && plan.post && !plan.restore && !plan.warnInvalid &&
                  state.originalValue == 0,
              "M5 mute: playback start remembers 0, writes 1 and posts the volume update");
        plan = PlanMusicMute(state, true, true, true, 1, true);
        Check(!plan.write && !plan.post && state.originalValue == 0,
              "M5 mute: a later frame while the player plays does not write again");
        plan = PlanMusicMute(state, true, true, false, 1, true);
        Check(plan.write && plan.value == 0 && plan.post && plan.restore &&
                  state.originalValue == -1,
              "M5 mute: race end restores the original 0 and posts again");

        MusicMuteState disabled;
        plan = PlanMusicMute(disabled, true, true, true, 0, true);
        plan = PlanMusicMute(disabled, false, true, true, 1, true);
        Check(plan.write && plan.value == 0 && plan.post && plan.restore,
              "M5 mute: a terminal engine failure during the race restores the original value");

        MusicMuteState stopped;
        plan = PlanMusicMute(stopped, true, true, true, 0, true);
        plan = PlanMusicMute(stopped, true, false, true, 1, true);
        Check(plan.write && plan.value == 0 && plan.post && plan.restore && plan.clearBlock,
              "M5 mute: a player that stops mid-race restores the original value and clears the block");

        // R3-native-block-stuck-midrace: the game changed the mute byte, so PlanMusicMute drops the
        // remembered value without a write. The native-resume block must still be cleared, or the
        // Esc and focus-loss loops cannot resume the game's music after the player stops.
        MusicMuteState changedMidRace;
        plan = PlanMusicMute(changedMidRace, true, true, true, 0, true);
        Check(plan.write && !plan.clearBlock, "R3 native block: applying the mute keeps the block armed");
        plan = PlanMusicMute(changedMidRace, true, false, true, 0, true);
        Check(!plan.write && !plan.restore && plan.clearBlock && changedMidRace.originalValue == -1,
              "R3 native block: a player stop after the game changed the byte clears the block");

        MusicMuteState originalOne;
        plan = PlanMusicMute(originalOne, true, true, true, 1, true);
        Check(plan.write && plan.value == 1 && plan.post && !plan.restore &&
                  originalOne.originalValue == 1,
              "M5 mute: an original 1 is written again and remembered");
        plan = PlanMusicMute(originalOne, true, true, false, 1, true);
        Check(plan.write && plan.value == 1 && plan.restore && plan.post,
              "M5 mute: race end restores the original 1");

        MusicMuteState invalid;
        plan = PlanMusicMute(invalid, true, true, true, 2, true);
        Check(!plan.write && !plan.post && plan.warnInvalid && invalid.originalValue == -1,
              "M5 mute: an invalid original byte is not touched and warns");
        plan = PlanMusicMute(invalid, true, true, true, 7, true);
        Check(!plan.write && !plan.warnInvalid, "M5 mute: the invalid-byte warning is printed once");
        plan = PlanMusicMute(invalid, true, true, false, 7, true);
        Check(!plan.write && !plan.warnInvalid, "M5 mute: nothing to restore after an invalid byte");

        // R3-warninvalid-no-longer-stops-write: a warning means the engine byte is not 0 or 1, so
        // the apply step must refuse the write and the volume post whatever the plan carries. The
        // block clear is a separate concern and is still honoured.
        {
            MusicMutePlan warned;
            warned.warnInvalid = true;
            warned.write = true;
            warned.value = 1;
            warned.post = true;
            const MusicMuteApply warnedApply = PlanMusicMuteApply(warned);
            Check(warnedApply.logInvalid && !warnedApply.write && !warnedApply.post &&
                      warnedApply.value == 0 && !warnedApply.clearBlock,
                  "R3 warnInvalid: a warned plan never writes the byte or posts a volume update");

            MusicMutePlan warnedClear;
            warnedClear.warnInvalid = true;
            warnedClear.write = true;
            warnedClear.value = 0;
            warnedClear.post = true;
            warnedClear.restore = true;
            warnedClear.clearBlock = true;
            const MusicMuteApply warnedClearApply = PlanMusicMuteApply(warnedClear);
            Check(warnedClearApply.logInvalid && !warnedClearApply.write && !warnedClearApply.post &&
                      warnedClearApply.clearBlock,
                  "R3 warnInvalid: the block clear is still honoured while the write is refused");

            MusicMutePlan normal;
            normal.write = true;
            normal.value = 1;
            normal.post = true;
            const MusicMuteApply normalApply = PlanMusicMuteApply(normal);
            Check(!normalApply.logInvalid && normalApply.write && normalApply.post && normalApply.value == 1,
                  "R3 warnInvalid: a plan without a warning keeps its write, value and post");

            MusicMutePlan noPost;
            noPost.write = true;
            noPost.value = 1;
            noPost.post = false;
            const MusicMuteApply noPostApply = PlanMusicMuteApply(noPost);
            Check(noPostApply.write && !noPostApply.post,
                  "R3 warnInvalid: a plan without a post stays without a post");

            // The real path: an invalid byte plans only the warning, and the apply writes nothing.
            MusicMuteState warnedState;
            const MusicMutePlan livePlan = PlanMusicMute(warnedState, true, true, true, 9, true);
            const MusicMuteApply liveApply = PlanMusicMuteApply(livePlan);
            Check(liveApply.logInvalid && !liveApply.write && !liveApply.post,
                  "R3 warnInvalid: an invalid byte through the plan and apply never reaches the engine");
        }

        MusicMuteState changed;
        plan = PlanMusicMute(changed, true, true, true, 0, true);
        plan = PlanMusicMute(changed, true, true, false, 0, true);
        Check(!plan.write && !plan.post && changed.originalValue == -1,
              "M5 mute: restore never overwrites a value somebody else changed");

        MusicMuteState noThread;
        plan = PlanMusicMute(noThread, true, true, true, 0, false);
        Check(plan.write && plan.value == 1 && !plan.post,
              "M5 mute: with no music thread the byte is written but no post is requested");
        plan = PlanMusicMute(noThread, true, true, false, 1, false);
        Check(plan.write && plan.value == 0 && !plan.post,
              "M5 mute: with no music thread the restore happens without a post");

        MusicMuteState noPlayer;
        plan = PlanMusicMute(noPlayer, false, false, true, 0, true);
        Check(!plan.write && !plan.post && noPlayer.originalValue == -1,
              "M5 mute: a disabled player never mutes the game music");
        plan = PlanMusicMute(noPlayer, true, false, false, 0, true);
        Check(!plan.write && !plan.post && noPlayer.originalValue == -1,
              "M5 mute: no race and no playback means no mute");

        // R3-clearblock-after-warninvalid: the glue applies the plan as ordered steps. A plan that
        // both warns about an invalid engine byte and drops the remembered value must still clear
        // the native-resume block; the warning is a log, not a reason to keep the Esc and
        // focus-loss loops from resuming the game's own music.
        MusicMutePlan warnAndDrop;
        warnAndDrop.warnInvalid = true;
        warnAndDrop.clearBlock = true;
        const MusicMuteApply applied = PlanMusicMuteApply(warnAndDrop);
        Check(applied.logInvalid && applied.clearBlock && !applied.write && !applied.post,
              "R3 clear block: a warning plan still clears the native-resume block");

        // A real drop with an invalid byte: the mod muted 0, the game set an invalid 2, and the
        // player stops. PlanMusicMute drops the remembered value without a restore write, and the
        // apply must hand the clear to the glue.
        MusicMuteState dropped;
        PlanMusicMute(dropped, true, true, true, 0, true);
        const MusicMutePlan dropPlan = PlanMusicMute(dropped, true, false, true, 2, true);
        const MusicMuteApply dropApply = PlanMusicMuteApply(dropPlan);
        Check(dropPlan.clearBlock && !dropPlan.write && !dropPlan.warnInvalid,
              "R3 clear block: a drop over an invalid byte clears the block and writes nothing");
        Check(dropApply.clearBlock && !dropApply.write && !dropApply.logInvalid,
              "R3 clear block: applying the drop passes the clear through");
    }

    // M5b (bug A): native MIDI pause and resume decisions. The mute byte alone does not stop a MIDI
    // stream that is already playing, and the game's pause loops resume it without reading the byte.
    {
        Check(ShouldPauseNativeMidi(true, true, true, true, true),
              "M5b MIDI: an enabled player applying its race mute pauses an active native MIDI");
        Check(!ShouldPauseNativeMidi(false, true, true, true, true),
              "M5b MIDI: [Music] Enabled=0 never pauses the native MIDI");
        Check(!ShouldPauseNativeMidi(true, false, true, true, true),
              "M5b MIDI: no pause outside a race");
        Check(!ShouldPauseNativeMidi(true, true, false, true, true),
              "M5b MIDI: no pause until the mute was actually written");
        Check(!ShouldPauseNativeMidi(true, true, true, false, true),
              "M5b MIDI: no pause when the game is not in MIDI mode");
        Check(!ShouldPauseNativeMidi(true, true, true, true, false),
              "R3 MIDI pause: unverified FUN_004459c0 bytes never call the routine");

        Check(ShouldBlockNativeMusicResume(true, true),
              "M5b MIDI: the resume detour skips the original while the mod's mute is in force");
        Check(!ShouldBlockNativeMusicResume(true, false),
              "M5b MIDI: the resume detour calls the original after the mute is restored");
        Check(!ShouldBlockNativeMusicResume(false, true),
              "M5b MIDI: [Music] Enabled=0 always calls the original resume");
        Check(ShouldBlockNativeMusicResume(true, true),
              "M5c stream: the same block also covers the game's stream resume (FUN_00411c00)");
    }

    // M5c (finding B): the race music mode is only known after FUN_004463a0 returns. The race loop
    // presents two frames before that call, so reading DAT_005a6908 on the first race frame always
    // sees 0 and used to report MIDI for a race that really uses the stream.
    {
        Check(DecideMusicNativeMode(true, false) == MusicNativeMode::Stream,
              "M5c mode: the stream flag (0x005a6908) reports the DirectSound stream");
        Check(DecideMusicNativeMode(false, true) == MusicNativeMode::Midi,
              "M5c mode: the MIDI flag (0x006163d3) reports MIDI");
        Check(DecideMusicNativeMode(false, false) == MusicNativeMode::Unknown,
              "M5c mode: neither flag set reports unknown (master mute or no race music)");
        Check(DecideMusicNativeMode(true, true) == MusicNativeMode::Stream,
              "M5c mode: the stream flag wins when both are set");
        Check(std::strcmp(MusicNativeModeName(MusicNativeMode::Stream), "stream") == 0 &&
                  std::strcmp(MusicNativeModeName(MusicNativeMode::Midi), "MIDI") == 0 &&
                  std::strcmp(MusicNativeModeName(MusicNativeMode::Unknown), "unknown") == 0,
              "M5c mode: the once-per-race log names the real mode");
    }

    // M5c (finding E): when race frames come back after an auto-pause, the log says what the player
    // is doing and what the game's stream thread holds in 0x005a6958 and 0x005a6909.
    {
        Check(ClassifyMusicResume(false, false, false) == MusicResumeState::Stopped,
              "M5c resume: an inactive player is reported as stopped");
        Check(ClassifyMusicResume(true, false, false) == MusicResumeState::Playing,
              "M5c resume: an active unpaused player is reported as playing");
        Check(ClassifyMusicResume(true, true, true) == MusicResumeState::AutoPaused,
              "M5c resume: the auto-pause flag is reported as auto-paused");
        Check(ClassifyMusicResume(true, true, false) == MusicResumeState::UserPaused,
              "M5c resume: a pause without the auto-pause flag is a user pause");

        MusicResumeDiagnostics info;
        info.state = MusicResumeState::Playing;
        info.trackIndex = 3;
        info.streamPlaying = 1;
        info.streamReady = 1;
        Check(DescribeMusicResume(info) == "playing, track 3, stream playing 1, stream ready 1",
              "M5c resume: a playing player with a live stream is described");
        info.state = MusicResumeState::UserPaused;
        info.trackIndex = 0;
        info.streamPlaying = 0;
        info.streamReady = 0;
        Check(DescribeMusicResume(info) == "user-paused, track 0, stream playing 0, stream ready 0",
              "M5c resume: a user-paused player with a stopped stream is described");
        info.state = MusicResumeState::AutoPaused;
        info.trackIndex = -1;
        info.streamPlaying = -1;
        info.streamReady = -1;
        Check(DescribeMusicResume(info) == "auto-paused, no track, stream playing ?, stream ready ?",
              "M5c resume: an unreadable stream byte and no track are described");
        info.state = MusicResumeState::Stopped;
        info.trackIndex = 7;
        info.streamPlaying = 0;
        info.streamReady = 1;
        Check(DescribeMusicResume(info) == "stopped, track 7, stream playing 0, stream ready 1",
              "M5c resume: a stopped player is described");
    }

    // M5c (finding D): the sink voice must not start under a pending pause. A Pause() that lands
    // after BeginTrack and before the first Submit used to leave the pause flag and the voice state
    // diverged, so the next auto-pause did nothing.
    {
        Check(MusicSinkStartsOnSubmit(false),
              "M5c sink: the first Submit starts the voice when no pause is pending");
        Check(!MusicSinkStartsOnSubmit(true),
              "M5c sink: the first Submit does not start a voice under a pending pause");
        Check(MusicSinkStartsOnResume(true, true),
              "M5c sink: Resume starts the voice prepared by Submit after a pending pause");
        Check(!MusicSinkStartsOnResume(false, true),
              "M5c sink: Resume does nothing when no pause is pending");
        Check(!MusicSinkStartsOnResume(true, false),
              "M5c sink: Resume does nothing before the voice has a buffer");
        Check(!MusicSinkStartsOnResume(false, false),
              "M5c sink: Resume does nothing on a fresh voice");
    }

    // M5: jukebox sync decisions.
    {
        MusicJukeboxState state;
        MusicJukeboxRaceStart start = PlanMusicJukeboxRaceStart(state, true, true, 5, 3);
        Check(start.startAtTrack && start.track == 5 && start.syncWrites && !start.logInactiveStream,
              "M5 sync: with the race stream active the saved pick (DAT_005a6970) starts the cassette");
        Check(PlanMusicJukeboxTrackWrite(true, true, 6).write &&
                  PlanMusicJukeboxTrackWrite(true, true, 6).track == 6 &&
                  PlanMusicJukeboxTrackWrite(true, true, 6).band == RoadRashBandOfTrack(6),
              "M5 sync: a track change writes the track and its band");
        Check(PlanMusicJukeboxTrackWrite(true, true, 13).band == 5,
              "M5 sync: the band comes from RoadRashBandOfTrack per track");
        Check(!PlanMusicJukeboxTrackWrite(false, true, 6).write,
              "M5 sync: user cassettes never write the game jukebox");
        Check(!PlanMusicJukeboxTrackWrite(true, false, 6).write,
              "M5 sync: no write when the race stream is not active");
        Check(!PlanMusicJukeboxTrackWrite(true, true, 14).write &&
                  !PlanMusicJukeboxTrackWrite(true, true, -1).write,
              "M5 sync: an out-of-range track never writes");

        MusicJukeboxState midi;
        start = PlanMusicJukeboxRaceStart(midi, true, false, 5, 3);
        Check(start.startAtTrack && start.track == 3 && !start.syncWrites && start.logInactiveStream,
              "M5 sync: MIDI mode starts at the menu track (DAT_005cd498), skips the writes and logs once");
        start = PlanMusicJukeboxRaceStart(midi, true, false, 5, 3);
        Check(start.startAtTrack && !start.logInactiveStream,
              "M5 sync: the inactive-stream info line is printed once per session");

        MusicJukeboxState invalid;
        start = PlanMusicJukeboxRaceStart(invalid, true, true, 14, 3);
        Check(!start.startAtTrack && !start.syncWrites,
              "M5 sync: a saved pick outside 0..13 is ignored");
        start = PlanMusicJukeboxRaceStart(invalid, true, false, 5, -1);
        Check(!start.startAtTrack && !start.syncWrites && start.logInactiveStream,
              "M5 sync: a menu pick outside 0..13 is ignored in MIDI mode");

        MusicJukeboxState user;
        start = PlanMusicJukeboxRaceStart(user, false, true, 5, 3);
        Check(!start.startAtTrack && !start.syncWrites,
              "M5 sync: the jukebox rules do not apply to user cassettes");
    }

    // M5: CP-1252 game metadata conversion.
    {
        const char music[] = {'M', static_cast<char>(0xFA), 's', 'i', 'c', 'a', 0};
        Check(DecodeMusicGameString(music, sizeof(music), L"fallback") == L"M\u00FAsica",
              "M5 names: CP-1252 0xFA converts to u acute");
        const char cafe[] = {'c', 'a', 'f', static_cast<char>(0xE9), 0};
        Check(DecodeMusicGameString(cafe, sizeof(cafe), L"fallback") == L"caf\u00E9",
              "M5 names: CP-1252 0xE9 converts to e acute");
        Check(DecodeMusicGameString(nullptr, 64, L"fallback") == L"fallback",
              "M5 names: a null pointer falls back to the file-name title");
        Check(DecodeMusicGameString("", 64, L"fallback") == L"fallback" &&
                  DecodeMusicGameString("name", 0, L"fallback") == L"fallback",
              "M5 names: an empty buffer or a zero cap falls back");
        const std::string longName(100, 'a');
        Check(DecodeMusicGameString(longName.c_str(), kMusicGameNameMaxBytes, L"fallback").size() ==
                  kMusicGameNameMaxBytes,
              "M5 names: the conversion is capped at 64 bytes");
        Check(MusicGameStringOffset(0xe1) == 0xe1 * 4 && MusicGameStringOffset(0xf1) == 0xf1 * 4,
              "M5 names: string table entries are 4-byte pointers at id * 4");
    }

    // M5: hotkey edge detection with exact modifiers.
    {
        MusicHotkeyBindings bindings;
        bindings.keys[0] = {0x22, false, false, false};
        bindings.keys[1] = {0x21, false, false, false};
        bindings.keys[2] = {0x23, false, false, false};
        bindings.keys[3] = {0x2D, false, false, false};
        bindings.keys[4] = {0x24, false, false, false};
        MusicHotkeyState state;
        MusicHotkeyInput input;
        input.keyDown[0] = true;
        Check(PollMusicHotkeys(state, bindings, input) == MusicHotkeyAction::Next,
              "M5 hotkeys: a press without modifiers gives the next-track action");
        Check(PollMusicHotkeys(state, bindings, input) == MusicHotkeyAction::None,
              "M5 hotkeys: a held key does not repeat");
        input.keyDown[0] = false;
        Check(PollMusicHotkeys(state, bindings, input) == MusicHotkeyAction::None,
              "M5 hotkeys: releasing the key gives no action");
        input.keyDown[0] = true;
        Check(PollMusicHotkeys(state, bindings, input) == MusicHotkeyAction::Next,
              "M5 hotkeys: pressing again gives another action");

        MusicHotkeyBindings ctrlBindings = bindings;
        ctrlBindings.keys[0].ctrl = true;
        MusicHotkeyState ctrlState;
        MusicHotkeyInput ctrlInput;
        ctrlInput.keyDown[0] = true;
        ctrlInput.ctrlDown = true;
        Check(PollMusicHotkeys(ctrlState, ctrlBindings, ctrlInput) == MusicHotkeyAction::Next,
              "M5 hotkeys: Ctrl+key matches a Ctrl binding exactly");
        MusicHotkeyState mismatchState;
        MusicHotkeyInput mismatchInput;
        mismatchInput.keyDown[0] = true;
        Check(PollMusicHotkeys(mismatchState, ctrlBindings, mismatchInput) == MusicHotkeyAction::None,
              "M5 hotkeys: a missing modifier does not match");
        mismatchInput.ctrlDown = true;
        Check(PollMusicHotkeys(mismatchState, ctrlBindings, mismatchInput) == MusicHotkeyAction::None,
              "M5 hotkeys: pressing the modifier while the key is held does not repeat");

        Hotkey reserved[2] = {{0x91, false, false, false}, {VK_F11, false, false, false}};
        MusicHotkeyBindings conflicting;
        conflicting.keys[0] = {0x91, false, false, false};
        conflicting.keys[1] = {0x21, false, false, false};
        conflicting.keys[2] = {0x23, false, false, false};
        conflicting.keys[3] = {0x2D, false, false, false};
        conflicting.keys[4] = {0x24, false, false, false};
        const int disabled = ResolveMusicHotkeyConflicts(conflicting, reserved, 2);
        Check(disabled == 1 && !conflicting.enabled[0] && conflicting.enabled[1],
              "M5 hotkeys: a music hotkey equal to ToggleKey is disabled, the others stay");
        MusicHotkeyState conflictState;
        MusicHotkeyInput conflictInput;
        conflictInput.keyDown[0] = true;
        Check(PollMusicHotkeys(conflictState, conflicting, conflictInput) == MusicHotkeyAction::None,
              "M5 hotkeys: a disabled conflicting binding never fires");
        Check(SameHotkey(Hotkey{0x41, true, false, false}, Hotkey{0x41, true, false, false}) &&
                  !SameHotkey(Hotkey{0x41, true, false, false}, Hotkey{0x41, false, false, false}),
              "M5 hotkeys: same key and exact modifiers compare equal");
    }

    // M5b (bug C): a hotkey bound without modifiers accepts the press while Shift, Ctrl or Alt is
    // held (the game uses those while racing). A binding with modifiers keeps exact matching, and an
    // exact binding on the same key wins over the no-modifier binding.
    {
        MusicHotkeyBindings bindings;
        bindings.keys[0] = {L'5', false, false, false};
        bindings.keys[1] = {L'6', false, false, false};
        bindings.keys[2] = {0x23, false, false, false};
        bindings.keys[3] = {0x2D, false, false, false};
        bindings.keys[4] = {0x24, false, false, false};
        Check(MusicHotkeyBindingMatches(bindings, 0, false, false, false),
              "M5b hotkeys: no modifiers held matches a no-modifier binding");
        Check(MusicHotkeyBindingMatches(bindings, 0, false, true, false),
              "M5b hotkeys: Shift held still matches a no-modifier binding");
        Check(MusicHotkeyBindingMatches(bindings, 0, true, false, false),
              "M5b hotkeys: Ctrl held still matches a no-modifier binding");
        Check(MusicHotkeyBindingMatches(bindings, 0, false, false, true),
              "M5b hotkeys: Alt held still matches a no-modifier binding");

        MusicHotkeyState shiftState;
        MusicHotkeyInput shiftInput;
        shiftInput.keyDown[0] = true;
        shiftInput.shiftDown = true;
        Check(PollMusicHotkeys(shiftState, bindings, shiftInput) == MusicHotkeyAction::Next,
              "M5b hotkeys: pressing the bound key with Shift held fires the action");

        MusicHotkeyBindings withModifier = bindings;
        withModifier.keys[1] = {L'5', true, false, false};
        Check(MusicHotkeyBindingMatches(withModifier, 1, true, false, false),
              "M5b hotkeys: a Ctrl binding matches Ctrl held");
        Check(!MusicHotkeyBindingMatches(withModifier, 1, true, true, false),
              "M5b hotkeys: a Ctrl binding does not match Ctrl+Shift held");
        Check(!MusicHotkeyBindingMatches(withModifier, 0, true, false, false),
              "M5b hotkeys: the exact Ctrl binding on the same key wins over the no-modifier one");

        MusicHotkeyState exactState;
        MusicHotkeyInput exactInput;
        exactInput.keyDown[1] = true;
        exactInput.ctrlDown = true;
        Check(PollMusicHotkeys(exactState, withModifier, exactInput) == MusicHotkeyAction::Previous,
              "M5b hotkeys: the Ctrl binding on the key gives its own action");
        MusicHotkeyState plainState;
        MusicHotkeyInput plainInput;
        plainInput.keyDown[0] = true;
        Check(PollMusicHotkeys(plainState, withModifier, plainInput) == MusicHotkeyAction::Next,
              "M5b hotkeys: without modifiers the plain binding still fires");
    }

    // M5: the player cassette list puts Road Rash first and prefixes user paths.
    {
        const std::vector<MusicCassette> cassettes =
            BuildMusicPlayerCassettes({L"a.mp3", L"Mixtape\\b.wav"}, L"music");
        Check(cassettes.size() == 3 && cassettes[0].builtIn && cassettes[0].label == L"Road Rash" &&
                  cassettes[0].tracks.size() == kRoadRashTrackCount,
              "M5 library: the built-in Road Rash cassette comes first");
        Check(cassettes.size() == 3 && !cassettes[1].builtIn && cassettes[1].tracks.size() == 1 &&
                  cassettes[1].tracks[0].relativePath == L"music\\Mixtape\\b.wav",
              "M5 library: user cassette paths are prefixed with the music root");
        Check(cassettes.size() == 3 && cassettes[2].label == L"music" &&
                  cassettes[2].tracks.size() == 1 && cassettes[2].tracks[0].relativePath == L"music\\a.mp3",
              "M5 library: the root cassette holds loose files prefixed with the music root");
        const std::vector<MusicCassette> withGame =
            BuildMusicPlayerCassettes({}, L"music", L"C:\\Game");
        Check(withGame.size() == 1 && withGame[0].tracks[0].relativePath ==
                                           L"C:\\Game\\AUDIO\\MUSIC\\HB_Trip.RRA",
              "M5 library: built-in tracks are opened from the game folder");

        MusicPlayerStateMachine picker;
        picker.Start({MakeRoadRashCassette()}, 0, false, 0, 6);
        Check(picker.Snapshot().trackIndex == 6 && picker.Snapshot().builtInCassette,
              "M5 library: the jukebox pick selects the matching track in the Road Rash cassette");
        picker.Start({MakeRoadRashCassette()}, 0, false, 0);
        Check(picker.Snapshot().trackIndex == 0,
              "M5 library: without a pick the cassette starts on its first track");
    }
}

// Graphics options menu 19 and its "Reproductor" popup (rr95-music-player M6a/M6b), ported to the
// standalone music mod (rr95-music-standalone S3b). Reference bytes are all eight shipped items
// (0x005a08a8..0x005a0a6f, identical in the original and patched exe).
void TestMusicMenu() {
    auto readU32 = [](const uint8_t* p, size_t off) {
        uint32_t v = 0;
        std::memcpy(&v, p + off, sizeof(v));
        return v;
    };
    auto writeU32 = [](uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, sizeof(v)); };

    // Shipped verification covers all eight slots; the five absolute handlers are rebased.
    const intptr_t delta = 0x00230000;
    std::vector<uint8_t> shipped(kGraphicsMenuSlotsSize);
    ShippedGraphicsMenuSlots(0, shipped.data());
    Check(kVaGraphicsMenuItems == 0x005a08a8 &&
              kVaGraphicsMenuItems + kGraphicsMenuWidescreenSlot * kMenuItemSize == 0x005a09c0 &&
              MusicGraphicsMenuItemVa(kGraphicsMenuReproductorSlot) == 0x005a0a30 &&
              kGraphicsMenuSlotsSize == 0x1c0 && kGraphicsMenuCount == 8,
          "menu M6a: eight items span 0x5a08a8..0x5a0a68; slot 5 at 0x5a09c0, slot 7 at 0x5a0a30");
    Check(GraphicsMenuSlotsAreShipped(shipped.data(), 0),
          "menu M6a: the shipped eight slots verify at the preferred base");
    std::vector<uint8_t> rebased(kGraphicsMenuSlotsSize);
    ShippedGraphicsMenuSlots(delta, rebased.data());
    Check(GraphicsMenuSlotsAreShipped(rebased.data(), delta),
          "menu M6a: the shipped eight slots verify when rebased");
    Check(!GraphicsMenuSlotsAreShipped(shipped.data(), delta),
          "menu M6a: unrelocated handlers do not verify against a rebased module");
    std::vector<uint8_t> withHwnd = rebased;
    for (uint32_t s = 0; s < kGraphicsMenuCount; ++s) {
        writeU32(withHwnd.data(), s * kMenuItemSize, 0x000a0b0c + s);
    }
    Check(GraphicsMenuSlotsAreShipped(withHwnd.data(), delta),
          "menu M6a: engine HWND dwords at +0x00 are ignored");
    bool everyByte = true;
    for (size_t i = 0; i < kGraphicsMenuSlotsSize; ++i) {
        if ((i % kMenuItemSize) < 4) {
            continue;
        }
        std::vector<uint8_t> changed = rebased;
        changed[i] ^= 0x01;
        everyByte = everyByte && !GraphicsMenuSlotsAreShipped(changed.data(), delta);
    }
    Check(everyByte, "menu M6a: any other changed byte fails the shipped verification");
    Check(readU32(shipped.data(), 0 * kMenuItemSize + kMenuItemRect) == 0 &&
              readU32(shipped.data(), 0 * kMenuItemSize + kMenuItemRect + 4) == 134 &&
              readU32(shipped.data(), 6 * kMenuItemSize + kMenuItemRect + 4) == 422 &&
              readU32(shipped.data(), 6 * kMenuItemSize + kMenuItemRect + 8) == 180 &&
              readU32(shipped.data(), 6 * kMenuItemSize + kMenuItemRect + 12) == 58 &&
              readU32(shipped.data(), 7 * kMenuItemSize + kMenuItemRect + 4) == 114 &&
              readU32(shipped.data(), 7 * kMenuItemSize + kMenuItemRect + 12) == 20,
          "menu M6a: shipped rects keep slot 0 at y=134 and Ya esta at (0,422,180,58); slot 7 is the 20 px filler");

    auto checkLayout = [&](bool wide, bool repro, int regular, int h, int start, const int* expected,
                           const char* what) {
        const GraphicsMenuLayout l = ComputeGraphicsMenuLayout(wide, repro);
        bool ok = l.active && l.regularCount == regular && l.orderCount == regular + 1;
        int lastBottom = 0;
        for (int i = 0; i < l.orderCount; ++i) {
            const int slot = l.order[i];
            ok = ok && slot == expected[i] && l.visible[slot];
            if (i < regular) {
                ok = ok && l.rect[slot].x == 0 && l.rect[slot].width == 180 &&
                     l.rect[slot].height == h && l.rect[slot].y == start + i * h;
                lastBottom = l.rect[slot].y + h;
            }
        }
        ok = ok && l.rect[kGraphicsMenuLastSlot].x == 0 && l.rect[kGraphicsMenuLastSlot].y == 422 &&
             l.rect[kGraphicsMenuLastSlot].width == 180 && l.rect[kGraphicsMenuLastSlot].height == 58;
        ok = ok && kGraphicsMenuExitY - lastBottom == kGraphicsMenuExitGap;
        for (int i = 0; i < l.orderCount; ++i) {
            const int slot = l.order[i];
            const int prev = l.order[(i + l.orderCount - 1) % l.orderCount];
            const int next = l.order[(i + 1) % l.orderCount];
            ok = ok && l.nav[slot].left == slot && l.nav[slot].right == slot &&
                 l.nav[slot].up == prev && l.nav[slot].shiftTab == prev &&
                 l.nav[slot].down == next && l.nav[slot].tab == next;
        }
        Check(ok, what);
        return l;
    };

    const GraphicsMenuLayout noMods = ComputeGraphicsMenuLayout(false, false);
    Check(!noMods.active && noMods.orderCount == 6 && noMods.order[4] == 4 && noMods.order[5] == 6 &&
              noMods.rect[0].y == 134 && noMods.rect[4].y == 326 && noMods.rect[5].y == 374 &&
              noMods.rect[6].y == 422 && noMods.rect[7].y == 114 && noMods.nav[0].up == 6 &&
              noMods.nav[6].down == 0,
          "menu M6a: with no mod row the layout keeps the shipped rects, nav and no rewrite");
    const int orderWidescreen[] = {0, 1, 2, 3, 4, 5, 6};
    const int orderReproductor[] = {0, 1, 2, 3, 4, 7, 6};
    const int orderBoth[] = {0, 1, 2, 3, 4, 5, 7, 6};
    checkLayout(true, false, 6, 48, 118, orderWidescreen,
                "menu M6a: six regular buttons keep h=48 and pitch 48 from y=118, 16 px above Ya esta");
    checkLayout(false, true, 6, 48, 118, orderReproductor,
                "menu M6a: the Reproductor row alone is the sixth regular button (h=48, y=118..358)");
    checkLayout(true, true, 7, 42, 112, orderBoth,
                "menu M6a: seven regular buttons use h=42 and pitch 42 from y=112, 16 px above Ya esta");

    // Item bytes: no mod row leaves the shipped snapshot untouched; each mod row fills its slot
    // and the navigation ring follows the visual order.
    std::vector<uint8_t> out(kGraphicsMenuSlotsSize, 0xCD);
    BuildGraphicsMenuSlots(withHwnd.data(), noMods, GraphicsMenuModRows{}, out.data());
    Check(out == withHwnd, "menu M6a: BuildGraphicsMenuSlots with no mod row returns the shipped bytes");
    Check(readU32(withHwnd.data(), 0) == 0x000a0b0c && readU32(withHwnd.data(), 7 * kMenuItemSize) == 0x000a0b13,
          "menu M6a: the engine HWND dwords are real values in the reference snapshot");

    const uint32_t wideHandler = 0x10203040;
    const uint32_t reproHandler = 0x50607080;
    GraphicsMenuModRows wideRows;
    wideRows.widescreen = true;
    wideRows.widescreenHandler = wideHandler;
    BuildGraphicsMenuSlots(withHwnd.data(), ComputeGraphicsMenuLayout(true, false), wideRows, out.data());
    const uint8_t* w5 = out.data() + kGraphicsMenuWidescreenSlot * kMenuItemSize;
    const uint8_t* w6 = out.data() + kGraphicsMenuLastSlot * kMenuItemSize;
    Check(readU32(w5, kMenuItemLabel) == 0x3da &&
              readU32(w5, kMenuItemFlags) == (kGraphicsMenuButtonFlags | kMenuFlagValueText) &&
              readU32(w5, kMenuItemEnabled) == 1 && readU32(w5, kMenuItemHandler) == wideHandler &&
              readU32(w5, kMenuItemHelp) == 0 && readU32(w5, kMenuItemExtra) == 0x4852,
          "menu M6a: the widescreen row keeps label 0x3da, text flag, handler and 0x4852");
    Check(readU32(w5, kMenuItemRect + 4) == 358 && readU32(w5, kMenuItemRect + 12) == 48 &&
              w5[kMenuItemNavUp] == 4 && w5[kMenuItemNavDown] == 6 && w5[kMenuItemNavTab] == 6 &&
              w5[kMenuItemNavShiftTab] == 4 && w6[kMenuItemNavDown] == 0 && w6[kMenuItemNavTab] == 0 &&
              out.data()[kMenuItemNavUp] == 6,
          "menu M6a: widescreen-only order 0..5,6 with a circular navigation ring");
    Check(readU32(out.data(), 7 * kMenuItemSize + kMenuItemRect + 4) == 114,
          "menu M6a: the unused slot 7 stays at its shipped filler rect when Reproductor is absent");
    Check(readU32(out.data(), 0) == 0x000a0b0c && readU32(out.data(), kMenuItemSize) == 0x000a0b0d,
          "menu M6a: rewriting the layout preserves the engine's HWND dwords");

    GraphicsMenuModRows reproRows;
    reproRows.reproductor = true;
    reproRows.reproductorHandler = reproHandler;
    BuildGraphicsMenuSlots(withHwnd.data(), ComputeGraphicsMenuLayout(false, true), reproRows, out.data());
    const uint8_t* r7 = out.data() + kGraphicsMenuReproductorSlot * kMenuItemSize;
    const uint8_t* r6 = out.data() + kGraphicsMenuLastSlot * kMenuItemSize;
    Check(readU32(r7, kMenuItemLabel) == 0x3da &&
              readU32(r7, kMenuItemFlags) == (kGraphicsMenuButtonFlags | kMenuFlagValueText) &&
              readU32(r7, kMenuItemEnabled) == 1 && readU32(r7, kMenuItemHandler) == reproHandler &&
              readU32(r7, kMenuItemHelp) == 0 && readU32(r7, kMenuItemExtra) == 0x4852,
          "menu M6a: slot 7 becomes an enabled Reproductor item with dynamic text");
    Check(readU32(r7, kMenuItemRect + 4) == 358 && readU32(r7, kMenuItemRect + 12) == 48 &&
              r7[kMenuItemNavUp] == 4 && r7[kMenuItemNavDown] == 6 && r7[kMenuItemNavTab] == 6 &&
              r7[kMenuItemNavShiftTab] == 4 && r6[kMenuItemNavDown] == 0 && r6[kMenuItemNavUp] == 7 &&
              readU32(out.data(), 5 * kMenuItemSize + kMenuItemRect + 4) == 374,
          "menu M6a: Reproductor-only order 0..4,7,6; the empty slot 5 is untouched");
    Check(!GraphicsMenuSlotsAreShipped(out.data(), delta),
          "menu M6a: a rewritten menu no longer verifies as shipped");

    GraphicsMenuModRows bothRows;
    bothRows.widescreen = true;
    bothRows.reproductor = true;
    bothRows.widescreenHandler = wideHandler;
    bothRows.reproductorHandler = reproHandler;
    BuildGraphicsMenuSlots(withHwnd.data(), ComputeGraphicsMenuLayout(true, true), bothRows, out.data());
    const uint8_t* b5 = out.data() + kGraphicsMenuWidescreenSlot * kMenuItemSize;
    const uint8_t* b7 = out.data() + kGraphicsMenuReproductorSlot * kMenuItemSize;
    Check(readU32(b5, kMenuItemRect + 4) == 322 && readU32(b5, kMenuItemRect + 12) == 42 &&
              readU32(b7, kMenuItemRect + 4) == 364 && readU32(b7, kMenuItemRect + 12) == 42 &&
              b5[kMenuItemNavDown] == 7 && b7[kMenuItemNavUp] == 5 && b7[kMenuItemNavDown] == 6 &&
              out.data()[kGraphicsMenuLastSlot * kMenuItemSize + kMenuItemNavUp] == 7,
          "menu M6a: with both rows the chain is 4 <-> 5 <-> 7 <-> 6 at h=42");

    // Native popup items: header, Cassette, Beeper, five hotkey rows and Volver at the native
    // 36 px pitch (M6b). Nine rows must fit the 640 x 480 screen.
    const uint32_t popupHandler = 0x11223344;
    std::vector<uint8_t> popup(kReproductorPopupItemCount * kPopupItemSize + 8, 0xCD);
    BuildReproductorPopupItems(popupHandler, popup.data());
    auto popupItem = [&](int i) { return popup.data() + i * kPopupItemSize; };
    Check(readU32(popupItem(0), 0x00) == 0 && readU32(popupItem(0), kMenuItemId) == 0 &&
              readU32(popupItem(0), kMenuItemLabel) == 0 && readU32(popupItem(0), kMenuItemRect) == 316 &&
              readU32(popupItem(0), kMenuItemRect + 4) == 140 &&
              readU32(popupItem(0), kMenuItemRect + 8) == 260 &&
              readU32(popupItem(0), kMenuItemRect + 12) == 30 &&
              readU32(popupItem(0), kMenuItemFlags) == (kReproductorPopupHeaderFlags | kMenuFlagValueText) &&
              popupItem(0)[kMenuItemEnabled] == 0 && readU32(popupItem(0), kMenuItemHandler) == 0 &&
              readU32(popupItem(0), kMenuItemExtra) == kPopupItemExtra &&
              std::all_of(popupItem(0) + kMenuItemNavLeft, popupItem(0) + kMenuItemNavShiftTab + 1,
                          [](uint8_t b) { return b == 0; }),
          "menu M6b: the popup header is the native disabled 0x8030 row with dynamic text at y=140");
    bool rowsOk = true;
    for (int i = 1; i < kReproductorPopupItemCount; ++i) {
        const uint8_t* it = popupItem(i);
        rowsOk = rowsOk && readU32(it, kMenuItemId) == static_cast<uint32_t>(i) &&
                 readU32(it, kMenuItemLabel) == kReproductorPopupLabelId &&
                 readU32(it, kMenuItemRect) == 316 && readU32(it, kMenuItemRect + 4) == 140 + 36u * i &&
                 readU32(it, kMenuItemRect + 8) == 260 && readU32(it, kMenuItemRect + 12) == 30 &&
                 readU32(it, kMenuItemFlags) == (kReproductorPopupRowFlags | kMenuFlagValueText) &&
                 it[kMenuItemEnabled] == 1 && readU32(it, kMenuItemHandler) == popupHandler &&
                 readU32(it, kMenuItemExtra) == kPopupItemExtra;
    }
    Check(rowsOk,
          "menu M6b: the eight popup rows (Cassette, Beeper, five keys, Volver) are enabled dynamic-text rows");
    Check(kReproductorPopupItemCount == 9 &&
              readU32(popupItem(8), kMenuItemRect + 4) == 428 &&
              readU32(popupItem(8), kMenuItemRect + 4) + kReproductorPopupH <= kReproductorPopupBottomLimit,
          "menu M6b: nine popup items at y=140..458 leave the last row above y=470");
    const uint8_t* p1 = popupItem(1);
    const uint8_t* p8 = popupItem(8);
    Check(p1[kMenuItemNavLeft] == 1 && p1[kMenuItemNavRight] == 1 && p1[kMenuItemNavUp] == 8 &&
              p1[kMenuItemNavDown] == 2 && p1[kMenuItemNavTab] == 2 && p1[kMenuItemNavShiftTab] == 8 &&
              p8[kMenuItemNavUp] == 7 && p8[kMenuItemNavDown] == 1,
          "menu M6b: the popup rows navigate in one ring 1 -> ... -> 8 -> 1");
    bool navOk = true;
    for (int i = 1; i < kReproductorPopupItemCount; ++i) {
        const uint8_t* it = popupItem(i);
        const int prev = i == 1 ? kReproductorPopupItemCount - 1 : i - 1;
        const int next = i == kReproductorPopupItemCount - 1 ? 1 : i + 1;
        navOk = navOk && it[kMenuItemNavUp] == prev && it[kMenuItemNavDown] == next &&
                 it[kMenuItemNavTab] == next && it[kMenuItemNavShiftTab] == prev;
    }
    Check(navOk, "menu M6b: every popup row has a reachable previous and next row");
    Check(std::all_of(popup.begin() + kReproductorPopupItemCount * kPopupItemSize, popup.end(),
                      [](uint8_t b) { return b == 0xCD; }),
          "menu M6b: the popup builder writes exactly nine items");

    // Popup text and CP-1252 truncation.
    Check(ReproductorHeaderText() == "Reproductor" && ReproductorBackText() == "Volver",
          "menu M6a: the popup title is 'Reproductor' and the last row is 'Volver'");
    Check(ReproductorCassetteRowText(L"Musica nueva 1") == "Cassette: Musica nueva 1",
          "menu M6a: the cassette row names the selected cassette");
    Check(ReproductorCassetteRowText(L"M\u00FAsica") == std::string("Cassette: M\xFAsica"),
          "menu M6a: cassette labels are CP-1252 in the row text");
    Check(ReproductorOverlayRowText(MusicOverlayMode::Pinned) == "Beeper: Fijo" &&
              ReproductorOverlayRowText(MusicOverlayMode::OnDemand) == "Beeper: A demanda",
          "menu M6a: the overlay row shows Fijo for pinned and A demanda for on demand");
    const std::string longRow = ReproductorCassetteRowText(std::wstring(120, L'x'));
    Check(longRow.size() == kReproductorRowTextMaxBytes &&
              longRow == ("Cassette: " + std::string(120, 'x')).substr(0, kReproductorRowTextMaxBytes),
          "menu M6a: a long cassette label is truncated to 48 CP-1252 bytes");
    const std::string accents = ReproductorCassetteRowText(std::wstring(80, L'\u00D1'));
    Check(accents.size() == kReproductorRowTextMaxBytes &&
              std::count(accents.begin() + 10, accents.end(), static_cast<char>(0xD1)) ==
                  static_cast<int>(kReproductorRowTextMaxBytes - 10),
          "menu M6a: truncation keeps whole single-byte CP-1252 characters");

    // M6a review R3-002 and S3b review R3-overlay-hotkey-persists-across-races: the overlay hotkey
    // is a per-race show/hide. It never changes the configured beeper mode (registry/ini), so the
    // Beeper row keeps showing and toggling that configured mode.
    {
        const MusicOverlayMode configuredOnDemand = MusicOverlayMode::OnDemand;
        MusicOverlayController overlay(configuredOnDemand);
        overlay.OnOverlayHotkey(1000, true);
        Check(overlay.Mode() == MusicOverlayMode::OnDemand &&
                  overlay.Visibility() == MusicOverlayVisibility::Shown &&
                  ReproductorOverlayRowText(configuredOnDemand) == "Beeper: A demanda",
              "menu M6a R3-002: the overlay hotkey leaves the configured beeper mode on A demanda");
        Check(ReproductorOverlayRowText(configuredOnDemand) == "Beeper: A demanda",
              "menu M6a R3-002: the Beeper row still shows the configured mode after a hotkey press");
        const MusicBeeperMenuDecision press = DecideMusicBeeperMenu(configuredOnDemand, true);
        Check(press.mode == MusicOverlayMode::OnDemand && press.next == MusicOverlayMode::Pinned,
              "menu M6a R3-002: a popup press after the hotkey toggles the opposite of the configured mode");

        const MusicOverlayMode configuredPinned = MusicOverlayMode::Pinned;
        MusicOverlayController pinned(configuredPinned);
        pinned.OnOverlayHotkey(1000, true);
        Check(pinned.Mode() == MusicOverlayMode::Pinned &&
                  pinned.Visibility() == MusicOverlayVisibility::Hidden &&
                  ReproductorOverlayRowText(configuredPinned) == "Beeper: Fijo" &&
                  DecideMusicBeeperMenu(configuredPinned, true).next == MusicOverlayMode::OnDemand,
              "menu M6a R3-002: the configured pinned mode survives the hotkey and toggles back");
        pinned.ResetForRace();
        Check(pinned.Mode() == MusicOverlayMode::Pinned &&
                  pinned.Visibility() == MusicOverlayVisibility::Configured,
              "menu M6a R3-002: the next race clears the hotkey override and keeps the configured mode");
    }

    // M6a review R3-003/R3-004: the Cassette row and its press effect are one pure decision.
    // The popup opens immediately; before the scan finishes the row is loading and a press does
    // nothing, so a placeholder label is never written to ModMusicCassette. After the scan the
    // saved label (or the first cassette) resolves on the next row-text request.
    {
        MusicCassetteMenuInput loading;
        loading.libraryReady = false;
        loading.savedLabel = L"Musica nueva 1";
        loading.currentIndex = 0;
        MusicCassetteMenuDecision decision = DecideMusicCassetteMenu(loading);
        Check(ReproductorCassetteLoadingText() == "Cassette: cargando...",
              "menu M6a R3-003: the loading row reads Cassette: cargando...");
        Check(decision.loading && !decision.canSelect && decision.index == -1 && decision.nextIndex == -1,
              "menu M6a R3-003: before the scan a Cassette press changes nothing");

        const std::vector<std::wstring> readyLabels = {L"Road Rash", L"Musica nueva 1"};
        MusicCassetteMenuInput ready;
        ready.libraryReady = true;
        ready.savedLabel = L"Musica nueva 1";
        ready.labels = readyLabels;
        ready.currentIndex = 0;
        decision = DecideMusicCassetteMenu(ready);
        Check(!decision.loading && decision.canSelect && decision.index == 1 && decision.nextIndex == 0 &&
                  ReproductorCassetteRowText(readyLabels[1]) == "Cassette: Musica nueva 1",
              "menu M6a R3-003: a ready library resolves the saved label on the next row-text request");

        ready.savedLabel.clear();
        decision = DecideMusicCassetteMenu(ready);
        Check(!decision.loading && decision.index == 0 && decision.nextIndex == 1,
              "menu M6a R3-003: without a saved label the ready row shows the first cassette");

        ready.selectionResolved = true;
        ready.currentIndex = 1;
        decision = DecideMusicCassetteMenu(ready);
        Check(decision.index == 1 && decision.nextIndex == 0,
              "menu M6a R3-003: a resolved popup pick wins over the saved label");

        ready.libraryReady = true;
        ready.labels.clear();
        decision = DecideMusicCassetteMenu(ready);
        Check(decision.loading && !decision.canSelect && decision.nextIndex == -1,
              "menu M6a R3-003: a ready library with no cassettes still never selects or writes a placeholder");
    }

    // M6b: five hotkey rows, capture state machine and key application.
    {
        Check(kMusicKeyActionCount == 5 && static_cast<int>(MusicKeyAction::NextTrack) == 0 &&
                  static_cast<int>(MusicKeyAction::PreviousTrack) == 1 &&
                  static_cast<int>(MusicKeyAction::PlayPause) == 2 &&
                  static_cast<int>(MusicKeyAction::NextCassette) == 3 &&
                  static_cast<int>(MusicKeyAction::Overlay) == 4,
              "menu M6b: five key rows in the next, previous, play/pause, cassette, overlay order");
        Check(std::string(MusicKeyActionLabel(MusicKeyAction::NextTrack)) == "Siguiente" &&
                  std::string(MusicKeyActionLabel(MusicKeyAction::PreviousTrack)) == "Anterior" &&
                  std::string(MusicKeyActionLabel(MusicKeyAction::PlayPause)) == "Pausa" &&
                  std::string(MusicKeyActionLabel(MusicKeyAction::NextCassette)) == "Cambiar cassette" &&
                  std::string(MusicKeyActionLabel(MusicKeyAction::Overlay)) == "Mostrar beeper",
              "menu M6b: the key rows use short Spanish labels");
        Check(ReproductorKeyRowText(MusicKeyAction::NextTrack, '2') == "Siguiente: 2" &&
                  ReproductorKeyRowText(MusicKeyAction::PreviousTrack, '1') == "Anterior: 1" &&
                  ReproductorKeyRowText(MusicKeyAction::PlayPause, '3') == "Pausa: 3" &&
                  ReproductorKeyRowText(MusicKeyAction::NextCassette, '4') == "Cambiar cassette: 4" &&
                  ReproductorKeyRowText(MusicKeyAction::Overlay, '5') == "Mostrar beeper: 5",
              "menu M6b: key rows read <label>: <key name>");
        Check(MusicKeyName(0x70) == "F1" && MusicKeyName(0x7B) == "F12" &&
                  MusicKeyName('2') == "2",
              "menu M6b: key names are the Windows names of the virtual key");
        Check(MusicKeyName(0xE8) == "0xE8" && MusicKeyName(0) == "0x00",
              "menu M6b: a key Windows has no name for falls back to 0xNN");
        bool captureTextFits = true;
        for (int i = 0; i < kMusicKeyActionCount; ++i) {
            const std::string text = ReproductorKeyCaptureText(static_cast<MusicKeyAction>(i));
            captureTextFits = captureTextFits && !text.empty() && text.size() <= kReproductorRowTextMaxBytes &&
                              text.find(": presione una tecla") != std::string::npos;
        }
        Check(captureTextFits && ReproductorKeyCaptureText(MusicKeyAction::NextTrack) ==
                                      "Siguiente: presione una tecla",
              "menu M6b: a capturing row reads <label>: presione una tecla within the 48-byte cap");

        // Capture: Enter starts it, the Enter that started it cannot be bound, and the first key
        // that goes down after every key is released is taken.
        MusicKeyCaptureState capture = StartMusicKeyCapture(MusicKeyAction::NextTrack);
        Check(capture.active && capture.waitingRelease && capture.action == 0,
              "menu M6b: Enter starts capture and waits for every key release");
        MusicKeyCaptureInput input;
        input.key = MusicKeyCaptureKey::Down;
        input.vk = '2';
        input.otherKeysDown = true;
        MusicKeyCaptureUpdate update = FeedMusicKeyCapture(capture, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active &&
                  update.state.waitingRelease && update.capturedVk == 0,
              "menu M6b: a key pressed while another is still down is ignored");
        input.key = MusicKeyCaptureKey::Up;
        input.vk = kMusicKeyReturn;
        input.otherKeysDown = false;
        update = FeedMusicKeyCapture(update.state, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active &&
                  !update.state.waitingRelease,
              "menu M6b: the key-up that releases every key ends the release wait");
        input.key = MusicKeyCaptureKey::Down;
        input.vk = '2';
        input.otherKeysDown = false;
        update = FeedMusicKeyCapture(update.state, input);
        Check(update.result == MusicKeyCaptureResult::Captured && update.capturedVk == '2' &&
                  !update.state.active,
              "menu M6b: the first key that goes down after release is captured");

        // Reserved keys and modifiers.
        capture = StartMusicKeyCapture(MusicKeyAction::Overlay);
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::Up;
        input.otherKeysDown = false;
        MusicKeyCaptureUpdate waiting = FeedMusicKeyCapture(capture, input);
        input.key = MusicKeyCaptureKey::Down;
        input.vk = kMusicKeyTab;
        update = FeedMusicKeyCapture(waiting.state, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active,
              "menu M6b: Tab is reserved and ignored during capture");
        input.vk = kMusicKeyDown;
        update = FeedMusicKeyCapture(waiting.state, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active,
              "menu M6b: arrow keys are reserved and ignored during capture");
        input.vk = 0x10;  // VK_SHIFT
        update = FeedMusicKeyCapture(waiting.state, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active,
              "menu M6b: modifier keys alone are ignored during capture");
        input.vk = kMusicKeyEscape;
        update = FeedMusicKeyCapture(waiting.state, input);
        Check(update.result == MusicKeyCaptureResult::Cancelled && !update.state.active &&
                  update.capturedVk == 0,
              "menu M6b: Esc cancels the capture without a binding");
        input.vk = kMusicKeyReturn;
        update = FeedMusicKeyCapture(waiting.state, input);
        Check(update.result == MusicKeyCaptureResult::Cancelled && !update.state.active,
              "menu M6b: Enter during capture cancels it");
        Check(!EndMusicKeyCapture().active,
              "menu M6b: closing the popup, leaving the menu or a race start ends the capture");

        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::None;
        input.otherKeysDown = false;
        MusicKeyCaptureState waitingState = StartMusicKeyCapture(MusicKeyAction::PlayPause);
        update = FeedMusicKeyCapture(waitingState, input);
        Check(update.state.active && !update.state.waitingRelease,
              "menu M6b: a non-key message clears the release wait once every key is up");

        // Enter and Esc cancel even while another key is still held during the release wait.
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::Down;
        input.vk = kMusicKeyEscape;
        input.otherKeysDown = true;
        update = FeedMusicKeyCapture(StartMusicKeyCapture(MusicKeyAction::PlayPause), input);
        Check(update.result == MusicKeyCaptureResult::Cancelled && !update.state.active &&
                  update.capturedVk == 0,
              "menu M6b: Esc cancels even while the release wait still sees a held key");
        input.vk = kMusicKeyReturn;
        update = FeedMusicKeyCapture(StartMusicKeyCapture(MusicKeyAction::PlayPause), input);
        Check(update.result == MusicKeyCaptureResult::Cancelled && !update.state.active,
              "menu M6b: Enter cancels even while the release wait still sees a held key");

        // A character message never binds or ends a capture.
        MusicKeyCaptureState ready = StartMusicKeyCapture(MusicKeyAction::NextCassette);
        ready.waitingRelease = false;
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::Char;
        input.vk = '7';
        update = FeedMusicKeyCapture(ready, input);
        Check(update.result == MusicKeyCaptureResult::Waiting && update.state.active &&
                  !update.state.draining && update.capturedVk == 0,
              "menu M6b: a WM_CHAR during capture is neither bound nor passed on");

        // Swallowing: the menu sees no input during a capture nor during the rest of the key
        // press that ended it (its WM_CHAR, auto-repeat and WM_KEYUP).
        Check(!MusicKeyCaptureSwallowsInput(MusicKeyCaptureState{}) &&
                  MusicKeyCaptureSwallowsInput(StartMusicKeyCapture(MusicKeyAction::NextTrack)),
              "menu M6b: menu input is swallowed only while a capture is active or draining");
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::Down;
        input.vk = 'K';
        update = FeedMusicKeyCapture(ready, input);
        Check(update.result == MusicKeyCaptureResult::Captured && update.capturedVk == 'K' &&
                  !update.state.active && update.state.draining &&
                  MusicKeyCaptureSwallowsInput(update.state),
              "menu M6b: a captured key keeps draining so its WM_CHAR and WM_KEYUP stay away from the menu");
        MusicKeyCaptureState drain = update.state;
        input.key = MusicKeyCaptureKey::Char;
        update = FeedMusicKeyCapture(drain, input);
        Check(update.result == MusicKeyCaptureResult::None && update.state.draining,
              "menu M6b: the captured key's WM_CHAR is swallowed while draining");
        input.key = MusicKeyCaptureKey::Down;
        update = FeedMusicKeyCapture(drain, input);
        Check(update.result == MusicKeyCaptureResult::None && update.state.draining &&
                  update.capturedVk == 0 && !update.state.active,
              "menu M6b: auto-repeat of the captured key while draining binds nothing");
        input.key = MusicKeyCaptureKey::Up;
        input.otherKeysDown = true;
        update = FeedMusicKeyCapture(drain, input);
        Check(update.state.draining && MusicKeyCaptureSwallowsInput(update.state),
              "menu M6b: draining lasts while another key is still held");
        input.otherKeysDown = false;
        update = FeedMusicKeyCapture(drain, input);
        Check(update.result == MusicKeyCaptureResult::None && !update.state.draining &&
                  !update.state.active && !MusicKeyCaptureSwallowsInput(update.state),
              "menu M6b: the key-up that releases every key ends draining and the menu reads input again");
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::None;
        update = FeedMusicKeyCapture(drain, input);
        Check(!update.state.draining,
              "menu M6b: a mouse message with every key released also ends draining");
        input.key = MusicKeyCaptureKey::Down;
        input.vk = kMusicKeyEscape;
        update = FeedMusicKeyCapture(ready, input);
        Check(update.result == MusicKeyCaptureResult::Cancelled && update.state.draining &&
                  MusicKeyCaptureSwallowsInput(update.state),
              "menu M6b: a cancelling Esc also drains, so its key-up cannot close the popup");
        Check(!MusicKeyCaptureSwallowsInput(EndMusicKeyCapture()) && !EndMusicKeyCapture().draining,
              "menu M6b: ending the capture also ends draining");
        input = MusicKeyCaptureInput{};
        input.key = MusicKeyCaptureKey::Down;
        input.vk = 'K';
        update = FeedMusicKeyCapture(MusicKeyCaptureState{}, input);
        Check(update.result == MusicKeyCaptureResult::None && !update.state.active &&
                  !update.state.draining && update.capturedVk == 0,
              "menu M6b: without a capture the state machine ignores every key");

        // Window messages the RashButton procedure sees.
        Check(MusicKeyCaptureKeyForMessage(WM_KEYDOWN) == MusicKeyCaptureKey::Down &&
                  MusicKeyCaptureKeyForMessage(WM_SYSKEYDOWN) == MusicKeyCaptureKey::Down &&
                  MusicKeyCaptureKeyForMessage(WM_KEYUP) == MusicKeyCaptureKey::Up &&
                  MusicKeyCaptureKeyForMessage(WM_SYSKEYUP) == MusicKeyCaptureKey::Up &&
                  MusicKeyCaptureKeyForMessage(WM_CHAR) == MusicKeyCaptureKey::Char &&
                  MusicKeyCaptureKeyForMessage(WM_SYSCHAR) == MusicKeyCaptureKey::Char &&
                  MusicKeyCaptureKeyForMessage(WM_DEADCHAR) == MusicKeyCaptureKey::Char &&
                  MusicKeyCaptureKeyForMessage(WM_SYSDEADCHAR) == MusicKeyCaptureKey::Char &&
                  MusicKeyCaptureKeyForMessage(WM_MOUSEMOVE) == MusicKeyCaptureKey::None &&
                  MusicKeyCaptureKeyForMessage(WM_PAINT) == MusicKeyCaptureKey::None,
              "menu M6b: key, character and other messages map to Down, Up, Char and None");
        Check(IsMenuInputMessage(WM_KEYDOWN) && IsMenuInputMessage(WM_KEYUP) && IsMenuInputMessage(WM_CHAR) &&
                  IsMenuInputMessage(WM_SYSKEYDOWN) && IsMenuInputMessage(WM_SYSKEYUP) &&
                  IsMenuInputMessage(WM_SYSCHAR) && IsMenuInputMessage(WM_MOUSEMOVE) &&
                  IsMenuInputMessage(WM_LBUTTONDOWN) && IsMenuInputMessage(WM_LBUTTONUP) &&
                  IsMenuInputMessage(WM_RBUTTONUP) && IsMenuInputMessage(WM_MBUTTONUP) &&
                  IsMenuInputMessage(WM_MOUSEWHEEL),
              "menu M6b: keyboard and mouse messages count as menu input");
        Check(!IsMenuInputMessage(WM_PAINT) && !IsMenuInputMessage(WM_SETFOCUS) &&
                  !IsMenuInputMessage(WM_KILLFOCUS) && !IsMenuInputMessage(WM_TIMER) &&
                  !IsMenuInputMessage(WM_CREATE) && !IsMenuInputMessage(WM_DESTROY) &&
                  !IsMenuInputMessage(WM_ENABLE),
              "menu M6b: paint, focus, timer and lifetime messages always reach the menu");

        // Popup-open check that ends a capture when the popup closes or the menu is left.
        Check(ReproductorPopupStillOpen(1, 0x1000, 0x1000),
              "menu M6b: the popup is open while the flag is set and slot 0 points at the mod items");
        Check(!ReproductorPopupStillOpen(0, 0x1000, 0x1000) && !ReproductorPopupStillOpen(1, 0x2000, 0x1000) &&
                  !ReproductorPopupStillOpen(1, 0, 0x1000) && !ReproductorPopupStillOpen(1, 0x1000, 0),
              "menu M6b: a cleared flag, another popup or the base menu means the popup is closed");

        // Swap and no-op.
        MusicKeys keys;
        keys.vk[0] = '2';
        keys.vk[1] = '1';
        keys.vk[2] = '3';
        keys.vk[3] = 0x2D;
        keys.vk[4] = 0x24;
        const MusicKeys swapped = ApplyMusicKeyCapture(keys, MusicKeyAction::NextTrack, '1');
        Check(swapped.vk[0] == '1' && swapped.vk[1] == '2' && swapped.vk[2] == '3' &&
                  swapped.vk[3] == 0x2D && swapped.vk[4] == 0x24,
              "menu M6b: a key already bound to another action swaps the two bindings");
        const MusicKeys same = ApplyMusicKeyCapture(keys, MusicKeyAction::NextTrack, '2');
        Check(std::memcmp(same.vk, keys.vk, sizeof(keys.vk)) == 0,
              "menu M6b: pressing the key the row already has keeps it unchanged");
        const MusicKeys reserved = ApplyMusicKeyCapture(keys, MusicKeyAction::NextTrack, kMusicKeyReturn);
        Check(std::memcmp(reserved.vk, keys.vk, sizeof(keys.vk)) == 0,
              "menu M6b: a reserved key is never bound");
        const MusicKeys modifier = ApplyMusicKeyCapture(keys, MusicKeyAction::NextTrack, 0x11);
        Check(std::memcmp(modifier.vk, keys.vk, sizeof(keys.vk)) == 0,
              "menu M6b: a modifier alone is never bound");
    }

    // S3b: the row is registered only when the general mod owns the menu.
    {
        const MusicMenuRowPlan pending = PlanMusicMenuRow(RR95_MENU_STATE_PENDING);
        Check(pending.action == MusicMenuRowAction::Register && pending.registerRow &&
                  pending.registerItemText && pending.logMessage[0] != '\0',
              "menu S3b: a pending menu relocation registers the Reproductor row and the item text");
        const MusicMenuRowPlan applied = PlanMusicMenuRow(RR95_MENU_STATE_APPLIED);
        Check(applied.action == MusicMenuRowAction::Register && applied.registerRow &&
                  applied.registerItemText,
              "menu S3b: an applied menu relocation keeps the row and the item text on the API");
        const MusicMenuRowPlan notUsed = PlanMusicMenuRow(RR95_MENU_STATE_NOT_USED);
        Check(notUsed.action == MusicMenuRowAction::NotUsed && !notUsed.registerRow &&
                  !notUsed.registerItemText && notUsed.logMessage[0] != '\0',
              "menu S3b: without a menu relocation the row is left out and the reason is logged");
        const MusicMenuRowPlan failed = PlanMusicMenuRow(RR95_MENU_STATE_FAILED);
        Check(failed.action == MusicMenuRowAction::Failed && !failed.registerRow &&
                  !failed.registerItemText && failed.logMessage[0] != '\0',
              "menu S3b: a failed relocation leaves the row out and logs it");
        const MusicMenuRowPlan unknown = PlanMusicMenuRow(RR95_MENU_STATE_UNKNOWN);
        const MusicMenuRowPlan invalid = PlanMusicMenuRow(-1);
        Check(unknown.action == MusicMenuRowAction::Unknown && !unknown.registerRow &&
                  !unknown.registerItemText && unknown.logMessage[0] != '\0' &&
                  invalid.action == MusicMenuRowAction::Unknown && !invalid.registerRow,
              "menu S3b: an unknown menu state never registers early; a later decision retries");
        Check(notUsed.logMessage != failed.logMessage && notUsed.logMessage != unknown.logMessage &&
                  failed.logMessage != unknown.logMessage,
              "menu S3b: every skipped row logs its own reason");
    }

    // S3b review R3-menu-row-unknown-never-retried: an unknown menu state is not a failure. The gate
    // asks again at the next bind retry point; only a final state ends the row for the session.
    {
        const MusicMenuRowGate registerNow =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, false, {});
        Check(registerNow.attempt && !registerNow.finalSkip && !registerNow.logTransient &&
                  !registerNow.logFinal,
              "menu S3b: a pending or applied menu state asks to register the row");
        const MusicMenuRowGate unknownGate =
            DecideMusicMenuRowGate(MusicMenuRowAction::Unknown, false, {});
        Check(!unknownGate.attempt && !unknownGate.finalSkip && !unknownGate.logTransient &&
                  !unknownGate.logFinal,
              "menu S3b: an unknown menu state retries at the next bind point and logs nothing");
        const MusicMenuRowGate notUsedGate =
            DecideMusicMenuRowGate(MusicMenuRowAction::NotUsed, false, {});
        const MusicMenuRowGate failedGate =
            DecideMusicMenuRowGate(MusicMenuRowAction::Failed, false, {});
        const MusicMenuRowGate tooLateGate =
            DecideMusicMenuRowGate(MusicMenuRowAction::TooLate, false, {});
        Check(!notUsedGate.attempt && notUsedGate.finalSkip && notUsedGate.logFinal &&
                  !failedGate.attempt && failedGate.finalSkip && failedGate.logFinal &&
                  !tooLateGate.attempt && tooLateGate.finalSkip && tooLateGate.logFinal,
              "menu S3b: not used, failed and too late are final states that log once");
        const MusicMenuRowGate loggedAgain = DecideMusicMenuRowGate(
            MusicMenuRowAction::Failed, false, MusicMenuRowLogState{false, true});
        Check(!loggedAgain.attempt && loggedAgain.finalSkip && !loggedAgain.logFinal,
              "menu S3b: a final state logs its reason only once");
        const MusicMenuRowGate installed =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, true, {});
        Check(!installed.attempt && !installed.finalSkip && !installed.logTransient &&
                  !installed.logFinal,
              "menu S3b: a registered row is never registered twice");
    }

    // S4 review R3-menu-row-register-ignores-logged: a failed registration attempt is a cause the
    // gate remembers. A final cause stops the retries and is logged once; only an undocumented API
    // answer can still change, so only that one is retried, and its reason is still logged once.
    {
        const MusicMenuRowGate routines = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::RoutinesUnverified);
        const MusicMenuRowGate tooLate = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::ApiTooLate);
        const MusicMenuRowGate rejected = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::ApiRejected);
        const MusicMenuRowGate invalid = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::ApiInvalidArgument);
        const MusicMenuRowGate text = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::ItemTextRejected);
        Check(!routines.attempt && routines.finalSkip && routines.logFinal && !tooLate.attempt &&
                  tooLate.finalSkip && tooLate.logFinal && !rejected.attempt && rejected.finalSkip &&
                  rejected.logFinal && !invalid.attempt && invalid.finalSkip && invalid.logFinal &&
                  !text.attempt && text.finalSkip && text.logFinal,
              "menu S4: a failed registration attempt is final for the session and logs once");
        const MusicMenuRowGate logged = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, MusicMenuRowLogState{false, true},
            MusicMenuRowFailure::ApiTooLate);
        Check(!logged.attempt && logged.finalSkip && !logged.logFinal,
              "menu S4: a failed registration attempt never logs twice");
        const MusicMenuRowGate unknownStatus = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, {}, MusicMenuRowFailure::ApiUnknownStatus);
        const MusicMenuRowGate unknownLogged = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, false, MusicMenuRowLogState{true, false},
            MusicMenuRowFailure::ApiUnknownStatus);
        Check(unknownStatus.attempt && !unknownStatus.finalSkip && unknownStatus.logTransient &&
                  !unknownStatus.logFinal && unknownLogged.attempt && !unknownLogged.finalSkip &&
                  !unknownLogged.logTransient && !unknownLogged.logFinal,
              "menu S4: only an undocumented status is retried, and its reason is logged once");
        Check(MusicMenuRowFailureIsTransient(MusicMenuRowFailure::ApiUnknownStatus) &&
                  !MusicMenuRowFailureIsTransient(MusicMenuRowFailure::ApiTooLate) &&
                  !MusicMenuRowFailureIsTransient(MusicMenuRowFailure::ApiRejected) &&
                  !MusicMenuRowFailureIsTransient(MusicMenuRowFailure::ApiInvalidArgument) &&
                  !MusicMenuRowFailureIsTransient(MusicMenuRowFailure::RoutinesUnverified) &&
                  !MusicMenuRowFailureIsTransient(MusicMenuRowFailure::ItemTextRejected),
              "menu S4: only an undocumented status can still change");
        Check(MusicMenuRowFailureFromApiStatus(RR95_STATUS_TOO_LATE) == MusicMenuRowFailure::ApiTooLate &&
                  MusicMenuRowFailureFromApiStatus(RR95_STATUS_REJECTED) ==
                      MusicMenuRowFailure::ApiRejected &&
                  MusicMenuRowFailureFromApiStatus(RR95_STATUS_INVALID_ARGUMENT) ==
                      MusicMenuRowFailure::ApiInvalidArgument &&
                  MusicMenuRowFailureFromApiStatus(99) == MusicMenuRowFailure::ApiUnknownStatus,
              "menu S4: every API status maps to its cause");
        const std::string tooLateMessage =
            MusicMenuRowFailureMessage(MusicMenuRowFailure::ApiTooLate, RR95_STATUS_TOO_LATE);
        const std::string rejectedMessage =
            MusicMenuRowFailureMessage(MusicMenuRowFailure::ApiRejected, RR95_STATUS_REJECTED);
        Check(!tooLateMessage.empty() && !rejectedMessage.empty() && tooLateMessage != rejectedMessage &&
                  rejectedMessage.find("status 1") != std::string::npos,
              "menu S4: every failure logs its own reason and the API status");
        const MusicMenuRowGate registeredAfterFailure = DecideMusicMenuRowGate(
            MusicMenuRowAction::Register, true, {}, MusicMenuRowFailure::ApiTooLate);
        Check(!registeredAfterFailure.attempt && !registeredAfterFailure.finalSkip &&
                  !registeredAfterFailure.logTransient && !registeredAfterFailure.logFinal,
              "menu S4: a registered row is not a failure even when an old cause is kept");
    }

    // S5 review R3-menu-row-final-cause-after-transient-never-logged: the transient line and the
    // final line are independent. A final cause that arrives after a logged transient failure is
    // still written once, because it is the answer that explains why the row is missing.
    {
        MusicMenuRowLogState log;
        const MusicMenuRowGate transient =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, false, log,
                                   MusicMenuRowFailure::ApiUnknownStatus);
        Check(transient.attempt && transient.logTransient && !transient.logFinal,
              "menu S5: the first transient cause is logged and the registration is retried");
        log.transientLogged = true;
        const MusicMenuRowGate transientAgain =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, false, log,
                                   MusicMenuRowFailure::ApiUnknownStatus);
        Check(transientAgain.attempt && !transientAgain.logTransient && !transientAgain.logFinal,
              "menu S5: the transient cause is never logged twice");
        const MusicMenuRowGate finalCause =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, false, log,
                                   MusicMenuRowFailure::ApiTooLate);
        Check(!finalCause.attempt && finalCause.finalSkip && finalCause.logFinal &&
                  !finalCause.logTransient,
              "menu S5: a final cause after a transient one is logged once");
        log.finalLogged = true;
        const MusicMenuRowGate finalCauseAgain =
            DecideMusicMenuRowGate(MusicMenuRowAction::Register, false, log,
                                   MusicMenuRowFailure::ApiTooLate);
        Check(!finalCauseAgain.attempt && finalCauseAgain.finalSkip && !finalCauseAgain.logFinal,
              "menu S5: the final cause is never logged twice");
        const MusicMenuRowGate finalAction =
            DecideMusicMenuRowGate(MusicMenuRowAction::NotUsed, false, log, MusicMenuRowFailure::None);
        Check(!finalAction.attempt && finalAction.finalSkip && !finalAction.logFinal,
              "menu S5: a final action already logged stays quiet");
    }

    // S3b review R3-capture-strips-ini-modifiers and R3-reserved-capture-shown-but-disabled: a
    // capture changes the captured row only and rejects what the conflict rule would disable.
    {
        MusicHotkeyBindings bindings;
        bindings.keys[0] = Hotkey{'N', true, false, false};
        bindings.keys[1] = Hotkey{'P', true, false, false};
        bindings.keys[2] = Hotkey{0x20, true, false, false};
        bindings.keys[3] = Hotkey{'C', true, false, false};
        bindings.keys[4] = Hotkey{'B', true, false, false};
        MusicKeys keys;
        keys.vk[0] = 'N';
        keys.vk[1] = 'P';
        keys.vk[2] = 0x20;
        keys.vk[3] = 'C';
        keys.vk[4] = 'B';
        const Hotkey reserved[2] = {Hotkey{0x91, false, false, false},   // VK_SCROLL, the general mod
                                    Hotkey{0x7A, false, false, false}};  // VK_F11
        const MusicHotkeyCaptureApply bound =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 'Q', reserved, 2);
        Check(bound.accepted && bound.hotkeys.keys[0].vk == 'Q' && !bound.hotkeys.keys[0].ctrl &&
                  bound.keys.vk[0] == 'Q' && bound.keys.vk[1] == 'P',
              "menu S3b: a capture writes the pressed key into its own row only");
        Check(bound.hotkeys.keys[1].ctrl && bound.hotkeys.keys[1].vk == 'P' &&
                  bound.hotkeys.keys[2].ctrl && bound.hotkeys.keys[2].vk == 0x20 &&
                  bound.hotkeys.keys[3].ctrl && bound.hotkeys.keys[3].vk == 'C' &&
                  bound.hotkeys.keys[4].ctrl && bound.hotkeys.keys[4].vk == 'B',
              "menu S3b: the other four rows keep their ini modifiers and keys");
        const MusicHotkeyCaptureApply swapped =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 'P', reserved, 2);
        Check(swapped.accepted && swapped.keys.vk[0] == 'P' && swapped.keys.vk[1] == 'N' &&
                  !swapped.hotkeys.keys[0].ctrl && swapped.hotkeys.keys[1].ctrl &&
                  swapped.hotkeys.keys[1].vk == 'N',
              "menu S3b: a duplicate swaps the plain keys and the other row keeps its modifiers");
        const MusicHotkeyCaptureApply toggle =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 0x91, reserved, 2);
        const MusicHotkeyCaptureApply f11 =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 0x7A, reserved, 2);
        const MusicHotkeyCaptureApply enter =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, kMusicKeyReturn, reserved, 2);
        Check(!toggle.accepted && !f11.accepted && !enter.accepted,
              "menu S3b: the general mod ToggleKey, F11 and a reserved menu key are never captured");
        Check(!toggle.accepted && std::memcmp(toggle.keys.vk, keys.vk, sizeof(keys.vk)) == 0 &&
                  SameHotkey(toggle.hotkeys.keys[0], bindings.keys[0]) &&
                  toggle.hotkeys.keys[1].ctrl && toggle.hotkeys.keys[1].vk == 'P',
              "menu S3b: a rejected capture changes no binding");
        Check(!EncodeMusicKeysRegistryValue(bound.keys).empty(),
              "menu S3b: the plain keys of an accepted capture are a valid ModMusicKeys value");
        // S4 review R3-capture-skips-conflict-rule-on-swap: the swap partner takes the captured
        // row's old key with its own modifiers, so that combination goes through the conflict rule
        // too, and a reserved one rejects the whole capture: no change and no registry write.
        const Hotkey ctrlReserved[3] = {Hotkey{0x91, false, false, false},  // VK_SCROLL
                                        Hotkey{0x7A, false, false, false},  // VK_F11
                                        Hotkey{'N', true, false, false}};   // row 0's old ctrl+N
        const MusicHotkeyCaptureApply rejectedSwap =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 'P', ctrlReserved, 3);
        Check(!rejectedSwap.accepted && SameHotkey(rejectedSwap.hotkeys.keys[0], bindings.keys[0]) &&
                  SameHotkey(rejectedSwap.hotkeys.keys[1], bindings.keys[1]) &&
                  std::memcmp(rejectedSwap.keys.vk, keys.vk, sizeof(keys.vk)) == 0,
              "menu S4: a swap that leaves a row with a reserved combination is rejected whole");
        const MusicHotkeyCaptureApply allowedSwap =
            ApplyMusicHotkeyCapture(bindings, keys, MusicKeyAction::NextTrack, 'P', ctrlReserved, 2);
        Check(allowedSwap.accepted && allowedSwap.hotkeys.keys[1].ctrl &&
                  allowedSwap.hotkeys.keys[1].vk == 'N' && allowedSwap.keys.vk[0] == 'P' &&
                  allowedSwap.keys.vk[1] == 'N',
              "menu S4: a swap with no reserved combination keeps the partner's modifiers");
    }

    // S3b review R3-glue-capture-untested: the RashButton swallow decision is pure and tested.
    {
        const MusicKeyCaptureState active = StartMusicKeyCapture(MusicKeyAction::PlayPause);
        const MusicKeyCaptureMessageDecision down =
            DecideMusicKeyCaptureMessage(active, true, WM_KEYDOWN, 'Q');
        const MusicKeyCaptureMessageDecision mouse =
            DecideMusicKeyCaptureMessage(active, true, WM_LBUTTONDOWN, 0);
        Check(down.feed && down.swallow && !down.endCapture && mouse.feed && mouse.swallow &&
                  !mouse.endCapture,
              "menu S3b: keyboard and mouse menu input is fed to the capture and swallowed");
        const MusicKeyCaptureMessageDecision paint =
            DecideMusicKeyCaptureMessage(active, true, WM_PAINT, 0);
        const MusicKeyCaptureMessageDecision idle =
            DecideMusicKeyCaptureMessage(EndMusicKeyCapture(), true, WM_KEYDOWN, 'Q');
        Check(!paint.feed && !paint.swallow && !paint.endCapture && !idle.feed && !idle.swallow &&
                  !idle.endCapture,
              "menu S3b: other messages and an idle capture leave the menu input alone");
        const MusicKeyCaptureMessageDecision shiftUp =
            DecideMusicKeyCaptureMessage(active, true, WM_KEYUP, kMusicKeyShift);
        const MusicKeyCaptureMessageDecision otherUp =
            DecideMusicKeyCaptureMessage(active, true, WM_KEYUP, 'Q');
        Check(shiftUp.feed && !shiftUp.swallow && otherUp.feed && otherUp.swallow,
              "menu S3b: a Shift key-up is fed but reaches the engine; other key-ups are swallowed");
        const MusicKeyCaptureMessageDecision closed =
            DecideMusicKeyCaptureMessage(active, false, WM_KEYDOWN, 'Q');
        Check(!closed.feed && !closed.swallow && closed.endCapture,
              "menu S3b: a closed popup ends the capture and lets the message through");
        MusicKeyCaptureInput letter;
        letter.key = MusicKeyCaptureKey::Down;
        letter.vk = 'Q';
        const MusicKeyCaptureUpdate applied = FeedMusicKeyCapture(active, letter);
        Check(applied.result == MusicKeyCaptureResult::Captured && applied.capturedVk == 'Q',
              "menu S3b: a captured key is applied to its row");
        MusicKeyCaptureInput enter;
        enter.key = MusicKeyCaptureKey::Down;
        enter.vk = kMusicKeyReturn;
        const MusicKeyCaptureUpdate cancelled = FeedMusicKeyCapture(StartMusicKeyCapture(MusicKeyAction::Overlay),
                                                                   enter);
        Check(cancelled.result == MusicKeyCaptureResult::Cancelled,
              "menu S3b: Enter cancels the capture instead of applying it");
    }

    // S3b: the popup row text comes from the mod state, not from a stale buffer.
    {
        MusicPopupTextInput input;
        input.overlayMode = MusicOverlayMode::Pinned;
        input.keyVk[0] = '2';
        input.keyVk[1] = '1';
        input.keyVk[2] = '3';
        input.keyVk[3] = '4';
        input.keyVk[4] = '5';
        Check(ReproductorPopupRowText(0, input) == "Reproductor" &&
                  ReproductorPopupRowText(8, input) == "Volver",
              "menu S3b: the popup row text serves the header and Volver");
        Check(ReproductorPopupRowText(1, input) == "Cassette: cargando...",
              "menu S3b: the Cassette row reads loading until the scan finishes");
        input.libraryReady = true;
        input.cassetteLabels = {L"Road Rash", L"Musica nueva 1"};
        input.savedCassetteLabel = L"Musica nueva 1";
        Check(ReproductorPopupRowText(1, input) == "Cassette: Musica nueva 1",
              "menu S3b: once the scan is done the Cassette row names the saved cassette");
        input.cassetteResolved = true;
        input.selectedCassette = 0;
        Check(ReproductorPopupRowText(1, input) == "Cassette: Road Rash",
              "menu S3b: a resolved popup pick wins over the saved label in the row text");
        Check(ReproductorPopupRowText(2, input) == "Beeper: Fijo",
              "menu S3b: the Beeper row shows the configured mode");
        Check(ReproductorPopupRowText(3, input) == "Siguiente: 2" &&
                  ReproductorPopupRowText(7, input) == "Mostrar beeper: 5",
              "menu S3b: the five key rows show their Windows key names");
        MusicPopupTextInput capturing = input;
        capturing.capture = StartMusicKeyCapture(MusicKeyAction::PlayPause);
        Check(ReproductorPopupRowText(5, capturing) == "Pausa: presione una tecla" &&
                  ReproductorPopupRowText(3, capturing) == "Siguiente: 2",
              "menu S3b: only the capturing key row shows the capture text");
        Check(ReproductorPopupRowText(-1, input) == "Reproductor" &&
                  ReproductorPopupRowText(9, input) == "Reproductor",
              "menu S3b: an unknown popup row returns the header text, never a stale buffer");
    }

    // S3b: the engine asks for a C string in a fixed buffer.
    {
        char buffer[kReproductorTextBufferSize];
        std::memset(buffer, 0x7F, sizeof(buffer));
        CopyReproductorText(buffer, sizeof(buffer), "Beeper: Fijo");
        Check(std::strcmp(buffer, "Beeper: Fijo") == 0,
              "menu S3b: the item text buffer holds the row text and a trailing NUL");
        const std::string longText(kReproductorTextBufferSize + 20, 'x');
        CopyReproductorText(buffer, sizeof(buffer), longText);
        Check(std::strlen(buffer) == kReproductorTextBufferSize - 1 &&
                  std::memcmp(buffer, longText.data(), kReproductorTextBufferSize - 1) == 0 &&
                  buffer[kReproductorTextBufferSize - 1] == '\0',
              "menu S3b: an over-long item text is cut to 95 bytes plus the NUL");
        char small[4] = {1, 2, 3, 4};
        CopyReproductorText(small, sizeof(small), "Volver");
        Check(std::strcmp(small, "Vol") == 0,
              "menu S3b: the copy respects a smaller buffer capacity");
        char untouched[2] = {1, 2};
        CopyReproductorText(untouched, 0, "Volver");
        CopyReproductorText(nullptr, 8, "Volver");
        Check(untouched[0] == 1 && untouched[1] == 2,
              "menu S3b: a zero capacity or a null buffer writes nothing");
    }
}

void TestMusicFallbackBinding() {
    using namespace rr95::music;

    Decisions disabled;
    bool anyOwn = false;
    for (int i = 0; i < static_cast<int>(Capability::Count); ++i) {
        anyOwn = anyOwn || ShouldInstallFallbackSite(disabled, static_cast<Capability>(i));
    }
    Check(!anyOwn, "binding S4: a disabled decision installs no fallback site");

    Decisions wait;
    wait.raceFrames = Binding::Wait;
    wait.menuFrames = Binding::Wait;
    wait.graphicsMenuRow = Binding::Wait;
    wait.itemText = Binding::Wait;
    wait.overlayLayer = Binding::Wait;
    Check(!ShouldInstallFallbackSite(wait, Capability::RaceFrames) &&
              !ShouldInstallFallbackSite(wait, Capability::MenuFrames) &&
              !ShouldInstallFallbackSite(wait, Capability::GraphicsMenuRow) &&
              !ShouldInstallFallbackSite(wait, Capability::ItemText) &&
              !ShouldInstallFallbackSite(wait, Capability::OverlayLayer),
          "binding S4: a wait installs no fallback site");

    Decisions api;
    api.raceFrames = Binding::UseApi;
    api.menuFrames = Binding::UseApi;
    api.graphicsMenuRow = Binding::UseApi;
    api.itemText = Binding::UseApi;
    api.overlayLayer = Binding::UseApi;
    Check(!ShouldInstallFallbackSite(api, Capability::RaceFrames) &&
              !ShouldInstallFallbackSite(api, Capability::MenuFrames) &&
              !ShouldInstallFallbackSite(api, Capability::GraphicsMenuRow) &&
              !ShouldInstallFallbackSite(api, Capability::ItemText) &&
              !ShouldInstallFallbackSite(api, Capability::OverlayLayer),
          "binding S4: a capability the general mod owns never gets a fallback site");

    Decisions own = api;
    own.raceFrames = Binding::OwnHooks;
    own.menuFrames = Binding::OwnHooks;
    own.graphicsMenuRow = Binding::OwnHooks;
    own.itemText = Binding::OwnHooks;
    own.overlayLayer = Binding::OwnHooks;
    Check(ShouldInstallFallbackSite(own, Capability::RaceFrames) &&
              ShouldInstallFallbackSite(own, Capability::MenuFrames) &&
              ShouldInstallFallbackSite(own, Capability::GraphicsMenuRow) &&
              ShouldInstallFallbackSite(own, Capability::ItemText) &&
              ShouldInstallFallbackSite(own, Capability::OverlayLayer),
          "binding S4: own hooks install the fallback site for that capability");

    Decisions mixed = own;
    mixed.itemText = Binding::UseApi;
    Check(ShouldInstallFallbackSite(mixed, Capability::GraphicsMenuRow) &&
              !ShouldInstallFallbackSite(mixed, Capability::ItemText),
          "binding S4: an applied menu without the item text detour owns only the item text site");

    Check(ShouldInstallGdiOverlayHook(own, false),
          "binding S4: own overlay hooks without a general mod blend the game DIB");
    Check(!ShouldInstallGdiOverlayHook(own, true),
          "binding S4: a loaded general mod keeps GDI untouched even when its hooks failed");
    Check(!ShouldInstallGdiOverlayHook(api, true) && !ShouldInstallGdiOverlayHook(api, false),
          "binding S4: the API overlay layer never hooks GDI");
    Decisions noOverlay = own;
    noOverlay.overlayLayer = Binding::Disabled;
    Check(!ShouldInstallGdiOverlayHook(noOverlay, false),
          "binding S4: a disabled overlay layer never hooks GDI");
}

void TestMusicMenuInPlace() {
    using namespace rr95::music;

    auto readU32 = [](const uint8_t* p, size_t off) {
        uint32_t v = 0;
        std::memcpy(&v, p + off, sizeof(v));
        return v;
    };

    const MusicMenuInPlacePlan install = PlanMusicMenuInPlace(true, true, true, false);
    Check(install.action == MusicMenuInPlaceAction::Install && install.write &&
              install.logMessage[0] != '\0',
          "menu S4: own row and item text with shipped bytes write the row in place");

    const MusicMenuInPlacePlan done = PlanMusicMenuInPlace(true, true, true, true);
    Check(done.action == MusicMenuInPlaceAction::AlreadyInstalled && !done.write &&
              done.logMessage[0] != '\0',
          "menu S4: the in-place row is written once");

    const MusicMenuInPlacePlan changed = PlanMusicMenuInPlace(true, true, false, false);
    Check(changed.action == MusicMenuInPlaceAction::NotShipped && !changed.write &&
              changed.logMessage[0] != '\0',
          "menu S4: an array that does not match the shipped bytes is never written");

    const MusicMenuInPlacePlan apiRow = PlanMusicMenuInPlace(false, true, true, false);
    const MusicMenuInPlacePlan apiText = PlanMusicMenuInPlace(true, false, true, false);
    Check(apiRow.action == MusicMenuInPlaceAction::NotOwned && !apiRow.write &&
              apiText.action == MusicMenuInPlaceAction::NotOwned && !apiText.write,
          "menu S4: the in-place row needs the row and the item text capabilities");
    Check(install.logMessage != done.logMessage && install.logMessage != changed.logMessage &&
              install.logMessage != apiRow.logMessage && changed.logMessage != apiRow.logMessage,
          "menu S4: every in-place plan logs its own line");

    // Install and restore on the pure slot builders: slot 7 becomes the row, the snapshot restores.
    std::vector<uint8_t> snapshot(kGraphicsMenuSlotsSize, 0);
    ShippedGraphicsMenuSlots(0, snapshot.data());
    std::vector<uint8_t> live = snapshot;
    GraphicsMenuModRows rows;
    rows.reproductor = true;
    rows.reproductorHandler = 0x11223344;
    BuildGraphicsMenuSlots(snapshot.data(), ComputeGraphicsMenuLayout(false, true), rows, live.data());
    Check(!GraphicsMenuSlotsAreShipped(live.data(), 0),
          "menu S4: the written array no longer verifies as shipped");
    Check(readU32(live.data(), kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemHandler) == 0x11223344 &&
              readU32(live.data(), kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemRect) == 0 &&
              readU32(live.data(), kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemRect + 4) == 358 &&
              readU32(live.data(), kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemRect + 8) == 180 &&
              readU32(live.data(), kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemRect + 12) == 48 &&
              live[kGraphicsMenuReproductorSlot * kMenuItemSize + kMenuItemEnabled] == 1 &&
              live[kGraphicsMenuLastSlot * kMenuItemSize + kMenuItemNavUp] ==
                  static_cast<uint8_t>(kGraphicsMenuReproductorSlot),
          "menu S4: slot 7 holds the Reproductor row above Ya esta in the shipped eight-item menu");
    Check(live[kGraphicsMenuLastSlot * kMenuItemSize + kMenuItemNavDown] == 0,
          "menu S4: Ya esta still closes the ring on slot 0");
    std::memcpy(live.data(), snapshot.data(), kGraphicsMenuSlotsSize);
    Check(GraphicsMenuSlotsAreShipped(live.data(), 0),
          "menu S4: writing the snapshot back restores the shipped menu byte for byte");
}

void TestMusicItemTextOwner() {
    using namespace rr95::music;

    // The item address is written out independently of the helper, so the check below is not
    // tautological: the helper must derive the same slot the ingame write targets.
    const uintptr_t row =
        static_cast<uintptr_t>(kVaGraphicsMenuItems + kGraphicsMenuReproductorSlot * 0x38u);
    const uintptr_t rowItem =
        static_cast<uintptr_t>(MusicGraphicsMenuItemVa(kGraphicsMenuReproductorSlot));
    const uintptr_t popup = 0x10000000u;
    const size_t popupBytes = static_cast<size_t>(kReproductorPopupItemCount) * kPopupItemSize;

    Check(row == 0x005a0a30u && rowItem == row,
          "menu S4: the in-place row address is the shipped base plus the Reproductor slot");

    const MusicItemTextTarget menuRow = DecideMusicItemTextOwner(row, true, rowItem, popup, popupBytes);
    Check(menuRow.owner == MusicItemTextOwner::MenuRow && menuRow.popupRow == -1,
          "menu S4: the installed row answers with the Reproductor text");

    const MusicItemTextTarget beforeInstall = DecideMusicItemTextOwner(row, false, rowItem, popup, popupBytes);
    Check(beforeInstall.owner == MusicItemTextOwner::Original,
          "menu S4: the row is answered only once the in-place install ran");

    const uintptr_t neighbour =
        static_cast<uintptr_t>(MusicGraphicsMenuItemVa(kGraphicsMenuLastSlot));
    const MusicItemTextTarget otherRow = DecideMusicItemTextOwner(neighbour, true, rowItem, popup, popupBytes);
    Check(otherRow.owner == MusicItemTextOwner::Original,
          "menu S4: the slot above the Reproductor row keeps the game's own text");

    const MusicItemTextTarget first = DecideMusicItemTextOwner(popup, true, row, popup, popupBytes);
    const MusicItemTextTarget last =
        DecideMusicItemTextOwner(popup + popupBytes - kPopupItemSize, true, row, popup, popupBytes);
    Check(first.owner == MusicItemTextOwner::PopupRow && first.popupRow == 0 &&
              last.owner == MusicItemTextOwner::PopupRow && last.popupRow == kReproductorPopupItemCount - 1,
          "menu S4: every popup item answers with its own row");

    const MusicItemTextTarget past = DecideMusicItemTextOwner(popup + popupBytes, true, row, popup, popupBytes);
    const MusicItemTextTarget misaligned =
        DecideMusicItemTextOwner(popup + kPopupItemSize + 4, true, row, popup, popupBytes);
    const MusicItemTextTarget nullItem = DecideMusicItemTextOwner(0, true, row, popup, popupBytes);
    Check(past.owner == MusicItemTextOwner::Original && misaligned.owner == MusicItemTextOwner::Original &&
              nullItem.owner == MusicItemTextOwner::Original,
          "menu S4: an item outside the popup array keeps the game's own text");

    const MusicItemTextTarget noPopup =
        DecideMusicItemTextOwner(row, true, row, 0, popupBytes);
    Check(noPopup.owner == MusicItemTextOwner::MenuRow,
          "menu S4: a missing popup array never hides the row text");
}

void TestMusicOverlayDibBlend() {
    using namespace rr95::music;

    // The game's DIB holds 0x00RRGGBB dwords; the panel is premultiplied BGRA.
    std::vector<uint32_t> dib(4 * 3, 0x00204060u);
    MusicOverlayBitmap panel;
    panel.width = 2;
    panel.height = 1;
    panel.pixels = {MusicOverlayBgra(50, 20, 0, 128), MusicOverlayBgra(0, 200, 0, 255)};
    BlendMusicOverlayIntoDib(dib.data(), 4, 3, MusicDibOrientation::TopDown, panel, 1, 1);
    Check(dib[1 * 4 + 1] == 0x00423430u,
          "overlay S4: a half transparent pixel blends premultiplied over the DIB RGB");
    Check((dib[1 * 4 + 1] >> 24) == 0,
          "overlay S4: the blend keeps the DIB high byte zero for GDI BI_RGB");
    Check(dib[1 * 4 + 2] == 0x0000C800u,
          "overlay S4: an opaque pixel replaces the DIB pixel");
    Check(dib[1 * 4 + 0] == 0x00204060u && dib[0 * 4 + 1] == 0x00204060u && dib[2 * 4 + 1] == 0x00204060u,
          "overlay S4: pixels next to the panel are untouched");

    // Clipping: a panel that starts left of the DIB keeps only the visible column.
    std::vector<uint32_t> clipped(4 * 3, 0x00204060u);
    MusicOverlayBitmap corner;
    corner.width = 2;
    corner.height = 2;
    corner.pixels = {MusicOverlayBgra(1, 2, 3, 255), MusicOverlayBgra(4, 5, 6, 255),
                     MusicOverlayBgra(7, 8, 9, 255), MusicOverlayBgra(10, 11, 12, 255)};
    BlendMusicOverlayIntoDib(clipped.data(), 4, 3, MusicDibOrientation::TopDown, corner, -1, 2);
    Check(clipped[2 * 4 + 0] == 0x00040506u && clipped[2 * 4 + 1] == 0x00204060u &&
              clipped[2 * 4 + 2] == 0x00204060u,
          "overlay S4: a negative x clips the panel column by column");
    Check(clipped[1 * 4 + 0] == 0x00204060u,
          "overlay S4: a clipped panel never writes a row above its own y");

    std::vector<uint32_t> outside(4 * 3, 0x00204060u);
    const std::vector<uint32_t> original = outside;
    BlendMusicOverlayIntoDib(outside.data(), 4, 3, MusicDibOrientation::TopDown, panel, 4, 0);
    BlendMusicOverlayIntoDib(outside.data(), 4, 3, MusicDibOrientation::TopDown, panel, 0, -5);
    Check(outside == original,
          "overlay S4: a panel outside the DIB writes nothing");

    // A transparent pixel adds nothing, a null DIB or an empty panel is ignored.
    std::vector<uint32_t> transparent(1, 0x00204060u);
    MusicOverlayBitmap clearPixel;
    clearPixel.width = 1;
    clearPixel.height = 1;
    clearPixel.pixels = {MusicOverlayBgra(30, 40, 50, 0)};
    BlendMusicOverlayIntoDib(transparent.data(), 1, 1, MusicDibOrientation::TopDown, clearPixel, 0, 0);
    Check(transparent[0] == 0x00204060u,
          "overlay S4: a fully transparent panel pixel adds nothing");
    MusicOverlayBitmap empty;
    BlendMusicOverlayIntoDib(nullptr, 4, 3, MusicDibOrientation::TopDown, panel, 0, 0);
    BlendMusicOverlayIntoDib(dib.data(), 4, 3, MusicDibOrientation::TopDown, empty, 0, 0);
    MusicOverlayBitmap tooShort = panel;
    tooShort.pixels.pop_back();
    BlendMusicOverlayIntoDib(dib.data(), 4, 3, MusicDibOrientation::TopDown, tooShort, 0, 0);
    Check(dib[0] == 0x00204060u,
          "overlay S4: a null DIB, an empty panel or a short buffer writes nothing");

    // S3b review R3-dib-orientation-unchecked: the header decides the row order, and a frame that is
    // not the engine's 32-bit frame is skipped with a one-line reason.
    {
        const MusicDibFramePlan topDown = PlanMusicDibFrame(32, 640, -480, 640, 480);
        const MusicDibFramePlan bottomUp = PlanMusicDibFrame(32, 640, 480, 640, 480);
        Check(topDown.blend && topDown.orientation == MusicDibOrientation::TopDown && bottomUp.blend &&
                  bottomUp.orientation == MusicDibOrientation::BottomUp,
              "overlay S4: a negative biHeight is top-down and a positive one is bottom-up");
        const MusicDibFramePlan narrow = PlanMusicDibFrame(32, 320, -480, 640, 480);
        const MusicDibFramePlan tall = PlanMusicDibFrame(32, 640, -400, 640, 480);
        const MusicDibFramePlan sixteen = PlanMusicDibFrame(16, 640, -480, 640, 480);
        const MusicDibFramePlan flat = PlanMusicDibFrame(32, 640, 0, 640, 480);
        Check(!narrow.blend && !tall.blend && !sixteen.blend && !flat.blend &&
                  narrow.logMessage[0] != '\0' && tall.logMessage != narrow.logMessage &&
                  sixteen.logMessage != tall.logMessage && flat.logMessage != sixteen.logMessage,
              "overlay S4: a different width, height or bit count is skipped with its own log line");
    }

    // Orientation: the same visual row lands on the first memory row of a top-down DIB and on the
    // last one of a bottom-up DIB.
    {
        MusicOverlayBitmap pixel;
        pixel.width = 1;
        pixel.height = 1;
        pixel.pixels = {MusicOverlayBgra(10, 20, 30, 255)};
        std::vector<uint32_t> topDownDib(4 * 3, 0x00204060u);
        std::vector<uint32_t> bottomUpDib(4 * 3, 0x00204060u);
        BlendMusicOverlayIntoDib(topDownDib.data(), 4, 3, MusicDibOrientation::TopDown, pixel, 1, 0);
        BlendMusicOverlayIntoDib(bottomUpDib.data(), 4, 3, MusicDibOrientation::BottomUp, pixel, 1, 0);
        Check(topDownDib[0 * 4 + 1] == 0x000A141Eu && topDownDib[2 * 4 + 1] == 0x00204060u,
              "overlay S4: a top-down DIB writes visual row 0 at the top");
        Check(bottomUpDib[2 * 4 + 1] == 0x000A141Eu && bottomUpDib[0 * 4 + 1] == 0x00204060u,
              "overlay S4: a bottom-up DIB writes visual row 0 at the bottom");
    }

    // S3b review R3-dib-blend-persistence: the present saves the covered pixels and writes them back,
    // so a repeated present of the same DIB blends the same output and hiding leaves no trace.
    {
        const int width = 4;
        const int height = 3;
        std::vector<uint32_t> frame(width * height, 0x00204060u);
        const std::vector<uint32_t> clean = frame;
        MusicDibSavedRegion saved;
        Check(SaveMusicDibRegion(frame.data(), width, height, MusicDibOrientation::TopDown, panel, 1, 1,
                                 saved),
              "overlay S4: the present saves the region the panel covers");
        BlendMusicOverlayIntoDib(frame.data(), width, height, MusicDibOrientation::TopDown, panel, 1, 1);
        const std::vector<uint32_t> firstOutput = frame;
        Check(firstOutput != clean, "overlay S4: the panel is visible in the blended frame");
        RestoreMusicDibRegion(frame.data(), saved);
        Check(frame == clean, "overlay S4: the game DIB is byte for byte unchanged after the blit");
        Check(SaveMusicDibRegion(frame.data(), width, height, MusicDibOrientation::TopDown, panel, 1, 1,
                                 saved),
              "overlay S4: a second present saves its region again");
        BlendMusicOverlayIntoDib(frame.data(), width, height, MusicDibOrientation::TopDown, panel, 1, 1);
        Check(frame == firstOutput,
              "overlay S4: a repeated present of the same DIB blends the same output");
        RestoreMusicDibRegion(frame.data(), saved);
        Check(frame == clean, "overlay S4: the second present leaves no trace either");
        MusicOverlayBitmap hidden;
        Check(!SaveMusicDibRegion(frame.data(), width, height, MusicDibOrientation::TopDown, hidden, 1, 1,
                                  saved) &&
                  frame == clean,
              "overlay S4: a hidden panel saves nothing and leaves no trace");
        Check(!SaveMusicDibRegion(frame.data(), width, height, MusicDibOrientation::TopDown, panel, width, 0,
                                  saved) &&
                  !SaveMusicDibRegion(frame.data(), width, height, MusicDibOrientation::TopDown, panel, 0, -5,
                                      saved),
              "overlay S4: a panel outside the DIB has no region to save");
    }

    // S3b review R3-blend-channel-overflow: a non-premultiplied pixel saturates each channel instead
    // of spilling into the next one.
    {
        std::vector<uint32_t> saturated(1, 0x00204060u);
        MusicOverlayBitmap bright;
        bright.width = 1;
        bright.height = 1;
        bright.pixels = {MusicOverlayBgra(255, 255, 255, 10)};
        BlendMusicOverlayIntoDib(saturated.data(), 1, 1, MusicDibOrientation::TopDown, bright, 0, 0);
        Check(saturated[0] == 0x00FFFFFFu,
              "overlay S4: every channel clamps to 255 and the result keeps the high byte zero");
    }
    // S4 review R3-dib-sequence-guard-drops-reblit: the DIB is restored after every blit, so two
    // blits of the same frame both carry the panel and the DIB ends byte for byte unchanged. Only a
    // reentrant blend inside one blit is refused.
    {
        MusicDibBlitTracker tracker;
        std::vector<uint32_t> frame(4 * 3, 0x00204060u);
        const std::vector<uint32_t> untouched = frame;
        MusicOverlayBitmap beeper;
        beeper.width = 2;
        beeper.height = 1;
        beeper.pixels = {MusicOverlayBgra(50, 20, 0, 128), MusicOverlayBgra(0, 200, 0, 255)};
        bool panelInBlit[2] = {false, false};
        for (int blit = 0; blit < 2; ++blit) {
            const bool begin = BeginMusicDibBlit(tracker, 7);  // the same frame sample both times
            MusicDibSavedRegion blitSaved;
            const bool blended =
                begin && SaveMusicDibRegion(frame.data(), 4, 3, MusicDibOrientation::TopDown, beeper, 1, 1,
                                            blitSaved);
            if (blended) {
                BlendMusicOverlayIntoDib(frame.data(), 4, 3, MusicDibOrientation::TopDown, beeper, 1, 1);
            }
            panelInBlit[blit] = blended && frame[1 * 4 + 1] == 0x00423430u;
            if (blended) {
                RestoreMusicDibRegion(frame.data(), blitSaved);
            }
            EndMusicDibBlit(tracker, begin);
        }
        Check(panelInBlit[0] && panelInBlit[1],
              "overlay S4: two blits of the same frame both carry the beeper panel");
        Check(frame == untouched, "overlay S4: every blit restores the game DIB, so it ends unchanged");
        Check(BeginMusicDibBlit(tracker, 7) && !BeginMusicDibBlit(tracker, 7),
              "overlay S4: a reentrant blend inside one blit is refused");
        EndMusicDibBlit(tracker, true);
    }
    // S5 review R3-dib-blit-tracker-refused-call-clears-active: the inner StretchDIBits calls
    // EndMusicDibBlit with the refused begin result, so the outer blit stays active until it ends.
    {
        MusicDibBlitTracker tracker;
        const bool outer = BeginMusicDibBlit(tracker, 9);
        const bool inner = BeginMusicDibBlit(tracker, 9);
        EndMusicDibBlit(tracker, inner);  // a refused call has no blit to end
        const bool outerStillActive = !BeginMusicDibBlit(tracker, 9);
        EndMusicDibBlit(tracker, outer);
        const bool afterOuter = BeginMusicDibBlit(tracker, 9);
        EndMusicDibBlit(tracker, afterOuter);
        Check(outer && !inner && outerStillActive && afterOuter,
              "overlay S5: a refused reentrant blit never clears the outer blit");
    }
}

}  // namespace

int main() {
    TestBindingDecision();
    TestApiTableValidation();
    TestBindingRetry();
    TestMusicEarlyRow();
    TestMusicConfig();
    TestMusicEnabledIniProbe();
    TestExportTable();
    TestMusicRegistryValues();
    TestMusicConfigAndHotkeys();
    TestMusicLibraryAndTags();
    TestMusicStateMachineAndEngine();
    TestMusicPlayerReliability();
    TestMusicOverlay();
    TestMusicRaceWiring();
    TestMusicMenu();
    TestMusicFallbackBinding();
    TestMusicMenuInPlace();
    TestMusicItemTextOwner();
    TestMusicOverlayDibBlend();

    std::printf("%s (%d failure(s))\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
