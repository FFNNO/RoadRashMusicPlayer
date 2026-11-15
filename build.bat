@echo off
rem Builds the Road Rash Music Player proxy dsound.dll and music_test.exe (Win32, Release, static CRT).
rem Output: build\Release\dsound.dll and build\Release\music_test.exe
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "SRC=%~dp0"
set "OUT=%~dp0build"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo build.bat: vcvars32.bat failed
  exit /b 1
)

"%CMAKE%" -S "%SRC%." -B "%OUT%" -G "Visual Studio 18 2026" -A Win32
if errorlevel 1 (
  echo build.bat: CMake configure failed
  exit /b 1
)

"%CMAKE%" --build "%OUT%" --config Release
if errorlevel 1 (
  echo build.bat: build failed
  exit /b 1
)

echo build.bat: OK
exit /b 0
