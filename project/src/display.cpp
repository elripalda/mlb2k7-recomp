// MLB 2K7 Recomp - display options
//
// Brightness: before building its brightness curve, the game's D3D asks the console
// what kind of screen it is on (VdGetCurrentDisplayGamma). The runtime says "TV"
// (BT.709), so the curve is made for an old TV and looks washed out on a PC monitor.
// display_gamma = "srgb" tells the game it is on a normal PC monitor instead.
//
// The two functions that send the curve to the GPU are wrapped only to log what the
// game asked for, so we can see the numbers.

#include "display.h"

#include "platform.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/gpu_plugin.h>

#include <cstdint>
#include <cstring>
#include <string>

REXCVAR_DEFINE_STRING(display_gamma, "srgb", "MLB2K7",
                      "Screen type the game corrects brightness for: srgb (PC monitor), tv "
                      "(original console behaviour)");
REXCVAR_DEFINE_STRING(gpu_backend, "d3d12", "MLB2K7", "Graphics engine: d3d12 or vulkan");

namespace {
uint16_t Be16(uint8_t* base, uint32_t addr) {
  uint16_t v;
  std::memcpy(&v, base + addr, 2);
  return __builtin_bswap16(v);
}
void StoreBe32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}
}  // namespace

namespace mlb {
void ChooseGraphicsBackend(rex::RuntimeConfig& config) {
  std::string backend = REXCVAR_GET(gpu_backend);
  if (backend != "vulkan") return;
  auto graphics = rex::system::LoadGpuPlugin("xenos", "vulkan");
  if (graphics) {
    REXLOG_INFO("[display] using the Vulkan graphics engine");
    config.graphics = std::move(graphics);
  } else {
    REXLOG_ERROR("[display] Vulkan graphics engine not available, using Direct3D 12");
  }
}
}  // namespace mlb

// VdGetCurrentDisplayGamma(type_ptr, power_ptr)
extern "C" REX_FUNC(__imp__VdGetCurrentDisplayGamma) {
  static PPCFunc* original =
      reinterpret_cast<PPCFunc*>(mlb::FindRuntimeExport("__imp__VdGetCurrentDisplayGamma"));
  const uint32_t type_ptr = ctx.r3.u32, power_ptr = ctx.r4.u32;
  std::string mode = REXCVAR_GET(display_gamma);
  if (mode == "tv" && original) {
    original(ctx, base);
    return;
  }
  // 1 = sRGB, 0 = linear (no correction).
  StoreBe32(base, type_ptr, mode == "linear" ? 0u : 1u);
  float power = 2.2f;
  uint32_t bits;
  std::memcpy(&bits, &power, 4);
  StoreBe32(base, power_ptr, bits);
}

// D3DDevice_SetGammaRamp(device, ramp): 256-entry table, WORD red[256], green[256], blue[256].
REX_EXTERN(__imp__sub_824FC060);
extern "C" REX_FUNC(sub_824FC060) {
  const uint32_t ramp = ctx.r4.u32;
  if (ramp) {
    REXLOG_INFO("[display] gamma table (red 0/64/128/192/255): {} {} {} {} {}  green[128] {}  blue[128] {}",
                Be16(base, ramp), Be16(base, ramp + 128), Be16(base, ramp + 256),
                Be16(base, ramp + 384), Be16(base, ramp + 510), Be16(base, ramp + 512 + 256),
                Be16(base, ramp + 1024 + 256));
  }
  __imp__sub_824FC060(ctx, base);
}

// D3DDevice_SetPWLGamma(device, pwl): 128 segments per channel, {WORD base, WORD delta}.
REX_EXTERN(__imp__sub_824FC140);
extern "C" REX_FUNC(sub_824FC140) {
  const uint32_t pwl = ctx.r4.u32;
  if (pwl) {
    REXLOG_INFO("[display] gamma PWL red base 0/32/64/96/127: {} {} {} {} {}", Be16(base, pwl),
                Be16(base, pwl + 32 * 4), Be16(base, pwl + 64 * 4), Be16(base, pwl + 96 * 4),
                Be16(base, pwl + 127 * 4));
  }
  __imp__sub_824FC140(ctx, base);
}

