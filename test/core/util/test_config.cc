/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/core/util/test_config.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"

int64_t g_fixture_slowdown_factor = 1;
int64_t g_poller_slowdown_factor = 1;

#if GPR_GETPID_IN_UNISTD_H
#include <unistd.h>
static unsigned seed(void) { return (unsigned)getpid(); }
#endif

#if GPR_GETPID_IN_PROCESS_H
#include <process.h>
static unsigned seed(void) { return (unsigned)_getpid(); }
#endif

#if GPR_WINDOWS_CRASH_HANDLER
#include <windows.h>

#include <tchar.h>

// disable warning 4091 - dbghelp.h is broken for msvc2015
#pragma warning(disable : 4091)
#define DBGHELP_TRANSLATE_TCHAR
#include <dbghelp.h>

#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif

static void print_current_stack() {
  typedef USHORT(WINAPI * CaptureStackBackTraceType)(
      __in ULONG, __in ULONG, __out PVOID*, __out_opt PULONG);
  CaptureStackBackTraceType func = (CaptureStackBackTraceType)(GetProcAddress(
      LoadLibrary(_T("kernel32.dll")), "RtlCaptureStackBackTrace"));

  if (func == NULL) return;  // WOE 29.SEP.2010

// Quote from Microsoft Documentation:
// ## Windows Server 2003 and Windows XP:
// ## The sum of the FramesToSkip and FramesToCapture parameters must be less
// than 63.
#define MAX_CALLERS 62

  void* callers_stack[MAX_CALLERS];
  unsigned short frames;
  SYMBOL_INFOW* symbol;
  HANDLE process;
  process = GetCurrentProcess();
  SymInitialize(process, NULL, TRUE);
  frames = (func)(0, MAX_CALLERS, callers_stack, NULL);
  symbol =
      (SYMBOL_INFOW*)calloc(sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t), 1);
  symbol->MaxNameLen = 255;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);

  const unsigned short MAX_CALLERS_SHOWN = 32;
  frames = frames < MAX_CALLERS_SHOWN ? frames : MAX_CALLERS_SHOWN;
  for (unsigned int i = 0; i < frames; i++) {
    SymFromAddrW(process, (DWORD64)(callers_stack[i]), 0, symbol);
    fwprintf(stderr, L"*** %d: %016I64X %ls - %016I64X\n", i,
             (DWORD64)callers_stack[i], symbol->Name, (DWORD64)symbol->Address);
    fflush(stderr);
  }

  free(symbol);
}

static void print_stack_from_context(CONTEXT c) {
  STACKFRAME s;  // in/out stackframe
  memset(&s, 0, sizeof(s));
  DWORD imageType;
#ifdef _M_IX86
  // normally, call ImageNtHeader() and use machine info from PE header
  imageType = IMAGE_FILE_MACHINE_I386;
  s.AddrPC.Offset = c.Eip;
  s.AddrPC.Mode = AddrModeFlat;
  s.AddrFrame.Offset = c.Ebp;
  s.AddrFrame.Mode = AddrModeFlat;
  s.AddrStack.Offset = c.Esp;
  s.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
  imageType = IMAGE_FILE_MACHINE_AMD64;
  s.AddrPC.Offset = c.Rip;
  s.AddrPC.Mode = AddrModeFlat;
  s.AddrFrame.Offset = c.Rsp;
  s.AddrFrame.Mode = AddrModeFlat;
  s.AddrStack.Offset = c.Rsp;
  s.AddrStack.Mode = AddrModeFlat;
#elif _M_IA64
  imageType = IMAGE_FILE_MACHINE_IA64;
  s.AddrPC.Offset = c.StIIP;
  s.AddrPC.Mode = AddrModeFlat;
  s.AddrFrame.Offset = c.IntSp;
  s.AddrFrame.Mode = AddrModeFlat;
  s.AddrBStore.Offset = c.RsBSP;
  s.AddrBStore.Mode = AddrModeFlat;
  s.AddrStack.Offset = c.IntSp;
  s.AddrStack.Mode = AddrModeFlat;
#else
#error "Platform not supported!"
#endif

  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();

  SYMBOL_INFOW* symbol =
      (SYMBOL_INFOW*)calloc(sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t), 1);
  symbol->MaxNameLen = 255;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);

  while (StackWalk(imageType, process, thread, &s, &c, 0,
                   SymFunctionTableAccess, SymGetModuleBase, 0)) {
    BOOL has_symbol =
        SymFromAddrW(process, (DWORD64)(s.AddrPC.Offset), 0, symbol);
    fwprintf(
        stderr, L"*** %016I64X %ls - %016I64X\n", (DWORD64)(s.AddrPC.Offset),
        has_symbol ? symbol->Name : L"<<no symbol>>", (DWORD64)symbol->Address);
    fflush(stderr);
  }

  free(symbol);
}

static LONG crash_handler(struct _EXCEPTION_POINTERS* ex_info) {
  fprintf(stderr, "Exception handler called, dumping information\n");
  bool try_to_print_stack = true;
  PEXCEPTION_RECORD exrec = ex_info->ExceptionRecord;
  while (exrec) {
    DWORD code = exrec->ExceptionCode;
    DWORD flgs = exrec->ExceptionFlags;
    PVOID addr = exrec->ExceptionAddress;
    if (code == EXCEPTION_STACK_OVERFLOW) try_to_print_stack = false;
    fprintf(stderr, "code: %x - flags: %d - address: %p\n", code, flgs, addr);
    exrec = exrec->ExceptionRecord;
  }
  if (try_to_print_stack) {
    print_stack_from_context(*ex_info->ContextRecord);
  }
  if (IsDebuggerPresent()) {
    __debugbreak();
  } else {
    _exit(1);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

static void abort_handler(int sig) {
  fprintf(stderr, "Abort handler called.\n");
  print_current_stack();
  if (IsDebuggerPresent()) {
    __debugbreak();
  } else {
    _exit(1);
  }
}

static void install_crash_handler() {
  if (!SymInitialize(GetCurrentProcess(), NULL, TRUE)) {
    fprintf(stderr, "SymInitialize failed: %d\n", GetLastError());
  }
  SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)crash_handler);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  _set_abort_behavior(0, _CALL_REPORTFAULT);
  signal(SIGABRT, abort_handler);
}
#elif GPR_POSIX_CRASH_HANDLER
#include <errno.h>
#include <execinfo.h>
#include <grpc/support/useful.h>
#include <stdio.h>
#include <string.h>

#define SIGNAL_NAMES_LENGTH 32

static const char* const signal_names[] = {
    nullptr,   "SIGHUP",  "SIGINT",    "SIGQUIT", "SIGILL",    "SIGTRAP",
    "SIGABRT", "SIGBUS",  "SIGFPE",    "SIGKILL", "SIGUSR1",   "SIGSEGV",
    "SIGUSR2", "SIGPIPE", "SIGALRM",   "SIGTERM", "SIGSTKFLT", "SIGCHLD",
    "SIGCONT", "SIGSTOP", "SIGTSTP",   "SIGTTIN", "SIGTTOU",   "SIGURG",
    "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH",  "SIGIO",
    "SIGPWR",  "SIGSYS"};

static char g_alt_stack[GPR_MAX(MINSIGSTKSZ, 65536)];

#define MAX_FRAMES 32

/* signal safe output */
static void output_string(const char* string) {
  size_t len = strlen(string);
  ssize_t r;

  do {
    r = write(STDERR_FILENO, string, len);
  } while (r == -1 && errno == EINTR);
}

static void output_num(long num) {
  char buf[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(num, buf);
  output_string(buf);
}

static void crash_handler(int signum, siginfo_t* info, void* data) {
  void* addrlist[MAX_FRAMES + 1];
  int addrlen;

  output_string("\n\n\n*******************************\nCaught signal ");
  if (signum > 0 && signum < SIGNAL_NAMES_LENGTH) {
    output_string(signal_names[signum]);
  } else {
    output_num(signum);
  }
  output_string("\n");

  addrlen = backtrace(addrlist, GPR_ARRAY_SIZE(addrlist));

  if (addrlen == 0) {
    output_string("  no backtrace\n");
  } else {
    backtrace_symbols_fd(addrlist, addrlen, STDERR_FILENO);
  }

  /* try to get a core dump for SIGTERM */
  if (signum == SIGTERM) signum = SIGQUIT;
  raise(signum);
}

static void install_crash_handler() {
  stack_t ss;
  struct sigaction sa;

  memset(&ss, 0, sizeof(ss));
  memset(&sa, 0, sizeof(sa));
  ss.ss_size = sizeof(g_alt_stack);
  ss.ss_sp = g_alt_stack;
  GPR_ASSERT(sigaltstack(&ss, nullptr) == 0);
  sa.sa_flags = (int)(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND);
  sa.sa_sigaction = crash_handler;
  GPR_ASSERT(sigaction(SIGILL, &sa, nullptr) == 0);
  GPR_ASSERT(sigaction(SIGABRT, &sa, nullptr) == 0);
  GPR_ASSERT(sigaction(SIGBUS, &sa, nullptr) == 0);
  GPR_ASSERT(sigaction(SIGSEGV, &sa, nullptr) == 0);
  GPR_ASSERT(sigaction(SIGTERM, &sa, nullptr) == 0);
  GPR_ASSERT(sigaction(SIGQUIT, &sa, nullptr) == 0);
}
#else
static void install_crash_handler() {}
#endif

bool BuiltUnderValgrind() {
#ifdef RUNNING_ON_VALGRIND
  return true;
#else
  return false;
#endif
}

bool BuiltUnderTsan() {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef THREAD_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderAsan() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef ADDRESS_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderMsan() {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef MEMORY_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderUbsan() {
#ifdef GRPC_UBSAN
  return true;
#else
  return false;
#endif
}

int64_t grpc_test_sanitizer_slowdown_factor() {
  int64_t sanitizer_multiplier = 1;
  if (BuiltUnderValgrind()) {
    sanitizer_multiplier = 20;
  } else if (BuiltUnderTsan()) {
    sanitizer_multiplier = 5;
  } else if (BuiltUnderAsan()) {
    sanitizer_multiplier = 3;
  } else if (BuiltUnderMsan()) {
    sanitizer_multiplier = 4;
  } else if (BuiltUnderUbsan()) {
    sanitizer_multiplier = 5;
  }
  return sanitizer_multiplier;
}

int64_t grpc_test_slowdown_factor() {
  return grpc_test_sanitizer_slowdown_factor() * g_fixture_slowdown_factor *
         g_poller_slowdown_factor;
}

gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(grpc_test_slowdown_factor() * (int64_t)1e3 * time_s,
                           GPR_TIMESPAN));
}

gpr_timespec grpc_timeout_milliseconds_to_deadline(int64_t time_ms) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_micros(grpc_test_slowdown_factor() * (int64_t)1e3 * time_ms,
                           GPR_TIMESPAN));
}

void grpc_test_init(int argc, char** argv) {
  install_crash_handler();
  { /* poll-cv poll strategy runs much more slowly than anything else */
    char* s = gpr_getenv("GRPC_POLL_STRATEGY");
    if (s != nullptr && 0 == strcmp(s, "poll-cv")) {
      g_poller_slowdown_factor = 5;
    }
    gpr_free(s);
  }
  gpr_log(GPR_DEBUG,
          "test slowdown factor: sanitizer=%" PRId64 ", fixture=%" PRId64
          ", poller=%" PRId64 ", total=%" PRId64,
          grpc_test_sanitizer_slowdown_factor(), g_fixture_slowdown_factor,
          g_poller_slowdown_factor, grpc_test_slowdown_factor());
  /* seed rng with pid, so we don't end up with the same random numbers as a
     concurrently running test binary */
  srand(seed());
}
