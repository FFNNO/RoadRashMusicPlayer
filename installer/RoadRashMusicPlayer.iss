; Road Rash Music Player - Inno Setup 6 installer.
; Build with installer\build.bat (builds dsound.dll first) or:
;   ISCC.exe [/DAppVersion=x.y.z] installer\RoadRashMusicPlayer.iss
; Silent install example:
;   RoadRashMusicPlayer-Setup-0.1.1.exe /VERYSILENT /CURRENTUSER /DIR="C:\...\RoadRash"
;   add /TASKS="othermodscompat" to opt in to the experimental other mods mode on a fresh ini.
; It installs dsound.dll, adds rr95music.ini only when the game folder has none, and creates the
; music folder. The optional, unchecked task "othermodscompat" sets OtherModsCompat=1 (experimental)
; only in an rr95music.ini this install created; an existing ini is never changed. A dsound.dll that
; is not ours (its version resource does not name this product) is
; always kept as dsound.dll.bak-before-musicplayer, or under that name plus the date and time when a
; backup of that name already exists, so no backup is ever replaced. The name of the backup this
; install made is recorded in dsound.dll.musicplayer-backup, and uninstall restores only that one.
; Uninstall never removes rr95music.ini, rr95music.log or the music folder.

#ifndef AppVersion
  #define AppVersion "0.1.1"
#endif
#define AppName "Road Rash Music Player"
#define AppURL "https://github.com/FFNNO/RoadRashMusicPlayer"
; The AppId GUID, written only here. installer/tests/test_installer.py reads it from this line.
#define AppGuid "E415CF3D-3D49-4060-9D06-DEB4DC3A45E1"
; "{{" is Inno Setup's escaped "{": the AppId is the GUID in braces.
#define AppId "{{" + AppGuid + "}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=FFNNO
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={code:GetDefaultGameDir}
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
OutputDir=Output
OutputBaseFilename=RoadRashMusicPlayer-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Original artwork (no game art), shared with the Road Rash FFNNO Patch installer by the same author.
WizardImageFile=assets\wizard-large-164x314.bmp,assets\wizard-large-328x628.bmp
WizardSmallImageFile=assets\wizard-small-55x55.bmp,assets\wizard-small-110x110.bmp
SetupIconFile=assets\setup.ico
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Messages]
english.FinishedLabelNoIcons=Setup has finished installing [name].%n%nPut your music in the music folder of the game: one subfolder is one cassette, and loose files form the "music" cassette (.wav, .mp3, .ogg, .flac). Settings are in rr95music.ini and the log is rr95music.log.%n%nTo uninstall, use Apps & features (Settings > Apps). Your music folder, rr95music.ini and rr95music.log are kept.
english.FinishedLabel=Setup has finished installing [name].%n%nPut your music in the music folder of the game: one subfolder is one cassette, and loose files form the "music" cassette (.wav, .mp3, .ogg, .flac). Settings are in rr95music.ini and the log is rr95music.log.%n%nTo uninstall, use Apps & features (Settings > Apps). Your music folder, rr95music.ini and rr95music.log are kept.
spanish.FinishedLabelNoIcons=El programa terminó de instalar [name].%n%nColoca tu música en la carpeta music del juego: cada subcarpeta es un casete y los archivos sueltos forman el casete "music" (.wav, .mp3, .ogg, .flac). La configuración está en rr95music.ini y el registro en rr95music.log.%n%nPara desinstalar, usa Aplicaciones y características (Configuración > Aplicaciones). Tu carpeta music, rr95music.ini y rr95music.log se conservan.
spanish.FinishedLabel=El programa terminó de instalar [name].%n%nColoca tu música en la carpeta music del juego: cada subcarpeta es un casete y los archivos sueltos forman el casete "music" (.wav, .mp3, .ogg, .flac). La configuración está en rr95music.ini y el registro en rr95music.log.%n%nPara desinstalar, usa Aplicaciones y características (Configuración > Aplicaciones). Tu carpeta music, rr95music.ini y rr95music.log se conservan.

[CustomMessages]
english.NoGameExe=RoadRash95.exe was not found in:%n%n%1%n%nChoose the Road Rash 95 game folder (the one that contains RoadRash95.exe).
spanish.NoGameExe=No se encontró RoadRash95.exe en:%n%n%1%n%nElige la carpeta del juego Road Rash 95 (la que contiene RoadRash95.exe).
english.NotWritable=Setup cannot write to:%n%n%1%n%nRun the installer as administrator (right-click > Run as administrator) or choose another folder.
spanish.NotWritable=El instalador no puede escribir en:%n%n%1%n%nEjecuta el instalador como administrador (clic derecho > Ejecutar como administrador) o elige otra carpeta.
english.ForeignDsound=The game folder already has a dsound.dll from another mod:%n%n%1%n%nOnly one dsound.dll can be active. Setup keeps the existing file as %2 and restores it when you uninstall Road Rash Music Player.%n%nContinue?
spanish.ForeignDsound=La carpeta del juego ya tiene un dsound.dll de otro mod:%n%n%1%n%nSolo puede haber un dsound.dll activo. El instalador guarda el archivo existente como %2 y lo restaura al desinstalar Road Rash Music Player.%n%n¿Continuar?
english.ForeignDsoundCancelled=Setup was cancelled. The existing dsound.dll was not changed.
spanish.ForeignDsoundCancelled=Se canceló la instalación. El dsound.dll existente no se modificó.
english.BackupFailed=Could not back up %1. Close the game and try again.
spanish.BackupFailed=No se pudo hacer copia de seguridad de %1. Cierra el juego y vuelve a intentarlo.
english.LaunchGame=Launch Road Rash 95 (Steam)
english.OtherModsTask=Experimental: work alongside other mods (OtherModsCompat=1, only for a new rr95music.ini)
spanish.LaunchGame=Iniciar Road Rash 95 (Steam)
spanish.OtherModsTask=Experimental: funcionar junto a otros mods (OtherModsCompat=1, solo para un rr95music.ini nuevo)

[Tasks]
; Experimental and off by default. It only changes an rr95music.ini this install created.
Name: "othermodscompat"; Description: "{cm:OtherModsTask}"; Flags: unchecked

[Files]
Source: "..\build\Release\dsound.dll"; DestDir: "{app}"; Flags: ignoreversion
; The settings belong to the player: added only when missing, never overwritten, never uninstalled.
Source: "..\rr95music.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Dirs]
; The player's cassettes: created when missing, kept on uninstall.
Name: "{app}\music"; Flags: uninsneveruninstall

[Run]
Filename: "steam://rungameid/2448570"; Description: "{cm:LaunchGame}"; \
  Flags: shellexec postinstall unchecked skipifsilent nowait

[Code]
const
  GameExe = 'RoadRash95.exe';
  DsoundName = 'dsound.dll';
  BackupSuffix = '.bak-before-musicplayer';
  { The file name of the backup this install made, kept next to it in the game folder. Uninstall
    restores only that backup, so a stale backup or another program's file is never touched. }
  BackupStateName = 'dsound.dll.musicplayer-backup';
  { Our dsound.dll names this product in its version resource (src/dsound.rc). }
  OurProductName = 'Road Rash Music Player';
  IniName = 'rr95music.ini';
  OtherModsTask = 'othermodscompat';

var
  { True when the game folder had no rr95music.ini before this install. }
  IniWasMissing: Boolean;

{ ---------- helpers ---------- }

function NormalizeDir(S: String): String;
begin
  S := Trim(S);
  StringChangeEx(S, '"', '', True);
  StringChangeEx(S, '/', '\', True);
  while (Length(S) > 3) and (S[Length(S)] = '\') do
    Delete(S, Length(S), 1);
  Result := S;
end;

function IsGameDir(Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(NormalizeDir(Dir)) + GameExe);
end;

function IsDirWritable(Dir: String): Boolean;
var
  F: String;
begin
  F := AddBackslash(NormalizeDir(Dir)) + 'rrmusic-write-test.tmp';
  Result := SaveStringToFile(F, 'x', False);
  if Result then
    DeleteFile(F);
end;

{ ---------- game folder detection (same order as the Road Rash FFNNO Patch installer) ---------- }

{ Value of a libraryfolders.vdf line such as:  "path"   "D:\\SteamLibrary" }
function VdfPathValue(Line: String): String;
var
  P: Integer;
begin
  Result := '';
  Line := Trim(Line);
  if Pos('"path"', Lowercase(Line)) <> 1 then
    Exit;
  Line := Copy(Line, 7, Length(Line));
  P := Pos('"', Line);
  if P = 0 then
    Exit;
  Line := Copy(Line, P + 1, Length(Line));
  P := Pos('"', Line);
  if P = 0 then
    Exit;
  Result := Copy(Line, 1, P - 1);
  StringChangeEx(Result, '\\', '\', True);
end;

function FindInSteam(SteamDir: String; var Found: String): Boolean;
var
  Lines: TArrayOfString;
  I: Integer;
  Lib, Cand: String;
begin
  Result := False;
  SteamDir := NormalizeDir(SteamDir);
  if SteamDir = '' then
    Exit;
  Cand := SteamDir + '\steamapps\common\RoadRash';
  if IsGameDir(Cand) then
  begin
    Found := Cand;
    Result := True;
    Exit;
  end;
  if LoadStringsFromFile(SteamDir + '\steamapps\libraryfolders.vdf', Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
    begin
      Lib := VdfPathValue(Lines[I]);
      if Lib <> '' then
      begin
        Cand := NormalizeDir(Lib) + '\steamapps\common\RoadRash';
        if IsGameDir(Cand) then
        begin
          Found := Cand;
          Result := True;
          Exit;
        end;
      end;
    end;
end;

{ 1) EA registry Path  2) Steam install + libraries  3) default Steam folder }
function GetDefaultGameDir(Param: String): String;
var
  S: String;
begin
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Electronic Arts\RoadRash 95', 'Path', S) and IsGameDir(S) then
  begin
    Result := NormalizeDir(S);
    Exit;
  end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Valve\Steam', 'InstallPath', S) and FindInSteam(S, Result) then
    Exit;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', S) and FindInSteam(S, Result) then
    Exit;
  Result := ExpandConstant('{commonpf32}\Steam\steamapps\common\RoadRash');
end;

{ ---------- existing dsound.dll ---------- }

{ ASCII text as the UTF-16LE bytes a version resource stores. }
function AsciiToUtf16(S: String): AnsiString;
var
  I: Integer;
begin
  Result := '';
  for I := 1 to Length(S) do
    Result := Result + S[I] + #0;
end;

{ True only when the file's version resource names this product, whoever put the file there. The
  ProductName entry stores the UTF-16 key, its terminator and the 32-bit alignment padding (four
  zero bytes together), then the value and its terminator. }
function IsOurDsound(FileName: String): Boolean;
var
  Data: AnsiString;
begin
  Result := LoadStringFromFile(FileName, Data) and
    (Pos(AsciiToUtf16('ProductName') + #0#0#0#0 + AsciiToUtf16(OurProductName) + #0#0, Data) > 0);
end;

{ A backup file name no file in App uses yet: dsound.dll.bak-before-musicplayer, else that name
  plus the date and time, else plus a counter as well. An existing backup is never replaced. }
function UniqueBackupName(App: String): String;
var
  Stamped: String;
  N: Integer;
begin
  Result := DsoundName + BackupSuffix;
  if not FileExists(AddBackslash(App) + Result) then
    Exit;
  Stamped := Result + '-' + GetDateTimeString('yyyymmdd-hhnnss', '-', ':');
  Result := Stamped;
  N := 2;
  while FileExists(AddBackslash(App) + Result) do
  begin
    Result := Stamped + '-' + IntToStr(N);
    N := N + 1;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App, Dll, BackupName: String;
begin
  Result := '';
  App := NormalizeDir(WizardDirValue);
  if not IsGameDir(App) then
  begin
    Result := FmtMessage(CustomMessage('NoGameExe'), [App]);
    Exit;
  end;
  if not IsDirWritable(App) then
  begin
    Result := FmtMessage(CustomMessage('NotWritable'), [App]);
    Exit;
  end;

  IniWasMissing := not FileExists(AddBackslash(App) + IniName);

  Dll := AddBackslash(App) + DsoundName;
  { Decided by content only, never by the uninstall entry or an existing backup: a dsound.dll whose
    version resource does not name this product is another program's file and is always kept. }
  if FileExists(Dll) and not IsOurDsound(Dll) then
  begin
    Log('A dsound.dll from another mod is present: ' + Dll);
    BackupName := UniqueBackupName(App);
    if SuppressibleMsgBox(FmtMessage(CustomMessage('ForeignDsound'), [Dll, BackupName]), mbConfirmation,
      MB_OKCANCEL, IDOK) <> IDOK then
    begin
      Result := CustomMessage('ForeignDsoundCancelled');
      Exit;
    end;
    if not FileCopy(Dll, AddBackslash(App) + BackupName, True) then
    begin
      Result := FmtMessage(CustomMessage('BackupFailed'), [DsoundName]);
      Exit;
    end;
    if not SaveStringToFile(AddBackslash(App) + BackupStateName, BackupName, False) then
    begin
      Result := FmtMessage(CustomMessage('BackupFailed'), [DsoundName]);
      Exit;
    end;
    Log(Format('Backup %s -> %s', [Dll, BackupName]));
  end;
end;

{ The experimental task turns OtherModsCompat on only in the template this install just added:
  the one line "OtherModsCompat=0" becomes "OtherModsCompat=1" and nothing else changes. A player's
  own rr95music.ini is never touched. }
procedure EnableOtherModsCompat(Ini: String);
var
  Data: AnsiString;
  Text: String;
begin
  if not LoadStringFromFile(Ini, Data) then
  begin
    Log('Cannot read ' + Ini + '; OtherModsCompat is left at 0');
    Exit;
  end;
  Text := String(Data);
  if StringChangeEx(Text, #10 + 'OtherModsCompat=0', #10 + 'OtherModsCompat=1', True) <> 1 then
  begin
    Log('No single OtherModsCompat=0 line in ' + Ini + '; it is left unchanged');
    Exit;
  end;
  if SaveStringToFile(Ini, AnsiString(Text), False) then
    Log('Experimental OtherModsCompat=1 written to ' + Ini)
  else
    Log('Cannot write ' + Ini + '; OtherModsCompat is left at 0');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and IniWasMissing and WizardIsTaskSelected(OtherModsTask) then
    EnableOtherModsCompat(AddBackslash(NormalizeDir(WizardDirValue)) + IniName);
end;

{ Interactive checks; PrepareToInstall repeats them so silent installs fail too. }
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if WizardSilent then
    Exit;
  if CurPageID = wpSelectDir then
  begin
    if not IsGameDir(WizardDirValue) then
    begin
      MsgBox(FmtMessage(CustomMessage('NoGameExe'), [WizardDirValue]), mbError, MB_OK);
      Result := False;
    end
    else if not IsDirWritable(WizardDirValue) then
    begin
      MsgBox(FmtMessage(CustomMessage('NotWritable'), [WizardDirValue]), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

{ ---------- uninstall ---------- }

{ True for a backup name this installer writes: a plain file name, never a path. }
function IsBackupName(Name: String): Boolean;
begin
  Result := (Pos(Lowercase(DsoundName + BackupSuffix), Lowercase(Name)) = 1) and
    (Pos('\', Name) = 0) and (Pos('/', Name) = 0) and (Pos(':', Name) = 0) and (Pos('..', Name) = 0);
end;

{ The uninstaller has removed our dsound.dll; put back the file the recorded backup holds. Only the
  backup this install made is restored; any other backup stays where it is. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App, Dll, State, Name: String;
  Recorded: AnsiString;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  App := ExpandConstant('{app}');
  Dll := AddBackslash(App) + DsoundName;
  State := AddBackslash(App) + BackupStateName;
  if not LoadStringFromFile(State, Recorded) then
    Exit;
  Name := Trim(String(Recorded));
  if not IsBackupName(Name) then
    Log('Ignoring an unexpected backup record: ' + Name)
  else if not FileExists(AddBackslash(App) + Name) then
    Log('The recorded backup is gone: ' + Name)
  else if FileExists(Dll) then
    Log(Format('%s exists; the backup %s is kept', [Dll, Name]))
  else
    Log(Format('Restore %s from %s: %d', [Dll, Name, Ord(RenameFile(AddBackslash(App) + Name, Dll))]));
  DeleteFile(State);
end;
