# Changelog

All notable changes to Road Rash Music Player are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.1] - 2026-11-15

### Added

- Experimental `[Music] OtherModsCompat` in `rr95music.ini`, off by default (`0`). With `1`, a game
  routine that another mod already hooked with a jump into its own code is chained instead of left
  alone: the player's hook runs first, then the other mod's, which still reaches the original code.
  Any other change to a routine is still refused. This applies to the four player-only routines and
  to the race present, menu present and item text routines. Tested with Road Rash FFNNO Patch v0.1.3:
  music and hotkeys are expected to work; the beeper and the `Reproductor` row may be unavailable
  next to a GPU mod. On unload the other mod's bytes are put back only while the routine still holds
  the player's jump.
- In the experimental mode, every chained routine is checked at most once per second. A routine that
  another mod rewrote is logged once and no longer used; it is never reinstalled.
- Next to a mod that moves graphics menu 19 (its count is not 8 or the menu table no longer points at
  the shipped items), the in-place `Reproductor` row is skipped and logged once; music and hotkeys
  keep working. When no race frame reaches the GDI `StretchDIBits` hook within 5 seconds while races
  run (another mod draws the game, for example with Direct3D), the log says once that the beeper
  cannot be drawn next to that mod.
- The setup has an optional, unchecked task "Experimental: work alongside other mods" that sets
  `OtherModsCompat=1` only in an `rr95music.ini` that the same install created. An existing
  `rr95music.ini` is never changed.

### Changed

- `rr95music.ini` ships `OtherModsCompat=0`. The setup's default version and the `dsound.dll` version
  resource are 0.1.1.

### Fixed

- Documentation: no released version of Road Rash FFNNO Patch has a mod API. The claims about
  v0.1.4, about `RR95_GetModApi` as a way to connect, and about the general mod owning
  `include/rr95_mod_api.h` are removed. The player runs standalone by default, and the header is
  described as an internal, unused interface that the dormant binding code still compiles against.

## [0.1.0] - 2026-11-15

First public test release.

### Added

- Standalone proxy `dsound.dll` for Road Rash 95 (Steam): forwards every DirectSound export to the
  system DLL and plays your own music in races and menus.
- Cassettes from the `music` folder (one subfolder per cassette, loose files form the `music`
  cassette) with `.wav`, `.mp3`, `.ogg` and `.flac`, plus the game's own `Road Rash` cassette.
- Hotkeys for next and previous track, play/pause, next cassette and the beeper, configurable in
  `rr95music.ini` or in game.
- The `Reproductor` row in the graphics and sound options menu with its popup: cassette, beeper mode,
  key capture and `Volver`.
- The beeper panel, on demand or pinned, in any corner.
- Works standalone: the player installs its own hooks, each one only after checking the game's
  original bytes.
- Inno Setup installer: finds the game folder, keeps the player's `rr95music.ini`, backs up and
  restores another mod's `dsound.dll`, and keeps the `music` folder, `rr95music.ini` and
  `rr95music.log` on uninstall.
- `dsound.dll` carries a version resource (product name `Road Rash Music Player`).
- `tools/check_hook_sites.py`, which checks every hook site against your own game executables and
  reports skipped without them.

### Fixed (before release, from review)

- The early import lookup reads the `FirstThunk` names when `OriginalFirstThunk` is 0, the same rule
  as `tools/check_hook_sites.py`, and skips entries the loader already bound.
- Dormant mod API code (no released mod exports the API): the item text provider registered with the
  early Reproductor row is cleared when the general mod reports a menu it will not draw with its
  item-text detour (`NOT_USED`, `FAILED`, or `APPLIED` without the detour), before the in-place
  fallback takes over. The clear uses the same menu state as the binding decision; a refused clear
  keeps the provider registered, is retried at the next binding decision and is logged once.
- The setup tells its own `dsound.dll` from another mod's file by the version resource only. Any
  other `dsound.dll` is always backed up, also on a reinstall, as `dsound.dll.bak-before-musicplayer`
  or, when a backup of that name already exists, as that name plus the date and time, so no backup is
  ever overwritten. Uninstall restores only the backup that install made (recorded in
  `dsound.dll.musicplayer-backup`) and leaves any older backup in place.
- The release workflow builds and tests with read-only access; a separate tag-only job attests and
  publishes the same files, checks every `gh` exit code, and writes the version into the release
  notes.
- `tools/check_hook_sites.py` checks the import slot's section with the same section bounds as the
  address lookup, fails when no section holds it, and scans every `DDRAW.dll` import descriptor.
