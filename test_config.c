/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test_config.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_LIB_SUPPORT_STRING_H
#define GRPC_CORE_LIB_SUPPORT_STRING_H

#include <stddef.h>

#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* String utility functions */

/* Flags for gpr_dump function. */
#define GPR_DUMP_HEX 0x00000001
#define GPR_DUMP_ASCII 0x00000002

/* Converts array buf, of length len, into a C string  according to the flags.
   Result should be freed with gpr_free() */
char *gpr_dump(const char *buf, size_t len, uint32_t flags);

/* Calls gpr_dump on a slice. */
char *gpr_dump_slice(gpr_slice slice, uint32_t flags);

/* Parses an array of bytes into an integer (base 10). Returns 1 on success,
   0 on failure. */
int gpr_parse_bytes_to_uint32(const char *data, size_t length,
                              uint32_t *result);

/* Minimum buffer size for calling ltoa */
#define GPR_LTOA_MIN_BUFSIZE (3 * sizeof(long))

/* Convert a long to a string in base 10; returns the length of the
   output string (or 0 on failure).
   output must be at least GPR_LTOA_MIN_BUFSIZE bytes long. */
int gpr_ltoa(long value, char *output);

/* Minimum buffer size for calling int64toa */
#define GPR_INT64TOA_MIN_BUFSIZE (3 * sizeof(int64_t))

/* Convert an int64 to a string in base 10; returns the length of the
output string (or 0 on failure).
output must be at least GPR_INT64TOA_MIN_BUFSIZE bytes long.
NOTE: This function ensures sufficient bit width even on Win x64,
where long is 32bit is size.*/
int int64_ttoa(int64_t value, char *output);

/* Reverse a run of bytes */
void gpr_reverse_bytes(char *str, int len);

/* Join a set of strings, returning the resulting string.
   Total combined length (excluding null terminator) is returned in total_length
   if it is non-null. */
char *gpr_strjoin(const char **strs, size_t nstrs, size_t *total_length);

/* Join a set of strings using a separator, returning the resulting string.
   Total combined length (excluding null terminator) is returned in total_length
   if it is non-null. */
char *gpr_strjoin_sep(const char **strs, size_t nstrs, const char *sep,
                      size_t *total_length);

/** Split \a str by the separator \a sep. Results are stored in \a dst, which
 * should be a properly initialized instance. */
void gpr_slice_split(gpr_slice str, const char *sep, gpr_slice_buffer *dst);

/* A vector of strings... for building up a final string one piece at a time */
typedef struct {
    char **strs;
    size_t count;
    size_t capacity;
} gpr_strvec;

/* Initialize/destroy */
void gpr_strvec_init(gpr_strvec *strs);
void gpr_strvec_destroy(gpr_strvec *strs);
/* Add a string to a strvec, takes ownership of the string */
void gpr_strvec_add(gpr_strvec *strs, char *add);
/* Return a joined string with all added substrings, optionally setting
   total_length as per gpr_strjoin */
char *gpr_strvec_flatten(gpr_strvec *strs, size_t *total_length);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SUPPORT_STRING_H */


double g_fixture_slowdown_factor = 1.0;

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
      __in ULONG, __in ULONG, __out PVOID *, __out_opt PULONG);
  CaptureStackBackTraceType func = (CaptureStackBackTraceType)(GetProcAddress(
      LoadLibrary(_T("kernel32.dll")), "RtlCaptureStackBackTrace"));

  if (func == NULL) return;  // WOE 29.SEP.2010

// Quote from Microsoft Documentation:
// ## Windows Server 2003 and Windows XP:
// ## The sum of the FramesToSkip and FramesToCapture parameters must be less
// than 63.
#define MAX_CALLERS 62

  void *callers_stack[MAX_CALLERS];
  unsigned short frames;
  SYMBOL_INFOW *symbol;
  HANDLE process;
  process = GetCurrentProcess();
  SymInitialize(process, NULL, TRUE);
  frames = (func)(0, MAX_CALLERS, callers_stack, NULL);
  symbol =
      (SYMBOL_INFOW *)calloc(sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t), 1);
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

  SYMBOL_INFOW *symbol =
      (SYMBOL_INFOW *)calloc(sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t), 1);
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

static LONG crash_handler(struct _EXCEPTION_POINTERS *ex_info) {
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
  print_current_stack(NULL);
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

static char g_alt_stack[GPR_MAX(MINSIGSTKSZ, 65536)];

#define MAX_FRAMES 32

/* signal safe output */
static void output_string(const char *string) {
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

static void crash_handler(int signum, siginfo_t *info, void *data) {
    void *addrlist[MAX_FRAMES + 1];
    int addrlen;

    output_string("\n\n\n*******************************\nCaught signal ");
    output_num(signum);
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
    GPR_ASSERT(sigaltstack(&ss, NULL) == 0);
    sa.sa_flags = (int)(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND);
    sa.sa_sigaction = crash_handler;
    GPR_ASSERT(sigaction(SIGILL, &sa, NULL) == 0);
    GPR_ASSERT(sigaction(SIGABRT, &sa, NULL) == 0);
    GPR_ASSERT(sigaction(SIGBUS, &sa, NULL) == 0);
    GPR_ASSERT(sigaction(SIGSEGV, &sa, NULL) == 0);
    GPR_ASSERT(sigaction(SIGTERM, &sa, NULL) == 0);
    GPR_ASSERT(sigaction(SIGQUIT, &sa, NULL) == 0);
}
#else
static void install_crash_handler() {}
#endif

void grpc_test_init(int argc, char **argv) {
    install_crash_handler();
    gpr_log(GPR_DEBUG, "test slowdown: machine=%f build=%f total=%f",
            (double)GRPC_TEST_SLOWDOWN_MACHINE_FACTOR,
            (double)GRPC_TEST_SLOWDOWN_BUILD_FACTOR,
            (double)GRPC_TEST_SLOWDOWN_FACTOR);
    /* seed rng with pid, so we don't end up with the same random numbers as a
       concurrently running test binary */
    srand(seed());
}
