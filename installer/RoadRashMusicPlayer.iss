; Road Rash Music Player - Inno Setup 6 installer.
; Build with installer\build.bat (builds dsound.dll first) or:
;   ISCC.exe [/DAppVersion=x.y.z] installer\RoadRashMusicPlayer.iss
; Silent install example:
;   RoadRashMusicPlayer-Setup-0.1.0.exe /VERYSILENT /CURRENTUSER /DIR="C:\...\RoadRash"
; It installs dsound.dll, adds rr95music.ini only when the game folder has none, and creates the
; music folder. A dsound.dll from another mod is kept as dsound.dll.bak-before-musicplayer and
; restored on uninstall. Uninstall never removes rr95music.ini, rr95music.log or the music folder.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define AppName "Road Rash Music Player"
#define AppURL "https://github.com/FFNNO/RoadRashMusicPlayer"

[Setup]
AppId={{E415CF3D-3D49-4060-9D06-DEB4DC3A45E1}
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
english.ForeignDsound=The game folder already has a dsound.dll from another mod:%n%n%1%n%nOnly one dsound.dll can be active. Setup keeps the existing file as dsound.dll.bak-before-musicplayer and restores it when you uninstall Road Rash Music Player.%n%nContinue?
spanish.ForeignDsound=La carpeta del juego ya tiene un dsound.dll de otro mod:%n%n%1%n%nSolo puede haber un dsound.dll activo. El instalador guarda el archivo existente como dsound.dll.bak-before-musicplayer y lo restaura al desinstalar Road Rash Music Player.%n%n¿Continuar?
english.ForeignDsoundCancelled=Setup was cancelled. The existing dsound.dll was not changed.
spanish.ForeignDsoundCancelled=Se canceló la instalación. El dsound.dll existente no se modificó.
english.BackupFailed=Could not back up %1. Close the game and try again.
spanish.BackupFailed=No se pudo hacer copia de seguridad de %1. Cierra el juego y vuelve a intentarlo.
english.LaunchGame=Launch Road Rash 95 (Steam)
spanish.LaunchGame=Iniciar Road Rash 95 (Steam)

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
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{E415CF3D-3D49-4060-9D06-DEB4DC3A45E1}_is1';

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

{ True when this AppId is already installed in Dir: the dsound.dll there is ours. }
function InstalledHereBefore(Dir: String): Boolean;
var
  S: String;
begin
  Result := False;
  if not RegQueryStringValue(HKCU, UninstallKey, 'Inno Setup: App Path', S) then
    if not RegQueryStringValue(HKLM32, UninstallKey, 'Inno Setup: App Path', S) then
      if not (IsWin64 and RegQueryStringValue(HKLM64, UninstallKey, 'Inno Setup: App Path', S)) then
        Exit;
  Result := CompareText(NormalizeDir(S), NormalizeDir(Dir)) = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App, Dll, Backup: String;
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

  Dll := AddBackslash(App) + DsoundName;
  Backup := Dll + BackupSuffix;
  { A dsound.dll that is not ours: warn, then keep it aside. An existing backup is never replaced. }
  if FileExists(Dll) and not InstalledHereBefore(App) and not FileExists(Backup) then
  begin
    Log('A dsound.dll from another mod is present: ' + Dll);
    if SuppressibleMsgBox(FmtMessage(CustomMessage('ForeignDsound'), [Dll]), mbConfirmation,
      MB_OKCANCEL, IDOK) <> IDOK then
    begin
      Result := CustomMessage('ForeignDsoundCancelled');
      Exit;
    end;
    if not FileCopy(Dll, Backup, True) then
    begin
      Result := FmtMessage(CustomMessage('BackupFailed'), [DsoundName]);
      Exit;
    end;
    Log(Format('Backup %s -> %s', [Dll, Backup]));
  end;
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

{ The uninstaller has removed our dsound.dll; put the other mod's file back. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dll, Backup: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Dll := ExpandConstant('{app}\') + DsoundName;
    Backup := Dll + BackupSuffix;
    if FileExists(Backup) and not FileExists(Dll) then
      Log(Format('Restore %s: %d', [Dll, Ord(RenameFile(Backup, Dll))]));
  end;
end;
