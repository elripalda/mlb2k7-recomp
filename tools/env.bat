@echo off
rem Shared settings for the build scripts. Edit GAME if you move your copy.
set "SDK=%USERPROFILE%\Desktop\rexglue-sdk-0.10.1-win-amd64\win-amd64"
set "GAME=%USERPROFILE%\Desktop\Major League Baseball 2K7 (USA)"

set "ROOT=%~dp0.."
for %%i in ("%ROOT%") do set "ROOT=%%~fi"
set "PROJ=%ROOT%\project"
set "OUT=%ROOT%\MLB2K7Recomp"
set "LOGS=%ROOT%\logs"
rem A source-built SDK (BUILD_SDK.bat) wins over the downloaded one once it has built.
if exist "%ROOT%\sdk\BUILD_OK.txt" if exist "%ROOT%\sdk\install\bin\rexglue.exe" set "SDK=%ROOT%\sdk\install"
if not exist "%LOGS%" mkdir "%LOGS%"

set "PATH=C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
set PY=py -3
where py >nul 2>&1 || set PY=python

if defined NO_SDK_CHECK exit /b 0
if not exist "%SDK%\bin\rexglue.exe" (
  echo Could not find the ReXGlue SDK at:
  echo   "%SDK%"
  exit /b 1
)
exit /b 0
