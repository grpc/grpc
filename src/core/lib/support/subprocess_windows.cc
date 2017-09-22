/*
 *
 * Copyright 2016 gRPC authors.
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
