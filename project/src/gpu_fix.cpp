// MLB 2K7 Recomp - gamma render target fix.
//
// The game draws its menus, its final picture and the jersey name/number mip levels into
// "gamma" render targets while feeding them colours that are already gamma-encoded. The
// emulated GPU encodes them a second time on the way out, which washes every colour out
// (the MLB logo's navy turns light blue) and makes each smaller jersey texture brighter
// than the last until white numbers vanish on white jerseys.
//
// Fix: whenever the game selects an 8_8_8_8_GAMMA colour target, hand the GPU the plain
// 8_8_8_8 format instead, so what the game draws is what gets stored.

#include "gpu_fix.h"

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(gamma_rt_fix, true, "MLB2K7",
                    "Store gamma render targets as plain colour (fixes washed-out colours and "
                    "faded jersey numbers)");

namespace {
using rex::graphics::CommandProcessor;

uint32_t* g_regs = nullptr;
using WriteRegFn = void (*)(CommandProcessor*, uint32_t, uint32_t);
using WriteRegsFn = void (*)(CommandProcessor*, uint32_t, uint32_t*, uint32_t);
WriteRegFn g_write_reg = nullptr;
WriteRegsFn g_write_regs = nullptr;

constexpr uint32_t kColorInfo[] = {0x2001, 0x2003, 0x2004, 0x2005};

inline void FixColorInfo(uint32_t first, uint32_t last) {
  for (uint32_t r : kColorInfo) {
    if (r < first || r > last) continue;
    uint32_t v = g_regs[r];
    if (((v >> 16) & 0xF) == 1) g_regs[r] = v & ~0xF0000u;  // k_8_8_8_8_GAMMA -> k_8_8_8_8
  }
}

void HookWriteRegister(CommandProcessor* self, uint32_t index, uint32_t value) {
  g_write_reg(self, index, value);
  if (index >= 0x2001 && index <= 0x2005) FixColorInfo(index, index);
}

void HookWriteRegistersFromMem(CommandProcessor* self, uint32_t start, uint32_t* base,
                               uint32_t count) {
  g_write_regs(self, start, base, count);
  if (count && start <= 0x2005 && start + count - 1 >= 0x2001)
    FixColorInfo(start, start + count - 1);
}

// Exposes the protected members so their vtable slots can be found.
struct Access : CommandProcessor {
  static auto WriteReg() { return &Access::WriteRegister; }
  static auto WriteRegs() { return &Access::WriteRegistersFromMem; }
};

// Finds which vtable slot a virtual member uses: call it on a fake object whose vtable is
// made of stubs that each record their own index.
int g_slot_hit = -1;
template <int N>
void SlotStub(void*, uintptr_t, uintptr_t, uintptr_t) {
  g_slot_hit = N;
}
template <int... N>
auto MakeSlotTable(std::integer_sequence<int, N...>) {
  return std::array<void*, sizeof...(N)>{reinterpret_cast<void*>(&SlotStub<N>)...};
}

template <typename PM, typename... Args>
int VtableSlot(PM pm, Args... args) {
  static auto table = MakeSlotTable(std::make_integer_sequence<int, 160>{});
  struct Fake {
    void** vptr;
  } fake{table.data()};
  g_slot_hit = -1;
  (reinterpret_cast<CommandProcessor*>(&fake)->*pm)(args...);
  return g_slot_hit;
}

#if defined(_WIN32)
template <typename Fn>
bool Patch(void** vtable, int slot, void* hook, Fn& original) {
  DWORD old;
  if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
  original = reinterpret_cast<Fn>(vtable[slot]);
  vtable[slot] = hook;
  VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
  return true;
}
#endif
}  // namespace

namespace mlb {
void InstallGpuFixes(rex::Runtime* runtime) {
#if defined(_WIN32)
  if (!REXCVAR_GET(gamma_rt_fix) || !runtime || g_write_reg) return;
  auto* gs = static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  CommandProcessor* cp = gs ? gs->command_processor() : nullptr;
  if (!cp) {
    REXLOG_ERROR("[gpu fix] graphics not ready, gamma fix not installed");
    return;
  }
  const int s1 = VtableSlot(Access::WriteReg(), 0u, 0u);
  const int s2 = VtableSlot(Access::WriteRegs(), 0u, static_cast<uint32_t*>(nullptr), 0u);
  if (s1 <= 0 || s2 <= 0 || s1 == s2) {
    REXLOG_ERROR("[gpu fix] could not locate register functions ({}, {})", s1, s2);
    return;
  }
  g_regs = gs->register_file()->values;
  void** vtable = *reinterpret_cast<void***>(cp);
  bool ok = Patch(vtable, s1, reinterpret_cast<void*>(&HookWriteRegister), g_write_reg) &&
            Patch(vtable, s2, reinterpret_cast<void*>(&HookWriteRegistersFromMem), g_write_regs);
  REXLOG_INFO("[gpu fix] gamma render target fix {} (slots {}, {})", ok ? "on" : "FAILED", s1, s2);
#else
  (void)runtime;
#endif
}
}  // namespace mlb
