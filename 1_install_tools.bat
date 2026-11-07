@echo off
title MLB 2K7 Recomp - Step 1: Install tools
cd /d "%~dp0"
echo.
echo  Step 1 of 3 - Installing build tools
echo  ------------------------------------
echo  This installs whatever is missing: LLVM (clang), CMake, Ninja, Python,
echo  and the Visual Studio 2022 C++ build tools. Windows will ask for
echo  permission (yes/no popups) a few times - say Yes.
echo  Anything you already have is skipped.
echo.
pause

set W=winget install -e --silent --accept-source-agreements --accept-package-agreements --id
%W% LLVM.LLVM
%W% Kitware.CMake
%W% Ninja-build.Ninja
%W% Python.Python.3.12

set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSW%" for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"

set COMPONENTS=--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --add Microsoft.VisualStudio.Component.VC.CMake.Project
if defined VSINSTALL (
  echo.
  echo Found Visual Studio at "%VSINSTALL%" - adding the C++ parts if missing...
  "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "%VSINSTALL%" %COMPONENTS% --passive --norestart
) else (
  echo.
  echo Installing Visual Studio 2022 Build Tools with C++ - this one takes a while...
  winget install -e --accept-source-agreements --accept-package-agreements --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools %COMPONENTS%"
)

echo.
echo  Checking...
call tools\env.bat >nul 2>&1
set OK=1
where clang++ >nul 2>&1 || (echo   MISSING: clang - close this window and run step 1 again & set OK=0)
where cmake >nul 2>&1 || (echo   MISSING: cmake & set OK=0)
where ninja >nul 2>&1 || (echo   MISSING: ninja & set OK=0)
where py >nul 2>&1 || where python >nul 2>&1 || (echo   MISSING: python & set OK=0)
call tools\vsenv.bat >nul 2>&1 || (echo   MISSING: Visual Studio C++ tools & set OK=0)
echo.
if "%OK%"=="1" (echo  All set. Next: double-click 2_recompile.bat) else (echo  Something is missing - restart the PC and run step 1 once more.)
echo.
pause
