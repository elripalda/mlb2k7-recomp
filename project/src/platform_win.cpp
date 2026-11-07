// MLB 2K7 Recomp - Windows-only helpers

#include "platform.h"

#include <csignal>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <shobjidl.h>
#endif

namespace mlb {

#if defined(_WIN32)
namespace {
std::wstring g_file;

void Line(FILE* f, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
}

FILE* Open() { return _wfopen(g_file.c_str(), L"a"); }

void WriteFrame(FILE* f, int i, DWORD64 addr) {
  HMODULE mod = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod)) {
    char full[MAX_PATH];
    if (GetModuleFileNameA(mod, full, MAX_PATH)) {
      const char* base = strrchr(full, '\\');
      snprintf(name, sizeof(name), "%s", base ? base + 1 : full);
    }
  }
  Line(f, "  #%02d %s+0x%llX", i, name,
       static_cast<unsigned long long>(mod ? addr - reinterpret_cast<DWORD64>(mod) : addr));
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
  FILE* f = Open();
  if (!f) return EXCEPTION_CONTINUE_SEARCH;
  SYSTEMTIME t;
  GetLocalTime(&t);
  Line(f, "=== CRASH %04d-%02d-%02d %02d:%02d:%02d ===", t.wYear, t.wMonth, t.wDay, t.wHour,
       t.wMinute, t.wSecond);
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  Line(f, "Exception 0x%08lX at:", er->ExceptionCode);
  WriteFrame(f, 0, reinterpret_cast<DWORD64>(er->ExceptionAddress));
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    Line(f, "Access violation: %s address 0x%llX", er->ExceptionInformation[0] ? "writing" : "reading",
         static_cast<unsigned long long>(er->ExceptionInformation[1]));

  CONTEXT ctx = *ep->ContextRecord;
  Line(f, "RIP=%016llX RSP=%016llX RCX=%016llX RDX=%016llX R8=%016llX R9=%016llX",
       ctx.Rip, ctx.Rsp, ctx.Rcx, ctx.Rdx, ctx.R8, ctx.R9);
  Line(f, "Call stack:");
  HANDLE proc = GetCurrentProcess();
  SymInitialize(proc, nullptr, TRUE);
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  for (int i = 0; i < 48; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset)
      break;
    WriteFrame(f, i, frame.AddrPC.Offset);
  }
  Line(f, "");
  fclose(f);
  return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int) {
  if (FILE* f = Open()) {
    Line(f, "=== abort() called ===");
    fclose(f);
  }
}

}  // namespace

namespace {
std::string WideToUtf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
}  // namespace

std::string BrowseForCopy(bool folder) {
  std::string result;
  HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) options |= FOS_PICKFOLDERS;
    dialog->SetOptions(options);
    if (!folder) {
      const COMDLG_FILTERSPEC types[] = {{L"Xbox 360 disc image (*.iso)", L"*.iso"},
                                         {L"All files", L"*.*"}};
      dialog->SetFileTypes(2, types);
    }
    dialog->SetTitle(folder ? L"Choose the folder with default.xex"
                            : L"Choose your Major League Baseball 2K7 disc image");
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          result = WideToUtf8(path);
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return result;
}

void WriteStdout(const std::string& text) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out && out != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(out, text.data(), DWORD(text.size()), &written, nullptr);
    return;
  }
  std::fputs(text.c_str(), stdout);
  std::fflush(stdout);
}

void* FindRuntimeExport(const char* name) {
  HMODULE m = GetModuleHandleW(L"rexruntime.dll");
  return m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr;
}

void InstallCrashReporter(const std::filesystem::path& file) {
  g_file = file.wstring();
  SetUnhandledExceptionFilter(OnCrash);
  std::signal(SIGABRT, OnAbort);
}
#else
void InstallCrashReporter(const std::filesystem::path&) {}
void* FindRuntimeExport(const char*) { return nullptr; }
std::string BrowseForCopy(bool) { return {}; }
void WriteStdout(const std::string& text) {
  std::fputs(text.c_str(), stdout);
  std::fflush(stdout);
}
#endif

}  // namespace mlb
