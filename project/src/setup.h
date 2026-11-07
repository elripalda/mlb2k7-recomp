// MLB 2K7 Recomp - first-run setup
#pragma once

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct ImFont;
struct ImFontAtlas;

namespace mlb {

constexpr const char* kVersion = "v1.0.0";

// <folder next to the exe>/game: where a disc image is unpacked to.
std::filesystem::path GameDir();
// True once a disc image has been unpacked there.
bool GameInstalled();

// The settings file (MLB2K7Recomp.toml). Set once the paths are known.
void SetConfigPath(const std::filesystem::path& path);

// The game folder for a copy given with --game: a folder copy is used where it is,
// a disc image means the unpacked game folder. Empty if a disc image hasn't been
// unpacked yet or the copy isn't usable.
std::filesystem::path GameFolderFor(const std::string& copy);

// Headless setup for RecompLauncher's "prepare" step:
//   MLB2K7Recomp.exe --prepare --game <disc image or folder>
// Prints "progress <0..1> <text>" lines and returns the process exit code.
int RunPrepare(const std::string& game_path);

// Exit codes (also listed in recomplauncher.json)
constexpr int kExitNotTheGame = 2;
constexpr int kExitUnpackFailed = 3;

// Segoe UI at the sizes the setup screen uses. Call from OnConfigureFonts.
void LoadSetupFonts(ImFontAtlas* atlas);

// First-run screen: asks for the player's copy, unpacks a disc image if needed, then
// calls on_ready with the game folder.
class SetupDialog : public rex::ui::ImGuiDialog {
 public:
  using Post = std::function<void(std::function<void()>)>;
  using Ready = std::function<void(std::filesystem::path)>;

  SetupDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, Post post_to_ui,
              Ready on_ready, std::function<void()> on_quit);
  ~SetupDialog() override;

  // Starts from a path (dropped on the window, picked, or --game).
  void Start(const std::string& path);
  bool busy() const { return state_ == State::kWorking || state_ == State::kDone; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class State { kPick, kWorking, kError, kDone };

  void Pick(bool folder);

  Post post_;
  Ready on_ready_;
  std::function<void()> on_quit_;
  std::unique_ptr<rex::ui::ImmediateTexture> icon_;
  std::thread worker_;
  std::atomic<State> state_{State::kPick};
  std::atomic<uint64_t> done_bytes_{0};
  std::atomic<uint64_t> total_bytes_{0};
  std::mutex mutex_;
  std::string current_file_;
  std::string source_name_;
  std::string error_;
  std::filesystem::path game_folder_;
  bool unpacking_ = false;
  bool ready_sent_ = false;
};

}  // namespace mlb
