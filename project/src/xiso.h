// MLB 2K7 Recomp - Xbox 360 disc image (XDVDFS) reader
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace mlb {

// A file that only MLB 2K7's disc has (the first song of the soundtrack), used to
// tell this game's copy from another's.
constexpr const char* kMarkerFile = "311D_P_01.wma";

// What the player picked.
struct CopyInfo {
  enum class Kind { kNone, kFolder, kImage } kind = Kind::kNone;
  std::filesystem::path path;  // the folder with default.xex, or the .iso
  std::string error;           // why it isn't usable, when kind == kNone
};

// Looks at a dropped/picked path: a folder holding default.xex (or a folder one level
// above it), or an Xbox 360 disc image of the game.
CopyInfo IdentifyCopy(const std::filesystem::path& input);

// progress(bytes done, bytes total, current file name)
using ProgressFn = std::function<void(uint64_t, uint64_t, const std::string&)>;

// Unpacks every file on the disc image into out_dir. Returns false and fills error on failure.
bool ExtractImage(const std::filesystem::path& image, const std::filesystem::path& out_dir,
                  const ProgressFn& progress, std::string* error);

}  // namespace mlb
