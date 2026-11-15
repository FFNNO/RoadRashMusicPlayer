# Changelog

All notable changes to Road Rash Music Player are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

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
- Works alone or with Road Rash FFNNO Patch v0.1.4 or later through its mod API; the Reproductor row
  is registered early, before the general mod builds its menu.
- Inno Setup installer: finds the game folder, keeps the player's `rr95music.ini`, backs up and
  restores another mod's `dsound.dll`, and keeps the `music` folder, `rr95music.ini` and
  `rr95music.log` on uninstall.
- `tools/check_hook_sites.py`, which checks every hook site against your own game executables and
  reports skipped without them.

### Fixed (before release, from review)

- The early import lookup reads the `FirstThunk` names when `OriginalFirstThunk` is 0, the same rule
  as `tools/check_hook_sites.py`, and skips entries the loader already bound.
- The item text provider registered with the early Reproductor row is cleared when the general mod
  reports a menu it will not draw with its item-text detour (`NOT_USED`, `FAILED`, or `APPLIED`
  without the detour), before the in-place fallback takes over.
