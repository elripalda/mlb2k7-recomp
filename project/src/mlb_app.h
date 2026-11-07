// MLB 2K7 Recomp - app

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>

#include <imgui.h>

#include <memory>
#include <optional>

#include "setup.h"

namespace mlb {
// Small frame counter in the top-left corner (show_fps in MLB2K7Recomp.toml).
class FpsOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override;
};
}  // namespace mlb

class MlbApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx);

  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnConfigureLogging(rex::LogConfig& config) override;
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostSetup() override;
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnShutdown() override;
  // Segoe UI for the setup screen (the debug overlays keep the SDK's font).
  void OnConfigureFonts(ImFontAtlas* atlas) override { mlb::LoadSetupFonts(atlas); }
  // A disc image or folder dropped on the window during setup.
  void OnFileDrop(rex::ui::FileDropEvent& e) override;

 private:
  std::unique_ptr<mlb::FpsOverlay> fps_overlay_;
  std::unique_ptr<mlb::SetupDialog> setup_dialog_;
};
