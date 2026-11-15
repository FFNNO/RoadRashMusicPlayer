#include "hook_chain.h"

#include <cstring>

namespace rr95 {

namespace {

uint32_t ReadU32(const uint8_t* p) {
    uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

bool InsideImage(uint32_t address, uint32_t imageBase, uint32_t imageSize) {
    // Unsigned distance: an address below the base wraps to a large value and is outside.
    return address - imageBase < imageSize;
}

}  // namespace

uint32_t JmpRel32Destination(uint32_t instructionAddress, int32_t rel32) {
    return instructionAddress + static_cast<uint32_t>(kJmpRel32Length) + static_cast<uint32_t>(rel32);
}

int32_t JmpRel32Displacement(uint32_t instructionAddress, uint32_t destination) {
    return static_cast<int32_t>(destination - (instructionAddress + static_cast<uint32_t>(kJmpRel32Length)));
}

void EncodeJmpRel32(uint32_t instructionAddress, uint32_t destination, uint8_t out[kJmpRel32Length]) {
    const uint32_t rel = static_cast<uint32_t>(JmpRel32Displacement(instructionAddress, destination));
    out[0] = 0xE9;
    std::memcpy(out + 1, &rel, sizeof(rel));
}

LeadingJump DecodeLeadingJump(const uint8_t* bytes, size_t length, uint32_t instructionAddress) {
    LeadingJump jump;
    if (bytes == nullptr) {
        return jump;
    }
    if (length >= kJmpRel32Length && bytes[0] == 0xE9) {
        jump.kind = LeadingJumpKind::Rel32;
        jump.destination = JmpRel32Destination(instructionAddress, static_cast<int32_t>(ReadU32(bytes + 1)));
        jump.length = kJmpRel32Length;
        return jump;
    }
    if (length >= kJmpIndirectLength && bytes[0] == 0xFF && bytes[1] == 0x25) {
        jump.kind = LeadingJumpKind::IndirectAbs32;
        jump.pointerAddress = ReadU32(bytes + 2);
        jump.length = kJmpIndirectLength;
        return jump;
    }
    return jump;
}

HookSiteClassification ClassifyHookSite(bool shippedMatches, const uint8_t* live, size_t liveLength,
                                        uint32_t siteAddress, uint32_t imageBase, uint32_t imageSize,
                                        const HookSiteMemory& memory) {
    HookSiteClassification result;
    if (shippedMatches) {
        result.state = HookSiteState::Shipped;
        result.reason = "the live bytes are the shipped bytes";
        return result;
    }
    const LeadingJump jump = DecodeLeadingJump(live, liveLength, siteAddress);
    result.jump = jump.kind;
    if (jump.kind == LeadingJumpKind::None) {
        result.reason = "the first instruction is not E9 rel32 or FF 25 [abs32]";
        return result;
    }
    uint32_t destination = jump.destination;
    if (jump.kind == LeadingJumpKind::IndirectAbs32) {
        if (memory.readPointer == nullptr || !memory.readPointer(jump.pointerAddress, destination, memory.ctx)) {
            result.reason = "the FF 25 pointer cannot be read";
            return result;
        }
    }
    result.destination = destination;
    if (InsideImage(destination, imageBase, imageSize)) {
        result.reason = "the jump destination is inside the game image";
        return result;
    }
    if (memory.isForeignExecutable == nullptr || !memory.isForeignExecutable(destination, memory.ctx)) {
        result.reason = "the jump destination is not executable memory of another module";
        return result;
    }
    result.state = HookSiteState::ForeignJmp;
    result.reason = "another mod's jump out of the game image";
    return result;
}

HookSiteInstallAction DecideHookSiteInstall(bool otherModsCompat, HookSiteState state) {
    switch (state) {
    case HookSiteState::Shipped:
        return HookSiteInstallAction::InstallNormal;
    case HookSiteState::ForeignJmp:
        return otherModsCompat ? HookSiteInstallAction::InstallChained : HookSiteInstallAction::Refuse;
    case HookSiteState::Unknown:
    default:
        return HookSiteInstallAction::Refuse;
    }
}

ChainedHookPlan PlanChainedHook(const uint8_t live[kJmpRel32Length], uint32_t siteAddress, uint32_t detour,
                                uint32_t trampolineAddress, uint32_t foreignDestination) {
    ChainedHookPlan plan;
    std::memcpy(plan.savedSite, live, kJmpRel32Length);
    EncodeJmpRel32(siteAddress, detour, plan.siteJump);
    EncodeJmpRel32(trampolineAddress, foreignDestination, plan.trampolineJump);
    return plan;
}

ChainedRestoreAction DecideChainedRestore(const uint8_t live[kJmpRel32Length],
                                          const uint8_t ours[kJmpRel32Length]) {
    return std::memcmp(live, ours, kJmpRel32Length) == 0 ? ChainedRestoreAction::Restore
                                                        : ChainedRestoreAction::LeaveAlone;
}

bool ShouldRunChainedSiteCheck(ChainedSiteCheckClock& clock, bool otherModsCompat, bool anyChainedSite,
                               uint64_t nowMs) {
    if (!otherModsCompat || !anyChainedSite) {
        return false;
    }
    if (clock.checked && nowMs - clock.lastCheckMs < kChainedSiteCheckIntervalMs) {
        return false;
    }
    clock.checked = true;
    clock.lastCheckMs = nowMs;
    return true;
}

ChainedSiteVerdict DecideChainedSiteHealth(ChainedSiteWatch& watch, bool siteHoldsOurJump) {
    ChainedSiteVerdict verdict;
    if (watch.lost) {
        // A lost site stays lost: it is never reinstalled and its state is never used again.
        verdict.keepUsing = false;
        verdict.log = false;
        return verdict;
    }
    if (siteHoldsOurJump) {
        verdict.keepUsing = true;
        verdict.log = false;
        return verdict;
    }
    watch.lost = true;
    verdict.keepUsing = false;
    verdict.log = true;
    return verdict;
}

const char* HookSiteStateToString(HookSiteState state) {
    switch (state) {
    case HookSiteState::Shipped:
        return "Shipped";
    case HookSiteState::ForeignJmp:
        return "ForeignJmp";
    case HookSiteState::Unknown:
        break;
    }
    return "Unknown";
}

}  // namespace rr95
