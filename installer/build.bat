@echo off
rem Builds build\Release\dsound.dll and the installer
rem installer\Output\RoadRashMusicPlayer-Setup-<version>.exe.
rem ISCC lookup: %ISCC% (override), then %LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe,
rem then %ProgramFiles(x86)%\Inno Setup 6\ISCC.exe.
rem Optional: set APPVERSION=x.y.z to override the default AppVersion (0.1.1).
setlocal

for %%I in ("%~dp0..") do set "ROOT=%%~fI"

call "%ROOT%\build.bat"
if errorlevel 1 (
  echo installer\build.bat: player build failed
  exit /b 1
)
if not exist "%ROOT%\build\Release\dsound.dll" (
  echo installer\build.bat: dsound.dll not found after the build
  exit /b 1
)

if defined ISCC goto have_iscc
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto have_iscc
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto have_iscc
echo installer\build.bat: ISCC.exe not found; install Inno Setup 6 or set ISCC
exit /b 1

:have_iscc
if not exist "%ISCC%" (
  echo installer\build.bat: ISCC not found at "%ISCC%"
  exit /b 1
)

set "VERSIONARG="
if defined APPVERSION set "VERSIONARG=/DAppVersion=%APPVERSION%"

"%ISCC%" /Q %VERSIONARG% "%ROOT%\installer\RoadRashMusicPlayer.iss"
if errorlevel 1 (
  echo installer\build.bat: ISCC failed
  exit /b 1
)

set "SETUP="
for /f "delims=" %%F in ('dir /b /o:d "%ROOT%\installer\Output\RoadRashMusicPlayer-Setup-*.exe" 2^>nul') do set "SETUP=%ROOT%\installer\Output\%%F"
if not defined SETUP (
  echo installer\build.bat: setup exe not found in installer\Output
  exit /b 1
)
echo installer\build.bat: OK %SETUP%
exit /b 0
