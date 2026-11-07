// MLB 2K7 Recomp - app

#include "generated/default/MLB2K7_init.h"

#include "mlb_app.h"
#include "display.h"
#include "fps_counter.h"
#include "gpu_fix.h"
#include "platform.h"
#include "xmp_music.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <system_error>

REXCVAR_DEFINE_BOOL(prepare, false, "MLB2K7",
                    "Unpack the game from --game and exit (used by RecompLauncher's setup)");
REXCVAR_DEFINE_STRING(game, "", "MLB2K7",
                      "Your copy of the game: its disc image (.iso) or the folder with default.xex");

namespace {
constexpr const char* kTitle = "MLB 2K7 Recomp";

// The game folder, in this order: --game (RecompLauncher passes the player's copy),
// game_data_root in the settings file, then a disc image unpacked into "game" next to
// the exe. The settings file is read by hand because it's loaded only after the paths
// are set.
std::filesystem::path FindGameFolder(const std::filesystem::path& config_path) {
  if (auto from_launcher = mlb::GameFolderFor(REXCVAR_GET(game)); !from_launcher.empty())
    return from_launcher;
  std::error_code ec;
  std::ifstream in(config_path);
  std::string line;
  static const std::regex key(R"(^\s*game_data_root\s*=\s*['"](.*)['"]\s*$)");
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::smatch m;
    if (std::regex_match(line, m, key)) {
      std::string v = m[1].str();
      const bool basic = line[size_t(m.position(1)) - 1] == '"';  // "..." has escapes, '...' doesn't
      std::string out;
      for (size_t i = 0; i < v.size(); ++i) {
        if (basic && v[i] == '\\' && i + 1 < v.size()) ++i;
        out += v[i];
      }
      std::filesystem::path p(reinterpret_cast<const char8_t*>(out.c_str()));
      if (std::filesystem::is_regular_file(p / "default.xex", ec)) return p;
      break;
    }
  }
  if (mlb::GameInstalled()) return mlb::GameDir();
  return {};
}

// Settings that differ from the SDK's defaults, used when the settings file doesn't say.
void SetDefault(const char* name, const char* value) {
  if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault)
    rex::cvar::SetFlagByName(name, value);
}
}  // namespace

void mlb::FpsOverlay::OnDraw(ImGuiIO& io) {
  (void)io;
  if (!FpsCounterEnabled()) return;
  auto stats = GetFrameStats();
  ImGui::SetNextWindowPos(ImVec2(12, 12));
  ImGui::SetNextWindowBgAlpha(0.45f);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##mlb_fps", nullptr, flags)) {
    if (stats.frame_count > 0)
      ImGui::Text("%.0f FPS  %.1f ms", stats.fps, stats.frame_time_ms);
    else
      ImGui::Text("-- FPS");
  }
  ImGui::End();
}

std::unique_ptr<rex::ui::WindowedApp> MlbApp::Create(rex::ui::WindowedAppContext& ctx) {
  // Headless setup step: no window, just unpack and report progress.
  if (REXCVAR_GET(prepare)) std::exit(mlb::RunPrepare(REXCVAR_GET(game)));
  // A crash report next to the exe (logs\crash.txt), to send with a bug report.
  std::error_code ec;
  std::filesystem::create_directories(rex::filesystem::GetExecutableFolder() / "logs", ec);
  mlb::InstallCrashReporter(rex::filesystem::GetExecutableFolder() / "logs" / "crash.txt");
  return std::unique_ptr<MlbApp>(new MlbApp(ctx, "MLB2K7Recomp", PPCImageConfig));
}

void MlbApp::OnConfigurePaths(rex::PathConfig& paths) {
  mlb::SetConfigPath(paths.config_path);
  if (paths.game_data_root.empty()) paths.game_data_root = FindGameFolder(paths.config_path);
  // Shader cache next to the exe, so Documents\MLB2K7Recomp holds only saves.
  if (paths.cache_root == paths.user_data_root / "cache")
    paths.cache_root = rex::filesystem::GetExecutableFolder() / "cache";
}

void MlbApp::OnConfigureLogging(rex::LogConfig& config) {
  // Errors go straight to disk so nothing important is lost in a crash. Flushing every
  // line made the game stutter (the GPU logs a warning several times per frame).
  config.flush_level = spdlog::level::err;
}

void MlbApp::OnPostInitLogging() {
  using rex::cvar::Source;
  if (rex::cvar::GetFlagSource("gpu_plugin") == Source::kDefault)
    rex::cvar::SetFlagByName("gpu_plugin", "xenos");
  // What this game needs to look right and hold 60:
  SetDefault("occlusion_query_enable", "false");  // don't stall on visibility checks
  SetDefault("direct_host_resolve", "false");     // the direct path hangs at the first loading screen
  SetDefault("use_fuzzy_alpha_epsilon", "true");
  // Looks: 1440p, 16x filtering, sharpened output.
  SetDefault("resolution_scale", "2");
  SetDefault("anisotropic_override", "5");
  SetDefault("present_effect", "cas");
  SetDefault("vsync", "true");
  if (const char* mode = std::getenv("RECOMPLAUNCHER_MODE"); mode && std::string(mode) == "bigpicture")
    rex::cvar::SetFlagByName("fullscreen", "true");
}

void MlbApp::OnPreSetup(rex::RuntimeConfig& config) { mlb::ChooseGraphicsBackend(config); }

void MlbApp::OnPostSetup() { mlb::InstallGpuFixes(runtime()); }

std::optional<rex::PathConfig> MlbApp::OnFinalizePaths(const rex::PathConfig& defaults,
                                                       std::function<void(rex::PathConfig)> resume) {
  if (auto* w = window()) w->SetTitle(kTitle);
  std::error_code ec;
  if (std::filesystem::is_regular_file(defaults.game_data_root / "default.xex", ec)) {
    mlb::SetMusicFolder(defaults.game_data_root);
    return defaults;
  }

  // First start: show the setup screen in a window, then carry on once the game is ready.
  bool want_fullscreen = window() && window()->IsFullscreen();
  if (want_fullscreen) window()->SetFullscreen(false);

  auto post = [this](std::function<void()> fn) { app_context().CallInUIThreadDeferred(std::move(fn)); };
  auto on_ready = [this, paths = defaults, resume, want_fullscreen](std::filesystem::path folder) mutable {
    paths.game_data_root = folder;
    mlb::SetMusicFolder(folder);
    if (want_fullscreen && window()) window()->SetFullscreen(true);
    // Drop the setup screen on the next UI tick, after it has finished drawing.
    app_context().CallInUIThreadDeferred([this] { setup_dialog_.reset(); });
    resume(paths);
  };
  auto on_quit = [this] { app_context().QuitFromUIThread(); };

  setup_dialog_ = std::make_unique<mlb::SetupDialog>(imgui_drawer(), immediate_drawer(), post,
                                                     on_ready, on_quit);
  std::string from_launcher = REXCVAR_GET(game);
  if (!from_launcher.empty()) setup_dialog_->Start(from_launcher);
  return std::nullopt;
}

void MlbApp::OnFileDrop(rex::ui::FileDropEvent& e) {
  if (setup_dialog_ && !setup_dialog_->busy()) {
    auto p = e.filename().u8string();
    setup_dialog_->Start(std::string(p.begin(), p.end()));
  }
}

void MlbApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  fps_overlay_ = std::make_unique<mlb::FpsOverlay>(drawer);
  SetGuestFrameStats([] { return mlb::GetFrameStats(); });
  rex::ui::RegisterBind("bind_toggle_window", "Escape", "Switch between fullscreen and windowed",
                        [this] {
                          if (auto* w = window()) w->SetFullscreen(!w->IsFullscreen());
                        });
}

void MlbApp::OnShutdown() {
  rex::ui::UnregisterBind("bind_toggle_window");
  setup_dialog_.reset();
  fps_overlay_.reset();
}
