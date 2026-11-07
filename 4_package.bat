@echo off
title MLB 2K7 Recomp - Package release
cd /d "%~dp0"
call tools\env.bat || (pause & exit /b 1)
set "VERSION=1.0.0"
set /p VERSION=Version number [1.0.0]: 
set "NAME=MLB2K7Recomp-v%VERSION%-Windows-x64"
set "STAGE=%ROOT%\release\%NAME%"

if not exist "%OUT%\MLB2K7Recomp.exe" (echo Build it first with 3_build.bat & pause & exit /b 1)
rmdir /s /q "%STAGE%" 2>nul
mkdir "%STAGE%"
rem Just the program: no game files, no settings file (the exe and RecompLauncher make their own).
copy /y "%OUT%\MLB2K7Recomp.exe" "%STAGE%\" >nul
for %%f in ("%OUT%\*.dll") do copy /y "%%f" "%STAGE%\" >nul
copy /y "%PROJ%\recomplauncher.json" "%STAGE%\" >nul
copy /y "%ROOT%\README.md" "%STAGE%\" >nul 2>nul
copy /y "%ROOT%\LICENSE" "%STAGE%\" >nul 2>nul

del /q "%ROOT%\release\%NAME%.zip" 2>nul
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ROOT%\release\%NAME%.zip'"

rem Source zip: everything needed to build, nothing from the game.
set "SRC=%ROOT%\release\MLB2K7Recomp-v%VERSION%-source"
rmdir /s /q "%SRC%" 2>nul
mkdir "%SRC%\project\generated"
xcopy /e /i /q /y "%PROJ%\src" "%SRC%\project\src" >nul
xcopy /e /i /q /y "%PROJ%\res" "%SRC%\project\res" >nul
for %%f in (CMakeLists.txt CMakePresets.json MLB2K7_manifest.toml mlb2k7_functions.toml mlb2k7_switches.toml recomplauncher.json) do copy /y "%PROJ%\%%f" "%SRC%\project\" >nul
copy /y "%PROJ%\generated\rexglue.cmake" "%SRC%\project\generated\" >nul
mkdir "%SRC%\tools"
for %%f in (env.bat vsenv.bat MLB2K7Recomp.toml patch_generated.py find_missing_functions.py fix_switch_tables.py set_includes.py recompile_loop.py) do copy /y "%ROOT%\tools\%%f" "%SRC%\tools\" >nul
xcopy /e /i /q /y "%ROOT%\sdk\patch" "%SRC%\sdk\patch" >nul
copy /y "%ROOT%\sdk\rexglue-sdk-0.10.1.patch" "%SRC%\sdk\" >nul 2>nul
xcopy /e /i /q /y "%ROOT%\docs" "%SRC%\docs" >nul 2>nul
for %%f in (1_install_tools.bat 2_recompile.bat 3_build.bat 4_package.bat BUILD_SDK.bat REBUILD.bat README.md BUILDING.md LICENSE .gitignore) do copy /y "%ROOT%\%%f" "%SRC%\" >nul 2>nul
del /q "%SRC%.zip" 2>nul
powershell -NoProfile -Command "Compress-Archive -Path '%SRC%\*' -DestinationPath '%SRC%.zip'"
rmdir /s /q "%SRC%" 2>nul

echo.
echo  Made release\%NAME%.zip and the source zip next to it.
echo.
pause
