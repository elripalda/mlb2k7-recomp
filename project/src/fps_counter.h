// MLB 2K7 Recomp - frame counter
#pragma once
#include <rex/ui/overlay/debug_overlay.h>

namespace mlb {
// Called once per game frame (from the game's present function).
void OnGameFrame();
// Frames per second and frame time, averaged over the last half second.
rex::ui::FrameStats GetFrameStats();
// show_fps setting from MLB2K7Recomp.toml
bool FpsCounterEnabled();
}  // namespace mlb
