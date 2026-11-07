// MLB 2K7 Recomp - soundtrack player (Windows Media Foundation)
//
// Plays the game's .wma songs for the XMP hook in xmp_music.cpp. Everything runs on
// one worker thread; the public functions just queue a command and return.

#include "platform.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <oleauto.h>

namespace mlb {
namespace {

class EngineNotify : public IMFMediaEngineNotify {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
      *ppv = static_cast<IMFMediaEngineNotify*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&refs_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP EventNotify(DWORD, DWORD_PTR, DWORD) override { return S_OK; }

 private:
  LONG refs_ = 1;
};

std::mutex g_queue_mutex;
std::condition_variable g_queue_cv;
std::deque<std::function<void()>> g_queue;
bool g_started = false;

// Worker-thread state.
IMFMediaEngine* g_engine = nullptr;
std::vector<std::filesystem::path> g_songs;
std::vector<size_t> g_order;
size_t g_pos = 0;
bool g_shuffle = false;
bool g_repeat = true;
bool g_playing = false;
bool g_paused = false;
double g_volume = 1.0;
size_t g_failures = 0;

void BuildOrder() {
  g_order.resize(g_songs.size());
  std::iota(g_order.begin(), g_order.end(), size_t(0));
  if (g_shuffle) {
    static std::mt19937 rng{std::random_device{}()};
    std::shuffle(g_order.begin(), g_order.end(), rng);
  }
}

void StartCurrent() {
  if (!g_engine || g_order.empty()) return;
  BSTR url = SysAllocString(g_songs[g_order[g_pos]].wstring().c_str());
  g_engine->SetSource(url);
  SysFreeString(url);
  g_engine->SetVolume(g_volume);
  g_engine->Play();
  g_playing = true;
  g_paused = false;
}

void Advance(int step) {
  if (g_order.empty()) return;
  if (step < 0) {
    g_pos = g_pos ? g_pos - 1 : g_order.size() - 1;
  } else if (++g_pos >= g_order.size()) {
    if (!g_repeat) {
      g_playing = false;
      if (g_engine) g_engine->Pause();
      return;
    }
    BuildOrder();
    g_pos = 0;
  }
  StartCurrent();
}

void Worker() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(MFStartup(MF_VERSION))) {
    IMFMediaEngineClassFactory* factory = nullptr;
    IMFAttributes* attr = nullptr;
    auto* notify = new EngineNotify();
    if (SUCCEEDED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(MFCreateAttributes(&attr, 1)) &&
        SUCCEEDED(attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify))) {
      factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attr, &g_engine);
    }
    if (attr) attr->Release();
    if (factory) factory->Release();
    notify->Release();
  }

  for (;;) {
    std::deque<std::function<void()>> work;
    {
      std::unique_lock lock(g_queue_mutex);
      g_queue_cv.wait_for(lock, std::chrono::milliseconds(250), [] { return !g_queue.empty(); });
      work.swap(g_queue);
    }
    for (auto& fn : work) fn();
    if (g_engine && g_playing && !g_paused) {
      IMFMediaError* err = nullptr;
      if (SUCCEEDED(g_engine->GetError(&err)) && err) {
        err->Release();
        // A song that can't be played: skip it, but give up if none of them work.
        if (++g_failures >= g_order.size()) {
          g_playing = false;
        } else {
          Advance(1);
        }
      } else if (g_engine->IsEnded()) {
        g_failures = 0;
        Advance(1);
      }
    }
  }
}

void Post(std::function<void()> fn) {
  std::lock_guard lock(g_queue_mutex);
  if (!g_started) {
    g_started = true;
    std::thread(Worker).detach();
  }
  g_queue.push_back(std::move(fn));
  g_queue_cv.notify_one();
}
}  // namespace

void MusicPlay(std::vector<std::filesystem::path> songs, bool shuffle, bool repeat) {
  Post([songs = std::move(songs), shuffle, repeat]() mutable {
    g_songs = std::move(songs);
    g_shuffle = shuffle;
    g_repeat = repeat;
    g_failures = 0;
    BuildOrder();
    g_pos = 0;
    StartCurrent();
  });
}

void MusicStop() {
  Post([] {
    g_playing = false;
    g_paused = false;
    if (g_engine) g_engine->Pause();
  });
}

void MusicPause() {
  Post([] {
    if (g_playing && g_engine) {
      g_engine->Pause();
      g_paused = true;
    }
  });
}

void MusicResume() {
  Post([] {
    if (g_playing && g_paused && g_engine) {
      g_engine->Play();
      g_paused = false;
    }
  });
}

void MusicNext() {
  Post([] {
    if (g_playing) Advance(1);
  });
}

void MusicPrevious() {
  Post([] {
    if (g_playing) Advance(-1);
  });
}

void MusicSetVolume(float volume) {
  Post([volume] {
    g_volume = std::clamp(double(volume), 0.0, 1.0);
    if (g_engine) g_engine->SetVolume(g_volume);
  });
}

}  // namespace mlb

#else
namespace mlb {
void MusicPlay(std::vector<std::filesystem::path>, bool, bool) {}
void MusicStop() {}
void MusicPause() {}
void MusicResume() {}
void MusicNext() {}
void MusicPrevious() {}
void MusicSetVolume(float) {}
}  // namespace mlb
#endif
