#include "music_overlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Built-in 5x7 dot-matrix font. One entry per printable ASCII glyph (32..126) followed by the
// accented characters the Spanish game text needs. Each row is 5 dots wide, bit 4 is the leftmost
// dot. Anything else is drawn as '?'.
struct Glyph {
    uint8_t rows[kMusicOverlayGlyphH];
};

constexpr int kAsciiFirst = 32;
constexpr int kAsciiLast = 126;
constexpr int kAccentFirst = kAsciiLast - kAsciiFirst + 1;  // 95

const Glyph kGlyphs[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  // space
    {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},  // !
    {{0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}},  // "
    {{0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},  // #
    {{0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},  // $
    {{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},  // %
    {{0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},  // &
    {{0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},  // '
    {{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},  // (
    {{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},  // )
    {{0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},  // *
    {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},  // +
    {{0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}},  // ,
    {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},  // -
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},  // .
    {{0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},  // /
    {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},  // 0
    {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},  // 1
    {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},  // 2
    {{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},  // 3
    {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},  // 4
    {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},  // 5
    {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},  // 6
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},  // 7
    {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},  // 8
    {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},  // 9
    {{0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},  // :
    {{0x00, 0x06, 0x06, 0x00, 0x06, 0x04, 0x08}},  // ;
    {{0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},  // <
    {{0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},  // =
    {{0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},  // >
    {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},  // ?
    {{0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}},  // @
    {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},  // A
    {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},  // B
    {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},  // C
    {{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},  // D
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},  // E
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},  // F
    {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},  // G
    {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},  // H
    {{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},  // I
    {{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},  // J
    {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},  // K
    {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},  // L
    {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},  // M
    {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},  // N
    {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},  // O
    {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},  // P
    {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},  // Q
    {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},  // R
    {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},  // S
    {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},  // T
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},  // U
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},  // V
    {{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},  // W
    {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},  // X
    {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},  // Y
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},  // Z
    {{0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},  // [
    {{0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00}},  // backslash
    {{0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},  // ]
    {{0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},  // ^
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},  // _
    {{0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},  // `
    {{0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},  // a
    {{0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}},  // b
    {{0x00, 0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E}},  // c
    {{0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}},  // d
    {{0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},  // e
    {{0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}},  // f
    {{0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}},  // g
    {{0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11}},  // h
    {{0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}},  // i
    {{0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}},  // j
    {{0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},  // k
    {{0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},  // l
    {{0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}},  // m
    {{0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11}},  // n
    {{0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}},  // o
    {{0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10}},  // p
    {{0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01}},  // q
    {{0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},  // r
    {{0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E}},  // s
    {{0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}},  // t
    {{0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}},  // u
    {{0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}},  // v
    {{0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}},  // w
    {{0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},  // x
    {{0x00, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x0E}},  // y
    {{0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}},  // z
    {{0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}},  // {
    {{0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},  // |
    {{0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}},  // }
    {{0x00, 0x00, 0x09, 0x16, 0x00, 0x00, 0x00}},  // ~
    // Accented characters: 95 a-acute, 96 e-acute, 97 i-acute, 98 o-acute, 99 u-acute,
    // 100 n-tilde, 101 N-tilde, 102 u-diaeresis, 103 inverted question mark, 104 inverted bang.
    {{0x02, 0x04, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},  // a-acute
    {{0x02, 0x04, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},  // e-acute
    {{0x02, 0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E}},  // i-acute
    {{0x02, 0x04, 0x0E, 0x11, 0x11, 0x11, 0x0E}},  // o-acute
    {{0x02, 0x04, 0x11, 0x11, 0x11, 0x13, 0x0D}},  // u-acute
    {{0x0A, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11}},  // n-tilde
    {{0x0A, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},  // N-tilde
    {{0x0A, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}},  // u-diaeresis
    {{0x04, 0x00, 0x04, 0x08, 0x10, 0x11, 0x0E}},  // inverted question mark
    {{0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04}},  // inverted bang
};

constexpr int kGlyphCount = static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));
static_assert(kGlyphCount == kAccentFirst + 10, "one glyph per printable ASCII character plus the accents");

int GlyphIndex(wchar_t c) {
    if (c >= kAsciiFirst && c <= kAsciiLast) {
        return static_cast<int>(c) - kAsciiFirst;
    }
    switch (c) {
        case 0x00E1: return kAccentFirst + 0;  // a-acute
        case 0x00E9: return kAccentFirst + 1;  // e-acute
        case 0x00ED: return kAccentFirst + 2;  // i-acute
        case 0x00F3: return kAccentFirst + 3;  // o-acute
        case 0x00FA: return kAccentFirst + 4;  // u-acute
        case 0x00F1: return kAccentFirst + 5;  // n-tilde
        case 0x00D1: return kAccentFirst + 6;  // N-tilde
        case 0x00FC: return kAccentFirst + 7;  // u-diaeresis
        case 0x00BF: return kAccentFirst + 8;  // inverted question mark
        case 0x00A1: return kAccentFirst + 9;  // inverted bang
        default: break;
    }
    return '?' - kAsciiFirst;
}

int FilledSegments(const MusicOverlayContent& content) {
    if (content.durationMs == 0) {
        return 0;
    }
    const uint64_t position = std::min(content.positionMs, content.durationMs);
    const uint64_t segments =
        (position * kMusicOverlaySegments + content.durationMs / 2) / content.durationMs;
    return static_cast<int>(std::min<uint64_t>(segments, kMusicOverlaySegments));
}

// A rounded rectangle test used for the case outline, so the bevel follows the corners.
bool InsideRoundedRect(int x, int y, int w, int h, int radius) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return false;
    }
    const int cx = (x < radius) ? radius : ((x >= w - radius) ? w - 1 - radius : x);
    const int cy = (y < radius) ? radius : ((y >= h - radius) ? h - 1 - radius : y);
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void Put(uint32_t* canvas, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= kMusicOverlayCaseW || y >= kMusicOverlayCaseH) {
        return;
    }
    canvas[static_cast<size_t>(y) * kMusicOverlayCaseW + static_cast<size_t>(x)] = color;
}

void DrawGlyph(uint32_t* canvas, int x, int y, wchar_t c) {
    const Glyph& glyph = kGlyphs[GlyphIndex(c)];
    for (int row = 0; row < kMusicOverlayGlyphH; ++row) {
        for (int col = 0; col < kMusicOverlayGlyphW; ++col) {
            const bool lit = (glyph.rows[row] & (1u << (kMusicOverlayGlyphW - 1 - col))) != 0;
            Put(canvas, x + col, y + row, lit ? kMusicOverlayLcdPixelColor : kMusicOverlayLcdOffColor);
        }
    }
}

// Draws text in the LCD, clipped to the usable inner area, scrolled left by scrollDots.
void DrawLcdText(uint32_t* canvas, const std::wstring& text, int y, int x, int scrollDots) {
    const int left = kMusicOverlayLcdInnerX;
    const int right = left + kMusicOverlayLcdInnerW;
    int penX = x - scrollDots;
    for (wchar_t c : text) {
        if (penX >= right) {
            break;
        }
        if (penX + kMusicOverlayGlyphW > left) {
            const Glyph& glyph = kGlyphs[GlyphIndex(c)];
            for (int row = 0; row < kMusicOverlayGlyphH; ++row) {
                for (int col = 0; col < kMusicOverlayGlyphW; ++col) {
                    const int px = penX + col;
                    if (px < left || px >= right) {
                        continue;
                    }
                    const bool lit = (glyph.rows[row] & (1u << (kMusicOverlayGlyphW - 1 - col))) != 0;
                    Put(canvas, px, y + row, lit ? kMusicOverlayLcdPixelColor : kMusicOverlayLcdOffColor);
                }
            }
        }
        penX += kMusicOverlayGlyphAdvance;
    }
}

std::wstring ElapsedText(uint64_t positionMs) {
    uint64_t totalSeconds = positionMs / 1000;
    uint64_t minutes = totalSeconds / 60;
    if (minutes > 99) {
        minutes = 99;
        totalSeconds = 99 * 60 + 59;
    }
    const uint64_t seconds = totalSeconds % 60;
    std::wstring text(5, L'0');
    text[0] = static_cast<wchar_t>(L'0' + minutes / 10);
    text[1] = static_cast<wchar_t>(L'0' + minutes % 10);
    text[2] = L':';
    text[3] = static_cast<wchar_t>(L'0' + seconds / 10);
    text[4] = static_cast<wchar_t>(L'0' + seconds % 10);
    return text;
}

void DrawProgress(uint32_t* canvas, int filledSegments) {
    const int x0 = kMusicOverlayLcdInnerX + kMusicOverlaySegmentX;
    const int y0 = kMusicOverlayLcdInnerY + kMusicOverlaySegmentY;
    for (int segment = 0; segment < kMusicOverlaySegments; ++segment) {
        const int sx = x0 + segment * (kMusicOverlaySegmentW + kMusicOverlaySegmentGap);
        const uint32_t color = (segment < filledSegments) ? kMusicOverlayLcdPixelColor
                                                         : kMusicOverlayProgressOffColor;
        for (int row = 0; row < kMusicOverlaySegmentH; ++row) {
            for (int col = 0; col < kMusicOverlaySegmentW; ++col) {
                Put(canvas, sx + col, y0 + row, color);
            }
        }
    }
}

void DrawPlayPauseIcon(uint32_t* canvas, bool playing) {
    // 1 = drawn, 0 = left as backlight, for a 5x7 cell.
    static constexpr uint8_t kPlay[kMusicOverlayGlyphH][kMusicOverlayGlyphW] = {
        {1, 0, 0, 0, 0}, {1, 1, 0, 0, 0}, {1, 1, 1, 0, 0}, {1, 1, 1, 1, 0},
        {1, 1, 1, 0, 0}, {1, 1, 0, 0, 0}, {1, 0, 0, 0, 0},
    };
    static constexpr uint8_t kPause[kMusicOverlayGlyphH][kMusicOverlayGlyphW] = {
        {1, 1, 0, 1, 1}, {1, 1, 0, 1, 1}, {1, 1, 0, 1, 1}, {1, 1, 0, 1, 1},
        {1, 1, 0, 1, 1}, {1, 1, 0, 1, 1}, {1, 1, 0, 1, 1},
    };
    const int x0 = kMusicOverlayLcdInnerX + kMusicOverlayIconX;
    const int y0 = kMusicOverlayLcdInnerY + kMusicOverlayRow3Y;
    for (int row = 0; row < kMusicOverlayGlyphH; ++row) {
        for (int col = 0; col < kMusicOverlayGlyphW; ++col) {
            if ((playing ? kPlay[row][col] : kPause[row][col]) != 0) {
                Put(canvas, x0 + col, y0 + row, kMusicOverlayLcdPixelColor);
            }
        }
    }
}

}  // namespace

MusicOverlayController::MusicOverlayController(MusicOverlayMode mode) : m_mode(mode) {}

void MusicOverlayController::SetMode(MusicOverlayMode mode) {
    m_mode = mode;
}

MusicOverlayMode MusicOverlayController::Mode() const {
    return m_mode;
}

MusicOverlayVisibility MusicOverlayController::Visibility() const {
    return m_visibility;
}

void MusicOverlayController::ResetForRace() {
    m_visibility = MusicOverlayVisibility::Configured;
}

bool MusicOverlayController::OnChange(uint32_t changeCounter, uint64_t nowMs) {
    const bool changed = !m_hasCounter || changeCounter != m_lastCounter;
    m_hasCounter = true;
    m_lastCounter = changeCounter;
    if (!changed || m_visibility != MusicOverlayVisibility::Configured ||
        m_mode != MusicOverlayMode::OnDemand) {
        return false;
    }
    m_showMs = nowMs;
    m_hasShown = true;
    return true;
}

void MusicOverlayController::OnOverlayHotkey(uint64_t nowMs, bool raceActive) {
    const bool visible = Alpha(nowMs, raceActive) > 0.0f;
    // R3-overlay-hotkey-persists-across-races: only the per-race override changes. m_mode is the
    // configured mode (registry, ini and the Beeper row) and stays untouched.
    if (visible) {
        m_visibility = MusicOverlayVisibility::Hidden;
        m_hasShown = false;
        m_showMs = nowMs;
        return;
    }
    m_visibility = MusicOverlayVisibility::Shown;
    m_hasShown = false;
}

float MusicOverlayController::Alpha(uint64_t nowMs, bool raceActive) const {
    if (m_visibility == MusicOverlayVisibility::Hidden) {
        return 0.0f;
    }
    if (m_visibility == MusicOverlayVisibility::Shown) {
        return raceActive ? 1.0f : 0.0f;
    }
    if (m_mode == MusicOverlayMode::Pinned) {
        return raceActive ? 1.0f : 0.0f;
    }
    if (!m_hasShown || nowMs <= m_showMs) {
        return m_hasShown ? 1.0f : 0.0f;
    }
    const uint64_t elapsed = nowMs - m_showMs;
    if (elapsed >= kMusicOverlayVisibleMs + kMusicOverlayFadeMs) {
        return 0.0f;
    }
    if (elapsed <= kMusicOverlayVisibleMs) {
        return 1.0f;
    }
    return 1.0f - static_cast<float>(elapsed - kMusicOverlayVisibleMs) /
                      static_cast<float>(kMusicOverlayFadeMs);
}

int MusicOverlayMargin(int clientH) {
    return std::max(4, clientH * 2 / 100);
}

int MusicOverlayScale(int clientH) {
    if (clientH <= 0) {
        return 1;
    }
    const double exact = 0.15 * static_cast<double>(clientH) / static_cast<double>(kMusicOverlayCaseH);
    return std::max(1, static_cast<int>(std::lround(exact)));
}

namespace {

// Largest integer scale, at most maxScale, whose panel fits areaW x areaH with the margin on every
// side. 0 when even scale 1 does not fit.
int FittingScale(int areaW, int areaH, int panelW, int panelH, int margin, int maxScale) {
    int scale = maxScale;
    while (scale > 1 && (panelW * scale > areaW - 2 * margin || panelH * scale > areaH - 2 * margin)) {
        --scale;
    }
    if (panelW * scale > areaW - 2 * margin || panelH * scale > areaH - 2 * margin) {
        return 0;
    }
    return scale;
}

}  // namespace

MusicOverlayPlacement ComputeMusicOverlayPlacement(int clientW, int clientH, const MusicOverlayRect& image,
                                                   int hudLeft, int hudRight, int panelW, int panelH,
                                                   MusicCorner corner) {
    MusicOverlayPlacement placement;
    if (clientW <= 0 || clientH <= 0 || image.width <= 0 || image.height <= 0 || panelW <= 0 || panelH <= 0) {
        return placement;
    }
    const int margin = MusicOverlayMargin(clientH);
    const int maxScale = MusicOverlayScale(clientH);

    // Side areas (rules 1 and 2) are used only when the panel fits there at full size: a shrunken
    // panel would be unreadable, so a narrow side area falls through to the image corner instead.
    // 1. Left pillarbox of a 4:3 image in a wider window.
    if (image.x > 0) {
        const int scale = FittingScale(image.x, image.height, panelW, panelH, margin, maxScale);
        if (scale == maxScale) {
            placement.draw = true;
            placement.scale = scale;
            placement.x = (image.x - panelW * scale) / 2;
            placement.y = image.y + image.height - margin - panelH * scale;
            return placement;
        }
    }

    // 2. The band between the image's left edge and the centred 4:3 HUD layer of a widescreen race.
    if (hudRight > hudLeft && hudLeft > image.x) {
        const int scale = FittingScale(hudLeft - image.x, image.height, panelW, panelH, margin, maxScale);
        if (scale == maxScale) {
            placement.draw = true;
            placement.scale = scale;
            placement.x = image.x + margin;
            placement.y = image.y + image.height - margin - panelH * scale;
            return placement;
        }
    }

    // 3. An image corner. 4. Nothing when even the native panel does not fit inside the image.
    const int scale = FittingScale(image.width, image.height, panelW, panelH, margin, maxScale);
    if (scale <= 0) {
        return placement;
    }
    placement.draw = true;
    placement.scale = scale;
    const int width = panelW * scale;
    const int height = panelH * scale;
    const bool right = (corner == MusicCorner::BottomRight || corner == MusicCorner::TopRight);
    const bool top = (corner == MusicCorner::TopLeft || corner == MusicCorner::TopRight);
    placement.x = right ? image.x + image.width - margin - width : image.x + margin;
    placement.y = top ? image.y + margin : image.y + image.height - margin - height;
    return placement;
}

std::wstring MusicOverlayMarqueeText(const MusicOverlayContent& content) {
    if (content.artist.empty()) {
        return content.title;
    }
    return content.artist + L" - " + content.title;
}

int MusicOverlayTextWidthDots(const std::wstring& text) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(text.size()) * kMusicOverlayGlyphAdvance - 1;
}

int MarqueeOffset(int textWidthDots, int lcdWidthDots, uint64_t nowMs) {
    if (textWidthDots <= lcdWidthDots) {
        return 0;
    }
    const int overflow = textWidthDots - lcdWidthDots;
    const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kMusicOverlayMarqueeStepMs;
    const uint64_t cycle = 2 * kMusicOverlayMarqueePauseMs + scrollMs;
    const uint64_t t = nowMs % cycle;
    if (t < kMusicOverlayMarqueePauseMs) {
        return 0;
    }
    const uint64_t scrolled = t - kMusicOverlayMarqueePauseMs;
    if (scrolled >= scrollMs) {
        return overflow;
    }
    return static_cast<int>(scrolled / kMusicOverlayMarqueeStepMs);
}

int MusicOverlayMarqueeOffset(const MusicOverlayContent& content, int lcdWidthDots, uint64_t nowMs) {
    return MarqueeOffset(MusicOverlayTextWidthDots(MusicOverlayMarqueeText(content)), lcdWidthDots, nowMs);
}

void ApplyMusicOverlayMarqueeOffset(MusicOverlayContent& content, uint64_t nowMs) {
    content.marqueeOffsetDots = MusicOverlayMarqueeOffset(content, kMusicOverlayLcdInnerW, nowMs);
}

bool RasterizeMusicOverlay(const MusicOverlayContent& content, int scale, MusicOverlayBitmap& out) {
    out = MusicOverlayBitmap{};
    if (scale < 1 || scale > 64) {
        return false;
    }

    std::vector<uint32_t> native(static_cast<size_t>(kMusicOverlayCaseW) * kMusicOverlayCaseH, 0);

    // Plastic case with a one-pixel bevel: lighter on the top and left, darker on the bottom and
    // right, following the rounded corners.
    for (int y = 0; y < kMusicOverlayCaseH; ++y) {
        for (int x = 0; x < kMusicOverlayCaseW; ++x) {
            if (!InsideRoundedRect(x, y, kMusicOverlayCaseW, kMusicOverlayCaseH, kMusicOverlayCaseRadius)) {
                continue;
            }
            uint32_t color = kMusicOverlayCaseColor;
            if (!InsideRoundedRect(x - 1, y, kMusicOverlayCaseW, kMusicOverlayCaseH, kMusicOverlayCaseRadius) ||
                !InsideRoundedRect(x, y - 1, kMusicOverlayCaseW, kMusicOverlayCaseH, kMusicOverlayCaseRadius)) {
                color = kMusicOverlayBevelLightColor;
            } else if (!InsideRoundedRect(x + 1, y, kMusicOverlayCaseW, kMusicOverlayCaseH,
                                          kMusicOverlayCaseRadius) ||
                       !InsideRoundedRect(x, y + 1, kMusicOverlayCaseW, kMusicOverlayCaseH,
                                          kMusicOverlayCaseRadius)) {
                color = kMusicOverlayBevelDarkColor;
            }
            Put(native.data(), x, y, color);
        }
    }

    // Inset LCD: backlight with a darker one-pixel inner border.
    for (int y = kMusicOverlayLcdY; y < kMusicOverlayLcdY + kMusicOverlayLcdH; ++y) {
        for (int x = kMusicOverlayLcdX; x < kMusicOverlayLcdX + kMusicOverlayLcdW; ++x) {
            const bool border = x == kMusicOverlayLcdX || x == kMusicOverlayLcdX + kMusicOverlayLcdW - 1 ||
                                y == kMusicOverlayLcdY || y == kMusicOverlayLcdY + kMusicOverlayLcdH - 1;
            Put(native.data(), x, y, border ? kMusicOverlayLcdBorderColor : kMusicOverlayLcdColor);
        }
    }

    // Row 1: cassette label.
    DrawLcdText(native.data(), content.cassetteLabel, kMusicOverlayLcdInnerY + kMusicOverlayRow1Y,
                kMusicOverlayLcdInnerX, 0);

    // Row 2: artist - title, scrolling when it is wider than the LCD.
    DrawLcdText(native.data(), MusicOverlayMarqueeText(content), kMusicOverlayLcdInnerY + kMusicOverlayRow2Y,
                kMusicOverlayLcdInnerX, content.marqueeOffsetDots);

    // Row 3: elapsed time, segmented progress and the play/pause icon.
    DrawLcdText(native.data(), ElapsedText(content.positionMs), kMusicOverlayLcdInnerY + kMusicOverlayRow3Y,
                kMusicOverlayLcdInnerX, 0);
    DrawProgress(native.data(), FilledSegments(content));
    DrawPlayPauseIcon(native.data(), content.playing);

    // Tiny LED next to the LCD: lit while playing, off while paused.
    const uint32_t ledColor = content.playing ? kMusicOverlayLedOnColor : kMusicOverlayLedOffColor;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx * dx + dy * dy <= 2) {
                Put(native.data(), kMusicOverlayLedX + dx, kMusicOverlayLedY + dy, ledColor);
            }
        }
    }

    // Expand every native pixel to an exact scale x scale block, so the LCD dots stay crisp.
    out.width = kMusicOverlayCaseW * scale;
    out.height = kMusicOverlayCaseH * scale;
    out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    for (int y = 0; y < kMusicOverlayCaseH; ++y) {
        for (int x = 0; x < kMusicOverlayCaseW; ++x) {
            const uint32_t color = native[static_cast<size_t>(y) * kMusicOverlayCaseW + static_cast<size_t>(x)];
            uint32_t* row = out.pixels.data() + static_cast<size_t>(y * scale) * out.width + static_cast<size_t>(x * scale);
            for (int sy = 0; sy < scale; ++sy) {
                std::fill(row, row + scale, color);
                row += out.width;
            }
        }
    }
    return true;
}

bool operator==(const MusicOverlayKey& a, const MusicOverlayKey& b) {
    return a.changeCounter == b.changeCounter && a.playing == b.playing && a.elapsedSeconds == b.elapsedSeconds &&
           a.filledSegments == b.filledSegments && a.marqueeOffsetDots == b.marqueeOffsetDots &&
           a.scale == b.scale;
}

bool operator!=(const MusicOverlayKey& a, const MusicOverlayKey& b) {
    return !(a == b);
}

MusicOverlayKey MakeMusicOverlayKey(const MusicOverlayContent& content, int scale, uint64_t nowMs) {
    return BuildMusicOverlayFrameInput(content, scale, nowMs, kMusicOverlayLcdInnerW).key;
}

MusicOverlayFrameInput BuildMusicOverlayFrameInput(const MusicOverlayContent& content, int scale,
                                                   uint64_t nowMs, int lcdWidthDots) {
    MusicOverlayFrameInput input;
    input.content = content;
    input.content.marqueeOffsetDots = MusicOverlayMarqueeOffset(content, lcdWidthDots, nowMs);
    input.key.changeCounter = content.changeCounter;
    input.key.playing = content.playing;
    input.key.elapsedSeconds = static_cast<int>(content.positionMs / 1000);
    input.key.filledSegments = FilledSegments(content);
    // The key compares the exact offset the rasterizer draws, so a marquee step always re-rasterizes.
    input.key.marqueeOffsetDots = input.content.marqueeOffsetDots;
    input.key.scale = scale;
    return input;
}

namespace {

uint8_t AlphaByte(float alpha) {
    if (!(alpha > 0.0f)) {
        return 0;
    }
    if (alpha >= 1.0f) {
        return 255;
    }
    return static_cast<uint8_t>(alpha * 255.0f + 0.5f);
}

// Scales one straight-alpha BGRA pixel to premultiplied alpha. The panel's own pixels are either
// fully opaque or clear, so this only changes the fade.
uint32_t Premultiplied(uint32_t pixel, uint8_t alpha) {
    const uint32_t a = (pixel >> 24) & 0xFFu;
    if (a == 0) {
        return 0;
    }
    const uint32_t scaledAlpha = (a * alpha + 127u) / 255u;
    const uint32_t r = (((pixel >> 16) & 0xFFu) * scaledAlpha + 127u) / 255u;
    const uint32_t g = (((pixel >> 8) & 0xFFu) * scaledAlpha + 127u) / 255u;
    const uint32_t b = ((pixel & 0xFFu) * scaledAlpha + 127u) / 255u;
    return (scaledAlpha << 24) | (r << 16) | (g << 8) | b;
}

}  // namespace

void PremultiplyMusicOverlayAlpha(MusicOverlayBitmap& bitmap, float alpha) {
    const uint8_t alphaByte = AlphaByte(alpha);
    for (uint32_t& pixel : bitmap.pixels) {
        pixel = Premultiplied(pixel, alphaByte);
    }
}

bool UpdateMusicOverlayFrame(const MusicOverlayContent& content, int scale, int x, int y, uint64_t nowMs,
                             float alpha, MusicOverlayFrameState& state) {
    // One builder fills the content the rasterizer draws and the key that decides a re-rasterization.
    const MusicOverlayFrameInput input =
        BuildMusicOverlayFrameInput(content, scale, nowMs, kMusicOverlayLcdInnerW);
    const MusicOverlayKey& key = input.key;
    const uint8_t alphaByte = AlphaByte(alpha);
    bool changed = false;
    if (!state.hasKey || key != state.key) {
        MusicOverlayBitmap straight;
        if (RasterizeMusicOverlay(input.content, scale, straight)) {
            state.key = key;
            state.hasKey = true;
        } else {
            straight = MusicOverlayBitmap{};
        }
        MusicOverlayBitmap bitmap = straight;
        PremultiplyMusicOverlayAlpha(bitmap, alpha);
        if (bitmap.pixels != state.bitmap.pixels || bitmap.width != state.bitmap.width ||
            bitmap.height != state.bitmap.height) {
            state.bitmap = std::move(bitmap);
            ++state.changeCounter;
            changed = true;
        }
        state.source = std::move(straight);
        state.alphaByte = alphaByte;
    } else if (alphaByte != state.alphaByte) {
        // The fade changes pixels without changing the rasterization key. Start again from the
        // straight-alpha source: premultiplying the faded bitmap would multiply alpha twice.
        MusicOverlayBitmap bitmap = state.source;
        PremultiplyMusicOverlayAlpha(bitmap, alpha);
        if (bitmap.pixels != state.bitmap.pixels) {
            state.bitmap = std::move(bitmap);
            ++state.changeCounter;
            changed = true;
        }
        state.alphaByte = alphaByte;
    }
    if (state.x != x || state.y != y) {
        state.x = x;
        state.y = y;
        changed = true;
    }
    return changed;
}

namespace {

// Memory index of the visual pixel (x, y). A negative biHeight means top-down; a positive one means
// bottom-up, where visual row y lives at memory row dibHeight - 1 - y.
size_t MusicDibIndex(int dibWidth, int dibHeight, MusicDibOrientation orientation, int x, int y) {
    const int row = orientation == MusicDibOrientation::BottomUp ? dibHeight - 1 - y : y;
    return static_cast<size_t>(row) * static_cast<size_t>(dibWidth) + static_cast<size_t>(x);
}

}  // namespace

void BlendMusicOverlayIntoDib(uint32_t* dib, int dibWidth, int dibHeight, MusicDibOrientation orientation,
                              const MusicOverlayBitmap& panel, int x, int y) {
    if (dib == nullptr || dibWidth <= 0 || dibHeight <= 0 || panel.width <= 0 || panel.height <= 0) {
        return;
    }
    const size_t needed = static_cast<size_t>(panel.width) * static_cast<size_t>(panel.height);
    if (panel.pixels.size() < needed) {
        return;
    }
    for (int row = 0; row < panel.height; ++row) {
        const int dstY = y + row;
        if (dstY < 0 || dstY >= dibHeight) {
            continue;
        }
        for (int column = 0; column < panel.width; ++column) {
            const int dstX = x + column;
            if (dstX < 0 || dstX >= dibWidth) {
                continue;
            }
            const uint32_t src = panel.pixels[static_cast<size_t>(row) * static_cast<size_t>(panel.width) +
                                           static_cast<size_t>(column)];
            const uint32_t alpha = src >> 24;
            uint32_t* dst = dib + MusicDibIndex(dibWidth, dibHeight, orientation, dstX, dstY);
            if (alpha == 0) {
                continue;
            }
            if (alpha == 255) {
                // Opaque: the premultiplied source is the result; the high byte stays zero for GDI.
                *dst = src & 0x00FFFFFFu;
                continue;
            }
            const uint32_t inverse = 255u - alpha;
            const auto blend = [inverse](uint32_t srcChannel, uint32_t dstChannel) {
                return srcChannel + (dstChannel * inverse + 127u) / 255u;
            };
            const auto clamp255 = [](uint32_t value) { return value > 255u ? 255u : value; };
            // A pixel that is not premultiplied can push a channel past 255. Clamping each channel
            // keeps the sum out of the next channel's bits, and the mask keeps the high byte zero.
            const uint32_t b = clamp255(blend(src & 0xFFu, *dst & 0xFFu));
            const uint32_t g = clamp255(blend((src >> 8) & 0xFFu, (*dst >> 8) & 0xFFu));
            const uint32_t r = clamp255(blend((src >> 16) & 0xFFu, (*dst >> 16) & 0xFFu));
            *dst = ((r << 16) | (g << 8) | b) & 0x00FFFFFFu;
        }
    }
}

MusicDibFramePlan PlanMusicDibFrame(int32_t bitCount, int32_t biWidth, int32_t biHeight, int engineWidth,
                                    int engineHeight) {
    MusicDibFramePlan plan;
    if (bitCount != 32) {
        plan.logMessage = "Music: the game DIB is not a 32-bit frame; the beeper is not blended into it";
        return plan;
    }
    if (engineWidth <= 0 || engineHeight <= 0 || biWidth != engineWidth) {
        plan.logMessage = "Music: the game DIB width does not match the engine frame; the beeper is not "
                          "blended into it";
        return plan;
    }
    const int64_t magnitude = biHeight < 0 ? -static_cast<int64_t>(biHeight) : static_cast<int64_t>(biHeight);
    if (biHeight == 0 || magnitude != static_cast<int64_t>(engineHeight)) {
        plan.logMessage = "Music: the game DIB height does not match the engine frame; the beeper is not "
                          "blended into it";
        return plan;
    }
    plan.blend = true;
    plan.orientation = biHeight < 0 ? MusicDibOrientation::TopDown : MusicDibOrientation::BottomUp;
    return plan;
}

bool SaveMusicDibRegion(const uint32_t* dib, int dibWidth, int dibHeight, MusicDibOrientation orientation,
                        const MusicOverlayBitmap& panel, int x, int y, MusicDibSavedRegion& out) {
    out = MusicDibSavedRegion{};
    if (dib == nullptr || dibWidth <= 0 || dibHeight <= 0 || panel.width <= 0 || panel.height <= 0) {
        return false;
    }
    const size_t needed = static_cast<size_t>(panel.width) * static_cast<size_t>(panel.height);
    if (panel.pixels.size() < needed) {
        return false;
    }
    const int x0 = x < 0 ? 0 : x;
    const int y0 = y < 0 ? 0 : y;
    const int x1 = x + panel.width > dibWidth ? dibWidth : x + panel.width;
    const int y1 = y + panel.height > dibHeight ? dibHeight : y + panel.height;
    if (x1 <= x0 || y1 <= y0) {
        return false;  // the panel does not overlap the DIB: nothing is written, nothing to save
    }
    out.dibWidth = dibWidth;
    out.dibHeight = dibHeight;
    out.orientation = orientation;
    out.x = x0;
    out.y = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    for (int row = 0; row < out.height; ++row) {
        for (int column = 0; column < out.width; ++column) {
            out.pixels[static_cast<size_t>(row) * static_cast<size_t>(out.width) +
                       static_cast<size_t>(column)] =
                dib[MusicDibIndex(dibWidth, dibHeight, orientation, x0 + column, y0 + row)];
        }
    }
    return true;
}

void RestoreMusicDibRegion(uint32_t* dib, const MusicDibSavedRegion& saved) {
    if (dib == nullptr || saved.width <= 0 || saved.height <= 0) {
        return;
    }
    const size_t needed = static_cast<size_t>(saved.width) * static_cast<size_t>(saved.height);
    if (saved.pixels.size() < needed) {
        return;
    }
    for (int row = 0; row < saved.height; ++row) {
        for (int column = 0; column < saved.width; ++column) {
            dib[MusicDibIndex(saved.dibWidth, saved.dibHeight, saved.orientation, saved.x + column,
                              saved.y + row)] =
                saved.pixels[static_cast<size_t>(row) * static_cast<size_t>(saved.width) +
                             static_cast<size_t>(column)];
        }
    }
}

bool BeginMusicDibBlit(MusicDibBlitTracker& tracker, uint32_t frameSequence) {
    if (tracker.active && tracker.activeSequence == frameSequence) {
        return false;  // a reentrant present for the same frame inside one blit: never blend twice
    }
    tracker.active = true;
    tracker.activeSequence = frameSequence;
    return true;
}

void EndMusicDibBlit(MusicDibBlitTracker& tracker, bool began) {
    if (!began) {
        return;  // a refused reentrant call has no blit of its own to end
    }
    tracker.active = false;
}
