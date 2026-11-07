// MLB 2K7 Recomp - fixes applied to the emulated GPU.
#pragma once

namespace rex {
class Runtime;
}

namespace mlb {
// Call once after the graphics system exists, before the game starts drawing.
void InstallGpuFixes(rex::Runtime* runtime);
}  // namespace mlb
