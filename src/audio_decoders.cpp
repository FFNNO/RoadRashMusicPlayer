#include "audio_decoders.h"

#include <cstdio>
#include <cwctype>

// Declarations only for the vendored single-file decoders: the implementations live in
// audio_decoders_impl.cpp (dr_libs, compiled once with /W0) and stb_vorbis_impl.c (C, /W0).
// stb_vorbis has no separate header, so its header part is included here with C linkage to match
// the C translation unit.
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PUSHDATA_API
extern "C" {
#include "../third_party/stb/stb_vorbis.c"
}

#include "../third_party/dr_libs/dr_wav.h"
#include "../third_party/dr_libs/dr_mp3.h"
#include "../third_party/dr_libs/dr_flac.h"

namespace {

// Lower-case extension including the dot, or an empty string when the path has no extension. A dot
// in a folder name is not an extension: the last separator must come first.
std::wstring LowerExtension(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= path.size()) {
        return std::wstring();
    }
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos && separator > dot) {
        return std::wstring();
    }
    std::wstring extension = path.substr(dot);
    for (wchar_t& c : extension) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return extension;
}

class WavDecoder final : public AudioDecoder {
public:
    ~WavDecoder() override {
        if (m_open) {
            drwav_uninit(&m_wav);
        }
    }

    bool Open(const std::wstring& path) override {
        if (m_open) {
            return false;
        }
        m_open = drwav_init_file_w(&m_wav, path.c_str(), nullptr) == DRWAV_TRUE;
        return m_open;
    }

    uint32_t SampleRate() const override { return m_open ? m_wav.sampleRate : 0; }
    uint32_t Channels() const override { return m_open ? m_wav.channels : 0; }
    uint64_t TotalFrames() const override { return m_open ? m_wav.totalPCMFrameCount : 0; }

    uint64_t ReadFrames(float* out, uint64_t frames) override {
        if (!m_open || out == nullptr || frames == 0) {
            return 0;
        }
        return drwav_read_pcm_frames_f32(&m_wav, frames, out);
    }

private:
    drwav m_wav = {};
    bool m_open = false;
};

class Mp3Decoder final : public AudioDecoder {
public:
    ~Mp3Decoder() override {
        if (m_open) {
            drmp3_uninit(&m_mp3);
        }
    }

    bool Open(const std::wstring& path) override {
        if (m_open) {
            return false;
        }
        m_open = drmp3_init_file_w(&m_mp3, path.c_str(), nullptr) == DRMP3_TRUE;
        if (m_open) {
            m_totalFrames = drmp3_get_pcm_frame_count(&m_mp3);
        }
        return m_open;
    }

    uint32_t SampleRate() const override { return m_open ? m_mp3.sampleRate : 0; }
    uint32_t Channels() const override { return m_open ? m_mp3.channels : 0; }
    uint64_t TotalFrames() const override { return m_totalFrames; }

    uint64_t ReadFrames(float* out, uint64_t frames) override {
        if (!m_open || out == nullptr || frames == 0) {
            return 0;
        }
        return drmp3_read_pcm_frames_f32(&m_mp3, frames, out);
    }

private:
    drmp3 m_mp3 = {};
    uint64_t m_totalFrames = 0;
    bool m_open = false;
};

class FlacDecoder final : public AudioDecoder {
public:
    ~FlacDecoder() override {
        if (m_flac != nullptr) {
            drflac_close(m_flac);
        }
    }

    bool Open(const std::wstring& path) override {
        if (m_flac != nullptr) {
            return false;
        }
        m_flac = drflac_open_file_w(path.c_str(), nullptr);
        return m_flac != nullptr;
    }

    uint32_t SampleRate() const override { return m_flac != nullptr ? m_flac->sampleRate : 0; }
    uint32_t Channels() const override { return m_flac != nullptr ? m_flac->channels : 0; }
    uint64_t TotalFrames() const override { return m_flac != nullptr ? m_flac->totalPCMFrameCount : 0; }

    uint64_t ReadFrames(float* out, uint64_t frames) override {
        if (m_flac == nullptr || out == nullptr || frames == 0) {
            return 0;
        }
        return drflac_read_pcm_frames_f32(m_flac, frames, out);
    }

private:
    drflac* m_flac = nullptr;
};

class VorbisDecoder final : public AudioDecoder {
public:
    ~VorbisDecoder() override {
        if (m_vorbis != nullptr) {
            stb_vorbis_close(m_vorbis);
        }
    }

    bool Open(const std::wstring& path) override {
        if (m_vorbis != nullptr) {
            return false;
        }
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
            return false;
        }
        int error = 0;
        // close_on_free = 1: stb_vorbis owns the FILE from here on, including the failure path,
        // where vorbis_deinit closes it.
        m_vorbis = stb_vorbis_open_file(file, 1, &error, nullptr);
        if (m_vorbis == nullptr) {
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(m_vorbis);
        m_sampleRate = info.sample_rate;
        m_channels = static_cast<uint32_t>(info.channels);
        m_totalFrames = stb_vorbis_stream_length_in_samples(m_vorbis);
        return true;
    }

    uint32_t SampleRate() const override { return m_sampleRate; }
    uint32_t Channels() const override { return m_channels; }
    uint64_t TotalFrames() const override { return m_totalFrames; }

    uint64_t ReadFrames(float* out, uint64_t frames) override {
        if (m_vorbis == nullptr || out == nullptr || frames == 0 || m_channels == 0) {
            return 0;
        }
        // R3-vorbis-int-overflow: bound the frame request before frames * channels can overflow int.
        frames = ClampVorbisFrameRequest(frames, m_channels);
        if (frames == 0) {
            return 0;
        }
        const int requested = static_cast<int>(frames);
        const int floats = requested * static_cast<int>(m_channels);
        const int decoded = stb_vorbis_get_samples_float_interleaved(m_vorbis, static_cast<int>(m_channels), out,
                                                                     floats);
        return decoded > 0 ? static_cast<uint64_t>(decoded) : 0;
    }

private:
    stb_vorbis* m_vorbis = nullptr;
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    uint64_t m_totalFrames = 0;
};

}  // namespace

uint64_t ClampVorbisFrameRequest(uint64_t frames, uint32_t channels) {
    if (channels == 0) {
        return 0;
    }
    const uint64_t maxFrames = static_cast<uint64_t>(0x7FFFFFFF) / channels;
    return frames > maxFrames ? maxFrames : frames;
}

std::unique_ptr<AudioDecoder> CreateDecoderForPath(const std::wstring& path) {
    const std::wstring extension = LowerExtension(path);
    if (extension == L".wav" || extension == L".rra") {
        return std::make_unique<WavDecoder>();
    }
    if (extension == L".mp3") {
        return std::make_unique<Mp3Decoder>();
    }
    if (extension == L".flac") {
        return std::make_unique<FlacDecoder>();
    }
    if (extension == L".ogg") {
        return std::make_unique<VorbisDecoder>();
    }
    return nullptr;
}
