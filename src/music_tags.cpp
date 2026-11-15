#include "music_tags.h"

#include <cstdio>
#include <cwctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint32_t ReadBe24(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

uint32_t ReadBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint32_t ReadLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t ReadSynchsafe(const uint8_t* p) {
    return ((static_cast<uint32_t>(p[0]) & 0x7F) << 21) | ((static_cast<uint32_t>(p[1]) & 0x7F) << 14) |
           ((static_cast<uint32_t>(p[2]) & 0x7F) << 7) | (static_cast<uint32_t>(p[3]) & 0x7F);
}

bool HasBytes(size_t size, size_t offset, size_t count) {
    return offset <= size && count <= size - offset;
}

void TrimTrailingWhitespace(std::wstring& value) {
    while (!value.empty()) {
        const wchar_t c = value.back();
        if (c == L'\0' || std::iswspace(static_cast<wint_t>(c))) {
            value.pop_back();
        } else {
            break;
        }
    }
}

std::wstring DecodeLatin1(const uint8_t* data, size_t size) {
    std::wstring out;
    out.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == 0) {
            break;
        }
        out.push_back(static_cast<wchar_t>(data[i]));
    }
    TrimTrailingWhitespace(out);
    return out;
}

std::wstring DecodeUtf8(const uint8_t* data, size_t size) {
    std::wstring out;
    size_t i = 0;
    while (i < size) {
        const uint8_t lead = data[i];
        if (lead == 0) {
            break;
        }
        if (lead < 0x80) {
            out.push_back(static_cast<wchar_t>(lead));
            ++i;
            continue;
        }
        size_t extra = 0;
        uint32_t codepoint = 0;
        if ((lead & 0xE0) == 0xC0) {
            codepoint = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            codepoint = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            codepoint = lead & 0x07;
            extra = 3;
        } else {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        if (extra > size - i - 1) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            break;
        }
        bool valid = true;
        for (size_t k = 1; k <= extra; ++k) {
            const uint8_t continuation = data[i + k];
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if (!valid || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        i += extra + 1;
        if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    TrimTrailingWhitespace(out);
    return out;
}

std::wstring DecodeUtf16(const uint8_t* data, size_t size, bool bigEndian) {
    std::wstring out;
    size_t i = 0;
    while (i + 1 < size) {
        const uint32_t unit = bigEndian
                                  ? ((static_cast<uint32_t>(data[i]) << 8) | data[i + 1])
                                  : (static_cast<uint32_t>(data[i]) | (static_cast<uint32_t>(data[i + 1]) << 8));
        if (unit == 0) {
            break;
        }
        out.push_back(static_cast<wchar_t>(unit));
        i += 2;
    }
    TrimTrailingWhitespace(out);
    return out;
}

std::wstring DecodeId3Text(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return L"";
    }
    const uint8_t encoding = data[0];
    const uint8_t* text = data + 1;
    const size_t length = size - 1;
    switch (encoding) {
        case 0:
            return DecodeLatin1(text, length);
        case 1:
            if (length >= 2 && text[0] == 0xFF && text[1] == 0xFE) {
                return DecodeUtf16(text + 2, length - 2, false);
            }
            if (length >= 2 && text[0] == 0xFE && text[1] == 0xFF) {
                return DecodeUtf16(text + 2, length - 2, true);
            }
            return DecodeUtf16(text, length, false);
        case 2:
            return DecodeUtf16(text, length, true);
        case 3:
            return DecodeUtf8(text, length);
        default:
            return L"";
    }
}

MusicTags ParseId3v1(const uint8_t* data, size_t size) {
    MusicTags tags;
    if (data == nullptr || size < 128) {
        return tags;
    }
    const uint8_t* tag = data + size - 128;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') {
        return tags;
    }
    tags.title = DecodeLatin1(tag + 3, 30);
    tags.artist = DecodeLatin1(tag + 33, 30);
    tags.found = !tags.title.empty();
    return tags;
}

bool FieldNameIs(const uint8_t* data, size_t size, const char* name) {
    size_t i = 0;
    for (; name[i] != '\0'; ++i) {
        if (i >= size) {
            return false;
        }
        char c = static_cast<char>(data[i]);
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
        if (c != name[i]) {
            return false;
        }
    }
    return i == size;
}

}  // namespace

MusicTags ParseId3Tags(const uint8_t* data, size_t size) {
    MusicTags tags;
    const MusicTags id3v1 = ParseId3v1(data, size);
    if (data != nullptr && size >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        const uint8_t major = data[3];
        if (major == 3 || major == 4) {
            const uint8_t flags = data[5];
            const uint32_t declaredSize = ReadSynchsafe(data + 6);
            size_t pos = 10;
            size_t end = size;
            if (declaredSize <= size - 10) {
                end = 10 + static_cast<size_t>(declaredSize);
            }

            if ((flags & 0x40) != 0 && HasBytes(end, pos, 4)) {
                const uint32_t extendedSize = (major == 3) ? ReadBe32(data + pos) : ReadSynchsafe(data + pos);
                const size_t skip = (major == 3) ? static_cast<size_t>(extendedSize) + 4 : extendedSize;
                if (skip >= 4 && skip <= end - pos) {
                    pos += skip;
                } else {
                    pos = end;
                }
            }

            while (HasBytes(end, pos, 10)) {
                const uint8_t* frame = data + pos;
                if (frame[0] == 0) {
                    break;
                }
                bool validId = true;
                for (size_t i = 0; i < 4; ++i) {
                    const uint8_t c = frame[i];
                    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                        validId = false;
                        break;
                    }
                }
                if (!validId) {
                    break;
                }
                const uint32_t frameSize = (major == 4) ? ReadSynchsafe(frame + 4) : ReadBe32(frame + 4);
                if (frameSize == 0) {
                    pos += 10;
                    continue;
                }
                if (frameSize > end - pos - 10) {
                    break;
                }
                if (std::memcmp(frame, "TIT2", 4) == 0 && tags.title.empty()) {
                    tags.title = DecodeId3Text(frame + 10, frameSize);
                } else if (std::memcmp(frame, "TPE1", 4) == 0 && tags.artist.empty()) {
                    tags.artist = DecodeId3Text(frame + 10, frameSize);
                }
                pos += 10 + static_cast<size_t>(frameSize);
            }
        }
    }

    if (tags.title.empty()) {
        tags.title = id3v1.title;
    }
    if (tags.artist.empty()) {
        tags.artist = id3v1.artist;
    }
    tags.found = !tags.title.empty();
    return tags;
}

MusicTags ParseVorbisCommentBlock(const uint8_t* data, size_t size) {
    MusicTags tags;
    if (data == nullptr || size < 8) {
        return tags;
    }
    size_t pos = 0;
    const uint32_t vendorLength = ReadLe32(data + pos);
    pos += 4;
    if (vendorLength > size - pos) {
        return tags;
    }
    pos += vendorLength;
    if (!HasBytes(size, pos, 4)) {
        return tags;
    }
    const uint32_t fieldCount = ReadLe32(data + pos);
    pos += 4;
    for (uint32_t field = 0; field < fieldCount; ++field) {
        if (!HasBytes(size, pos, 4)) {
            break;
        }
        const uint32_t length = ReadLe32(data + pos);
        pos += 4;
        if (length > size - pos) {
            break;
        }
        const uint8_t* entry = data + pos;
        size_t equals = 0;
        while (equals < length && entry[equals] != '=') {
            ++equals;
        }
        if (equals < length) {
            if (tags.title.empty() && FieldNameIs(entry, equals, "title")) {
                tags.title = DecodeUtf8(entry + equals + 1, length - equals - 1);
            } else if (tags.artist.empty() && FieldNameIs(entry, equals, "artist")) {
                tags.artist = DecodeUtf8(entry + equals + 1, length - equals - 1);
            }
        }
        pos += length;
    }
    tags.found = !tags.title.empty();
    return tags;
}

bool ExtractFlacVorbisComment(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr || size < 4 || std::memcmp(data, "fLaC", 4) != 0) {
        return false;
    }
    size_t pos = 4;
    while (HasBytes(size, pos, 4)) {
        const uint8_t header = data[pos];
        const uint32_t length = ReadBe24(data + pos + 1);
        pos += 4;
        const bool last = (header & 0x80) != 0;
        const uint8_t type = header & 0x7F;
        if (length > size - pos) {
            return false;
        }
        if (type == 4) {
            out.assign(data + pos, data + pos + length);
            return true;
        }
        pos += length;
        if (last) {
            break;
        }
    }
    return false;
}

bool ExtractOggVorbisComment(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr || size < 27) {
        return false;
    }
    constexpr size_t kMaxPacketSize = 4u * 1024u * 1024u;
    size_t pos = 0;
    size_t packetIndex = 0;
    std::vector<uint8_t> packet;
    for (int page = 0; page < 16 && HasBytes(size, pos, 27); ++page) {
        if (std::memcmp(data + pos, "OggS", 4) != 0) {
            return false;
        }
        const size_t segmentCount = data[pos + 26];
        if (!HasBytes(size, pos + 27, segmentCount)) {
            return false;
        }
        const uint8_t* lacing = data + pos + 27;
        size_t payload = pos + 27 + segmentCount;
        for (size_t segment = 0; segment < segmentCount; ++segment) {
            const size_t length = lacing[segment];
            if (length > size - payload) {
                return false;
            }
            if (packet.size() + length > kMaxPacketSize) {
                return false;
            }
            packet.insert(packet.end(), data + payload, data + payload + length);
            payload += length;
            if (length < 255) {
                if (packetIndex == 1 && packet.size() >= 7 && packet[0] == 3 &&
                    std::memcmp(packet.data() + 1, "vorbis", 6) == 0) {
                    out.assign(packet.begin() + 7, packet.end());
                    return true;
                }
                ++packetIndex;
                packet.clear();
                if (packetIndex > 1) {
                    return false;
                }
            }
        }
        pos = payload;
    }
    return false;
}

MusicTags ParseFlacTags(const uint8_t* data, size_t size) {
    std::vector<uint8_t> comment;
    if (!ExtractFlacVorbisComment(data, size, comment)) {
        return MusicTags{};
    }
    return ParseVorbisCommentBlock(comment.data(), comment.size());
}

MusicTags ParseOggTags(const uint8_t* data, size_t size) {
    std::vector<uint8_t> comment;
    if (!ExtractOggVorbisComment(data, size, comment)) {
        return MusicTags{};
    }
    return ParseVorbisCommentBlock(comment.data(), comment.size());
}

MusicTags ParseWaveInfoTags(const uint8_t* data, size_t size) {
    MusicTags tags;
    if (data == nullptr || size < 12 || std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        return tags;
    }
    size_t pos = 12;
    while (HasBytes(size, pos, 8)) {
        const uint8_t* chunk = data + pos;
        const uint32_t length = ReadLe32(chunk + 4);
        pos += 8;
        if (length > size - pos) {
            break;
        }
        if (std::memcmp(chunk, "LIST", 4) == 0 && length >= 4 && std::memcmp(data + pos, "INFO", 4) == 0) {
            size_t sub = pos + 4;
            const size_t end = pos + length;
            while (HasBytes(end, sub, 8)) {
                const uint8_t* info = data + sub;
                const uint32_t infoLength = ReadLe32(info + 4);
                sub += 8;
                if (infoLength > end - sub) {
                    break;
                }
                if (std::memcmp(info, "INAM", 4) == 0 && tags.title.empty()) {
                    tags.title = DecodeLatin1(data + sub, infoLength);
                } else if (std::memcmp(info, "IART", 4) == 0 && tags.artist.empty()) {
                    tags.artist = DecodeLatin1(data + sub, infoLength);
                }
                sub += infoLength + (infoLength & 1u);
            }
        }
        pos += length + (length & 1u);
    }
    tags.found = !tags.title.empty();
    return tags;
}

uint32_t DeclaredId3v2TagBytes(const uint8_t* head, size_t size) {
    if (head == nullptr || size < 10 || std::memcmp(head, "ID3", 3) != 0) {
        return 0;
    }
    const uint8_t major = head[3];
    if (major != 3 && major != 4) {
        return 0;
    }
    uint64_t total = 10u + static_cast<uint64_t>(ReadSynchsafe(head + 6));
    // An ID3v2.4 footer is part of the tag and sits after the frames.
    if (major == 4 && (head[5] & 0x10) != 0) {
        total += 10u;
    }
    return total > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(total);
}

MusicTagReadPlan PlanMusicTagRead(uint64_t fileSize, uint32_t declaredId3v2Bytes) {
    MusicTagReadPlan plan;
    if (fileSize == 0) {
        return plan;
    }
    uint64_t prefix = 0;
    if (fileSize <= kMusicTagWholeFileBytes) {
        // As before the 4 MiB cap: a file that is not large is read whole, so its ID3v1 suffix is
        // already part of the buffer.
        prefix = fileSize;
    } else if (declaredId3v2Bytes > 0) {
        // The tag body is exactly what the parser needs; a lying or huge size is capped.
        prefix = (declaredId3v2Bytes < kMusicTagId3v2MaxBytes) ? declaredId3v2Bytes
                                                               : kMusicTagId3v2MaxBytes;
        prefix = (prefix < fileSize) ? prefix : fileSize;
    } else {
        // FLAC metadata blocks and the Ogg comment headers live near the start of the file.
        prefix = kMusicTagHeaderBytes;
    }
    plan.prefixBytes = static_cast<size_t>(prefix);
    // The ID3v1 tag at the end is parsed on its own; appending it to a truncated ID3v2 body would
    // let the frame walker read the tag bytes as frame data.
    plan.suffixBytes = (prefix < fileSize) ? kMusicTagSuffixBytes : 0;
    return plan;
}

MusicTags MergeMusicTags(const MusicTags& primary, const MusicTags& fallback) {
    MusicTags merged = primary;
    if (merged.title.empty()) {
        merged.title = fallback.title;
    }
    if (merged.artist.empty()) {
        merged.artist = fallback.artist;
    }
    merged.found = !merged.title.empty();
    return merged;
}

bool ReadMusicTagSuffix(FILE* file, long long fileSize, size_t suffixBytes, std::vector<uint8_t>& suffix,
                        MusicTagSeekFn seek) {
    // R3-suffix-zero-on-seek-failure: the suffix must be empty on every failure path, never the
    // zero buffer a pre-seek resize left behind (ParseId3Tags would read it as a tag).
    suffix.clear();
    if (file == nullptr || suffixBytes == 0) {
        return false;
    }
    const long long suffixStart = fileSize - static_cast<long long>(suffixBytes);
    if (suffixStart < 0) {
        return false;
    }
    if (seek(file, suffixStart, SEEK_SET) != 0) {
        return false;
    }
    suffix.resize(suffixBytes);
    if (std::fread(suffix.data(), 1, suffix.size(), file) != suffix.size()) {
        suffix.clear();
        return false;
    }
    return true;
}

namespace {

std::wstring LowerExtensionOf(const std::wstring& path) {
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

}  // namespace

MusicTags ReadMusicTrackTags(const std::wstring& path) {
    MusicTags tags;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
        return tags;
    }
    long long size = -1;
    if (_fseeki64(file, 0, SEEK_END) == 0) {
        size = _ftelli64(file);
    }
    if (size <= 0) {
        std::fclose(file);
        return tags;
    }
    uint8_t head[10] = {};
    size_t headBytes = 0;
    if (_fseeki64(file, 0, SEEK_SET) == 0) {
        headBytes = std::fread(head, 1, sizeof(head), file);
    }
    const MusicTagReadPlan plan =
        PlanMusicTagRead(static_cast<uint64_t>(size), DeclaredId3v2TagBytes(head, headBytes));
    std::vector<uint8_t> prefix;
    if (plan.prefixBytes > 0 && _fseeki64(file, 0, SEEK_SET) == 0) {
        prefix.resize(plan.prefixBytes);
        prefix.resize(std::fread(prefix.data(), 1, prefix.size(), file));
    }
    std::vector<uint8_t> suffix;
    if (plan.suffixBytes > 0 && prefix.size() == plan.prefixBytes) {
        ReadMusicTagSuffix(file, size, plan.suffixBytes, suffix);
    }
    std::fclose(file);
    if (prefix.empty()) {
        return tags;
    }
    const std::wstring extension = LowerExtensionOf(path);
    if (extension == L".mp3") {
        tags = ParseId3Tags(prefix.data(), prefix.size());
    } else if (extension == L".ogg") {
        tags = ParseOggTags(prefix.data(), prefix.size());
    } else if (extension == L".flac") {
        tags = ParseFlacTags(prefix.data(), prefix.size());
    } else if (extension == L".wav" || extension == L".rra") {
        tags = ParseWaveInfoTags(prefix.data(), prefix.size());
    } else {
        return tags;
    }
    if (!suffix.empty()) {
        tags = MergeMusicTags(tags, ParseId3Tags(suffix.data(), suffix.size()));
    }
    return tags;
}

TrackDisplayInfo TrackDisplay(const std::wstring& path, const MusicTags& tags) {
    TrackDisplayInfo info;
    if (tags.found && !tags.title.empty()) {
        info.title = tags.title;
        info.artist = tags.artist;
        return info;
    }
    std::wstring name = path;
    const size_t separator = name.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        name = name.substr(separator + 1);
    }
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot != 0) {
        name = name.substr(0, dot);
    }
    info.title = name;
    return info;
}
