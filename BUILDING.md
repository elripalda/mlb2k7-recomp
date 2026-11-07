# Building MLB 2K7 Recomp

Windows 10 or 11. You need your own copy of the game: nothing from it is stored here.

1. Download the [ReXGlue SDK 0.10.1](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.1) for Windows (`win-amd64`) and unzip it to your Desktop as `rexglue-sdk-0.10.1-win-amd64`. It's used to recompile the game code.
2. Put the game's files on your Desktop as `Major League Baseball 2K7 (USA)` (the folder with `default.xex`), or change the `SDK` and `GAME` lines in `tools\env.bat`. If you only have a disc image, run a release build once: its setup screen unpacks the disc into its `game` folder.
3. Install [Git](https://git-scm.com) so `BUILD_SDK.bat` can download the SDK source.
4. Run these in order:

| Script | Does |
|---|---|
| `1_install_tools.bat` | Installs LLVM (clang), CMake, Ninja, Python and the Visual Studio 2022 C++ tools. Once. |
| `2_recompile.bat` | Recompiles `default.xex` to C++ with ReXGlue. |
| `BUILD_SDK.bat` | Builds the ReXGlue runtime from source with this recomp's changes (`sdk/patch`), then builds the game against it. |
| `3_build.bat` | Builds `MLB2K7Recomp.exe` into the `MLB2K7Recomp` folder. |
| `4_package.bat` | Makes the release zip and the source zip in `release\`. |

After changing the app code (`project/src`), run `3_build.bat`. After changing the function list (`project/mlb2k7_functions.toml`), run `REBUILD.bat`.

## What's where
- `project/src`: the app. The setup screen and disc image reader, the graphics fixes (render target gamma, display brightness), the menu music player, the frame counter and the RecompLauncher setup step.
- `project/mlb2k7_functions.toml` and `project/mlb2k7_switches.toml`: help for the recompiler (function boundaries and jump tables).
- `project/recomplauncher.json`: how RecompLauncher installs, sets up and shows settings for the game.
- `sdk/patch`: the files changed in ReXGlue 0.10.1 (built-in CAS and FSR output, one warning logged once instead of every frame). `sdk/rexglue-sdk-0.10.1.patch` is the same change as a diff.
