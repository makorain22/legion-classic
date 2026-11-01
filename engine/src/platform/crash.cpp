// Slave Engine - crash reports. See crash.hpp.
#include "slave/platform/crash.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

#include "slave/core/log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// dbghelp after windows.h
#include <dbghelp.h>
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace slave::crash {

namespace {
char gDir[1024] = "";
char gVersion[64] = "";
char gNote[256] = "";
volatile long gEntered = 0;

FILE* openReport() {
  char path[1200];
  std::snprintf(path, sizeof path, "%slegion-crash.txt", gDir);
  return std::fopen(path, "w");
}

void both(FILE* f, const char* line) {
  if (f) std::fputs(line, f);
  SLV_ERROR("%s", line);
}

#ifdef _WIN32
void moduleOffset(DWORD64 addr, char* out, size_t n) {
  HMODULE mod = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod) &&
      mod) {
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = std::strrchr(name, '\\');
    std::snprintf(out, n, "%s+0x%llx", base ? base + 1 : name,
                  static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(mod)));
  } else {
    std::snprintf(out, n, "0x%llx", static_cast<unsigned long long>(addr));
  }
}

void report(const char* what, CONTEXT* ctx, EXCEPTION_POINTERS* ep) {
  if (InterlockedExchange(&gEntered, 1)) return;
  FILE* f = openReport();
  char line[512];
  std::snprintf(line, sizeof line, "Legion Classic %s crashed: %s\n", gVersion, what);
  both(f, line);
  if (gNote[0]) {
    std::snprintf(line, sizeof line, "While: %s\n", gNote);
    both(f, line);
  }
  if (ctx) {
    // Walk the faulting thread's stack.
    HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool syms = SymInitialize(proc, nullptr, TRUE) != FALSE;
    CONTEXT c = *ctx;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = c.Rip;
    sf.AddrFrame.Offset = c.Rbp;
    sf.AddrStack.Offset = c.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48; ++i) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &c, nullptr, SymFunctionTableAccess64,
                       SymGetModuleBase64, nullptr) ||
          sf.AddrPC.Offset == 0)
        break;
      char where[400];
      moduleOffset(sf.AddrPC.Offset, where, sizeof where);
      char symbol[sizeof(SYMBOL_INFO) + 256] = {};
      auto* si = reinterpret_cast<SYMBOL_INFO*>(symbol);
      si->SizeOfStruct = sizeof(SYMBOL_INFO);
      si->MaxNameLen = 255;
      DWORD64 disp = 0;
      if (syms && SymFromAddr(proc, sf.AddrPC.Offset, &disp, si))
        std::snprintf(line, sizeof line, "  #%02d %s  %s+0x%llx\n", i, where, si->Name, static_cast<unsigned long long>(disp));
      else
        std::snprintf(line, sizeof line, "  #%02d %s\n", i, where);
      both(f, line);
    }
  }
  if (f) std::fclose(f);
  if (ep) {
    char path[1200];
    std::snprintf(path, sizeof path, "%slegion-crash.dmp", gDir);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpNormal, &mei, nullptr, nullptr);
      CloseHandle(h);
    }
  }
  char msg[1600];
  std::snprintf(msg, sizeof msg,
                "Legion Classic crashed (%s).\n\nA report was saved to:\n%slegion-crash.txt\n(and legion.log, "
                "legion-crash.dmp)\n\nPlease send those files.",
                what, gDir);
  MessageBoxA(nullptr, msg, "Legion Classic", MB_OK | MB_ICONERROR);
}

LONG WINAPI onException(EXCEPTION_POINTERS* ep) {
  char what[96];
  const DWORD code = ep->ExceptionRecord->ExceptionCode;
  const char* name = code == EXCEPTION_ACCESS_VIOLATION       ? "access violation"
                     : code == EXCEPTION_STACK_OVERFLOW       ? "stack overflow"
                     : code == EXCEPTION_INT_DIVIDE_BY_ZERO   ? "division by zero"
                     : code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ? "array bounds"
                     : code == 0xE06D7363                     ? "C++ exception"
                                                              : "exception";
  if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    std::snprintf(what, sizeof what, "%s %s 0x%llx", name, ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                  static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
  else
    std::snprintf(what, sizeof what, "%s (0x%08lx)", name, static_cast<unsigned long>(code));
  report(what, ep->ContextRecord, ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

void reportHere(const char* what) {
  CONTEXT ctx{};
  RtlCaptureContext(&ctx);
  report(what, &ctx, nullptr);
}
#else
void reportHere(const char* what) {
  if (__sync_lock_test_and_set(&gEntered, 1)) return;
  FILE* f = openReport();
  char line[512];
  std::snprintf(line, sizeof line, "Legion Classic %s crashed: %s\n", gVersion, what);
  both(f, line);
  if (gNote[0]) {
    std::snprintf(line, sizeof line, "While: %s\n", gNote);
    both(f, line);
  }
  void* frames[48];
  const int n = backtrace(frames, 48);
  if (f) {
    std::fflush(f);
    backtrace_symbols_fd(frames, n, fileno(f));
    std::fclose(f);
  }
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
}

void onSignal(int sig) {
  reportHere(sig == SIGSEGV ? "segmentation fault" : sig == SIGFPE ? "arithmetic error" : sig == SIGILL ? "illegal instruction" : "abort");
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
#endif

void onTerminate() {
  const char* what = "uncaught exception";
  static char buf[256];
  if (std::exception_ptr ex = std::current_exception()) {
    try {
      std::rethrow_exception(ex);
    } catch (const std::exception& e) {
      std::snprintf(buf, sizeof buf, "uncaught exception: %s", e.what());
      what = buf;
    } catch (...) {
    }
  } else {
    what = "std::terminate";
  }
  reportHere(what);
  std::abort();
}
}  // namespace

void install(const std::string& dir, const char* version) {
  std::snprintf(gDir, sizeof gDir, "%s", dir.c_str());
  std::snprintf(gVersion, sizeof gVersion, "%s", version);
  std::set_terminate(onTerminate);
#ifdef _WIN32
  SetUnhandledExceptionFilter(onException);
  ULONG guarantee = 64 * 1024;  // room to write the report after a stack overflow
  SetThreadStackGuarantee(&guarantee);
#else
  for (int sig : {SIGSEGV, SIGFPE, SIGILL, SIGABRT}) std::signal(sig, onSignal);
#endif
}

void note(const char* what) { std::snprintf(gNote, sizeof gNote, "%s", what ? what : ""); }

}  // namespace slave::crash
