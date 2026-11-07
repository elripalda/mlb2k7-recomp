// MLB 2K7 Recomp - menu music
//
// The game plays its soundtrack (the .wma files in the game folder) through the
// Xbox music player (XMP): it hands the system a playlist and says "play". The
// runtime only pretends to play it, so we watch the same XMP messages on their
// way to the runtime and play the songs ourselves (platform_win.cpp decodes them
// with Windows Media Foundation).

#include "xmp_music.h"

#include "platform.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

REXCVAR_DEFINE_INT32(music_volume, 100, "MLB2K7", "Menu music volume, 0-100");

namespace mlb {
namespace {
std::mutex g_mutex;
std::filesystem::path g_folder;
std::map<uint32_t, std::vector<std::filesystem::path>> g_playlists;  // handle -> songs
bool g_shuffle = false;
bool g_repeat = true;
float g_game_volume = 1.0f;

uint32_t Be32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}

float BeFloat(uint8_t* base, uint32_t addr) {
  uint32_t v = Be32(base, addr);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

std::u16string BeString16(uint8_t* base, uint32_t addr) {
  std::u16string s;
  if (!addr) return s;
  for (uint32_t i = 0; i < 1024; ++i) {
    uint16_t c;
    std::memcpy(&c, base + addr + i * 2, 2);
    c = __builtin_bswap16(c);
    if (!c) break;
    s.push_back(static_cast<char16_t>(c));
  }
  return s;
}

// "game:\TAPE_P_01.wma" -> <game folder>\TAPE_P_01.wma
std::filesystem::path ToHostPath(const std::u16string& guest) {
  std::u16string rel = guest;
  if (auto colon = rel.rfind(u':'); colon != std::u16string::npos) rel = rel.substr(colon + 1);
  if (rel.rfind(u"\\Device\\", 0) == 0) {  // \Device\Cdrom0\file
    auto p = rel.find(u'\\', 8);
    rel = p == std::u16string::npos ? std::u16string() : rel.substr(p);
  }
  while (!rel.empty() && (rel[0] == u'\\' || rel[0] == u'/')) rel.erase(0, 1);
  for (auto& c : rel)
    if (c == u'\\') c = u'/';
  std::filesystem::path p = g_folder / std::filesystem::path(rel);
  std::error_code ec;
  if (std::filesystem::exists(p, ec)) return p;
  // Fall back to the bare file name in the game folder.
  return g_folder / std::filesystem::path(rel).filename();
}

void ApplyVolume() {
  int pct = REXCVAR_GET(music_volume);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  MusicSetVolume(g_game_volume * (pct / 100.0f));
}

// Messages to the XMP app (id 0xFA). `buffer` is the message's argument block.
void Before(uint8_t* base, uint32_t app, uint32_t msg, uint32_t buffer) {
  if (app != 0xFA || !buffer) return;
  std::lock_guard lock(g_mutex);
  switch (msg) {
    case 0x00070002: {  // XMPPlayTitlePlaylist
      uint32_t storage = Be32(base, buffer + 4);
      uint32_t handle = storage ? Be32(base, storage) : 0;
      auto it = g_playlists.find(handle);
      if (it == g_playlists.end() || it->second.empty()) {
        REXLOG_WARN("[music] play: unknown playlist {:08X}", handle);
        break;
      }
      REXLOG_INFO("[music] play playlist {:08X} ({} songs, shuffle={}, repeat={})", handle,
                  it->second.size(), g_shuffle, g_repeat);
      ApplyVolume();
      MusicPlay(it->second, g_shuffle, g_repeat);
      break;
    }
    case 0x00070003: MusicResume(); break;    // XMPContinue
    case 0x00070004: MusicStop(); break;      // XMPStop
    case 0x00070005: MusicPause(); break;     // XMPPause
    case 0x00070006: MusicNext(); break;      // XMPNext
    case 0x00070007: MusicPrevious(); break;  // XMPPrevious
    case 0x00070008:                          // XMPSetPlaybackBehavior(mode, repeat, flags)
      g_shuffle = Be32(base, buffer + 4) == 1;  // 0 = in order, 1 = shuffle
      g_repeat = Be32(base, buffer + 8) == 0;   // 0 = repeat playlist, 1 = no repeat
      break;
    case 0x0007000C:  // XMPSetVolume
      g_game_volume = BeFloat(base, buffer + 4);
      ApplyVolume();
      break;
    default:
      break;
  }
}

void After(uint8_t* base, uint32_t app, uint32_t msg, uint32_t buffer) {
  if (app != 0xFA || !buffer || msg != 0x0007000D) return;  // XMPCreateTitlePlaylist
  uint32_t storage = Be32(base, buffer + 4);
  uint32_t songs = Be32(base, buffer + 12);
  uint32_t count = Be32(base, buffer + 16);
  uint32_t handle = storage ? Be32(base, storage) : 0;
  std::vector<std::filesystem::path> list;
  for (uint32_t i = 0; songs && i < count && i < 256; ++i) {
    auto path = BeString16(base, Be32(base, songs + i * 36));
    if (!path.empty()) list.push_back(ToHostPath(path));
  }
  std::lock_guard lock(g_mutex);
  REXLOG_INFO("[music] playlist {:08X}: {} songs{}{}", handle, list.size(),
              list.empty() ? "" : ", first ", list.empty() ? "" : list[0].filename().string());
  g_playlists[handle] = std::move(list);
}

PPCFunc* Original(const char* name) {
  auto* f = reinterpret_cast<PPCFunc*>(FindRuntimeExport(name));
  if (!f) REXLOG_ERROR("[music] {} not found in rexruntime.dll", name);
  return f;
}
}  // namespace

void SetMusicFolder(const std::filesystem::path& folder) {
  std::lock_guard lock(g_mutex);
  g_folder = folder;
}
}  // namespace mlb

// The game reaches XMP through these two xam calls. Our definitions take the place
// of the runtime's (the generated code calls them by name); each looks at the
// message and then hands it to the runtime's own version.

// XMsgInProcessCall(app, message, buffer, length)
extern "C" REX_FUNC(__imp__XMsgInProcessCall) {
  static PPCFunc* original = mlb::Original("__imp__XMsgInProcessCall");
  const uint32_t app = ctx.r3.u32, msg = ctx.r4.u32, buffer = ctx.r5.u32;
  mlb::Before(base, app, msg, buffer);
  if (original) original(ctx, base);
  mlb::After(base, app, msg, buffer);
}

// XMsgStartIORequest(app, message, overlapped, buffer, length)
extern "C" REX_FUNC(__imp__XMsgStartIORequest) {
  static PPCFunc* original = mlb::Original("__imp__XMsgStartIORequest");
  const uint32_t app = ctx.r3.u32, msg = ctx.r4.u32, buffer = ctx.r6.u32;
  mlb::Before(base, app, msg, buffer);
  if (original) original(ctx, base);
  mlb::After(base, app, msg, buffer);
}
