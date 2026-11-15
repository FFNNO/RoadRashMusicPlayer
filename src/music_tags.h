#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Metadata parsers over byte buffers (feature rr95-music-player, task M2). Every function is
// bounds-checked: a truncated or malicious buffer returns found = false and never reads past size.
struct MusicTags {
    std::wstring title;
    std::wstring artist;
    bool found = false;  // true when a non-empty title was read
};

// ID3v2.3/2.4 TIT2/TPE1 (ISO-8859-1, UTF-16 with BOM, UTF-16BE, UTF-8), with ID3v1 (last 128
// bytes, "TAG") filling any field the v2 tag did not provide.
MusicTags ParseId3Tags(const uint8_t* data, size_t size);

// One Vorbis comment block: vendor length + vendor, count, then length-prefixed "FIELD=value"
// entries. Field names are case-insensitive; TITLE and ARTIST are read as UTF-8.
MusicTags ParseVorbisCommentBlock(const uint8_t* data, size_t size);

// Locate the first Vorbis comment metadata block in a FLAC file ("fLaC", block type 4) and the
// second header packet in an Ogg Vorbis stream ("\x03vorbis"). The block bytes are copied into out.
bool ExtractFlacVorbisComment(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
bool ExtractOggVorbisComment(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

MusicTags ParseFlacTags(const uint8_t* data, size_t size);
MusicTags ParseOggTags(const uint8_t* data, size_t size);

// RIFF WAVE "LIST"/"INFO" sub-chunks INAM (title) and IART (artist).
MusicTags ParseWaveInfoTags(const uint8_t* data, size_t size);

// Metadata reads are bounded (rr95-music-standalone S3a review R3-tag-size-cap, S3b review
// R3-tag-prefix-regression-4-8mib). A file up to kMusicTagWholeFileBytes is read whole, as before
// the original 4 MiB cap. A larger file is read in two bounded parts: the ID3v2 body when the file
// starts with a tag (its declared size, capped at kMusicTagId3v2MaxBytes), or a header prefix that
// holds the FLAC metadata blocks and the Ogg comment headers; plus the 128-byte ID3v1 suffix, which
// is parsed on its own and never appended inside a truncated ID3v2 body.
inline constexpr size_t kMusicTagWholeFileBytes = 8u * 1024u * 1024u;
inline constexpr size_t kMusicTagHeaderBytes = 1u * 1024u * 1024u;
inline constexpr size_t kMusicTagId3v2MaxBytes = 16u * 1024u * 1024u;
inline constexpr size_t kMusicTagSuffixBytes = 128;

struct MusicTagReadPlan {
    size_t prefixBytes = 0;  // bytes to read from the start
    size_t suffixBytes = 0;  // bytes to read from the end, separately; 0 when the prefix is whole
};

// Declared size of the ID3v2 tag at head, including its 10-byte header (and the 2.4 footer), 0 when
// head does not start with an ID3v2.3/2.4 tag.
uint32_t DeclaredId3v2TagBytes(const uint8_t* head, size_t size);

// The reads a tag scan performs. declaredId3v2Bytes comes from DeclaredId3v2TagBytes on the file
// head and is ignored for a file small enough to read whole.
MusicTagReadPlan PlanMusicTagRead(uint64_t fileSize, uint32_t declaredId3v2Bytes);

// Fields the fallback provides fill the empty fields of the primary set; found follows the title.
MusicTags MergeMusicTags(const MusicTags& primary, const MusicTags& fallback);

// R3-suffix-zero-on-seek-failure: reads the ID3v1 suffix into suffix. A failed seek, a start before
// the file or a short read leaves the vector empty, so zero bytes are never parsed as a tag. The
// seek is a parameter so music_test can inject a failure; production uses the CRT.
using MusicTagSeekFn = int (*)(FILE*, long long, int);
bool ReadMusicTagSuffix(FILE* file, long long fileSize, size_t suffixBytes, std::vector<uint8_t>& suffix,
                        MusicTagSeekFn seek = &_fseeki64);

// Reads the metadata of one audio file with the plan above. Never reads more than the plan says.
MusicTags ReadMusicTrackTags(const std::wstring& path);

struct TrackDisplayInfo {
    std::wstring title;
    std::wstring artist;
};

// Uses the tags when a title was found; otherwise title = file name without extension and artist
// is empty.
TrackDisplayInfo TrackDisplay(const std::wstring& path, const MusicTags& tags);
