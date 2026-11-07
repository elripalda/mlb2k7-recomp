// MLB 2K7 Recomp - menu music
#pragma once
#include <filesystem>

namespace mlb {
// Where the game's .wma soundtrack lives (the game folder).
void SetMusicFolder(const std::filesystem::path& folder);
}  // namespace mlb
