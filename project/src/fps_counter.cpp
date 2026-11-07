// MLB 2K7 Recomp - frame counter
//
// The game presents each frame from sub_82506588 (the function that calls VdSwap).
// Generated functions are weak symbols, so defining it here wraps the original:
// count the frame, then run the game's own code.

#include "fps_counter.h"

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include <chrono>
#include <mutex>

REXCVAR_DEFINE_BOOL(show_fps, false, "MLB2K7", "Show the frame counter in the top-left corner");

namespace mlb {
namespace {
using Clock = std::chrono::steady_clock;
std::mutex g_mutex;
Clock::time_point g_window_start = Clock::now();
uint64_t g_window_frames = 0;
uint64_t g_total_frames = 0;
rex::ui::FrameStats g_stats;
}  // namespace

void OnGameFrame() {
  std::lock_guard lock(g_mutex);
  ++g_total_frames;
  ++g_window_frames;
  auto now = Clock::now();
  double elapsed = std::chrono::duration<double>(now - g_window_start).count();
  if (elapsed >= 0.5) {
    g_stats.fps = g_window_frames / elapsed;
    g_stats.frame_time_ms = g_window_frames ? (elapsed * 1000.0) / g_window_frames : 0.0;
    g_window_frames = 0;
    g_window_start = now;
  }
  g_stats.frame_count = g_total_frames;
}

rex::ui::FrameStats GetFrameStats() {
  std::lock_guard lock(g_mutex);
  return g_stats;
}

bool FpsCounterEnabled() { return REXCVAR_GET(show_fps); }
}  // namespace mlb

REX_EXTERN(__imp__sub_82506588);

extern "C" REX_FUNC(sub_82506588) {
  mlb::OnGameFrame();
  __imp__sub_82506588(ctx, base);
}
