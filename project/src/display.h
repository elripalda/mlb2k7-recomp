// MLB 2K7 Recomp - display options (brightness curve, graphics engine)
#pragma once
#include <rex/runtime.h>

namespace mlb {
// gpu_backend setting: "d3d12" (default) or "vulkan". Fills config.graphics when Vulkan
// is asked for; leaves it empty otherwise so the runtime loads the default.
void ChooseGraphicsBackend(rex::RuntimeConfig& config);
}  // namespace mlb
