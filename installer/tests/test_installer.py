"""Silent install/uninstall tests for the Road Rash Music Player setup.

Run with:  python -I installer/tests/test_installer.py [path\\to\\RoadRashMusicPlayer-Setup-x.y.z.exe]

Without an argument the newest setup exe in installer/Output/ is used. Every case installs
into a temporary fake game folder under %TEMP% (never the real Steam folder), runs the
uninstaller, waits for it to finish and removes the temporary folders. The setup runs with
/CURRENTUSER, so Inno Setup registers its uninstall entry under HKCU for the duration of a case and
the uninstaller removes it again; the last check confirms that no entry is left behind.
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUTPUT_DIR = os.path.join(ROOT, "installer", "Output")
ISS = os.path.join(ROOT, "installer", "RoadRashMusicPlayer.iss")
BUILT_DLL = os.path.join(ROOT, "build", "Release", "dsound.dll")
TEMPLATE_INI = os.path.join(ROOT, "rr95music.ini")

DLL = "dsound.dll"
INI = "rr95music.ini"
LOG = "rr95music.log"
MUSIC = "music"
BACKUP_SUFFIX = ".bak-before-musicplayer"
APP_ID = "{E415CF3D-3D49-4060-9D06-DEB4DC3A45E1}"
UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall" + "\\" + APP_ID + "_is1"

failures = []


def check(cond, message):
    print(("PASS " if cond else "FAIL ") + message)
    if not cond:
        failures.append(message)


def find_setup():
    if len(sys.argv) > 1:
        return os.path.abspath(sys.argv[1])
    candidates = glob.glob(os.path.join(OUTPUT_DIR, "RoadRashMusicPlayer-Setup-*.exe"))
    if not candidates:
        sys.exit("No setup exe in %s; run installer\\build.bat first" % OUTPUT_DIR)
    return max(candidates, key=os.path.getmtime)


def make_game_dir(with_exe=True):
    path = tempfile.mkdtemp(prefix="rrmusic-game-")
    assert "steamapps" not in path.lower(), path
    if with_exe:
        open(os.path.join(path, "RoadRash95.exe"), "wb").close()
    return path


def run_setup(setup, game_dir, log_name, extra=None):
    log = os.path.join(tempfile.gettempdir(), log_name)
    args = [setup, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
            "/DIR=" + game_dir, "/LOG=" + log] + (extra or [])
    proc = subprocess.run(args, timeout=300)
    return proc.returncode, log


def uninstall(game_dir, timeout=120):
    """Run unins*.exe silently and wait until it has removed itself (it re-spawns a child)."""
    unins = sorted(glob.glob(os.path.join(game_dir, "unins*.exe")))
    if not unins:
        return False
    subprocess.run([unins[-1], "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=timeout)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not glob.glob(os.path.join(game_dir, "unins*.*")):
            time.sleep(0.5)  # let the post-uninstall restore settle
            return True
        time.sleep(0.25)
    return False


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def cleanup(*paths):
    for p in paths:
        shutil.rmtree(p, ignore_errors=True)


def uninstall_entry_exists():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY):
            return True
    except OSError:
        return False


def case_script():
    print("== (s) installer script")
    text = open(ISS, "r", encoding="utf-8").read()
    check(re.search(r'#ifndef AppVersion\s+#define AppVersion "0\.1\.0"\s+#endif', text) is not None,
          "(s) AppVersion defaults to 0.1.0 and can be overridden with /DAppVersion")
    check(re.search(r'Source: "\.\.\\rr95music\.ini";.*Flags: onlyifdoesntexist uninsneveruninstall', text)
          is not None, "(s) rr95music.ini is added only when missing and never uninstalled")
    check(re.search(r'Name: "\{app\}\\music"; Flags: uninsneveruninstall', text) is not None,
          "(s) the music folder is created and kept on uninstall")
    check("[UninstallDelete]" not in text, "(s) uninstall deletes nothing beyond the installed files")
    check(APP_ID[1:] in text, "(s) the AppId matches the one this test cleans up")


def case_clean_install(setup):
    print("== (a) clean install and uninstall")
    game = make_game_dir()
    try:
        code, _ = run_setup(setup, game, "rrmusic-a.log")
        check(code == 0, "(a) setup exit code 0 (got %s)" % code)
        dll = os.path.join(game, DLL)
        check(os.path.isfile(dll), "(a) dsound.dll installed")
        if os.path.isfile(BUILT_DLL) and os.path.isfile(dll):
            check(read_bytes(dll) == read_bytes(BUILT_DLL), "(a) installed dsound.dll is the built one")
        ini = os.path.join(game, INI)
        check(os.path.isfile(ini) and read_bytes(ini) == read_bytes(TEMPLATE_INI),
              "(a) rr95music.ini installed from the template")
        check(os.path.isdir(os.path.join(game, MUSIC)), "(a) music folder created")
        check(not os.path.exists(dll + BACKUP_SUFFIX), "(a) no backup without a previous dsound.dll")
        check(uninstall(game), "(a) uninstall finished")
        check(not os.path.exists(dll), "(a) dsound.dll removed")
        check(os.path.isfile(ini), "(a) rr95music.ini kept")
        check(os.path.isdir(os.path.join(game, MUSIC)), "(a) music folder kept")
        check(os.path.isfile(os.path.join(game, "RoadRash95.exe")), "(a) game exe kept")
    finally:
        cleanup(game)


def case_existing_ini(setup):
    print("== (b) the user's rr95music.ini is never overwritten")
    game = make_game_dir()
    user_ini = b"[Music]\r\nEnabled=1\r\nVolume=35\r\n; my settings\r\n"
    try:
        write_bytes(os.path.join(game, INI), user_ini)
        code, _ = run_setup(setup, game, "rrmusic-b.log")
        check(code == 0, "(b) setup exit code 0 (got %s)" % code)
        check(read_bytes(os.path.join(game, INI)) == user_ini, "(b) rr95music.ini unchanged by install")
        code, _ = run_setup(setup, game, "rrmusic-b2.log")
        check(code == 0, "(b) reinstall exit code 0 (got %s)" % code)
        check(read_bytes(os.path.join(game, INI)) == user_ini, "(b) rr95music.ini unchanged by reinstall")
        check(uninstall(game), "(b) uninstall finished")
        check(os.path.isfile(os.path.join(game, INI)) and read_bytes(os.path.join(game, INI)) == user_ini,
              "(b) rr95music.ini kept byte-identical on uninstall")
    finally:
        cleanup(game)


def case_foreign_dsound(setup):
    print("== (c) another mod's dsound.dll is backed up and restored")
    game = make_game_dir()
    other = b"MZ another mod's dsound wrapper\x00\x01\x02"
    try:
        dll = os.path.join(game, DLL)
        backup = dll + BACKUP_SUFFIX
        write_bytes(dll, other)
        code, log = run_setup(setup, game, "rrmusic-c.log")
        check(code == 0, "(c) setup exit code 0 (got %s)" % code)
        check(os.path.isfile(backup) and read_bytes(backup) == other,
              "(c) the other dsound.dll is kept as dsound.dll.bak-before-musicplayer")
        check(os.path.isfile(dll) and read_bytes(dll) != other, "(c) our dsound.dll installed")
        log_text = open(log, "r", encoding="utf-8", errors="replace").read() if os.path.isfile(log) else ""
        check("A dsound.dll from another mod is present" in log_text, "(c) the setup warned about it")
        # A reinstall over our own install must not replace the backup with our own DLL.
        code, _ = run_setup(setup, game, "rrmusic-c2.log")
        check(code == 0, "(c) reinstall exit code 0 (got %s)" % code)
        check(os.path.isfile(backup) and read_bytes(backup) == other, "(c) backup kept on reinstall")
        check(uninstall(game), "(c) uninstall finished")
        check(os.path.isfile(dll) and read_bytes(dll) == other, "(c) the other dsound.dll restored byte-identical")
        check(not os.path.exists(backup), "(c) backup consumed")
    finally:
        cleanup(game)


def case_uninstall_keeps_user_data(setup):
    print("== (d) uninstall keeps the player's files")
    game = make_game_dir()
    try:
        code, _ = run_setup(setup, game, "rrmusic-d.log")
        check(code == 0, "(d) setup exit code 0 (got %s)" % code)
        cassette = os.path.join(game, MUSIC, "My cassette")
        os.makedirs(cassette)
        write_bytes(os.path.join(cassette, "01 Track.ogg"), b"OggS dummy")
        write_bytes(os.path.join(game, MUSIC, "loose.mp3"), b"ID3 dummy")
        write_bytes(os.path.join(game, LOG), b"log written by the mod")
        write_bytes(os.path.join(game, "notes.txt"), b"unrelated")
        check(uninstall(game), "(d) uninstall finished")
        check(os.path.isfile(os.path.join(cassette, "01 Track.ogg")), "(d) cassette subfolder kept")
        check(os.path.isfile(os.path.join(game, MUSIC, "loose.mp3")), "(d) loose music file kept")
        check(os.path.isfile(os.path.join(game, LOG)), "(d) rr95music.log kept")
        check(os.path.isfile(os.path.join(game, INI)), "(d) rr95music.ini kept")
        check(os.path.isfile(os.path.join(game, "notes.txt")), "(d) unrelated file kept")
        check(not os.path.exists(os.path.join(game, DLL)), "(d) dsound.dll removed")
    finally:
        cleanup(game)


def case_no_game_exe(setup):
    print("== (e) folder without RoadRash95.exe")
    game = make_game_dir(with_exe=False)
    try:
        code, _ = run_setup(setup, game, "rrmusic-e.log")
        check(code != 0, "(e) setup fails with non-zero exit (got %s)" % code)
        check(not os.path.exists(os.path.join(game, DLL)), "(e) nothing installed")
        check(not os.path.exists(os.path.join(game, MUSIC)), "(e) no music folder created")
        if glob.glob(os.path.join(game, "unins*.exe")):
            uninstall(game)
    finally:
        cleanup(game)


def main():
    case_script()
    setup = find_setup()
    print("Setup: " + setup)
    check(not uninstall_entry_exists(), "no Road Rash Music Player uninstall entry before the cases")
    for case in (case_clean_install, case_existing_ini, case_foreign_dsound,
                 case_uninstall_keeps_user_data, case_no_game_exe):
        case(setup)
    check(not uninstall_entry_exists(), "no Road Rash Music Player uninstall entry left after the cases")
    if failures:
        print("FAILED (%d failure(s))" % len(failures))
        return 1
    print("OK (0 failure(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
