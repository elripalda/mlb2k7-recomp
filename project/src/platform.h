// MLB 2K7 Recomp - the few bits that talk to Windows directly.
// Kept in their own file so <windows.h> macros (small, DrawText, ...) can't
// leak into the rest of the code.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mlb {

// Writes a crash report (exception, address, call stack as module+offset) to `file`
// if the program crashes.
void InstallCrashReporter(const std::filesystem::path& file);

// Native file/folder picker. Returns a UTF-8 path, or an empty string if cancelled.
std::string BrowseForCopy(bool folder);

// Writes to stdout even from a windowed (GUI) program, for RecompLauncher's setup step.
void WriteStdout(const std::string& text);

// Address of a function exported by rexruntime.dll, or nullptr.
void* FindRuntimeExport(const char* name);

// Music player for the game's soundtrack (runs on its own thread; all calls return at once).
void MusicPlay(std::vector<std::filesystem::path> songs, bool shuffle, bool repeat);
void MusicStop();
void MusicPause();
void MusicResume();
void MusicNext();
void MusicPrevious();
void MusicSetVolume(float volume);  // 0..1

}  // namespace mlb
