"""Verify the music player's hook sites against the original and the patched RoadRash95.exe.

The game executables are not part of this repository and are never published. Pass your own copies:

    python -I tools/check_hook_sites.py --original <original RoadRash95.exe> --patched <patched RoadRash95.exe>

or set RR95_ORIGINAL_EXE and RR95_PATCHED_EXE. Without them the script prints
"skipped: reference executables not provided" and exits 0, so CI can run it without game files.

With both executables it checks the music detours in src/music_glue.cpp against the shipped bytes of
each exe: the four player-only sites never overlap the general mod's hook-site table, and the
fallback sites (race present, menu present, item text) match the general table at the same address,
carry the capability that guards them, and keep the ShouldInstallFallbackSite /
ShouldInstallGdiOverlayHook guards in the install path. It checks the music call-target table (the
native MIDI pause FUN_004459c0) against the expected bytes of the original and the patched exe,
because that routine is called through a rebased pointer without a hook site.

It also checks the early Reproductor row registration: a chained IAT hook on the exe's own
DDRAW.dll!DirectDrawCreate import, so both exes must import that function by name into a read-only
slot, and src/dsound_proxy.cpp with src/early_hook.{h,cpp} must keep the import lookup, the detour,
the call into the original function, the conditional slot restore and the loader-lock safe [Music]
Enabled guard, with no module load, no export lookup and no log call in the code DllMain runs.

The import lookup here and FindImportSlot in src/early_hook.cpp follow one rule: every descriptor with
the DLL name is scanned in order and the first match wins; the name table is OriginalFirstThunk, or
FirstThunk when OriginalFirstThunk is 0; ordinal entries are skipped, and so is an entry that is not an
RVA inside the image (an entry the loader already bound to an address).

The general mod (Road Rash FFNNO Patch) owns its hook-site table, renderer/src/hook_sites.cpp in its
own repository. GENERAL_SITES below is a snapshot of that table, verified against both exes too.

Exit code 0 when every check passes, or when the executables are not provided.
"""
import argparse
import os
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MUSIC_TABLE = ROOT / "src" / "music_glue.cpp"
ENGINE_HEADER = ROOT / "src" / "engine.h"
EARLY_HOOK_SOURCE = ROOT / "src" / "dsound_proxy.cpp"
EARLY_HOOK_IMPL = ROOT / "src" / "early_hook.cpp"
EARLY_HOOK_HEADER = ROOT / "src" / "early_hook.h"

ORIGINAL_ENV = "RR95_ORIGINAL_EXE"
PATCHED_ENV = "RR95_PATCHED_EXE"

# Snapshot of the general mod's hook-site table (Road Rash FFNNO Patch, renderer/src/hook_sites.cpp,
# as of v0.1.4, the first release with RR95_GetModApi): name, VA, shipped bytes, patched span,
# instruction lengths.
GENERAL_SITES = [
    ("FUN_0046e630", 0x0046E630, bytes.fromhex("55 8b ec 81 ec 2c 04 00 00"), 9, [1, 2, 6]),
    ("FUN_0046ded0", 0x0046DED0, bytes.fromhex("55 8b ec 83 ec 24"), 6, [1, 2, 3]),
    ("FUN_00460b00", 0x00460B00, bytes.fromhex("55 8b ec 83 ec 24"), 6, [1, 2, 3]),
    ("FUN_004688e0", 0x004688E0, bytes.fromhex("55 8b ec 83 ec 7c"), 6, [1, 2, 3]),
    ("FUN_0042e850", 0x0042E850, bytes.fromhex("55 8b ec 83 ec 7c"), 6, [1, 2, 3]),
    ("FUN_00467cb0", 0x00467CB0, bytes.fromhex("55 8b ec 81 ec a8 00 00 00"), 9, [1, 2, 6]),
    ("FUN_00457ef0", 0x00457EF0, bytes.fromhex("a0 59 80 61 00"), 5, [5]),
    ("FUN_0045b930", 0x0045B930, bytes.fromhex("55 8b ec 83 ec 20"), 6, [1, 2, 3]),
    ("FUN_0045a790", 0x0045A790, bytes.fromhex("69 05 90 ec 59 00 24 02 00 00"), 10, [10]),
    ("FUN_0045a870", 0x0045A870, bytes.fromhex("69 05 90 ec 59 00 24 02 00 00"), 10, [10]),
    ("FUN_0043f580", 0x0043F580, bytes.fromhex("55 8b ec 83 ec 10"), 6, [1, 2, 3]),
    ("FUN_0043f7c0", 0x0043F7C0, bytes.fromhex("55 8b ec 83 ec 0c"), 6, [1, 2, 3]),
]

# The music mod keeps the VAs in engine:: constants. The fallback sites duplicate a general mod
# site: they are installed only when the binding decision says the music mod owns that capability
# (ShouldInstallFallbackSite), so both DLLs never patch the same bytes.
MUSIC_ENTRY = re.compile(
    r'(?P<var>g_site\w+)\{\s*"(?P<name>FUN_[0-9A-Fa-f]{8})",\s*engine::(?P<const>\w+),\s*'
    r'\{(?P<bytes>[^}]*)\},\s*(?P<len>\d+),\s*\{(?P<insn>[^}]*)\}'
)
# The four player-only sites: this mod always owns them, the general mod never touches them.
MUSIC_VAS = {0x00445BA0, 0x00411C00, 0x004463A0, 0x0043CEA0}

# The fallback table pairs every fallback site with the capability that guards it.
FALLBACK_ENTRY = re.compile(
    r'\{\s*&(?P<var>g_site\w+),\s*Capability::(?P<cap>RaceFrames|MenuFrames|GraphicsMenuRow|ItemText|OverlayLayer)\s*,'
)
FALLBACK_CAPABILITY_VA = {"RaceFrames": 0x0046E630, "MenuFrames": 0x0046DED0, "ItemText": 0x0045B930}
# Markers the install path must keep, so a fallback site can never be installed unguarded.
FALLBACK_GUARD = "ShouldInstallFallbackSite(decisions, fallback.capability)"
FALLBACK_INSTALL = "rr95::InstallHook(*fallback.site"
GDI_GUARD = "ShouldInstallGdiOverlayHook(decisions, api != nullptr)"
GDI_INSTALL = "rr95::InstallIatHook(g_iatDibBlend"

# The MIDI pause routine is not hooked: music_glue.cpp calls it through a rebased pointer after
# checking its bytes once. The struct holds the expected bytes for the original and the patched exe.
# The address checked is the engine constant the C++ code calls (engine::kMidiPause), never the
# address parsed out of the FUN_ name; the name must agree with it.
CALL_TARGET = re.compile(
    r'\{\s*"(?P<name>FUN_[0-9A-Fa-f]{8})",\s*engine::(?P<const>\w+),\s*\{(?P<orig>[^}]*)\},\s*'
    r'\{(?P<patched>[^}]*)\},\s*(?P<len>\d+),\s*(?P<abs>\d+)\s*\}'
)
MIDI_PAUSE_VA = 0x004459C0
MIDI_PAUSE_CONSTANT = "kMidiPause"
ENGINE_CONSTANT = re.compile(r'inline constexpr uint32_t (?P<name>\w+) = 0x(?P<va>[0-9A-Fa-f]+);')

# The early row registration is a chained IAT hook on the exe's own DDRAW.dll!DirectDrawCreate
# import. DllMain attaches and detaches it, so both run under the loader lock: they read only
# [Music] Enabled with plain file APIs and store one pointer.
EARLY_HOOK_IMPORT_DLL = "ddraw.dll"
EARLY_HOOK_IMPORT_FUNCTION = "DirectDrawCreate"
EARLY_HOOK_MARKERS = (
    (EARLY_HOOK_SOURCE, 'FindImportSlot(GetModuleHandleW(nullptr), "DDRAW.dll", "DirectDrawCreate")',
     "the import slot lookup on the host image"),
    (EARLY_HOOK_SOURCE, "DetourEarlyDirectDrawCreate", "the chained detour"),
    (EARLY_HOOK_SOURCE, "ReadMusicEnabledIni(iniPath.c_str(), true)", "the loader-lock safe music guard"),
    (EARLY_HOOK_SOURCE, "ForwardEarlyImportCall<DirectDrawCreateFn>(g_earlyHook, &RunEarlyRowRegistration",
     "the run-once registration in the detour"),
    (EARLY_HOOK_HEADER, "return previous(args...);", "the call into the original DirectDrawCreate"),
    (EARLY_HOOK_SOURCE, "RestoreEarlyImportHook(g_earlyHook, reinterpret_cast<void*>(&DetourEarlyDirectDrawCreate)",
     "the slot restore on detach"),
    (EARLY_HOOK_IMPL, "*hook.slot == detour", "the conditional slot restore"),
    (EARLY_HOOK_IMPL,
     "descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk",
     "the FirstThunk name table fallback this script uses"),
    (EARLY_HOOK_IMPL, "names[i].u1.AddressOfData >= imageSize", "the in-image name RVA bound"),
)
# The code DllMain runs: no other module may be loaded or called and no log opened.
EARLY_HOOK_SAFE_FUNCTIONS = (
    (EARLY_HOOK_SOURCE, "void MusicEarlyHookAttach"),
    (EARLY_HOOK_SOURCE, "void MusicEarlyHookDetach"),
    (EARLY_HOOK_IMPL, "void** FindImportSlot("),
    (EARLY_HOOK_IMPL, "bool StoreImportPointer("),
    (EARLY_HOOK_IMPL, "bool InstallEarlyImportHook("),
    (EARLY_HOOK_IMPL, "bool RestoreEarlyImportHook("),
)
EARLY_HOOK_FORBIDDEN = ("LoadLibrary", "GetProcAddress", "LOG_INFO", "LOG_WARN", "LOG_ERROR")
IMAGE_SCN_MEM_WRITE = 0x80000000


def read_c_string(pe, offset):
    end = pe.find(b"\x00", offset)
    return pe[offset:end if end >= 0 else len(pe)]


def find_section(pe, va):
    """(section header offset, offset of VA inside the section) of the section that holds VA.

    A section spans max(VirtualSize, SizeOfRawData) from its VirtualAddress. Raises ValueError when no
    section holds VA, so a caller never reads an answer for an address outside the image."""
    lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    nsec = struct.unpack_from("<H", pe, lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", pe, lfanew + 20)[0]
    image_base = struct.unpack_from("<I", pe, lfanew + 24 + 28)[0]
    rva = va - image_base
    sec = lfanew + 24 + opt_size
    for i in range(nsec):
        s = sec + 40 * i
        vsize, vaddr, rsize = struct.unpack_from("<III", pe, s + 8)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return s, rva - vaddr
    raise ValueError(f"VA 0x{va:08X} is not inside any section")


def va_to_offset(pe, va):
    s, delta = find_section(pe, va)
    return struct.unpack_from("<I", pe, s + 20)[0] + delta  # PointerToRawData


def section_characteristics(pe, va):
    s, _delta = find_section(pe, va)
    return struct.unpack_from("<I", pe, s + 36)[0]


def import_slots(pe, dll_name):
    """Function name -> IAT slot VA of one imported DLL, with the rule FindImportSlot uses.

    Like FindImportSlot, every descriptor with that DLL name is scanned, in order, and the first slot
    found for a name wins. Returns (slots, tables) where tables maps each name to the array that
    served as its name table."""
    lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    image_base = struct.unpack_from("<I", pe, lfanew + 24 + 28)[0]
    image_size = struct.unpack_from("<I", pe, lfanew + 24 + 56)[0]
    rva, size = struct.unpack_from("<II", pe, lfanew + 24 + 96 + 8)
    out = {}
    tables = {}
    if rva == 0 or size == 0:
        return out, tables
    p = va_to_offset(pe, image_base + rva)
    while True:
        original, _ts, _fwd, name_rva, first = struct.unpack_from("<IIIII", pe, p)
        p += 20
        if name_rva == 0:
            break
        module = read_c_string(pe, va_to_offset(pe, image_base + name_rva))
        if module.decode("ascii", "replace").lower() != dll_name:
            continue
        table = original if original else first
        if table == 0 or first == 0:
            continue
        label = "OriginalFirstThunk" if original else "FirstThunk"
        q = va_to_offset(pe, image_base + table)
        i = 0
        while True:
            value = struct.unpack_from("<I", pe, q + 4 * i)[0]
            if value == 0:
                break
            if not value >> 31 and value < image_size:
                name = read_c_string(pe, va_to_offset(pe, image_base + value + 2)).decode("ascii", "replace")
                if name not in out:
                    out[name] = image_base + first + 4 * i
                    tables[name] = label
            i += 1
    return out, tables


def function_body(text, start_marker):
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find("\n}\n", start)
    return text[start:end + 3] if end >= 0 else text[start:]


def check_early_hook_source(texts):
    failures = 0
    for path, marker, what in EARLY_HOOK_MARKERS:
        ok = marker in texts[path]
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} music early row hook keeps {what} ({path.name}): {marker}")
    for path, start_marker in EARLY_HOOK_SAFE_FUNCTIONS:
        body = function_body(texts[path], start_marker)
        ok = bool(body)
        failures += not ok
        if not ok:
            print(f"FAIL music early row hook has no {start_marker} body to check in {path.name}")
            continue
        for token in EARLY_HOOK_FORBIDDEN:
            bad = token in body
            failures += bad
            print(f"{'OK  ' if not bad else 'FAIL'} {start_marker.rstrip('(')} is loader-lock safe: no {token}")
    return failures


def check_early_hook_exe(label, pe):
    failures = 0
    slots, tables = import_slots(pe, EARLY_HOOK_IMPORT_DLL)
    va = slots.get(EARLY_HOOK_IMPORT_FUNCTION)
    ok = va is not None
    failures += not ok
    where = (f" at VA 0x{va:08X} (name table: {tables[EARLY_HOOK_IMPORT_FUNCTION]})" if ok
             else f" (imports {sorted(slots)[:6]})")
    print(f"{'OK  ' if ok else 'FAIL'} {label} imports DDRAW.dll!DirectDrawCreate by name{where}")
    if ok:
        try:
            writable = bool(section_characteristics(pe, va) & IMAGE_SCN_MEM_WRITE)
        except ValueError as error:
            print(f"FAIL {label} the DirectDrawCreate IAT slot has no section: {error}")
            return failures + 1
        failures += writable
        print(f"{'OK  ' if not writable else 'FAIL'} {label} the DirectDrawCreate IAT slot 0x{va:08X} is in a "
              f"read-only section, so the hook needs VirtualProtect")
    return failures


def parse_music_table(text):
    sites = []
    for m in MUSIC_ENTRY.finditer(text):
        data = bytes(int(b, 16) for b in m.group("bytes").replace(" ", "").split(",") if b)
        insn = [int(x) for x in m.group("insn").replace(" ", "").split(",") if x]
        sites.append((m.group("var"), f"FUN_{m.group('name')[4:].upper()}", int(m.group("name")[4:], 16),
                      data, int(m.group("len")), insn))
    return sites


def parse_fallback_table(text):
    return [(m.group("var"), m.group("cap")) for m in FALLBACK_ENTRY.finditer(text)]


def parse_engine_constants(text):
    return {m.group("name"): int(m.group("va"), 16) for m in ENGINE_CONSTANT.finditer(text)}


def parse_call_targets(text, constants):
    targets = []
    for m in CALL_TARGET.finditer(text):
        orig = bytes(int(b, 16) for b in m.group("orig").replace(" ", "").split(",") if b)
        patched = bytes(int(b, 16) for b in m.group("patched").replace(" ", "").split(",") if b)
        name_va = int(m.group("name")[4:], 16)
        constant = m.group("const")
        va = constants.get(constant, -1)
        targets.append((m.group("name").upper(), va, orig, patched, int(m.group("len")),
                        int(m.group("abs")), constant, name_va))
    return targets


def check_general_snapshot(label, pe):
    """The snapshot of the general table must still describe the shipped bytes of this exe."""
    failures = 0
    for name, va, expected, length, insn in GENERAL_SITES:
        off = va_to_offset(pe, va)
        actual = pe[off:off + length]
        ok = actual == expected[:length] and len(expected) == length and sum(insn) == length
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {label} general snapshot {name} VA 0x{va:08X} "
              f"file 0x{off:X} len {length} insn {insn} expected {expected.hex(' ')} actual {actual.hex(' ')}")
    return failures


def check_music_sites(label, pe, sites, general_vas, fallback_vas):
    """Shipped bytes of every music site; player-only sites never overlap the general table, fallback
    sites must be the same engine bytes the general table verifies."""
    failures = 0
    for _var, site_name, va, expected, length, insn in sites:
        off = va_to_offset(pe, va)
        actual = pe[off:off + length]
        ok = actual == expected[:length] and len(expected) == length and sum(insn) == length
        problem = ""
        if va in fallback_vas:
            if va not in general_vas:
                ok = False
                problem = " (a fallback site the general table does not verify)"
        elif va in general_vas:
            ok = False
            problem = " (overlaps the general table)"
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {label} music {site_name} VA 0x{va:08X} "
              f"file 0x{off:X} len {length} insn {insn} expected {expected.hex(' ')} actual {actual.hex(' ')}"
              f"{problem}")
    return failures


def check_music_table(sites, fallbacks):
    failures = 0
    fallback_vars = {var for var, _cap in fallbacks}
    found = {va for var, _name, va, _data, _len, _insn in sites if var not in fallback_vars}
    ok = found == MUSIC_VAS
    failures += not ok
    if not ok:
        print(f"FAIL music hook sites parsed {sorted(hex(v) for v in found)}, expected "
              f"{sorted(hex(v) for v in MUSIC_VAS)}")
    else:
        print(f"OK   music player-only hook sites parsed: {sorted(hex(v) for v in found)}")

    by_var = {var: (name, va, data, length, insn) for var, name, va, data, length, insn in sites}
    seen_caps = {}
    seen_vas = {}
    for var, capability in fallbacks:
        if var not in by_var:
            failures += 1
            print(f"FAIL music fallback {var} is not a hook site in the music table")
            continue
        _name, va, _data, _length, _insn = by_var[var]
        if capability not in FALLBACK_CAPABILITY_VA:
            failures += 1
            print(f"FAIL music fallback {var} uses capability {capability}, which has no fallback site")
            continue
        if va != FALLBACK_CAPABILITY_VA[capability]:
            failures += 1
            print(f"FAIL music fallback {var} guards engine::0x{va:08X} with {capability}, expected "
                  f"0x{FALLBACK_CAPABILITY_VA[capability]:08X}")
            continue
        seen_caps[capability] = var
        seen_vas[va] = var
    ok = set(seen_caps) == set(FALLBACK_CAPABILITY_VA) and len(seen_vas) == len(FALLBACK_CAPABILITY_VA)
    failures += not ok
    if ok:
        print(f"OK   music fallback sites parsed and guarded by capability: {sorted(seen_caps)}")
    else:
        print(f"FAIL music fallback sites parsed {sorted(seen_caps)}, expected {sorted(FALLBACK_CAPABILITY_VA)}")
    return failures


def check_fallback_against_general(sites, fallbacks, general_sites):
    """Every fallback site must be the same engine bytes the general mod verifies at that VA."""
    failures = 0
    general = {va: (name, data, length, insn) for name, va, data, length, insn in general_sites}
    by_var = {var: (name, va, data, length, insn) for var, name, va, data, length, insn in sites}
    for var, _capability in fallbacks:
        if var not in by_var:
            continue
        name, va, data, length, insn = by_var[var]
        if va not in general:
            failures += 1
            print(f"FAIL music fallback {name} 0x{va:08X} has no general mod site to compare with")
            continue
        g_name, g_data, g_length, g_insn = general[va]
        ok = g_data[:g_length] == data[:length] and g_length == length and g_insn == insn
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} music fallback {name} 0x{va:08X} matches the general site "
              f"{g_name} (bytes, span and instruction lengths)")
    return failures


def check_fallback_guard(text):
    """The install path must keep the binding guard, so no fallback site is ever installed unguarded."""
    failures = 0
    for marker, what in ((FALLBACK_GUARD, "the per-capability fallback guard"),
                         (FALLBACK_INSTALL, "the guarded fallback install call"),
                         (GDI_GUARD, "the GDI overlay binding guard"),
                         (GDI_INSTALL, "the GDI overlay install call")):
        ok = marker in text
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} music fallback install keeps {what}: {marker}")
    return failures


def check_music_call_target_table(targets):
    failures = 0
    found = {va for _, va, _, _, _, _, _, _ in targets}
    ok = found == {MIDI_PAUSE_VA}
    failures += not ok
    if not ok:
        print(f"FAIL music call targets parsed {sorted(hex(v) for v in found)}, expected "
              f"{[hex(MIDI_PAUSE_VA)]}")
    else:
        print(f"OK   music call targets parsed from engine constants: {sorted(hex(v) for v in found)}")
    for name, va, _, _, _, _, constant, name_va in targets:
        ok = constant == MIDI_PAUSE_CONSTANT and va == MIDI_PAUSE_VA and name_va == va
        failures += not ok
        if not ok:
            print(f"FAIL music call target {name} uses engine::{constant} = 0x{va:08X}, but the name says "
                  f"0x{name_va:08X}; expected engine::{MIDI_PAUSE_CONSTANT} = 0x{MIDI_PAUSE_VA:08X}")
        else:
            print(f"OK   music call target {name} checks the address of engine::{constant} (0x{va:08X})")
    return 1 if failures else 0


def check_music_call_targets(label, pe, targets, patched):
    """The native MIDI pause bytes, with the expected sequence for this exe variant."""
    failures = 0
    variant = "patched" if patched else "original"
    for site_name, va, orig, expected_patched, length, absoff, _constant, _name_va in targets:
        expected = expected_patched if patched else orig
        off = va_to_offset(pe, va)
        actual = pe[off:off + length]
        ok = len(expected) == length and actual == expected and absoff + 4 <= length
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {label} music call target {site_name} VA 0x{va:08X} "
              f"({variant} bytes) expected {expected.hex(' ')} actual {actual.hex(' ')}")
    return failures


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Check the music player's hook sites against your own RoadRash95.exe copies.")
    parser.add_argument("--original", help=f"the original RoadRash95.exe (or set {ORIGINAL_ENV})")
    parser.add_argument("--patched", help=f"the patched RoadRash95.exe (or set {PATCHED_ENV})")
    args = parser.parse_args(argv)
    original = args.original or os.environ.get(ORIGINAL_ENV) or ""
    patched = args.patched or os.environ.get(PATCHED_ENV) or ""
    return original, patched


def main(argv):
    original, patched = parse_args(argv)
    if not original and not patched:
        print("skipped: reference executables not provided "
              f"(pass --original and --patched, or set {ORIGINAL_ENV} and {PATCHED_ENV})")
        return 0
    if not original or not patched:
        print(f"FAIL both executables are required: --original ({ORIGINAL_ENV}) and --patched ({PATCHED_ENV})")
        return 2
    exes = [(pathlib.Path(original), False), (pathlib.Path(patched), True)]
    for exe, _patched in exes:
        if not exe.is_file():
            print(f"FAIL executable not found: {exe}")
            return 1

    music_text = MUSIC_TABLE.read_text(encoding="utf-8")
    music_sites = parse_music_table(music_text)
    fallbacks = parse_fallback_table(music_text)
    if not music_sites:
        print(f"FAIL no music hook sites parsed from {MUSIC_TABLE}")
        return 1
    if check_music_table(music_sites, fallbacks):
        return 1
    if check_fallback_against_general(music_sites, fallbacks, GENERAL_SITES):
        return 1
    if check_fallback_guard(music_text):
        return 1
    texts = {path: path.read_text(encoding="utf-8")
             for path in (EARLY_HOOK_SOURCE, EARLY_HOOK_IMPL, EARLY_HOOK_HEADER)}
    if check_early_hook_source(texts):
        return 1
    fallback_vars = {var for var, _cap in fallbacks}
    fallback_vas = {va for var, _name, va, _data, _len, _insn in music_sites if var in fallback_vars}
    constants = parse_engine_constants(ENGINE_HEADER.read_text(encoding="utf-8"))
    call_targets = parse_call_targets(music_text, constants)
    if check_music_call_target_table(call_targets):
        return 1
    general_vas = {va for _, va, _, _, _ in GENERAL_SITES}

    failures = 0
    for exe, is_patched in exes:
        label = f"{'patched' if is_patched else 'original'}/{exe.name}"
        pe = exe.read_bytes()
        failures += check_general_snapshot(label, pe)
        failures += check_music_sites(label, pe, music_sites, general_vas, fallback_vas)
        failures += check_music_call_targets(label, pe, call_targets, is_patched)
        failures += check_early_hook_exe(label, pe)
    print("OK   every music hook site matches both executables" if not failures
          else f"FAIL {failures} check(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
