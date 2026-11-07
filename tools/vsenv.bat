@echo off
rem Loads the Visual Studio C++ environment (Windows SDK + MSVC libraries).
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSW%" for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo Visual Studio C++ tools were not found. Run 1_install_tools.bat first.
  exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
rem Make sure the standalone LLVM clang wins over anything else on PATH.
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
exit /b 0
