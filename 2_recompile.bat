@echo off
title MLB 2K7 Recomp - Recompile
cd /d "%~dp0"
call tools\env.bat || (pause & exit /b 1)
echo.
echo  Recompiling the game code
echo  -------------------------
echo  This takes a while: it recompiles, scans for anything missed, and repeats.
echo.

if not exist "%GAME%\default.xex" (echo Could not find default.xex in: & echo   "%GAME%" & echo Edit the GAME line in tools\env.bat. & pause & exit /b 1)
if not exist "%PROJ%\input" mkdir "%PROJ%\input"
copy /y "%GAME%\default.xex" "%PROJ%\input\default.xex" >nul

if not exist "%PROJ%\MLB2K7_manifest.toml" (
  echo Creating the project...
  "%SDK%\bin\rexglue.exe" init --project-name MLB2K7 --xex-path "%PROJ%\input\default.xex" --project-root "%PROJ%" --log-file "%LOGS%\2_init.log" || (echo Project setup failed - see logs\2_init.log & pause & exit /b 1)
  %PY% -I tools\set_includes.py "%PROJ%\MLB2K7_manifest.toml" mlb2k7_functions.toml
  if not exist "%PROJ%\mlb2k7_functions.toml" (echo [functions]> "%PROJ%\mlb2k7_functions.toml")
)

%PY% -I tools\recompile_loop.py "%SDK%\bin\rexglue.exe" "%PROJ%" "%LOGS%" || (pause & exit /b 1)

echo.
echo  Done. Next: double-click 3_build.bat
echo.
pause
