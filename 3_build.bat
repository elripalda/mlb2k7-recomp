@echo off
title MLB 2K7 Recomp - Build
cd /d "%~dp0"
call tools\env.bat || (pause & exit /b 1)
call tools\vsenv.bat || (pause & exit /b 1)
echo.
echo  Building MLB2K7Recomp.exe
echo  ---------------------------------
echo.

pushd "%PROJ%"
cmake -S . -B out\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23 "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="%SDK%" -Drexglue_DIR="%SDK%\lib\cmake\rexglue" > "%LOGS%\3_configure.log" 2>&1
if errorlevel 1 (popd & echo Configure failed - send me logs\3_configure.log & pause & exit /b 1)
rem Fix instructions the recompiler gets wrong (tools\patch_generated.py).
%PY% -I "%ROOT%\tools\patch_generated.py" generated\default
if errorlevel 3 goto patched
if errorlevel 1 (popd & echo Patch step failed - send me a screenshot of this window & pause & exit /b 1)
:patched
rem Remove the last exe first, so a failed build can never look like a good one.
for /r out\build %%f in (MLB2K7Recomp.exe) do if exist "%%f" del /q "%%f"
cmake --build out\build 2>&1 | powershell -NoProfile -Command "$input | Tee-Object -FilePath '%LOGS%\3_build.log'"
rem If the build regenerated the code, patch again and rebuild.
%PY% -I "%ROOT%\tools\patch_generated.py" generated\default >nul 2>&1
if errorlevel 3 cmake --build out\build 2>&1 | powershell -NoProfile -Command "$input | Tee-Object -FilePath '%LOGS%\3_build.log'"
set "EXE="
for /r out\build %%f in (MLB2K7Recomp.exe) do if exist "%%f" set "EXE=%%f"
if not defined EXE (popd & echo Build failed - send me logs\3_build.log & pause & exit /b 1)
for %%d in ("%EXE%") do set "EXEDIR=%%~dpd"
popd

if not exist "%OUT%" mkdir "%OUT%"
copy /y "%EXE%" "%OUT%\" >nul 2>&1
if errorlevel 1 (
  echo.
  echo  The game is still open, so the new version couldn't be copied in.
  echo  Close the game and run 3_build.bat again.
  echo.
  pause
  exit /b 1
)
for %%f in ("%EXEDIR%*.dll") do copy /y "%%f" "%OUT%\" >nul
for %%f in (rexruntime.dll rexgpu-xenos.dll TracyClient.dll) do copy /y "%SDK%\bin\%%f" "%OUT%\" >nul
if not exist "%OUT%\MLB2K7Recomp.toml" copy /y tools\MLB2K7Recomp.toml "%OUT%\" >nul

echo.
echo  Done! Double-click MLB2K7Recomp\MLB2K7Recomp.exe to play.
echo  Settings are in MLB2K7Recomp\MLB2K7Recomp.toml
echo.
pause
