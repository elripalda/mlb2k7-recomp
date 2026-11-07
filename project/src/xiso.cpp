// MLB 2K7 Recomp - Xbox 360 disc image (XDVDFS) reader
//
// An Xbox 360 game disc holds a small video partition, then the game partition.
// The game partition is XDVDFS: sector 32 has "MICROSOFT*XBOX*MEDIA", then the root
// directory's sector and size. A directory is a run of entries:
//   u16 left, u16 right, u32 first sector, u32 size, u8 attributes, u8 name length, name
// padded to 4 bytes; 0xFFFF fills the rest of a sector.

#include "xiso.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

namespace mlb {
namespace fs = std::filesystem;

namespace {

constexpr uint64_t kSector = 2048;
// Where the game partition starts: plain XDVDFS, XGD3, XGD2, XGD1.
constexpr std::array<uint64_t, 4> kPartitions = {0, 0x2080000, 0xFD90000, 0x18300000};

uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct Entry {
  fs::path path;  // relative to the disc's root
  uint64_t offset = 0;
  uint64_t size = 0;
  bool dir = false;
};

class Image {
 public:
  explicit Image(const fs::path& p) : f_(p, std::ios::binary) {}
  bool ok() const { return bool(f_); }

  bool Read(uint64_t at, void* out, size_t n) {
    f_.clear();
    f_.seekg(std::streamoff(at));
    f_.read(static_cast<char*>(out), std::streamsize(n));
    return size_t(f_.gcount()) == n;
  }

  // Finds the game partition. Returns false if this isn't an Xbox disc image.
  bool Open() {
    for (uint64_t part : kPartitions) {
      uint8_t d[28];
      if (!Read(part + 32 * kSector, d, sizeof(d))) continue;
      if (std::memcmp(d, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        base_ = part;
        root_sector_ = U32(d + 20);
        root_size_ = U32(d + 24);
        return true;
      }
    }
    return false;
  }

  bool List(std::vector<Entry>* out) { return ListDir(root_sector_, root_size_, {}, 0, out); }

  // Names in the top folder, lower case.
  std::vector<std::string> RootNames() {
    std::vector<Entry> all;
    std::vector<std::string> names;
    if (!ListDir(root_sector_, root_size_, {}, 31, &all)) return names;
    for (auto& e : all) names.push_back(Lower(e.path.filename().string()));
    return names;
  }

  std::ifstream& stream() { return f_; }

 private:
  bool ListDir(uint64_t sector, uint64_t size, const fs::path& dir, int depth, std::vector<Entry>* out) {
    if (depth > 32 || size == 0) return true;
    if (size > (64u << 20)) return false;
    std::vector<uint8_t> t(size);
    if (!Read(base_ + sector * kSector, t.data(), t.size())) return false;
    size_t pos = 0;
    while (pos + 14 <= t.size()) {
      if (t[pos] == 0xFF && t[pos + 1] == 0xFF) {
        pos = (pos / kSector + 1) * kSector;
        continue;
      }
      size_t name_len = t[pos + 13];
      if (name_len == 0 || pos + 14 + name_len > t.size()) break;
      uint32_t start = U32(&t[pos + 4]);
      uint32_t len = U32(&t[pos + 8]);
      uint8_t attr = t[pos + 12];
      std::string name(reinterpret_cast<const char*>(&t[pos + 14]), name_len);
      bool safe = name != "." && name != ".." &&
                  name.find_first_of("/\\:") == std::string::npos;
      for (char c : name) safe = safe && static_cast<unsigned char>(c) >= 32;
      if (safe) {
        Entry e;
        e.path = dir / name;
        if (attr & 0x10) {
          e.dir = true;
          out->push_back(e);
          if (depth < 31 && !ListDir(start, len, e.path, depth + 1, out)) return false;
        } else {
          e.offset = base_ + uint64_t(start) * kSector;
          e.size = len;
          out->push_back(e);
        }
      }
      pos = (pos + 14 + name_len + 3) & ~size_t(3);
    }
    return true;
  }

  std::ifstream f_;
  uint64_t base_ = 0;
  uint64_t root_sector_ = 0;
  uint64_t root_size_ = 0;
};

bool HasFile(const fs::path& dir, const char* name) {
  std::error_code ec;
  if (fs::is_regular_file(dir / name, ec)) return true;
  // Case doesn't matter on the disc.
  std::string want = Lower(name);
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (Lower(it->path().filename().string()) == want && it->is_regular_file(ec)) return true;
  }
  return false;
}

bool IsGameFolder(const fs::path& dir) { return HasFile(dir, "default.xex"); }

}  // namespace

CopyInfo IdentifyCopy(const fs::path& input) {
  CopyInfo info;
  std::error_code ec;
  if (fs::is_directory(input, ec)) {
    fs::path found;
    if (IsGameFolder(input)) {
      found = input;
    } else {
      // A folder one level up, like the one a dump tool makes around the game.
      for (auto it = fs::directory_iterator(input, ec); !ec && it != fs::directory_iterator();
           it.increment(ec)) {
        if (it->is_directory(ec) && IsGameFolder(it->path())) {
          found = it->path();
          break;
        }
      }
      // Or a disc image inside it.
      if (found.empty()) {
        for (auto it = fs::directory_iterator(input, ec); !ec && it != fs::directory_iterator();
             it.increment(ec)) {
          if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".iso") {
            CopyInfo inner = IdentifyCopy(it->path());
            if (inner.kind == CopyInfo::Kind::kImage) return inner;
          }
        }
      }
    }
    if (found.empty()) {
      info.error = "That folder doesn't have the game in it. Pick the folder with default.xex, or your disc image (.iso).";
      return info;
    }
    if (!HasFile(found, kMarkerFile)) {
      info.error = "That's a different game's folder. Pick your copy of Major League Baseball 2K7.";
      return info;
    }
    info.kind = CopyInfo::Kind::kFolder;
    info.path = found;
    return info;
  }

  if (!fs::is_regular_file(input, ec)) {
    info.error = "That file couldn't be opened.";
    return info;
  }
  std::string ext = Lower(input.extension().string());
  if (ext == ".zip" || ext == ".7z" || ext == ".rar") {
    info.error = "That's a compressed file. Unpack it first, then pick the .iso inside it.";
    return info;
  }
  if (Lower(input.filename().string()) == "default.xex") return IdentifyCopy(input.parent_path());

  Image image(input);
  if (!image.ok() || !image.Open()) {
    info.error = "That isn't an Xbox 360 disc image. Use a full, uncompressed .iso of your disc.";
    return info;
  }
  auto names = image.RootNames();
  bool has_xex = std::find(names.begin(), names.end(), "default.xex") != names.end();
  bool has_marker = std::find(names.begin(), names.end(), Lower(kMarkerFile)) != names.end();
  if (!has_xex) {
    info.error = "That disc image can't be read: it may be incomplete or compressed.";
    return info;
  }
  if (!has_marker) {
    info.error = "That's a different game's disc image. Pick your copy of Major League Baseball 2K7.";
    return info;
  }
  info.kind = CopyInfo::Kind::kImage;
  info.path = input;
  return info;
}

bool ExtractImage(const fs::path& image_path, const fs::path& out_dir, const ProgressFn& progress,
                  std::string* error) {
  Image image(image_path);
  if (!image.ok() || !image.Open()) {
    *error = "That isn't an Xbox 360 disc image.";
    return false;
  }
  std::vector<Entry> entries;
  if (!image.List(&entries)) {
    *error = "The disc image's file list couldn't be read. It may be incomplete.";
    return false;
  }
  uint64_t total = 0;
  for (auto& e : entries) total += e.size;

  std::error_code ec;
  fs::create_directories(out_dir, ec);
  if (ec) {
    *error = "Couldn't make the game folder: " + ec.message();
    return false;
  }
  auto space = fs::space(out_dir, ec);
  if (!ec && space.available < total + (64u << 20)) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "There isn't enough free space. The game needs %.1f GB on this drive.",
                  double(total) / (1024.0 * 1024.0 * 1024.0));
    *error = buf;
    return false;
  }

  constexpr size_t kChunk = 8u << 20;
  std::unique_ptr<char[]> buffer(new char[kChunk]);
  uint64_t done = 0;
  auto& in = image.stream();
  for (auto& e : entries) {
    fs::path dest = out_dir / e.path;
    if (e.dir) {
      fs::create_directories(dest, ec);
      continue;
    }
    fs::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
      *error = "Couldn't write " + e.path.generic_string() + ".";
      return false;
    }
    std::string name = e.path.generic_string();
    in.clear();
    in.seekg(std::streamoff(e.offset));
    uint64_t left = e.size;
    while (left > 0) {
      size_t n = size_t(std::min<uint64_t>(left, kChunk));
      in.read(buffer.get(), std::streamsize(n));
      if (size_t(in.gcount()) != n) {
        *error = "The disc image ended early. It may be incomplete.";
        return false;
      }
      out.write(buffer.get(), std::streamsize(n));
      if (!out) {
        *error = "Couldn't write " + name + ". The drive may be full.";
        return false;
      }
      left -= n;
      done += n;
      if (progress) progress(done, total, name);
    }
    if (e.size == 0 && progress) progress(done, total, name);
  }
  return true;
}

}  // namespace mlb
