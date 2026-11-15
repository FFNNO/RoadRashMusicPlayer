#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "music_config.h"

// In-game music "beeper" overlay (feature rr95-music-player, task M4; standalone mod S3b). Pure
// logic: timing and fade alpha, placement, the marquee offset, and a software BGRA rasterizer for a
// generic 90s pager panel. Nothing here touches Win32, GDI or D3D11: the general mod's presenter
// uploads the layer that music_glue hands it through the mod API.
//
// The panel is drawn at a small native size and every native pixel becomes an exact
// scale x scale block, so the LCD dots stay crisp at any window size.

// Native panel geometry, in dots. One place for every constant the rasterizer uses.
inline constexpr int kMusicOverlayCaseW = 140;
inline constexpr int kMusicOverlayCaseH = 50;
inline constexpr int kMusicOverlayCaseRadius = 4;
inline constexpr int kMusicOverlayLcdX = 10;
inline constexpr int kMusicOverlayLcdY = 8;
inline constexpr int kMusicOverlayLcdW = 120;
inline constexpr int kMusicOverlayLcdH = 30;
// Usable LCD area inside the 1-pixel inner border: 118 x 28 dots.
inline constexpr int kMusicOverlayLcdInnerX = kMusicOverlayLcdX + 1;
inline constexpr int kMusicOverlayLcdInnerY = kMusicOverlayLcdY + 1;
inline constexpr int kMusicOverlayLcdInnerW = kMusicOverlayLcdW - 2;
inline constexpr int kMusicOverlayLcdInnerH = kMusicOverlayLcdH - 2;
// Row tops relative to the inner LCD area. The 5x7 font is 7 dots tall.
inline constexpr int kMusicOverlayRow1Y = 1;
inline constexpr int kMusicOverlayRow2Y = 10;
inline constexpr int kMusicOverlayRow3Y = 21;
inline constexpr int kMusicOverlayGlyphW = 5;
inline constexpr int kMusicOverlayGlyphH = 7;
inline constexpr int kMusicOverlayGlyphAdvance = 6;
inline constexpr int kMusicOverlaySegments = 16;
inline constexpr int kMusicOverlaySegmentW = 3;
inline constexpr int kMusicOverlaySegmentGap = 1;
inline constexpr int kMusicOverlaySegmentX = 33;
inline constexpr int kMusicOverlaySegmentY = 22;
inline constexpr int kMusicOverlaySegmentH = 5;
inline constexpr int kMusicOverlayIconX = 108;
inline constexpr int kMusicOverlayLedX = 134;
inline constexpr int kMusicOverlayLedY = 44;

// Straight-alpha BGRA, the layout the software rasterizer works in. The mod API layer wants
// premultiplied BGRA (the presenter blends with ONE / INV_SRC_ALPHA), so music_glue premultiplies
// the bitmap with the frame's fade alpha before it hands it over
// (PremultiplyMusicOverlayAlpha). DXGI_FORMAT_B8G8R8A8_UNORM reads byte 0 as blue, so the packed
// dword holds B, G, R, A in memory order (task M5b: this was R, G, B, A and swapped red with blue
// on screen).
constexpr uint32_t MusicOverlayBgra(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

inline constexpr uint32_t kMusicOverlayCaseColor = MusicOverlayBgra(43, 43, 46);
inline constexpr uint32_t kMusicOverlayBevelLightColor = MusicOverlayBgra(70, 70, 74);
inline constexpr uint32_t kMusicOverlayBevelDarkColor = MusicOverlayBgra(24, 24, 27);
inline constexpr uint32_t kMusicOverlayLcdColor = MusicOverlayBgra(168, 198, 78);
inline constexpr uint32_t kMusicOverlayLcdBorderColor = MusicOverlayBgra(120, 145, 55);
inline constexpr uint32_t kMusicOverlayLcdOffColor = MusicOverlayBgra(158, 186, 73);
inline constexpr uint32_t kMusicOverlayLcdPixelColor = MusicOverlayBgra(30, 42, 16);
inline constexpr uint32_t kMusicOverlayLedOnColor = MusicOverlayBgra(232, 72, 48);
inline constexpr uint32_t kMusicOverlayLedOffColor = MusicOverlayBgra(58, 56, 52);
inline constexpr uint32_t kMusicOverlayProgressOffColor = MusicOverlayBgra(150, 178, 66);

// What the panel shows for one frame. The presenter fills it from MusicPlayerSnapshot; the marquee
// offset is an input so RasterizeMusicOverlay stays pure.
struct MusicOverlayContent {
    std::wstring cassetteLabel;
    std::wstring title;
    std::wstring artist;
    bool playing = false;      // LED lit and play icon; paused shows the LED off and the pause icon
    bool raceActive = false;   // pinned mode is visible only while a race is running
    uint64_t positionMs = 0;
    uint64_t durationMs = 0;
    uint32_t changeCounter = 0;
    int marqueeOffsetDots = 0;  // from MarqueeOffset, in LCD dots
};

constexpr uint64_t kMusicOverlayVisibleMs = 5000;  // on demand: full alpha for this long
constexpr uint64_t kMusicOverlayFadeMs = 500;      // then a linear fade to 0
constexpr uint64_t kMusicOverlayMarqueeStepMs = 120;
constexpr uint64_t kMusicOverlayMarqueePauseMs = 800;

// Mode and fade timing. On demand shows the panel for kMusicOverlayVisibleMs after a track or
// cassette change or the overlay hotkey, then fades out. Pinned keeps it while the race runs.
//
// R3-overlay-hotkey-persists-across-races: the hotkey is a per-race show/hide only. It never writes
// the configured mode (the registry ModMusicOverlay, the ini or the Beeper popup row); ResetForRace
// at every race start returns the visibility to the configured mode.
enum class MusicOverlayVisibility {
    Configured = 0,  // follow Mode()
    Shown = 1,       // the hotkey showed the panel for this race
    Hidden = 2       // the hotkey hid the panel for this race
};

class MusicOverlayController {
public:
    explicit MusicOverlayController(MusicOverlayMode mode = MusicOverlayMode::OnDemand);

    void SetMode(MusicOverlayMode mode);
    MusicOverlayMode Mode() const;
    MusicOverlayVisibility Visibility() const;

    // Back to the configured mode's own behaviour. Called at every race start.
    void ResetForRace();

    // Shows the panel (and restarts the on-demand window) when changeCounter differs from the last
    // value seen. Returns true when this call showed it.
    bool OnChange(uint32_t changeCounter, uint64_t nowMs);

    // Show/hide toggle (task M5b, bug C; R3-overlay-hotkey-persists-across-races): a press while
    // the panel is visible hides it for this race; a press while hidden shows it for this race. The
    // configured mode is never changed.
    void OnOverlayHotkey(uint64_t nowMs, bool raceActive);

    // On demand: 1.0 for kMusicOverlayVisibleMs after the last show, then a linear fade over
    // kMusicOverlayFadeMs. Pinned: 1.0 while raceActive, 0 outside a race. The per-race override
    // wins over both: Hidden is always 0, Shown is 1 while raceActive.
    float Alpha(uint64_t nowMs, bool raceActive) const;

private:
    MusicOverlayMode m_mode;
    MusicOverlayVisibility m_visibility = MusicOverlayVisibility::Configured;
    bool m_hasShown = false;
    bool m_hasCounter = false;
    uint64_t m_showMs = 0;
    uint32_t m_lastCounter = 0;
};

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

struct MusicOverlayRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct MusicOverlayPlacement {
    bool draw = false;
    int x = 0;
    int y = 0;
    int scale = 1;
};

// Margin around the panel: 2% of the client height, at least 4 pixels.
int MusicOverlayMargin(int clientH);

// Integer scale so the panel is about 15% of the client height, never below 1.
int MusicOverlayScale(int clientH);

// Client-pixel placement of the panel, or draw == false. hudLeft / hudRight are the centred 4:3
// HUD layer in client pixels; pass hudRight <= hudLeft for "no HUD layer". The rules, in order:
//  1. left pillarbox [0, image.x) when the panel fits there with the margin on both sides;
//  2. the band [image.x, hudLeft) of a widescreen race, bottom-left;
//  3. the image corner given by corner, inset by the margin;
//  4. no draw when the panel does not fit the image even at scale 1.
// The scale is reduced while the panel does not fit the chosen area.
MusicOverlayPlacement ComputeMusicOverlayPlacement(int clientW, int clientH, const MusicOverlayRect& image,
                                                   int hudLeft, int hudRight, int panelW, int panelH,
                                                   MusicCorner corner);

// ---------------------------------------------------------------------------
// Text and marquee
// ---------------------------------------------------------------------------

// Row 2 text: "artist - title", or just the title when the artist is empty.
std::wstring MusicOverlayMarqueeText(const MusicOverlayContent& content);

// Width in dots of text in the built-in 5x7 font: 6 dots per glyph minus the trailing gap. An
// unsupported character counts as '?', which is what the rasterizer draws.
int MusicOverlayTextWidthDots(const std::wstring& text);

// Horizontal scroll in dots for text wider than the LCD: no scroll when it fits, otherwise one dot
// every kMusicOverlayMarqueeStepMs with a short pause at both ends and a jump back to the start.
int MarqueeOffset(int textWidthDots, int lcdWidthDots, uint64_t nowMs);

// The marquee offset of one content line for a given LCD width. One derivation for every caller.
int MusicOverlayMarqueeOffset(const MusicOverlayContent& content, int lcdWidthDots, uint64_t nowMs);

// R3-marquee-never-scrolls: fills content.marqueeOffsetDots from the time, exactly what
// MakeMusicOverlayKey compares, so the rasterizer draws the offset the key describes.
void ApplyMusicOverlayMarqueeOffset(MusicOverlayContent& content, uint64_t nowMs);

// ---------------------------------------------------------------------------
// Rasterizer
// ---------------------------------------------------------------------------

struct MusicOverlayBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;  // straight-alpha BGRA, top-down
};

// Draws the panel at native size and expands every native pixel to an exact scale x scale block.
// False for scale < 1 or an empty result.
bool RasterizeMusicOverlay(const MusicOverlayContent& content, int scale, MusicOverlayBitmap& out);

// Rasterization key: the bitmap is rebuilt only when one of these changes. position is reduced to
// the filled progress segments and the time text to whole seconds, so a running track does not
// re-rasterize every frame.
struct MusicOverlayKey {
    uint32_t changeCounter = 0;
    bool playing = false;
    int elapsedSeconds = 0;
    int filledSegments = 0;
    int marqueeOffsetDots = 0;
    int scale = 1;
};

bool operator==(const MusicOverlayKey& a, const MusicOverlayKey& b);
bool operator!=(const MusicOverlayKey& a, const MusicOverlayKey& b);

// Key for content at scale and nowMs. MarqueeOffset is evaluated with the usable LCD width.
MusicOverlayKey MakeMusicOverlayKey(const MusicOverlayContent& content, int scale, uint64_t nowMs);

// S3b review R3-marquee-glue-fill-unproved: one pure call builds the content the rasterizer draws and
// the key that decides the re-rasterization, from the same time and LCD width. The drawn marquee
// offset and the key's offset are therefore always the same number.
struct MusicOverlayFrameInput {
    MusicOverlayContent content;
    MusicOverlayKey key;
};

MusicOverlayFrameInput BuildMusicOverlayFrameInput(const MusicOverlayContent& content, int scale,
                                                   uint64_t nowMs, int lcdWidthDots);

// Premultiplies every pixel by alpha (clamped to 0..1), the layout the mod API overlay layer
// blends (the presenter uses ONE / INV_SRC_ALPHA). Alpha 1 leaves the pixels as they are; a clear
// pixel stays fully transparent.
void PremultiplyMusicOverlayAlpha(MusicOverlayBitmap& bitmap, float alpha);

// One frame of the panel, as music_glue hands it to the mod API layer. The change counter grows
// only when a pixel differs, so the presenter uploads once per visible change and never for a
// frame that only redraws the same pixels.
struct MusicOverlayFrameState {
    MusicOverlayKey key;
    bool hasKey = false;
    uint8_t alphaByte = 0;
    // Straight-alpha pixels of the last rasterization. Kept so a fade step premultiplies the
    // source once instead of premultiplying the already-premultiplied bitmap again.
    MusicOverlayBitmap source;
    MusicOverlayBitmap bitmap;  // premultiplied, what the presenter uploads
    int x = 0;
    int y = 0;
    uint32_t changeCounter = 0;
};

// Rasterizes content at scale for the given game coordinates and fade alpha, and keeps state in
// step. Returns true when the pixels or the position changed since the last call.
bool UpdateMusicOverlayFrame(const MusicOverlayContent& content, int scale, int x, int y, uint64_t nowMs,
                             float alpha, MusicOverlayFrameState& state);

// S4: draws the panel into a 32-bit DIB the game presents. The game keeps its own DIB at 0x005ce864
// whose dwords are 0x00RRGGBB (GDI with BI_RGB ignores the high byte), while the panel pixels are
// premultiplied BGRA, so the blend keeps the high byte zero and never touches a pixel outside the DIB.
//
// S3b review R3-dib-orientation-unchecked: a DIB with a negative biHeight is top-down (row 0 is the
// top); a positive one is bottom-up (row 0 is the bottom), so the same visual row lands on a
// different memory row. The blend follows the header, never a fixed order.
enum class MusicDibOrientation {
    TopDown = 0,
    BottomUp = 1
};

// The frame plan for the engine's own DIB: bitCount must be 32, biWidth must match the engine width
// and |biHeight| the engine height. biHeight < 0 is top-down and > 0 bottom-up; 0 is invalid.
// Anything else is skipped with a once-only log line, because a frame the panel does not fit must not
// be written at a guessed offset.
struct MusicDibFramePlan {
    bool blend = false;
    MusicDibOrientation orientation = MusicDibOrientation::TopDown;
    // English line for the mod log; never shown to the player.
    const char* logMessage = "";
};

MusicDibFramePlan PlanMusicDibFrame(int32_t bitCount, int32_t biWidth, int32_t biHeight, int engineWidth,
                                    int engineHeight);

// Blends the panel into dib with the given orientation, clipping to the DIB edges.
void BlendMusicOverlayIntoDib(uint32_t* dib, int dibWidth, int dibHeight, MusicDibOrientation orientation,
                              const MusicOverlayBitmap& panel, int x, int y);

// S3b review R3-dib-blend-persistence: the game's DIB is never left modified. The present saves the
// pixels the panel covers, blends into the DIB for the original StretchDIBits call and writes the
// saved pixels back right after it. This is the cheaper of the two correct options (the other is a
// full-frame copy per present): the saved region is only the panel's rectangle, and every blit of
// the same frame blends again because the previous one restored the DIB, so hiding the panel leaves
// no trace.
struct MusicDibSavedRegion {
    int dibWidth = 0;
    int dibHeight = 0;
    MusicDibOrientation orientation = MusicDibOrientation::TopDown;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;  // row by row, x..x+width at y..y+height
};

// False when the panel does not overlap the DIB or has no pixels: nothing to save and nothing to blend.
bool SaveMusicDibRegion(const uint32_t* dib, int dibWidth, int dibHeight, MusicDibOrientation orientation,
                        const MusicOverlayBitmap& panel, int x, int y, MusicDibSavedRegion& out);
void RestoreMusicDibRegion(uint32_t* dib, const MusicDibSavedRegion& saved);

// S4 review R3-dib-sequence-guard-drops-reblit: one GDI StretchDIBits call is one beeper blit. The
// game's DIB is restored after every blit, so a repaint, a pause loop or a focus-loss loop that blits
// the same frame again must carry the panel again. The tracker therefore guards one blit only: a
// second call with the same frame sequence while the first blit is still running is refused, because
// blending it twice would double the panel's alpha.
struct MusicDibBlitTracker {
    bool active = false;          // a blit is inside the original StretchDIBits call
    uint32_t activeSequence = 0;  // the frame sample the running blit declared
};

bool BeginMusicDibBlit(MusicDibBlitTracker& tracker, uint32_t frameSequence);
// Ends only a blit that began: pass the value BeginMusicDibBlit returned. A refused reentrant call
// must not clear the outer blit's active state (S5 review R3-dib-blit-tracker-refused-call-clears-active).
void EndMusicDibBlit(MusicDibBlitTracker& tracker, bool began);

// The beeper next to a mod that renders the game itself (for example with Direct3D). The GDI
// StretchDIBits hook still installs, but the game's race present may never blit through GDI. When no
// race-frame blit reaches the hook within kMusicBeeperBlitTimeoutMs while race frames arrive, the
// beeper cannot be drawn next to that mod: this is logged once and nothing is faked.
inline constexpr uint64_t kMusicBeeperBlitTimeoutMs = 5000;

struct MusicBeeperBlitWatch {
    bool raceFrameSeen = false;
    uint64_t firstRaceFrameMs = 0;
    bool blitSeen = false;
    bool unavailableLogged = false;
};

enum class MusicBeeperBlitVerdict {
    NotWatched = 0,  // no GDI hook: the install path already logged that
    Waiting = 1,     // race frames arrive, no blit yet, still inside the timeout
    Drawing = 2,     // a race-frame blit reached the hook
    Unavailable = 3  // no race-frame blit within the timeout
};

struct MusicBeeperBlitDecision {
    MusicBeeperBlitVerdict verdict = MusicBeeperBlitVerdict::NotWatched;
    bool log = false;
};

// The GDI hook saw a blit of the game DIB during a race present.
void NoteMusicBeeperRaceBlit(MusicBeeperBlitWatch& watch);
// Called after every race present.
MusicBeeperBlitDecision OnMusicBeeperRaceFrame(MusicBeeperBlitWatch& watch, bool gdiHookInstalled,
                                               uint64_t nowMs);
