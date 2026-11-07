// MLB 2K7 Recomp - first-run setup
//
// The setup screen is drawn by hand with ImGui's draw list rather than stock
// widgets, so it looks like part of the game instead of a debug tool.
//
// A folder copy (the one with default.xex) is used where it is; its location goes in
// the settings file. A disc image is unpacked once into the "game" folder.

#include "setup.h"

#include "icon_rgba.h"
#include "xiso.h"

#include <rex/filesystem.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "platform.h"

namespace mlb {
namespace fs = std::filesystem;

namespace {
fs::path g_config_path;
}

fs::path GameDir() { return rex::filesystem::GetExecutableFolder() / "game"; }

bool GameInstalled() {
  std::error_code ec;
  return fs::is_regular_file(GameDir() / "default.xex", ec);
}

void SetConfigPath(const fs::path& path) { g_config_path = path; }

namespace {

// ---------------------------------------------------------------------------
// Setting up
// ---------------------------------------------------------------------------

fs::path Utf8Path(const std::string& s) {
  return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
}

std::string PathUtf8(const fs::path& p) {
  auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

// Writes game_data_root into the settings file (replacing the line if it's there).
void SaveGameFolder(const fs::path& folder) {
  if (g_config_path.empty()) return;
  std::string escaped;
  for (char c : PathUtf8(folder)) {
    if (c == '\\' || c == '"') escaped += '\\';
    escaped += c;
  }
  std::string line = "game_data_root = \"" + escaped + "\"";
  std::vector<std::string> lines;
  bool replaced = false;
  {
    std::ifstream in(g_config_path);
    std::string l;
    while (std::getline(in, l)) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      size_t i = l.find_first_not_of(" \t");
      if (i != std::string::npos && l.compare(i, 14, "game_data_root") == 0 &&
          l.find('=', i) != std::string::npos) {
        if (!replaced) lines.push_back(line);
        replaced = true;
        continue;
      }
      lines.push_back(l);
    }
  }
  if (!replaced) {
    // At the top, so it's the first thing anyone opening the file sees.
    lines.insert(lines.begin(), {"# Your copy of the game (the folder with default.xex)", line, ""});
  }
  std::ofstream out(g_config_path, std::ios::trunc);
  for (auto& l : lines) out << l << "\n";
}

// Sets up from a picked path. On success, *game_folder is the folder the game runs from.
// A disc image is unpacked into game.tmp, then swapped in, so a half-finished unpack never counts.
bool Install(const std::string& input, const ProgressFn& progress, bool* unpacking,
             fs::path* game_folder, std::string* error) {
  CopyInfo copy = IdentifyCopy(Utf8Path(input));
  if (copy.kind == CopyInfo::Kind::kNone) {
    *error = copy.error;
    return false;
  }
  if (copy.kind == CopyInfo::Kind::kFolder) {
    *unpacking = false;
    SaveGameFolder(copy.path);
    *game_folder = copy.path;
    return true;
  }
  *unpacking = true;
  fs::path final_dir = GameDir();
  fs::path tmp_dir = final_dir;
  tmp_dir += ".tmp";
  std::error_code ec;
  fs::remove_all(tmp_dir, ec);
  if (!ExtractImage(copy.path, tmp_dir, progress, error)) {
    fs::remove_all(tmp_dir, ec);
    return false;
  }
  fs::remove_all(final_dir, ec);
  fs::rename(tmp_dir, final_dir, ec);
  if (ec) {
    *error = "Couldn't finish setting up the game folder: " + ec.message();
    return false;
  }
  *game_folder = final_dir;
  return true;
}

void WriteLine(const std::string& line) { WriteStdout(line + "\n"); }

// ---------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------

struct Fonts {
  ImFont* note = nullptr;     // 12.5
  ImFont* body = nullptr;     // 14
  ImFont* bar = nullptr;      // 15 semibold
  ImFont* heading = nullptr;  // 18 semibold
  ImFont* title = nullptr;    // 30 semibold
};
Fonts g_fonts;

constexpr ImU32 Rgb(uint32_t hex, float a = 1.0f) {
  return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, uint32_t(a * 255.0f));
}
constexpr ImU32 kBg = Rgb(0x0E1013);
constexpr ImU32 kPanel = Rgb(0x16191E);
constexpr ImU32 kLine = Rgb(0x262A31);
constexpr ImU32 kText = Rgb(0xEEF1F5);
constexpr ImU32 kMuted = Rgb(0x9AA3AE);
constexpr ImU32 kFaint = Rgb(0x6F7884);
constexpr ImU32 kBlue = Rgb(0x3AA0E6);
constexpr ImU32 kBlueHover = Rgb(0x52B0EE);
constexpr ImU32 kRed = Rgb(0xFF6B5E);

// Design canvas; the content block is centred in the window at this size.
constexpr float kCanvasW = 1280.0f;
constexpr float kBarH = 56.0f;
constexpr float kFootH = 44.0f;

void PushFontSafe(ImFont* f) {
  if (f) {
    ImGui::PushFont(f, f->LegacySize);
  } else {
    ImGui::PushFont(nullptr, 0.0f);
  }
}

ImVec2 TextSize(ImFont* f, const char* text, float wrap = 0.0f) {
  PushFontSafe(f);
  ImVec2 s = ImGui::CalcTextSize(text, nullptr, false, wrap);
  ImGui::PopFont();
  return s;
}

// Draws text; returns its height.
float Label(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* text,
               float wrap = 0.0f) {
  PushFontSafe(f);
  ImFont* font = ImGui::GetFont();
  float size = ImGui::GetFontSize();
  dl->AddText(font, size, pos, col, text, nullptr, wrap);
  float h = ImGui::CalcTextSize(text, nullptr, false, wrap).y;
  ImGui::PopFont();
  return h;
}

// Clickable area; returns true when clicked. hovered is set while the mouse is over it.
bool HitArea(const char* id, ImVec2 pos, ImVec2 size, bool* hovered) {
  ImGui::SetCursorScreenPos(pos);
  bool clicked = ImGui::InvisibleButton(id, size);
  *hovered = ImGui::IsItemHovered();
  if (*hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  return clicked;
}

bool Button(ImDrawList* dl, const char* id, const char* label, ImVec2 pos, ImVec2 size,
            bool primary) {
  bool hovered = false;
  bool clicked = HitArea(id, pos, size, &hovered);
  ImVec2 end(pos.x + size.x, pos.y + size.y);
  if (primary) {
    dl->AddRectFilled(pos, end, hovered ? kBlueHover : kBlue, 9.0f);
  } else {
    if (hovered) dl->AddRectFilled(pos, end, Rgb(0xFFFFFF, 0.04f), 9.0f);
    dl->AddRect(pos, end, hovered ? Rgb(0x56606C) : Rgb(0x39404A), 9.0f, 0, 1.0f);
  }
  ImVec2 ts = TextSize(g_fonts.bar, label);
  Label(dl, g_fonts.bar, ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
           primary ? Rgb(0xFFFFFF) : kText, label);
  return clicked;
}

// Rounded rectangle with a dashed outline.
void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float r) {
  const float dash = 6.0f, gap = 5.0f, t = 1.5f;
  auto line = [&](ImVec2 p0, ImVec2 p1) {
    float len = std::hypot(p1.x - p0.x, p1.y - p0.y);
    ImVec2 d((p1.x - p0.x) / len, (p1.y - p0.y) / len);
    for (float s = 0; s < len; s += dash + gap) {
      float e = std::min(s + dash, len);
      dl->AddLine(ImVec2(p0.x + d.x * s, p0.y + d.y * s), ImVec2(p0.x + d.x * e, p0.y + d.y * e),
                  col, t);
    }
  };
  line(ImVec2(a.x + r, a.y), ImVec2(b.x - r, a.y));
  line(ImVec2(b.x, a.y + r), ImVec2(b.x, b.y - r));
  line(ImVec2(b.x - r, b.y), ImVec2(a.x + r, b.y));
  line(ImVec2(a.x, b.y - r), ImVec2(a.x, a.y + r));
  const float pi = 3.14159265f;
  dl->PathArcTo(ImVec2(a.x + r, a.y + r), r, pi, pi * 1.5f, 8);
  dl->PathStroke(col, 0, t);
  dl->PathArcTo(ImVec2(b.x - r, a.y + r), r, pi * 1.5f, pi * 2.0f, 8);
  dl->PathStroke(col, 0, t);
  dl->PathArcTo(ImVec2(b.x - r, b.y - r), r, 0.0f, pi * 0.5f, 8);
  dl->PathStroke(col, 0, t);
  dl->PathArcTo(ImVec2(a.x + r, b.y - r), r, pi * 0.5f, pi, 8);
  dl->PathStroke(col, 0, t);
}

// Upload arrow for the drop area.
void UploadIcon(ImDrawList* dl, ImVec2 c, ImU32 col) {
  const float t = 1.8f;
  dl->AddLine(ImVec2(c.x, c.y + 6), ImVec2(c.x, c.y - 10), col, t);
  dl->AddLine(ImVec2(c.x - 6, c.y - 4), ImVec2(c.x, c.y - 10), col, t);
  dl->AddLine(ImVec2(c.x + 6, c.y - 4), ImVec2(c.x, c.y - 10), col, t);
  dl->PathLineTo(ImVec2(c.x - 10, c.y + 3));
  dl->PathLineTo(ImVec2(c.x - 10, c.y + 10));
  dl->PathLineTo(ImVec2(c.x + 10, c.y + 10));
  dl->PathLineTo(ImVec2(c.x + 10, c.y + 3));
  dl->PathStroke(col, 0, t);
}

void Check(ImDrawList* dl, ImVec2 c, ImU32 col) {
  dl->PathLineTo(ImVec2(c.x - 4.5f, c.y + 0.5f));
  dl->PathLineTo(ImVec2(c.x - 1.5f, c.y + 3.5f));
  dl->PathLineTo(ImVec2(c.x + 4.5f, c.y - 3.5f));
  dl->PathStroke(col, 0, 1.8f);
}

std::string Megabytes(uint64_t bytes) {
  char buf[32];
  if (bytes >= (1ull << 30))
    std::snprintf(buf, sizeof(buf), "%.1f GB", double(bytes) / double(1ull << 30));
  else
    std::snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>(bytes >> 20));
  return buf;
}

std::string Shorten(const std::string& s, size_t n) {
  return s.size() <= n ? s : s.substr(0, n) + "...";
}

}  // namespace

void LoadSetupFonts(ImFontAtlas* atlas) {
#if defined(_WIN32)
  const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* semibold = "C:\\Windows\\Fonts\\seguisb.ttf";
  std::error_code ec;
  if (!fs::exists(regular, ec)) return;
  if (!fs::exists(semibold, ec)) semibold = regular;
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
  g_fonts.note = atlas->AddFontFromFileTTF(regular, 12.5f, &cfg, ranges);
  g_fonts.body = atlas->AddFontFromFileTTF(regular, 14.0f, &cfg, ranges);
  g_fonts.bar = atlas->AddFontFromFileTTF(semibold, 15.0f, &cfg, ranges);
  g_fonts.heading = atlas->AddFontFromFileTTF(semibold, 18.0f, &cfg, ranges);
  g_fonts.title = atlas->AddFontFromFileTTF(semibold, 30.0f, &cfg, ranges);
#else
  (void)atlas;
#endif
}

fs::path GameFolderFor(const std::string& copy) {
  if (copy.empty()) return {};
  CopyInfo info = IdentifyCopy(Utf8Path(copy));
  if (info.kind == CopyInfo::Kind::kFolder) return info.path;
  if (info.kind == CopyInfo::Kind::kImage && GameInstalled()) return GameDir();
  return {};
}

int RunPrepare(const std::string& game_path) {
  WriteLine("progress 0 Looking at your copy");
  CopyInfo copy = IdentifyCopy(Utf8Path(game_path));
  if (copy.kind == CopyInfo::Kind::kNone) {
    WriteLine("error " + copy.error);
    return kExitNotTheGame;
  }
  if (copy.kind == CopyInfo::Kind::kFolder) {
    WriteLine("progress 1 Ready to play");
    return 0;
  }
  if (GameInstalled()) {
    WriteLine("progress 1 Already unpacked");
    return 0;
  }
  std::string error;
  int last_pct = -1;
  bool unpacking = true;
  fs::path folder;
  bool ok = Install(
      game_path,
      [&](uint64_t done, uint64_t total, const std::string& name) {
        double f = total ? double(done) / double(total) : 0.0;
        int pct = int(f * 100.0);
        if (pct == last_pct) return;
        last_pct = pct;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "progress %.2f Unpacking %s", f, name.c_str());
        WriteLine(buf);
      },
      &unpacking, &folder, &error);
  if (!ok) {
    WriteLine("error " + error);
    return kExitUnpackFailed;
  }
  WriteLine("progress 1 Ready to play");
  return 0;
}

SetupDialog::SetupDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate,
                         Post post_to_ui, Ready on_ready, std::function<void()> on_quit)
    : rex::ui::ImGuiDialog(drawer),
      post_(std::move(post_to_ui)),
      on_ready_(std::move(on_ready)),
      on_quit_(std::move(on_quit)) {
  if (immediate) {
    icon_ = immediate->CreateTexture(kIconSize, kIconSize, rex::ui::ImmediateTextureFilter::kLinear,
                                     false, kIconRgba);
  }
}

SetupDialog::~SetupDialog() {
  if (worker_.joinable()) worker_.join();
}

void SetupDialog::Start(const std::string& path) {
  if (busy()) return;
  if (worker_.joinable()) worker_.join();
  {
    std::lock_guard lock(mutex_);
    current_file_.clear();
    error_.clear();
    auto name = Utf8Path(path).filename().u8string();
    source_name_ = name.empty() ? path : std::string(name.begin(), name.end());
  }
  done_bytes_ = 0;
  total_bytes_ = 0;
  unpacking_ = false;
  state_ = State::kWorking;
  worker_ = std::thread([this, path] {
    std::string error;
    bool unpacking = false;
    fs::path folder;
    bool ok = Install(
        path,
        [this](uint64_t done, uint64_t total, const std::string& name) {
          unpacking_ = true;
          done_bytes_ = done;
          total_bytes_ = total;
          std::lock_guard lock(mutex_);
          current_file_ = name;
        },
        &unpacking, &folder, &error);
    std::lock_guard lock(mutex_);
    if (ok) {
      game_folder_ = folder;
      state_ = State::kDone;
    } else {
      error_ = error;
      state_ = State::kError;
    }
  });
}

void SetupDialog::Pick(bool folder) {
  // The native picker runs its own message loop, so open it outside of ImGui drawing.
  post_([this, folder] {
    std::string path = BrowseForCopy(folder);
    if (!path.empty()) Start(path);
  });
}

void SetupDialog::OnDraw(ImGuiIO& io) {
  const float W = io.DisplaySize.x;
  const float H = io.DisplaySize.y;
  const State state = state_;

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::Begin("##mlb_setup", nullptr, flags);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImTextureID icon = reinterpret_cast<ImTextureID>(icon_.get());

  // Top bar
  dl->AddLine(ImVec2(0, kBarH), ImVec2(W, kBarH), kLine);
  if (icon_) dl->AddImageRounded(icon, ImVec2(32, 15), ImVec2(58, 41), ImVec2(0, 0), ImVec2(1, 1),
                                 IM_COL32_WHITE, 6.0f);
  float bar_text_y = (kBarH - TextSize(g_fonts.bar, "A").y) * 0.5f;
  Label(dl, g_fonts.bar, ImVec2(icon_ ? 70.0f : 32.0f, bar_text_y), kText,
           "MLB 2K7 Recomp");
  ImVec2 vs = TextSize(g_fonts.note, kVersion);
  Label(dl, g_fonts.note, ImVec2(W - 32 - vs.x, (kBarH - vs.y) * 0.5f), kMuted, kVersion);

  // Footer
  dl->AddLine(ImVec2(0, H - kFootH), ImVec2(W, H - kFootH), kLine);
  float foot_y = H - kFootH + (kFootH - TextSize(g_fonts.note, "A").y) * 0.5f;
  Label(dl, g_fonts.note, ImVec2(32, foot_y), kFaint, "Recomp by Ripalda");
  {
    ImVec2 qs = TextSize(g_fonts.note, "Quit");
    ImVec2 qp(W - 32 - qs.x, foot_y);
    bool hovered = false;
    if (HitArea("##quit", ImVec2(qp.x - 8, qp.y - 6), ImVec2(qs.x + 16, qs.y + 12), &hovered) &&
        !busy() && on_quit_) {
      post_(on_quit_);
    }
    Label(dl, g_fonts.note, qp, hovered ? kText : kFaint, "Quit");
  }

  // Content block, centred
  const float left = std::max(24.0f, (W - kCanvasW) * 0.5f + 72.0f);
  const float content_w = std::min(kCanvasW - 144.0f, W - 48.0f);
  const float side_w = content_w >= 900.0f ? 360.0f : 0.0f;  // side column hides on small windows
  const float gap = side_w > 0 ? 56.0f : 0.0f;
  const float card_x = left + side_w + gap;
  const float card_w = content_w - side_w - gap;
  const float top = kBarH + std::max(24.0f, std::min(56.0f, (H - kBarH - kFootH - 520.0f) * 0.5f));

  // Side column: icon, title, steps
  if (side_w > 0) {
    float y = top;
    if (icon_) {
      dl->AddRectFilled(ImVec2(left + 4, y + 10), ImVec2(left + 172, y + 178), Rgb(0x000000, 0.35f),
                        28.0f);
      dl->AddImageRounded(icon, ImVec2(left, y), ImVec2(left + 168, y + 168), ImVec2(0, 0),
                          ImVec2(1, 1), IM_COL32_WHITE, 28.0f);
      y += 196;
    }
    const char* title = state == State::kDone ? "You're all set" : "Let's set up\nyour game";
    y += Label(dl, g_fonts.title, ImVec2(left, y), kText, title) + 10;
    const char* blurb = state == State::kDone
                            ? "Starting the game now. Next time it opens straight to the title screen."
                            : "This only happens once. After this, the game starts straight away.";
    y += Label(dl, g_fonts.body, ImVec2(left, y), kMuted, blurb, left + side_w) + 28;

    const char* steps[3] = {"Choose your copy", "Get it ready", "Play"};
    int active = state == State::kWorking ? 1 : state == State::kDone ? 2 : 0;
    for (int i = 0; i < 3; ++i) {
      ImVec2 c(left + 11, y + 11);
      bool done = i < active;
      bool on = i <= active;
      if (on) {
        dl->AddCircleFilled(c, 11, kBlue, 24);
      } else {
        dl->AddCircle(c, 10.5f, kLine, 24, 1.0f);
      }
      if (done) {
        Check(dl, c, Rgb(0xFFFFFF));
      } else {
        char n[2] = {char('1' + i), 0};
        ImVec2 ns = TextSize(g_fonts.note, n);
        Label(dl, g_fonts.note, ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f),
                 on ? Rgb(0xFFFFFF) : kMuted, n);
      }
      ImVec2 ls = TextSize(g_fonts.body, steps[i]);
      Label(dl, g_fonts.body, ImVec2(left + 34, c.y - ls.y * 0.5f), on ? kText : kMuted, steps[i]);
      y += 34;
    }
  }

  // Card
  const float pad = 32.0f;
  const float inner_x = card_x + pad;
  const float inner_w = card_w - pad * 2;
  // Draw the card's contents on a second channel so the background can be sized afterwards.
  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);
  float y = top + pad;

  if (state == State::kPick || state == State::kError) {
    y += Label(dl, g_fonts.heading, ImVec2(inner_x, y), kText,
                  "Choose your copy of Major League Baseball 2K7", inner_x + inner_w) + 6;
    y += Label(dl, g_fonts.body, ImVec2(inner_x, y), kMuted,
                  "Your Xbox 360 disc image (.iso), or the folder with default.xex in it.", inner_x + inner_w) + 24;

    // Drop area (also clickable)
    ImVec2 da(inner_x, y), db(inner_x + inner_w, y + 190);
    bool hovered = false;
    if (HitArea("##drop", da, ImVec2(inner_w, 190), &hovered)) Pick(false);
    if (hovered) dl->AddRectFilled(da, db, Rgb(0xFFFFFF, 0.025f), 12.0f);
    DashedRect(dl, da, db, hovered ? Rgb(0x5A6470) : Rgb(0x3A414B), 12.0f);
    float cy = y + 190 * 0.5f;
    UploadIcon(dl, ImVec2(inner_x + inner_w * 0.5f, cy - 26), hovered ? kText : kMuted);
    ImVec2 t1 = TextSize(g_fonts.bar, "Drop it here");
    Label(dl, g_fonts.bar, ImVec2(inner_x + (inner_w - t1.x) * 0.5f, cy + 2), kText,
             "Drop it here");
    ImVec2 t2 = TextSize(g_fonts.note, "or browse below");
    Label(dl, g_fonts.note, ImVec2(inner_x + (inner_w - t2.x) * 0.5f, cy + 28), kMuted,
             "or browse below");
    y += 190 + 20;

    if (state == State::kError) {
      std::string err;
      {
        std::lock_guard lock(mutex_);
        err = error_;
      }
      float eh = TextSize(g_fonts.body, err.c_str(), inner_w - 28).y + 22;
      dl->AddRectFilled(ImVec2(inner_x, y), ImVec2(inner_x + inner_w, y + eh), Rgb(0xFF6B5E, 0.08f),
                        9.0f);
      dl->AddRect(ImVec2(inner_x, y), ImVec2(inner_x + inner_w, y + eh), Rgb(0xFF6B5E, 0.35f), 9.0f);
      Label(dl, g_fonts.body, ImVec2(inner_x + 14, y + 11), kRed, err.c_str(),
               inner_x + inner_w - 14);
      y += eh + 16;
    }

    float bw = (inner_w - 12) * 0.5f;
    if (Button(dl, "##file", "Choose file", ImVec2(inner_x, y), ImVec2(bw, 44), true)) Pick(false);
    if (Button(dl, "##folder", "Choose folder", ImVec2(inner_x + bw + 12, y), ImVec2(bw, 44), false))
      Pick(true);
    y += 44 + 22;

    dl->AddLine(ImVec2(inner_x, y), ImVec2(inner_x + inner_w, y), kLine);
    y += 18;
    y += Label(dl, g_fonts.note, ImVec2(inner_x, y), kMuted,
                  "A disc image is unpacked once into the game folder (about 7 GB). A folder copy is used where it is.",
                  inner_x + inner_w) + 6;
    y += Label(dl, g_fonts.note, ImVec2(inner_x, y), kMuted,
                  "You need your own copy; no game files come with this recomp.", inner_x + inner_w);
  } else {
    bool done = state == State::kDone;
    y += Label(dl, g_fonts.heading, ImVec2(inner_x, y), kText,
                  done ? "All set" : (unpacking_ ? "Unpacking your disc" : "Checking your copy")) + 6;
    std::string from;
    {
      std::lock_guard lock(mutex_);
      from = done ? "Starting the game..."
                  : unpacking_ ? "From " + Shorten(source_name_, 40) + " into the game folder."
                               : "Looking at " + Shorten(source_name_, 40) + ".";
    }
    y += Label(dl, g_fonts.body, ImVec2(inner_x, y), kMuted, from.c_str(), inner_x + inner_w) + 22;

    uint64_t d = done_bytes_, t = total_bytes_;
    float f = done ? 1.0f : (t ? float(double(d) / double(t)) : 0.0f);
    dl->AddRectFilled(ImVec2(inner_x, y), ImVec2(inner_x + inner_w, y + 6), Rgb(0x242931), 3.0f);
    if (f > 0.0f)
      dl->AddRectFilled(ImVec2(inner_x, y), ImVec2(inner_x + std::max(6.0f, inner_w * f), y + 6), kBlue,
                        3.0f);
    y += 6 + 10;
    std::string file, amount;
    {
      std::lock_guard lock(mutex_);
      file = current_file_.empty() ? "Reading your copy" : current_file_;
    }
    amount = t ? Megabytes(d) + " of " + Megabytes(t) : "";
    Label(dl, g_fonts.note, ImVec2(inner_x, y), kMuted, file.c_str());
    ImVec2 as = TextSize(g_fonts.note, amount.c_str());
    Label(dl, g_fonts.note, ImVec2(inner_x + inner_w - as.x, y), kMuted, amount.c_str());
    y += as.y + 22;
    dl->AddLine(ImVec2(inner_x, y), ImVec2(inner_x + inner_w, y), kLine);
    y += 18;
    y += Label(dl, g_fonts.note, ImVec2(inner_x, y), kMuted,
                  "Keep this window open. Unpacking a disc image takes a few minutes; a folder is ready right away.", inner_x + inner_w);

    if (done && !ready_sent_) {
      ready_sent_ = true;
      if (on_ready_) {
        fs::path folder;
        {
          std::lock_guard lock(mutex_);
          folder = game_folder_;
        }
        post_([this, folder] { on_ready_(folder); });
      }
    }
  }

  y += pad;
  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled(ImVec2(card_x, top), ImVec2(card_x + card_w, y), kPanel, 14.0f);
  dl->AddRect(ImVec2(card_x, top), ImVec2(card_x + card_w, y), kLine, 14.0f);
  dl->ChannelsMerge();

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

}  // namespace mlb
