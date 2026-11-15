# Road Rash Music Player

[![Download](https://img.shields.io/github/v/release/FFNNO/RoadRashMusicPlayer?label=Download&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/FFNNO/RoadRashMusicPlayer/release.yml?branch=main&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/actions/workflows/release.yml)

[Leer en español](README.es.md)

An in-game music player for **Road Rash 95** (Windows, the Steam release). Put your own music in a folder, and the game plays it in races and menus, with hotkeys, a `Reproductor` row in the game's options menu and an on-screen "beeper" panel that shows the cassette and the track.

It is a proxy `dsound.dll`: it sits next to `RoadRash95.exe`, forwards every DirectSound call to the real Windows `dsound.dll`, and adds the player. It does not modify any game file. Delete the DLL and the game is back to normal.

**This is a fan mod, not affiliated with or endorsed by Electronic Arts.** No game files are included or distributed. You need your own copy of the game.

## Contents

- [Requirements](#requirements)
- [Install](#install)
- [The music folder](#the-music-folder)
- [Hotkeys](#hotkeys)
- [The Reproductor row and popup](#the-reproductor-row-and-popup)
- [The beeper](#the-beeper)
- [Settings: rr95music.ini](#settings-rr95musicini)
- [Registry values](#registry-values)
- [With and without the general mod](#with-and-without-the-general-mod)
- [Uninstall](#uninstall)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Building from source](#building-from-source)
- [License](#license)

## Requirements

- Windows 10 or 11.
- **Road Rash 95 on Steam** (30th Anniversary Edition, app 2448570).
- Optional: the general mod **Road Rash FFNNO Patch** (<https://github.com/FFNNO/RoadRashFFNNOPatch>), a proxy `ddraw.dll` with a GPU renderer, widescreen and more. The music player works without it. **To run both together, the general mod must be v0.1.4 or later.**

## Install

### With the installer

1. Download `RoadRashMusicPlayer-Setup-<version>.exe` from the [latest release](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest). Every release is built by GitHub Actions from the tagged commit and lists the SHA-256 of each file.
2. Close the game and run the setup. It finds the game folder from the registry or your Steam libraries, and it only continues when `RoadRash95.exe` is in the chosen folder. If it cannot write there, run it as administrator.
3. It installs `dsound.dll`, adds `rr95music.ini` only when the folder has none (your settings are never overwritten), and creates the `music` folder.

If the game folder already has a `dsound.dll` from another mod, the setup warns you, keeps that file as `dsound.dll.bak-before-musicplayer`, and puts it back when you uninstall.

Silent install: `RoadRashMusicPlayer-Setup-0.1.0.exe /VERYSILENT /CURRENTUSER /DIR="<game folder>"`.

### Manually

1. Close the game.
2. Download `RoadRashMusicPlayer-<version>.zip` from the [latest release](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest) and copy `dsound.dll` next to `RoadRash95.exe` (usually `C:\Program Files (x86)\Steam\steamapps\common\RoadRash`).
3. Optional: copy `rr95music.ini` next to it and change what you need. Every key has a default, so the player works without the file.
4. Start the game. The `music` folder is created on the first start, and `rr95music.log` shows what loaded.

| File | Contents |
|---|---|
| `dsound.dll` | the music player (32-bit) |
| `rr95music.ini` | optional settings (see below) |
| `rr95music.log` | the player's log, written in the game folder |
| `music\` | your cassettes, created on startup when missing |

## The music folder

Each first-level subfolder of `music\` is one **cassette**, named after the folder; files in deeper folders belong to their first-level cassette. Loose audio files directly in `music\` form a cassette called `music`. The game's own soundtrack is the `Road Rash` cassette; it comes first and follows the game's jukebox choice.

Supported formats: `.wav`, `.mp3`, `.ogg` and `.flac`. The title and artist come from ID3v1/v2, Vorbis, FLAC and RIFF `INFO` tags.

```
RoadRash\
  RoadRash95.exe
  dsound.dll
  rr95music.ini
  music\
    Driving song.mp3          <- the "music" cassette
    Grunge\                   <- the "Grunge" cassette
      01 First track.flac
      02 Second track.ogg
```

`Path` in `rr95music.ini` changes the folder. `Shuffle` and `Volume` apply to every cassette.

## Hotkeys

| Action | Default key | ini key |
|---|---|---|
| Next track | `PageDown` | `NextKey` |
| Previous track | `PageUp` | `PrevKey` |
| Play / pause | `End` | `PlayPauseKey` |
| Next cassette | `Insert` | `NextCassetteKey` |
| Show or hide the beeper for this race | `Home` | `OverlayKey` |

The defaults are keys the game does not use. Change them in the Reproductor popup (see [Key capture](#key-capture)) or in `rr95music.ini` with the syntax `Ctrl+Shift+Alt+Key`; the modifiers are optional and may come in any order. A hotkey that equals the general mod's `ToggleKey` (from `rr95gpu.ini`, `ScrollLock` when that file is missing) or `F11` is disabled.

## The Reproductor row and popup

The game's graphics and sound options menu gets a `Reproductor` row. It opens a popup:

| Row | What it does |
|---|---|
| `Cassette` | Switches to the next cassette; the choice is kept for the next races. |
| `Beeper` | Switches between `Fijo` (pinned) and `A demanda` (on demand). |
| `Siguiente`, `Anterior`, `Pausa`, `Cambiar cassette`, `Mostrar beeper` | Show the current key of each action; select one to capture a new key. |
| `Volver` | Closes the popup. |

The in-game texts are in Spanish, like the game.

### Key capture

Select a key row, then press the new key. `Enter` or `Esc` cancels and keeps the old key. If another action already uses that key, the two actions swap keys. Reserved keys (`Enter`, `Esc`, `Tab`, the arrows, the general mod's toggle key and `F11`) and modifier keys alone are refused. The result is saved in the registry (`ModMusicKeys`) for the next start.

## The beeper

The beeper is the on-screen player panel: the cassette, the track and the artist, drawn over the game image in the corner you choose (`Corner`).

- `A demanda` / `Overlay=ondemand` (default): the panel appears for a few seconds after each track change.
- `Fijo` / `Overlay=pinned`: the panel stays on screen for the whole race.

The beeper hotkey (`Home`) shows or hides the panel for the current race only; the saved mode does not change. Long titles scroll.

## Settings: rr95music.ini

`rr95music.ini` sits next to `RoadRash95.exe`. The player never reads `rr95gpu.ini` settings except the general mod's `ToggleKey`, and never writes `rr95gpu.log`.

| Key | Default | What it does |
|---|---|---|
| `Enabled` | `1` | `1` = load the player. `0` = forward DirectSound calls only: no hooks, no mod API calls. |
| `LogLevel` | `info` | `debug`, `info`, `warn` or `error`; anything else uses `info`. |
| `Path` | `music` | The cassette folder, relative to the game folder or absolute. Created on startup when missing. |
| `Shuffle` | `0` | `1` = shuffle the tracks of every cassette; `0` = play them in name order. |
| `Volume` | `80` | `0..100`. Values outside the range are clamped; a value that is not a number keeps 80. |
| `NextKey` | `PageDown` | Next track. |
| `PrevKey` | `PageUp` | Previous track. |
| `PlayPauseKey` | `End` | Play or pause. |
| `NextCassetteKey` | `Insert` | Next cassette. |
| `OverlayKey` | `Home` | Show or hide the beeper for the current race. |
| `Overlay` | `ondemand` | Beeper mode: `ondemand` or `pinned`. |
| `Corner` | `bottomleft` | Panel corner: `bottomleft`, `topleft`, `bottomright` or `topright`. |

## Registry values

The player stores the choices you make in game under `HKCU\SOFTWARE\Electronic Arts\RoadRash 95`. A valid value wins over the matching ini key at startup.

| Value | Type | Contents |
|---|---|---|
| `ModMusicCassette` | `REG_SZ` | the selected cassette (`Road Rash` for the game's soundtrack) |
| `ModMusicKeys` | `REG_BINARY` | version 1 plus one virtual-key byte per action |
| `ModMusicOverlay` | `REG_BINARY` | one byte: `1` = pinned, `0` = on demand |

## With and without the general mod

**With Road Rash FFNNO Patch v0.1.4 or later**, the player connects to it through its mod API (`RR95_GetModApi`) instead of patching the game code the general mod owns. The general mod draws the beeper with its renderer and shows the `Reproductor` row in its relocated graphics menu, next to its own Widescreen and Far view rows. The player registers the row very early, before the general mod builds that menu.

**Without the general mod**, the player installs its own hooks on the same game routines, each one only after checking the game's original bytes. It writes the `Reproductor` row into the game's own graphics menu (an unused slot) and blends the beeper into the game image. Everything works the same way.

**With an older general mod** (before v0.1.4, without the mod API), both DLLs would want the same game routines. The player refuses to patch code that is already patched, so parts of it stay off. Update the general mod.

The player always patches four routines of its own (the native music pause and resume, the race music start and the menu input used by key capture), each one only after checking its original bytes.

The API contract between the two mods is [`include/rr95_mod_api.h`](include/rr95_mod_api.h). **That header is owned by the general mod** (Road Rash FFNNO Patch, `renderer/src/rr95_mod_api.h`) and is copied here byte for byte; change it there first.

## Uninstall

- **Installed with the setup:** uninstall "Road Rash Music Player" from Apps & features (Settings > Apps). It removes `dsound.dll` and puts back a `dsound.dll` from another mod that the setup kept aside.
- **Installed manually:** delete `dsound.dll` from the game folder.

Your `music` folder, `rr95music.ini`, `rr95music.log` and the registry values are kept either way; delete them yourself if you no longer want them. The general mod is never touched.

## Troubleshooting

- **Nothing happens in game:** open `rr95music.log` in the game folder. It says whether the player loaded, whether it found the general mod, which hooks it installed and which cassettes it read. A line about an unknown exe build means the game executable changed: the player then only forwards DirectSound calls. Set `LogLevel=debug` for more detail.
- **No music from my folder:** check `Path` and the file formats. Each first-level subfolder of `music\` is one cassette; files in deeper folders belong to their first-level cassette, and a cassette without supported files is skipped.
- **Another mod also uses `dsound.dll`:** only one file of that name can be in the game folder, so only one of the two mods can be active. The setup keeps the other file as `dsound.dll.bak-before-musicplayer`; uninstall the player to get it back, or swap the files by hand.
- **A hotkey does nothing:** it may equal the general mod's `ToggleKey` or `F11`, which disables it; the log says so. Pick another key.
- **Antivirus warning:** some antivirus tools may flag a proxy DLL because it patches the game's code in memory. It does not modify any game file.

## Known limitations

- **`ModMusicKeys` stores plain keys only.** A binding with `Ctrl`, `Shift` or `Alt` from the ini keeps its modifiers for the current session only; the next start reads the plain key from the registry.
- **It conflicts with any other mod that is also a `dsound.dll`.** Only one can be installed at a time.
- **It needs the Steam executable it knows.** On another exe build the player stays off and only forwards DirectSound calls.
- **This is a first public test release (v0.1.0).** Please report problems with your `rr95music.log`.

## Building from source

Requirements: Visual Studio 2026 (VS 18) Community with "Desktop development with C++" (MSVC x86, Windows SDK, CMake), Python 3, and [Inno Setup 6](https://jrsoftware.org/isinfo.php) for the installer.

```bat
build.bat
```

builds `build\Release\dsound.dll` and `build\Release\music_test.exe` (Win32, Release, static CRT) and ends with `build.bat: OK`. Then:

```bat
build\Release\music_test.exe
python -I tools\check_hook_sites.py --original <original RoadRash95.exe> --patched <patched RoadRash95.exe>
installer\build.bat
python -I installer\tests\test_installer.py
```

- `music_test.exe` runs the unit tests and ends with `OK (0 failure(s))`.
- `tools/check_hook_sites.py` checks every hook site against your own copies of the game executable (or set `RR95_ORIGINAL_EXE` and `RR95_PATCHED_EXE`). The executables are never part of this repository; without them the script reports `skipped` and exits 0.
- `installer\build.bat` builds the player and `installer\Output\RoadRashMusicPlayer-Setup-<version>.exe`. Set `APPVERSION` to override the version, or `ISCC` when Inno Setup is not found.
- `test_installer.py` installs into temporary fake game folders and uninstalls again.

| Path | Contents |
|---|---|
| `src/` | the player (C++17); `src/shared/` holds the hook engine, log and exe check shared with the general mod |
| `include/rr95_mod_api.h` | the mod API contract, owned by the general mod |
| `third_party/` | dr_libs and stb_vorbis, vendored unmodified |
| `tests/music_test.cpp` | unit tests |
| `tools/check_hook_sites.py` | hook-site checker |
| `installer/` | Inno Setup script, build script, tests and artwork |
| `rr95music.ini` | settings template |

## License

MIT, see [LICENSE](LICENSE). Third-party code: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Changes: see [CHANGELOG.md](CHANGELOG.md).
