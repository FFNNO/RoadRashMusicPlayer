#pragma once
#include <cstdint>
#include <memory>
#include <string>

// Decoder interface for the music player (feature rr95-music-player, task M3). Every implementation
// returns interleaved float32 samples in [-1, 1] and converts from its on-disk format. Mono and
// stereo are supported; more than two channels are rejected by the decoders with a warning.
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    // Opens path. Returns false for a missing, empty, truncated or unrecognised file and never
    // throws. Calling Open twice on the same decoder is not supported.
    virtual bool Open(const std::wstring& path) = 0;

    virtual uint32_t SampleRate() const = 0;
    virtual uint32_t Channels() const = 0;
    virtual uint64_t TotalFrames() const = 0;

    // Reads up to frames interleaved frames into out and returns the frames read. A short read or a
    // return of 0 marks the end of the file; the caller treats 0 as end of track.
    virtual uint64_t ReadFrames(float* out, uint64_t frames) = 0;
};

// Chooses the decoder by file extension, case-insensitively: .wav and .rra (the game's own RIFF
// WAVE PCM files) use dr_wav, .mp3 uses dr_mp3, .flac uses dr_flac and .ogg uses stb_vorbis.
// Returns nullptr for an unknown extension. The decoder is returned closed; call Open next.
// This function does not touch the disk.
std::unique_ptr<AudioDecoder> CreateDecoderForPath(const std::wstring& path);

// stb_vorbis takes the sample count as an int, so frames * channels must fit. Clamp the frame
// request to INT_MAX / channels before the multiplication; zero channels yield zero.
uint64_t ClampVorbisFrameRequest(uint64_t frames, uint32_t channels);
