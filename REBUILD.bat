@echo off
title MLB 2K7 Recomp - Rebuild
cd /d "%~dp0"
call tools\env.bat || (pause & exit /b 1)
echo.
echo  Rebuilding after a fix (recompile + build)
echo.
%PY% -I tools\recompile_loop.py "%SDK%\bin\rexglue.exe" "%PROJ%" "%LOGS%" || (pause & exit /b 1)
call 3_build.bat
