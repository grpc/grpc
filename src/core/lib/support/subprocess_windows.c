/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_SUBPROCESS

#include <string.h>
#include <tchar.h>
#include <windows.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/subprocess.h>
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/string_windows.h"

struct gpr_subprocess {
  PROCESS_INFORMATION pi;
  int joined;
  int interrupted;
};

const char *gpr_subprocess_binary_extension() { return ".exe"; }

gpr_subprocess *gpr_subprocess_create(int argc, const char **argv) {
  gpr_subprocess *r;

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  char *args = gpr_strjoin_sep(argv, (size_t)argc, " ", NULL);
  TCHAR *args_tchar;

  args_tchar = gpr_char_to_tchar(args);
  gpr_free(args);

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));

  if (!CreateProcess(NULL, args_tchar, NULL, NULL, FALSE,
                     CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
    gpr_free(args_tchar);
    return NULL;
  }
  gpr_free(args_tchar);

  r = gpr_malloc(sizeof(gpr_subprocess));
  memset(r, 0, sizeof(*r));
  r->pi = pi;
  return r;
}

void gpr_subprocess_destroy(gpr_subprocess *p) {
  if (p) {
    if (!p->joined) {
      gpr_subprocess_interrupt(p);
      gpr_subprocess_join(p);
    }
    if (p->pi.hProcess) {
      CloseHandle(p->pi.hProcess);
    }
    if (p->pi.hThread) {
      CloseHandle(p->pi.hThread);
    }
    gpr_free(p);
  }
}

int gpr_subprocess_join(gpr_subprocess *p) {
  DWORD dwExitCode;
  if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode)) {
    if (dwExitCode == STILL_ACTIVE) {
      if (WaitForSingleObject(p->pi.hProcess, INFINITE) == WAIT_OBJECT_0) {
        p->joined = 1;
        goto getExitCode;
      }
      return -1;  // failed to join
    } else {
      goto getExitCode;
    }
  } else {
    return -1;  // failed to get exit code
  }

getExitCode:
  if (p->interrupted) {
    return 0;
  }
  if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode)) {
    return (int)dwExitCode;
  } else {
    return -1;  // failed to get exit code
  }
}

void gpr_subprocess_interrupt(gpr_subprocess *p) {
  DWORD dwExitCode;
  if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode)) {
    if (dwExitCode == STILL_ACTIVE) {
      gpr_log(GPR_INFO, "sending ctrl-break");
      GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, p->pi.dwProcessId);
      p->joined = 1;
      p->interrupted = 1;
    }
  }
  return;
}

#endif /* GPR_WINDOWS_SUBPROCESS */
