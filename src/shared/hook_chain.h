#pragma once
#include <cstddef>
#include <cstdint>

// Experimental "other mods" compatibility ([Music] OtherModsCompat=1).
//
// Normally a hook site is installed only when its live bytes equal the shipped bytes. Another mod
// that already detoured the same routine leaves a jump there, so the site is refused and that part
// of the player stays off. With OtherModsCompat=1 the player classifies the live bytes instead:
//
//   Shipped    the live bytes are the shipped bytes: install normally.
//   ForeignJmp the first instruction is E9 rel32 or FF 25 [abs32] and its destination lies outside
//              the game image, in executable memory (another loaded module or an executable
//              allocation): chain onto it.
//   Unknown    anything else: refuse and log, as without the setting.
//
// A chained hook writes our own E9 over the first 5 bytes of the site, and our trampoline is a
// single E9 to the other mod's destination, so its detour still runs after ours and its own
// trampoline still reaches the original code. On unload the other mod's 5 bytes are put back only
// while the site still holds our E9.
//
// Everything here is pure: addresses are 32-bit values and memory is reached only through the
// callbacks in HookSiteMemory, so music_test exercises every rule without touching real code.
namespace rr95 {

inline constexpr size_t kJmpRel32Length = 5;
inline constexpr size_t kJmpIndirectLength = 6;

enum class HookSiteState {
    Shipped = 0,
    ForeignJmp = 1,
    Unknown = 2
};

enum class LeadingJumpKind {
    None = 0,
    Rel32 = 1,          // E9 rel32
    IndirectAbs32 = 2   // FF 25 [abs32]
};

struct LeadingJump {
    LeadingJumpKind kind = LeadingJumpKind::None;
    uint32_t destination = 0;     // Rel32: the jump target
    uint32_t pointerAddress = 0;  // IndirectAbs32: the address of the 4-byte target pointer
    size_t length = 0;            // 5 for E9, 6 for FF 25
};

// E9 rel32 at instructionAddress: the target is the address of the next instruction plus rel32,
// computed modulo 2^32 like the CPU does.
uint32_t JmpRel32Destination(uint32_t instructionAddress, int32_t rel32);
// The rel32 an E9 at instructionAddress needs to reach destination (modulo 2^32: always reachable).
int32_t JmpRel32Displacement(uint32_t instructionAddress, uint32_t destination);
// Writes E9 and the little-endian rel32 into out[0..4].
void EncodeJmpRel32(uint32_t instructionAddress, uint32_t destination, uint8_t out[kJmpRel32Length]);
// Decodes the first instruction of bytes when it is E9 rel32 or FF 25 [abs32]; None otherwise or
// when length is too short for the instruction.
LeadingJump DecodeLeadingJump(const uint8_t* bytes, size_t length, uint32_t instructionAddress);

// The memory reads a classification needs, injected so the rule stays pure.
struct HookSiteMemory {
    // Reads the 4-byte pointer at address; false when it cannot be read.
    bool (*readPointer)(uint32_t address, uint32_t& value, void* ctx) = nullptr;
    // True when address is committed executable memory: another loaded module's code or an
    // executable allocation. The game image range is checked separately by ClassifyHookSite.
    bool (*isForeignExecutable)(uint32_t address, void* ctx) = nullptr;
    void* ctx = nullptr;
};

struct HookSiteClassification {
    HookSiteState state = HookSiteState::Unknown;
    LeadingJumpKind jump = LeadingJumpKind::None;
    uint32_t destination = 0;  // the other mod's detour, for ForeignJmp
    // English reason for the log; never shown to the player.
    const char* reason = "";
};

// shippedMatches: the live bytes equal the (rebased) shipped bytes. live holds liveLength bytes read
// at siteAddress. The game image spans [imageBase, imageBase + imageSize).
HookSiteClassification ClassifyHookSite(bool shippedMatches, const uint8_t* live, size_t liveLength,
                                        uint32_t siteAddress, uint32_t imageBase, uint32_t imageSize,
                                        const HookSiteMemory& memory);

// The per-site decision: shipped bytes always install normally, a foreign jump is chained only with
// OtherModsCompat=1, and everything else is refused. Never chains when the setting is 0.
enum class HookSiteInstallAction {
    InstallNormal = 0,
    InstallChained = 1,
    Refuse = 2
};

HookSiteInstallAction DecideHookSiteInstall(bool otherModsCompat, HookSiteState state);

// The bytes a chained install writes.
struct ChainedHookPlan {
    uint8_t savedSite[kJmpRel32Length] = {};       // the other mod's bytes, put back on unload
    uint8_t siteJump[kJmpRel32Length] = {};        // our E9 at the site, to our detour
    uint8_t trampolineJump[kJmpRel32Length] = {};  // our trampoline: E9 to the other mod's detour
};

ChainedHookPlan PlanChainedHook(const uint8_t live[kJmpRel32Length], uint32_t siteAddress, uint32_t detour,
                                uint32_t trampolineAddress, uint32_t foreignDestination);

// Unload: the other mod's bytes go back only while the site still holds our E9. When anything else
// is there (the other mod or a third one rewrote it), the site is left alone.
enum class ChainedRestoreAction {
    Restore = 0,
    LeaveAlone = 1
};

ChainedRestoreAction DecideChainedRestore(const uint8_t live[kJmpRel32Length],
                                          const uint8_t ours[kJmpRel32Length]);

// Re-hook monitoring: in the experimental mode, at most once per second, every chained site is
// checked for our E9. A site another mod rewrote is logged once and its state is no longer used; it
// is never reinstalled.
inline constexpr uint64_t kChainedSiteCheckIntervalMs = 1000;

struct ChainedSiteCheckClock {
    bool checked = false;
    uint64_t lastCheckMs = 0;
};

// True when a check is due now; records the check in clock. Never true with the setting at 0 or
// without a chained site that is still in use.
bool ShouldRunChainedSiteCheck(ChainedSiteCheckClock& clock, bool otherModsCompat, bool anyChainedSite,
                               uint64_t nowMs);

struct ChainedSiteWatch {
    bool lost = false;  // another mod rewrote the site: its state is no longer used
};

struct ChainedSiteVerdict {
    bool keepUsing = true;
    bool log = false;  // log once, the first time the site is found rewritten
};

ChainedSiteVerdict DecideChainedSiteHealth(ChainedSiteWatch& watch, bool siteHoldsOurJump);

const char* HookSiteStateToString(HookSiteState state);

}  // namespace rr95
