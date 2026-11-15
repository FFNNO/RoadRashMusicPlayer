#include "music_player.h"

#include "audio_decoders.h"
#include "shared/log.h"
#include "music_game.h"
#include "music_tags.h"

#include <objbase.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <mmreg.h>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

// ---------------------------------------------------------------------------
// Pure state machine (device-free, fully unit tested)
// ---------------------------------------------------------------------------

void MusicPlayerStateMachine::Start(std::vector<MusicCassette> cassettes, int cassetteIndex, bool shuffle,
                                    uint32_t seed, int startTrack) {
    m_cassettes = std::move(cassettes);
    m_display.clear();
    m_order.clear();
    m_cassetteIndex = -1;
    m_position = -1;
    m_shuffle = shuffle;
    m_seed = seed;
    m_positionMs = 0;
    m_durationMs = 0;
    m_pendingFailure.clear();

    if (m_cassettes.empty() || cassetteIndex < 0 || cassetteIndex >= static_cast<int>(m_cassettes.size()) ||
        m_cassettes[static_cast<size_t>(cassetteIndex)].tracks.empty()) {
        m_state = MusicPlayerState::Stopped;
        return;
    }

    m_cassetteIndex = cassetteIndex;
    SelectTrack(startTrack);
    m_state = MusicPlayerState::Playing;
    BumpChange();
}

bool MusicPlayerStateMachine::Next() {
    if (m_state == MusicPlayerState::Stopped || m_order.empty()) {
        return false;
    }
    m_position = NextPlayPosition(m_position, static_cast<int>(m_order.size()));
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
    return true;
}

bool MusicPlayerStateMachine::Previous() {
    if (m_state == MusicPlayerState::Stopped || m_order.empty()) {
        return false;
    }
    m_position = PreviousPlayPosition(m_position, static_cast<int>(m_order.size()));
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
    return true;
}

bool MusicPlayerStateMachine::TogglePause() {
    if (m_state == MusicPlayerState::Stopped) {
        return false;
    }
    m_state = (m_state == MusicPlayerState::Playing) ? MusicPlayerState::Paused : MusicPlayerState::Playing;
    return true;
}

bool MusicPlayerStateMachine::NextCassette() {
    const int count = static_cast<int>(m_cassettes.size());
    if (count <= 0) {
        return false;
    }
    int candidate = (m_cassetteIndex < 0) ? -1 : m_cassetteIndex;
    bool found = false;
    for (int step = 0; step < count; ++step) {
        candidate = NextCassetteIndex(candidate, count);
        if (candidate >= 0 && !m_cassettes[static_cast<size_t>(candidate)].tracks.empty()) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    m_cassetteIndex = candidate;
    SelectFirstTrack();
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
    return true;
}

void MusicPlayerStateMachine::RaceStarted() {
    if (m_state == MusicPlayerState::Playing) {
        return;
    }
    if (m_state == MusicPlayerState::Paused) {
        m_state = MusicPlayerState::Playing;
        return;
    }
    if (m_cassettes.empty()) {
        return;
    }
    if (m_cassetteIndex < 0 || m_cassetteIndex >= static_cast<int>(m_cassettes.size()) ||
        m_cassettes[static_cast<size_t>(m_cassetteIndex)].tracks.empty()) {
        return;
    }
    SelectFirstTrack();
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
}

void MusicPlayerStateMachine::RaceEnded() {
    if (m_state == MusicPlayerState::Playing) {
        m_state = MusicPlayerState::Paused;
    }
}

void MusicPlayerStateMachine::TrackFinished() {
    if (m_state == MusicPlayerState::Stopped || m_order.empty()) {
        return;
    }
    m_position = NextPlayPosition(m_position, static_cast<int>(m_order.size()));
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
}

void MusicPlayerStateMachine::TrackFailed() {
    const MusicTrack* track = CurrentTrack();
    if (track == nullptr) {
        m_state = MusicPlayerState::Stopped;
        return;
    }
    const std::wstring path = track->relativePath;
    if (!IsTrackFailed(path)) {
        m_failedPaths.push_back(path);
        m_pendingFailure = path;
    }
    if (AllTracksFailed()) {
        m_state = MusicPlayerState::Stopped;
        return;
    }
    if (!AdvanceSkippingFailed()) {
        m_state = MusicPlayerState::Stopped;
        return;
    }
    m_state = MusicPlayerState::Playing;
    m_positionMs = 0;
    m_durationMs = 0;
    BumpChange();
}

void MusicPlayerStateMachine::Stop() {
    // A consumer that refreshes on the change counter must see the engine failure: bump it when the
    // state actually changes, but keep a repeated Stop a no-op.
    const bool wasActive = m_state != MusicPlayerState::Stopped;
    m_state = MusicPlayerState::Stopped;
    m_position = -1;
    m_cassetteIndex = -1;
    m_positionMs = 0;
    m_durationMs = 0;
    m_pendingFailure.clear();
    if (wasActive) {
        BumpChange();
    }
}

void MusicPlayerStateMachine::SetTrackDisplay(const std::wstring& relativePath, const std::wstring& title,
                                              const std::wstring& artist) {
    for (DisplayOverride& entry : m_display) {
        if (entry.relativePath == relativePath) {
            entry.title = title;
            entry.artist = artist;
            return;
        }
    }
    DisplayOverride entry;
    entry.relativePath = relativePath;
    entry.title = title;
    entry.artist = artist;
    m_display.push_back(std::move(entry));
}

void MusicPlayerStateMachine::SetProgress(uint64_t positionMs, uint64_t durationMs) {
    m_positionMs = positionMs;
    m_durationMs = durationMs;
}

std::wstring MusicPlayerStateMachine::TakeFailureWarning() {
    std::wstring warning;
    warning.swap(m_pendingFailure);
    return warning;
}

MusicPlayerNowPlaying MusicPlayerStateMachine::Snapshot() const {
    MusicPlayerNowPlaying snapshot;
    const MusicCassette* cassette = CurrentCassette();
    const MusicTrack* track = CurrentTrack();
    snapshot.active = m_state != MusicPlayerState::Stopped && cassette != nullptr && track != nullptr;
    snapshot.paused = m_state == MusicPlayerState::Paused;
    snapshot.cassetteIndex = (cassette != nullptr) ? m_cassetteIndex : -1;
    snapshot.cassetteLabel = (cassette != nullptr) ? cassette->label : std::wstring();
    snapshot.builtInCassette = cassette != nullptr && cassette->builtIn;
    snapshot.trackIndex = (track != nullptr) ? CurrentTrackIndex() : -1;
    snapshot.changeCounter = m_changeCounter;
    snapshot.positionMs = m_positionMs;
    snapshot.durationMs = m_durationMs;
    if (track != nullptr) {
        snapshot.title = track->name;
        const DisplayOverride* display = FindDisplay(track->relativePath);
        if (display != nullptr) {
            if (!display->title.empty()) {
                snapshot.title = display->title;
            }
            snapshot.artist = display->artist;
        }
    }
    return snapshot;
}

const MusicCassette* MusicPlayerStateMachine::CurrentCassette() const {
    if (m_cassetteIndex < 0 || m_cassetteIndex >= static_cast<int>(m_cassettes.size())) {
        return nullptr;
    }
    return &m_cassettes[static_cast<size_t>(m_cassetteIndex)];
}

const MusicTrack* MusicPlayerStateMachine::CurrentTrack() const {
    const MusicCassette* cassette = CurrentCassette();
    const int index = CurrentTrackIndex();
    if (cassette == nullptr || index < 0 || index >= static_cast<int>(cassette->tracks.size())) {
        return nullptr;
    }
    return &cassette->tracks[static_cast<size_t>(index)];
}

int MusicPlayerStateMachine::CurrentTrackIndex() const {
    if (m_position < 0 || m_position >= static_cast<int>(m_order.size())) {
        return -1;
    }
    return m_order[static_cast<size_t>(m_position)];
}

void MusicPlayerStateMachine::RebuildPlayOrder() {
    const MusicCassette* cassette = CurrentCassette();
    const int count = (cassette != nullptr) ? static_cast<int>(cassette->tracks.size()) : 0;
    m_order = BuildPlayOrder(count, m_shuffle, m_seed);
}

void MusicPlayerStateMachine::SelectFirstTrack() {
    RebuildPlayOrder();
    m_position = m_order.empty() ? -1 : 0;
}

void MusicPlayerStateMachine::SelectTrack(int trackIndex) {
    RebuildPlayOrder();
    m_position = m_order.empty() ? -1 : 0;
    if (trackIndex < 0) {
        return;
    }
    for (size_t i = 0; i < m_order.size(); ++i) {
        if (m_order[i] == trackIndex) {
            m_position = static_cast<int>(i);
            return;
        }
    }
}

bool MusicPlayerStateMachine::AdvanceSkippingFailed() {
    if (m_order.empty()) {
        return false;
    }
    const MusicCassette* cassette = CurrentCassette();
    if (cassette == nullptr) {
        return false;
    }
    const int size = static_cast<int>(m_order.size());
    int position = m_position;
    for (int step = 0; step < size; ++step) {
        position = NextPlayPosition(position, size);
        const int trackIndex = m_order[static_cast<size_t>(position)];
        if (trackIndex < 0 || trackIndex >= static_cast<int>(cassette->tracks.size())) {
            continue;
        }
        if (!IsTrackFailed(cassette->tracks[static_cast<size_t>(trackIndex)].relativePath)) {
            m_position = position;
            return true;
        }
    }
    return false;
}

void MusicPlayerStateMachine::BumpChange() {
    ++m_changeCounter;
}

bool MusicPlayerStateMachine::AllTracksFailed() const {
    const MusicCassette* cassette = CurrentCassette();
    if (cassette == nullptr || cassette->tracks.empty()) {
        return true;
    }
    for (const MusicTrack& track : cassette->tracks) {
        if (!IsTrackFailed(track.relativePath)) {
            return false;
        }
    }
    return true;
}

bool MusicPlayerStateMachine::IsTrackFailed(const std::wstring& relativePath) const {
    for (const std::wstring& failed : m_failedPaths) {
        if (failed == relativePath) {
            return true;
        }
    }
    return false;
}

const MusicPlayerStateMachine::DisplayOverride* MusicPlayerStateMachine::FindDisplay(
    const std::wstring& relativePath) const {
    for (const DisplayOverride& entry : m_display) {
        if (entry.relativePath == relativePath) {
            return &entry;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// XAudio2 output (one source voice, own engine and mastering voice, lazily created)
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kInvalidChangeCounter = 0xFFFFFFFFu;
constexpr int kQueuedBuffers = 4;  // about 400 ms of decoded audio ahead of the voice

using XAudio2CreateProc = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

// One XAudio2 source voice with the buffer accounting handed in by the engine. Each submitted
// buffer owns its samples and is released in OnBufferEnd, which runs on an XAudio2 worker thread.
class XAudio2Sink final : public MusicAudioSink, public IXAudio2VoiceCallback {
public:
    XAudio2Sink() = default;
    XAudio2Sink(const XAudio2Sink&) = delete;
    XAudio2Sink& operator=(const XAudio2Sink&) = delete;
    ~XAudio2Sink() override = default;

    bool Initialize(float volume) override {
        m_volume = volume;
        return EnsureEngine();
    }

    // Stops and flushes the previous track and prepares the voice for rate/channels. The voice is
    // recreated when the format changes, so 22.05, 44.1 and 48 kHz tracks all play.
    bool BeginTrack(uint32_t sampleRate, uint32_t channels, float volume,
                    MusicAudioCounter* counter) override {
        m_volume = volume;
        if (sampleRate == 0 || channels == 0 || channels > 2 || counter == nullptr) {
            return false;
        }
        ResetTrack();
        if (!EnsureFormat(sampleRate, channels)) {
            return false;
        }
        m_voicePrepared = false;
        m_paused = false;
        return true;
    }

    bool Submit(const float* samples, uint32_t frames, uint32_t channels,
                MusicAudioCounter* counter) override {
        if (m_voice == nullptr || counter == nullptr || samples == nullptr || frames == 0) {
            return false;
        }
        Buffer* buffer = new (std::nothrow) Buffer();
        if (buffer == nullptr) {
            return false;
        }
        const size_t sampleCount = static_cast<size_t>(frames) * channels;
        buffer->samples.assign(samples, samples + sampleCount);
        buffer->frames = frames;
        buffer->counter = counter;

        XAUDIO2_BUFFER xbuffer = {};
        xbuffer.AudioBytes = static_cast<UINT32>(buffer->samples.size() * sizeof(float));
        xbuffer.pAudioData = reinterpret_cast<const BYTE*>(buffer->samples.data());
        xbuffer.pContext = buffer;

        counter->inFlight.fetch_add(1);
        counter->framesRemaining.fetch_add(frames);
        if (FAILED(m_voice->SubmitSourceBuffer(&xbuffer))) {
            counter->inFlight.fetch_sub(1);
            counter->framesRemaining.fetch_sub(frames);
            delete buffer;
            return false;
        }
        if (!m_voicePrepared) {
            // M5c (finding D): a pause can land after BeginTrack and before this first Submit. Do
            // not start the voice then; Resume starts it once the pause is lifted.
            m_voicePrepared = true;
            if (MusicSinkStartsOnSubmit(m_paused)) {
                m_voice->Start(0, XAUDIO2_COMMIT_NOW);
            }
        }
        return true;
    }

    void Pause() {
        if (m_voice != nullptr && !m_paused) {
            m_voice->Stop(0, XAUDIO2_COMMIT_NOW);
            m_paused = true;
        }
    }

    void Resume() {
        if (m_voice == nullptr || !m_paused) {
            return;
        }
        if (MusicSinkStartsOnResume(m_paused, m_voicePrepared)) {
            m_voice->Start(0, XAUDIO2_COMMIT_NOW);
        }
        // Lift the pause even when the voice was not prepared yet, so the first Submit starts it.
        // Keeping it set here left the voice stopped while the engine believed it was playing.
        m_paused = false;
    }

    // Discards the queued audio of the current track. The engine keeps the counters alive until
    // their inFlight count falls to zero, so a callback that fires after this returns still reaches
    // valid memory.
    void ResetTrack() override {
        if (m_voice != nullptr) {
            m_voice->Stop(0, XAUDIO2_COMMIT_NOW);
            m_voice->FlushSourceBuffers();
        }
        m_voicePrepared = false;
        m_paused = false;
    }

    int FreeBuffers(const MusicAudioCounter* counter) const override {
        if (counter == nullptr) {
            return kQueuedBuffers;
        }
        const int inFlight = counter->inFlight.load();
        return (kQueuedBuffers - inFlight > 0) ? kQueuedBuffers - inFlight : 0;
    }

    bool Drained(const MusicAudioCounter* counter) const override {
        return counter == nullptr || counter->inFlight.load() <= 0;
    }

    uint64_t FramesRemaining(const MusicAudioCounter* counter) const override {
        if (counter == nullptr) {
            return 0;
        }
        return counter->framesRemaining.load();
    }

    void Shutdown() override {
        DestroyVoice();
        if (m_master != nullptr) {
            m_master->DestroyVoice();
            m_master = nullptr;
        }
        if (m_engine != nullptr) {
            m_engine->Release();
            m_engine = nullptr;
        }
        if (m_library != nullptr) {
            FreeLibrary(m_library);
            m_library = nullptr;
        }
        m_create = nullptr;
    }

    // IXAudio2VoiceCallback
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) override {
        Buffer* buffer = static_cast<Buffer*>(context);
        if (buffer != nullptr) {
            if (buffer->counter != nullptr) {
                buffer->counter->framesRemaining.fetch_sub(buffer->frames);
                buffer->counter->inFlight.fetch_sub(1);
            }
            delete buffer;
        }
    }
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

private:
    struct Buffer {
        std::vector<float> samples;
        MusicAudioCounter* counter = nullptr;
        uint32_t frames = 0;
    };

    bool EnsureEngine() {
        if (m_engine != nullptr) {
            return true;
        }
        if (m_library == nullptr) {
            m_library = LoadLibraryW(L"xaudio2_9.dll");
            if (m_library == nullptr) {
                return false;
            }
            m_create = reinterpret_cast<XAudio2CreateProc>(GetProcAddress(m_library, "XAudio2Create"));
            if (m_create == nullptr) {
                return false;
            }
        }
        if (m_create == nullptr) {
            return false;
        }
        if (FAILED(m_create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
            m_engine = nullptr;
            return false;
        }
        if (FAILED(m_engine->CreateMasteringVoice(&m_master))) {
            m_master = nullptr;
            m_engine->Release();
            m_engine = nullptr;
            return false;
        }
        return true;
    }

    bool EnsureFormat(uint32_t sampleRate, uint32_t channels) {
        if (m_engine == nullptr) {
            return false;
        }
        if (m_voice != nullptr && sampleRate == m_sampleRate && channels == m_channels) {
            return true;
        }
        DestroyVoice();
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = static_cast<WORD>(channels);
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = static_cast<WORD>(channels * sizeof(float));
        format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
        format.cbSize = 0;
        if (FAILED(m_engine->CreateSourceVoice(&m_voice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this))) {
            m_voice = nullptr;
            return false;
        }
        m_sampleRate = sampleRate;
        m_channels = channels;
        m_voice->SetVolume(m_volume);
        return true;
    }

    void DestroyVoice() {
        if (m_voice != nullptr) {
            m_voice->DestroyVoice();
            m_voice = nullptr;
        }
        m_sampleRate = 0;
        m_channels = 0;
    }

    HMODULE m_library = nullptr;
    XAudio2CreateProc m_create = nullptr;
    IXAudio2* m_engine = nullptr;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SourceVoice* m_voice = nullptr;
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    float m_volume = 1.0f;
    bool m_voicePrepared = false;  // Submit has queued a buffer; Start may be pending a Resume
    bool m_paused = false;
};

// ---------------------------------------------------------------------------
// Worker engine
// ---------------------------------------------------------------------------

enum class Command {
    LoadLibrary,
    RaceStarted,
    RaceStartedAtTrack,
    RaceEnded,
    Next,
    Previous,
    TogglePause,
    NextCassette,
    Shutdown
};

struct PendingCommand {
    Command command = Command::RaceEnded;
    int track = -1;
};

// Test-only factories (see MusicPlayerTestSetHooks). Empty in production.
MusicPlayerTestHooks g_testHooks;

class MusicPlayerEngine {
public:
    MusicPlayerEngine(const MusicConfig& config, std::vector<MusicCassette> cassettes,
                      std::wstring musicRoot, std::wstring gameFolder)
        : m_config(config),
          m_cassettes(std::move(cassettes)),
          m_musicRoot(std::move(musicRoot)),
          m_gameFolder(std::move(gameFolder)) {
        m_scanOnLoad = !m_musicRoot.empty() || !m_gameFolder.empty();
        if (!m_scanOnLoad) {
            m_libraryLoaded.store(true);
            m_hasPlayable.store(!m_cassettes.empty());
        }
        int volume = m_config.volume;
        volume = (volume < 0) ? 0 : (volume > 100) ? 100 : volume;
        m_volume = static_cast<float>(volume) / 100.0f;
        m_seed = GetTickCount();
        if (g_testHooks.createSink != nullptr) {
            m_output.reset(g_testHooks.createSink(g_testHooks.user));
        }
        if (!m_output) {
            m_output.reset(new XAudio2Sink());
        }
    }

    void Start() {
        HANDLE wake = nullptr;
        if (g_testHooks.createWakeEvent != nullptr) {
            wake = g_testHooks.createWakeEvent();
        } else {
            wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        if (wake == nullptr) {
            // R3-004: without a wake event the worker could only spin on a failed wait, so keep
            // the player disabled and start no thread at all.
            m_audioDisabled.store(true);
            LOG_WARN("music: cannot create the player wake event; the music player stays disabled");
            PublishSnapshot();
            return;
        }
        m_wakeEvent = wake;
        m_worker = std::thread(&MusicPlayerEngine::WorkerMain, this);
        // M5: scanning and building the cassette list happens on the worker, never on the game
        // thread. A race start posts after this command, so the library is ready by then.
        if (m_scanOnLoad) {
            Post(Command::LoadLibrary);
        }
    }

    void Post(Command command, int track = -1) {
        if (m_shutdown.load() || m_audioDisabled.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            m_commands.push_back(PendingCommand{command, track});
            if (m_wakeEvent != nullptr) {
                SetEvent(m_wakeEvent);
            }
        }
    }

    void Shutdown() {
        bool expected = false;
        if (!m_shutdown.compare_exchange_strong(expected, true)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            m_commands.push_back(PendingCommand{Command::Shutdown, -1});
            if (m_wakeEvent != nullptr) {
                SetEvent(m_wakeEvent);
            }
        }
        if (m_worker.joinable()) {
            m_worker.join();
        }
        std::lock_guard<std::mutex> lock(m_commandMutex);
        if (m_wakeEvent != nullptr) {
            CloseHandle(m_wakeEvent);
            m_wakeEvent = nullptr;
        }
    }

    MusicPlayerNowPlaying Snapshot() const {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshot;
    }

    size_t CounterPoolSize() const { return m_counterPoolSize.load(); }
    bool AudioDisabled() const { return m_audioDisabled.load(); }
    bool WorkerRunning() const { return m_worker.joinable(); }
    bool LibraryReady() const { return m_libraryLoaded.load(); }
    bool HasPlayableCassette() const { return m_hasPlayable.load(); }
    bool AutoPaused() const { return m_autoPaused.load(); }
    uint32_t AutoResumeCount() const { return m_autoResumes.load(); }
    bool HeartbeatActive() const { return m_heartbeatActive.load(); }

    std::vector<std::wstring> CassetteLabels() const {
        std::lock_guard<std::mutex> lock(m_cassetteMutex);
        std::vector<std::wstring> labels;
        labels.reserve(m_cassettes.size());
        for (const MusicCassette& cassette : m_cassettes) {
            labels.push_back(cassette.label);
        }
        return labels;
    }

    void SelectCassette(int index) {
        if (index < 0) {
            m_selectedCassette.store(-1);
            return;
        }
        std::lock_guard<std::mutex> lock(m_cassetteMutex);
        if (index < static_cast<int>(m_cassettes.size())) {
            m_selectedCassette.store(index);
        }
    }

    int SelectedCassette() const { return m_selectedCassette.load(); }


    // M5b: the race-frame path reports every frame. The worker compares the last report with its
    // own clock, so a game frozen in a modal loop (no frames, no heartbeat) auto-pauses the player.
    void Heartbeat(uint64_t nowMs) { m_heartbeatMs.store(nowMs); }

private:
    int SelectedCassetteIndex() const { return m_selectedCassette.load(); }

    void StartCassette(int index, int track) {
        std::lock_guard<std::mutex> lock(m_cassetteMutex);
        m_state.Start(m_cassettes, index, m_config.shuffle, m_seed, track);
    }

    void WorkerMain() {
        for (;;) {
            std::vector<PendingCommand> commands;
            {
                std::lock_guard<std::mutex> lock(m_commandMutex);
                commands.swap(m_commands);
            }
            bool quit = false;
            for (const PendingCommand& command : commands) {
                if (command.command == Command::Shutdown) {
                    quit = true;
                    break;
                }
                ApplyCommand(command);
            }
            if (quit) {
                break;
            }

            if (!m_audioDisabled.load()) {
                // Any player command comes from the race-frame path (hotkeys are polled there), so
                // it is proof that the game is not frozen: refresh the heartbeat with it.
                UpdateAutoPause(!commands.empty());
                ServicePlayback();
                PublishSnapshot();
            }

            // A disabled player has no per-tick work: sleep until a command (Shutdown included)
            // signals the event. The live player polls because XAudio2 callbacks are asynchronous.
            const bool activelyPlaying =
                m_state.State() == MusicPlayerState::Playing && !m_autoPaused.load();
            const DWORD timeout = m_audioDisabled.load() ? INFINITE : (activelyPlaying ? 5 : 20);
            WaitForSingleObject(m_wakeEvent, timeout);
        }

        m_output->Shutdown();
        m_decoder.reset();
        m_outputActive = false;
        if (m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
        PublishSnapshot();
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            m_snapshot.active = false;
            m_snapshot.paused = false;
        }
    }

    void ApplyCommand(const PendingCommand& pending) {
        if (m_audioDisabled.load()) {
            return;
        }
        switch (pending.command) {
            case Command::LoadLibrary:
                LoadMusicLibrary();
                break;
            case Command::RaceStarted: {
                // R3-001: any selected cassette, the built-in one (index 0) included, wins over the
                // current one. Only a start on the cassette that is already loaded resumes it.
                const int selected = SelectedCassetteIndex();
                // An unset selection keeps the loaded cassette; only a fresh player starts the built-in one.
                const int target = selected >= 0 ? selected
                                   : (m_state.State() == MusicPlayerState::Stopped ? 0
                                                                                : m_state.Snapshot().cassetteIndex);
                if (m_state.State() == MusicPlayerState::Stopped ||
                    m_state.Snapshot().cassetteIndex != target) {
                    StartCassette(target, -1);
                } else {
                    m_state.RaceStarted();
                }
                OpenHeartbeatWindow();
                break;
            }
            case Command::RaceStartedAtTrack: {
                const int selected = SelectedCassetteIndex();
                // An unset selection keeps the loaded cassette; only a fresh player starts the built-in one.
                const int target = selected >= 0 ? selected
                                   : (m_state.State() == MusicPlayerState::Stopped ? 0
                                                                                : m_state.Snapshot().cassetteIndex);
                const bool switchCassette = m_state.State() == MusicPlayerState::Stopped ||
                                            m_state.Snapshot().cassetteIndex != target;
                if (switchCassette) {
                    // A cassette picked in the Reproductor menu wins over the jukebox pick. The
                    // built-in cassette (index 0) starts at the pick; user cassettes ignore it.
                    StartCassette(target, target == 0 ? pending.track : -1);
                } else if (target == 0) {
                    const MusicPlayerNowPlaying now = m_state.Snapshot();
                    // A saved jukebox pick wins over a paused built-in track: restart the cassette
                    // at that track. Otherwise just resume where the race last stopped.
                    if (pending.track >= 0 && now.builtInCassette && now.trackIndex != pending.track) {
                        StartCassette(now.cassetteIndex, pending.track);
                    } else {
                        m_state.RaceStarted();
                    }
                } else {
                    m_state.RaceStarted();
                }
                OpenHeartbeatWindow();
                break;
            }
            case Command::RaceEnded:
                m_state.RaceEnded();
                ClearHeartbeatWindow();
                break;
            case Command::Next:
                m_state.Next();
                break;
            case Command::Previous:
                m_state.Previous();
                break;
            case Command::TogglePause:
                m_state.TogglePause();
                break;
            case Command::NextCassette:
                // The hotkey cassette becomes the selection, so the next race keeps it.
                if (m_state.NextCassette()) {
                    m_selectedCassette.store(m_state.Snapshot().cassetteIndex);
                }
                break;
            case Command::Shutdown:
                break;
        }
    }

    uint64_t NowMs() const {
        return (g_testHooks.nowMs != nullptr) ? g_testHooks.nowMs() : GetTickCount64();
    }

    // M5b: a race start opens the heartbeat window. Race frames then keep the timestamp fresh; when
    // they stop (Esc pause, end-of-race video, focus loss) the worker sees the gap by itself.
    void OpenHeartbeatWindow() {
        m_heartbeatMs.store(NowMs());
        m_autoPaused.store(false);
        m_heartbeatActive.store(true);
    }

    void ClearHeartbeatWindow() {
        m_heartbeatActive.store(false);
        m_autoPaused.store(false);
    }

    // Runs only on the worker: the game thread may be stuck inside its modal loop. The user pause is
    // the state machine's own Paused state and is never touched here.
    void UpdateAutoPause(bool commandActivity) {
        if (!m_heartbeatActive.load()) {
            m_autoPaused.store(false);
            return;
        }
        if (commandActivity) {
            m_heartbeatMs.store(NowMs());
        }
        const uint64_t now = NowMs();
        const uint64_t last = m_heartbeatMs.load();
        const bool gap = now > last && now - last > kMusicHeartbeatTimeoutMs;
        if (m_autoPaused.exchange(gap) != gap) {
            if (gap) {
                LOG_INFO("music: no race frame for %llu ms; the player pauses until the race presents again",
                         static_cast<unsigned long long>(now - last));
            } else {
                // M5c (finding E): one resume event for the race-frame glue's diagnostics line.
                m_autoResumes.fetch_add(1);
                LOG_INFO("music: race frames are back; the player resumes");
            }
        }
    }

    void LoadMusicLibrary() {
        std::vector<std::wstring> scanned;
        if (!m_musicRoot.empty()) {
            scanned = ScanMusicFolder(m_musicRoot);
        }
        std::vector<MusicCassette> cassettes = BuildMusicPlayerCassettes(scanned, m_musicRoot, m_gameFolder);
        bool playable = false;
        size_t cassetteCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_cassetteMutex);
            m_cassettes = std::move(cassettes);
            if (m_selectedCassette.load() >= static_cast<int>(m_cassettes.size())) {
                m_selectedCassette.store(-1);
            }
            cassetteCount = m_cassettes.size();
            for (const MusicCassette& cassette : m_cassettes) {
                playable = playable || !cassette.tracks.empty();
            }
        }
        m_hasPlayable.store(playable);
        m_libraryLoaded.store(true);
        LOG_INFO("music: %u cassette(s) ready (built-in Road Rash and %u user cassette(s))",
                 static_cast<unsigned>(cassetteCount),
                 static_cast<unsigned>(cassetteCount == 0 ? 0 : cassetteCount - 1));
    }

    bool EnsureAudio() {
        if (m_audioDisabled.load()) {
            return false;
        }
        if (m_audioReady) {
            return true;
        }
        if (!m_comInitialized) {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            m_comInitialized = SUCCEEDED(hr);
        }
        if (!m_output->Initialize(m_volume)) {
            DisableAudio();
            return false;
        }
        m_audioReady = true;
        return true;
    }

    // R3-001: an engine-level audio failure is terminal for the session. One warning, an inactive
    // snapshot, no more playback work, and Post/ApplyCommand ignore every later command.
    void DisableAudio() {
        if (m_audioDisabled.exchange(true)) {
            return;
        }
        LOG_WARN("music: XAudio2 2.9 is not available; the music player stays disabled");
        m_audioReady = false;
        m_decoder.reset();
        m_endOfTrack = false;
        m_outputActive = false;
        m_playbackStarted = false;
        m_openedChange = kInvalidChangeCounter;
        ClearHeartbeatWindow();
        m_state.Stop();
        m_output->Shutdown();
        PublishSnapshot();
    }

    void ServicePlayback() {
        if (m_audioDisabled.load()) {
            return;
        }
        if (m_state.State() == MusicPlayerState::Playing) {
            // M5b: the game froze (Esc pause, end-of-race video, focus loss). Keep the queued audio
            // paused so the shown position, which comes from samples played, stops advancing.
            if (m_autoPaused.load()) {
                m_output->Pause();
                UpdateProgress();
                return;
            }
            if (!EnsureAudio()) {
                return;
            }
            const MusicPlayerNowPlaying now = m_state.Snapshot();
            if (now.changeCounter != m_openedChange) {
                BeginTrack();
                UpdateProgress();
                return;
            }
            m_output->Resume();
            PumpAudio();
            UpdateProgress();
            return;
        }
        if (m_state.State() == MusicPlayerState::Paused) {
            m_output->Pause();
            UpdateProgress();
            return;
        }
        if (m_outputActive) {
            m_output->ResetTrack();
            m_decoder.reset();
            m_outputActive = false;
            m_openedChange = kInvalidChangeCounter;
        }
        m_autoPaused.store(false);
        m_playbackStarted = false;
    }

    void BeginTrack() {
        m_decoder.reset();
        m_endOfTrack = false;
        m_framesSubmitted = 0;
        m_totalFrames = 0;
        m_openedChange = m_state.Snapshot().changeCounter;
        const MusicTrack* track = m_state.CurrentTrack();
        if (track == nullptr) {
            m_openedChange = kInvalidChangeCounter;
            return;
        }
        std::unique_ptr<AudioDecoder> decoder = CreateDecoderForPath(track->relativePath);
        bool ok = false;
        if (decoder != nullptr && decoder->Open(track->relativePath)) {
            const uint32_t channels = decoder->Channels();
            if (channels >= 1 && channels <= 2 && decoder->SampleRate() > 0) {
                ok = true;
            } else {
                LOG_WARN("music: unsupported channel count in %ls; skipping it", track->relativePath.c_str());
            }
        }
        if (!ok) {
            m_state.TrackFailed();
            const std::wstring warning = m_state.TakeFailureWarning();
            if (!warning.empty()) {
                LOG_WARN("music: cannot play %ls", warning.c_str());
            }
            m_openedChange = kInvalidChangeCounter;
            if (m_state.State() == MusicPlayerState::Stopped) {
                m_playbackStarted = false;
            }
            return;
        }

        const MusicTags tags = ReadMusicTrackTags(track->relativePath);
        const TrackDisplayInfo display = TrackDisplay(track->relativePath, tags);
        m_state.SetTrackDisplay(track->relativePath, display.title, display.artist);

        m_totalFrames = decoder->TotalFrames();
        m_framesPerBuffer = decoder->SampleRate() / 10;  // about 100 ms per buffer
        if (m_framesPerBuffer < 256) {
            m_framesPerBuffer = 256;
        }
        m_decoder = std::move(decoder);
        m_outputActive = true;
        m_counter = AcquireCounter();
        if (!m_output->BeginTrack(m_decoder->SampleRate(), m_decoder->Channels(), m_volume, m_counter)) {
            // R3-002: a source-voice failure is a track failure, never an engine failure. The state
            // machine advances to the next track and prints one warning per file per session.
            m_counter = nullptr;
            m_decoder.reset();
            m_outputActive = false;
            m_openedChange = kInvalidChangeCounter;
            m_state.TrackFailed();
            const std::wstring warning = m_state.TakeFailureWarning();
            if (!warning.empty()) {
                LOG_WARN("music: cannot start the XAudio2 voice for %ls", warning.c_str());
            }
            if (m_state.State() == MusicPlayerState::Stopped) {
                m_playbackStarted = false;
            }
        }
    }

    // R3-003: reuse a counter only when no callback can still reach it. The pool therefore holds at
    // most the counters of the tracks whose flushed buffers are still finishing, never one per
    // track, and nothing is freed while XAudio2 may still touch it.
    MusicAudioCounter* AcquireCounter() {
        for (const std::unique_ptr<MusicAudioCounter>& counter : m_counters) {
            if (counter->inFlight.load() <= 0) {
                counter->framesRemaining.store(0);
                return counter.get();
            }
        }
        m_counters.push_back(std::make_unique<MusicAudioCounter>());
        m_counterPoolSize.store(m_counters.size());
        return m_counters.back().get();
    }

    void PumpAudio() {
        if (!m_decoder || m_counter == nullptr) {
            return;
        }
        const uint32_t channels = m_decoder->Channels();
        if (channels == 0) {
            return;
        }
        while (!m_endOfTrack && m_output->FreeBuffers(m_counter) > 0) {
            std::vector<float> buffer(static_cast<size_t>(m_framesPerBuffer) * channels);
            const uint64_t read = m_decoder->ReadFrames(buffer.data(), m_framesPerBuffer);
            if (read == 0) {
                m_endOfTrack = true;
                break;
            }
            buffer.resize(static_cast<size_t>(read) * channels);
            if (!m_output->Submit(buffer.data(), static_cast<uint32_t>(read), channels, m_counter)) {
                m_endOfTrack = true;
                break;
            }
            m_playbackStarted = true;
            m_framesSubmitted += read;
        }
        if (m_endOfTrack && m_output->Drained(m_counter)) {
            m_state.TrackFinished();
            m_decoder.reset();
            m_outputActive = false;
            m_counter = nullptr;
            m_openedChange = kInvalidChangeCounter;
        }
    }

    void UpdateProgress() {
        if (!m_decoder) {
            return;
        }
        const uint64_t remaining = m_output->FramesRemaining(m_counter);
        const uint64_t played = (m_framesSubmitted > remaining) ? m_framesSubmitted - remaining : 0;
        const uint32_t rate = m_decoder->SampleRate();
        const uint64_t positionMs = (rate != 0) ? (played * 1000) / rate : 0;
        const uint64_t durationMs = (rate != 0) ? (m_totalFrames * 1000) / rate : 0;
        m_state.SetProgress(positionMs, durationMs);
    }

    void PublishSnapshot() {
        MusicPlayerNowPlaying snapshot = m_state.Snapshot();
        snapshot.paused = snapshot.paused ||
                          (m_autoPaused.load() && m_state.State() == MusicPlayerState::Playing);
        snapshot.playbackStarted = m_playbackStarted &&
                                   m_state.State() != MusicPlayerState::Stopped &&
                                   !m_audioDisabled.load();
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot = snapshot;
    }

    MusicConfig m_config;
    std::vector<MusicCassette> m_cassettes;
    mutable std::mutex m_cassetteMutex;  // guards m_cassettes for MusicPlayerCassetteLabels
    std::atomic<int> m_selectedCassette{-1};
    std::wstring m_musicRoot;
    std::wstring m_gameFolder;
    bool m_scanOnLoad = false;
    std::atomic<bool> m_libraryLoaded{false};
    std::atomic<bool> m_hasPlayable{false};
    float m_volume = 1.0f;
    uint32_t m_seed = 0;

    MusicPlayerStateMachine m_state;
    std::unique_ptr<MusicAudioSink> m_output;
    std::unique_ptr<AudioDecoder> m_decoder;
    std::vector<std::unique_ptr<MusicAudioCounter>> m_counters;
    MusicAudioCounter* m_counter = nullptr;
    uint32_t m_openedChange = kInvalidChangeCounter;
    uint64_t m_framesSubmitted = 0;
    uint64_t m_totalFrames = 0;
    uint32_t m_framesPerBuffer = 0;
    bool m_endOfTrack = false;
    bool m_outputActive = false;
    bool m_playbackStarted = false;
    bool m_audioReady = false;
    std::atomic<bool> m_autoPaused{false};
    std::atomic<uint32_t> m_autoResumes{0};
    std::atomic<bool> m_heartbeatActive{false};
    std::atomic<uint64_t> m_heartbeatMs{0};
    std::atomic<bool> m_audioDisabled{false};
    std::atomic<size_t> m_counterPoolSize{0};
    bool m_comInitialized = false;

    std::thread m_worker;
    std::mutex m_commandMutex;
    std::vector<PendingCommand> m_commands;
    HANDLE m_wakeEvent = nullptr;
    std::atomic<bool> m_shutdown{false};

    mutable std::mutex m_snapshotMutex;
    MusicPlayerNowPlaying m_snapshot;
};

// Intentionally leaked singleton: it is never destroyed. Its destructor would join the worker and
// release XAudio2 objects during DLL_PROCESS_DETACH at process exit. MusicPlayerShutdown performs
// the explicit in-session teardown; calls after it are no-ops.
MusicPlayerEngine* g_engine = nullptr;

MusicPlayerNowPlaying InactiveSnapshot() {
    return MusicPlayerNowPlaying{};
}

}  // namespace

void MusicPlayerInit(const MusicConfig& config, std::vector<MusicCassette> cassettes) {
    if (!config.enabled) {
        return;
    }
    if (g_engine != nullptr) {
        LOG_WARN("music: MusicPlayerInit called more than once; keeping the first instance");
        return;
    }
    g_engine = new MusicPlayerEngine(config, std::move(cassettes), std::wstring(), std::wstring());
    g_engine->Start();
}

void MusicPlayerInitWithPaths(const MusicConfig& config, const std::wstring& musicRoot,
                              const std::wstring& gameFolder) {
    if (!config.enabled) {
        return;
    }
    if (g_engine != nullptr) {
        LOG_WARN("music: MusicPlayerInit called more than once; keeping the first instance");
        return;
    }
    g_engine = new MusicPlayerEngine(config, {}, musicRoot, gameFolder);
    g_engine->Start();
}

void MusicPlayerRaceStarted() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::RaceStarted);
    }
}

void MusicPlayerRaceStartedAtTrack(int track) {
    if (g_engine != nullptr) {
        g_engine->Post(Command::RaceStartedAtTrack, track);
    }
}

void MusicPlayerRaceEnded() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::RaceEnded);
    }
}

void MusicPlayerNext() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::Next);
    }
}

void MusicPlayerPrevious() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::Previous);
    }
}

void MusicPlayerTogglePause() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::TogglePause);
    }
}

void MusicPlayerNextCassette() {
    if (g_engine != nullptr) {
        g_engine->Post(Command::NextCassette);
    }
}

void MusicPlayerRaceHeartbeat(uint64_t nowMs) {
    if (g_engine != nullptr) {
        g_engine->Heartbeat(nowMs);
    }
}

MusicPlayerNowPlaying MusicPlayerSnapshot() {
    if (g_engine == nullptr) {
        return InactiveSnapshot();
    }
    return g_engine->Snapshot();
}

bool MusicPlayerAutoPaused() {
    return g_engine != nullptr && g_engine->AutoPaused();
}

uint32_t MusicPlayerAutoResumeCount() {
    return g_engine != nullptr ? g_engine->AutoResumeCount() : 0;
}

bool MusicSinkStartsOnSubmit(bool paused) {
    return !paused;
}

bool MusicSinkStartsOnResume(bool paused, bool voicePrepared) {
    return paused && voicePrepared;
}

bool MusicPlayerLibraryReady() {
    return g_engine != nullptr && g_engine->LibraryReady();
}

std::vector<std::wstring> MusicPlayerCassetteLabels() {
    return g_engine != nullptr ? g_engine->CassetteLabels() : std::vector<std::wstring>();
}

void MusicPlayerSelectCassette(int index) {
    if (g_engine != nullptr) {
        g_engine->SelectCassette(index);
    }
}

int MusicPlayerSelectedCassette() {
    return g_engine != nullptr ? g_engine->SelectedCassette() : -1;
}

bool MusicPlayerHasPlayableCassette() {
    return g_engine != nullptr && g_engine->HasPlayableCassette();
}

bool MusicPlayerAudioEnabled() {
    return g_engine != nullptr && !g_engine->AudioDisabled();
}

void MusicPlayerShutdown() {
    if (g_engine != nullptr) {
        g_engine->Shutdown();
    }
}

void MusicPlayerTestSetHooks(const MusicPlayerTestHooks& hooks) {
    g_testHooks = hooks;
}

void MusicPlayerTestReset() {
    if (g_engine != nullptr) {
        g_engine->Shutdown();
        delete g_engine;
        g_engine = nullptr;
    }
    g_testHooks = MusicPlayerTestHooks{};
}

size_t MusicPlayerTestCounterPoolSize() {
    return (g_engine != nullptr) ? g_engine->CounterPoolSize() : 0;
}

bool MusicPlayerTestAudioDisabled() {
    return g_engine != nullptr && g_engine->AudioDisabled();
}

bool MusicPlayerTestWorkerRunning() {
    return g_engine != nullptr && g_engine->WorkerRunning();
}

bool MusicPlayerTestAutoPaused() {
    return g_engine != nullptr && g_engine->AutoPaused();
}

bool MusicPlayerTestHeartbeatActive() {
    return g_engine != nullptr && g_engine->HeartbeatActive();
}
