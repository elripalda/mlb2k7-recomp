@echo off
title MLB 2K7 Recomp - Build graphics SDK
cd /d "%~dp0"
set NO_SDK_CHECK=1
call tools\env.bat || (pause & exit /b 1)
call tools\vsenv.bat || (pause & exit /b 1)
echo.
echo  Building the ReXGlue SDK from source (graphics fixes + FSR output)
echo  This takes a while the first time. Later runs only rebuild what changed.
echo  -----------------------------------------------------------------------
echo.

set "SDKSRC=%ROOT%\sdk\src"
set "SDKBUILD=%ROOT%\sdk\build"
set "SDKINST=%ROOT%\sdk\install"

rem Unpack the source if a new zip was dropped in (sdk\rexglue-sdk-src.zip).
if exist "%ROOT%\sdk\rexglue-sdk-src.zip" (
  echo  Unpacking source...
  if not exist "%SDKSRC%" mkdir "%SDKSRC%"
  tar -xf "%ROOT%\sdk\rexglue-sdk-src.zip" -C "%SDKSRC%"
  if errorlevel 1 (echo Unpack failed & pause & exit /b 1)
  move /y "%ROOT%\sdk\rexglue-sdk-src.zip" "%ROOT%\sdk\rexglue-sdk-src.unpacked.zip" >nul
)
rem Or straight from GitHub (needs Git).
if not exist "%SDKSRC%\CMakeLists.txt" (
  echo  Downloading the ReXGlue SDK 0.10.1 source...
  git clone --depth 1 --branch v0.10.1 --recurse-submodules --shallow-submodules https://github.com/rexglue/rexglue-sdk.git "%SDKSRC%"
  if errorlevel 1 (echo Download failed - install Git, or put the source in sdk\src & pause & exit /b 1)
)
rem Loose patched files (sdk\patch\...) are copied over the source every run.
if exist "%ROOT%\sdk\patch" xcopy /e /y /q "%ROOT%\sdk\patch\*" "%SDKSRC%\" >nul

cmake -S "%SDKSRC%" -B "%SDKBUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_STANDARD=23 ^
  "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" ^
  -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=OFF -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_VERSION_OVERRIDE=0.10.1 ^
  "-DCMAKE_INSTALL_PREFIX=%SDKINST%" > "%LOGS%\sdk_configure.log" 2>&1
if errorlevel 1 (echo SDK configure failed - send me logs\sdk_configure.log & pause & exit /b 1)

cmake --build "%SDKBUILD%" --target install 2>&1 | powershell -NoProfile -Command "$input | Tee-Object -FilePath '%LOGS%\sdk_build.log'"
if not exist "%SDKINST%\bin\rexruntime.dll" (echo SDK build failed - send me logs\sdk_build.log & pause & exit /b 1)
if not exist "%SDKINST%\bin\rexgpu-xenos.dll" (echo SDK build failed - send me logs\sdk_build.log & pause & exit /b 1)
echo ok> "%ROOT%\sdk\BUILD_OK.txt"

echo.
echo  SDK built. Now rebuilding the game against it...
echo.
call "%ROOT%\3_build.bat"
