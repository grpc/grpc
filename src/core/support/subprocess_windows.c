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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_SUBPROCESS

#include <windows.h>
#include <string.h>

#include <grpc/support/subprocess.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

struct gpr_subprocess {
  PROCESS_INFORMATION pi;
  int joined;
};

const char *gpr_subprocess_binary_extension() { return ".exe"; }

gpr_subprocess *gpr_subprocess_create(int argc, const char **argv) {
  gpr_subprocess *r;
  char **exec_args;

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  memset(&si, 0, sizeof(si);
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi);

  pi.dwCreationFlags = CREATE_NEW_PROCESS_GROUP;

  if( !CreateProcess( NULL, // No module name (use command line)
      argv[0],              // Command line
      NULL,                 // Process handle not inheritable
      NULL,                 // Thread handle not inheritable
      FALSE,                // Set handle inheritance to FALSE
      0,                    // No creation flags
      NULL,                 // Use parent's environment block
      NULL,                 // Use parent's starting directory
      &si,                  // Pointer to STARTUPINFO structure
      &pi ))                // Pointer to PROCESS_INFORMATION structure
  {
    return NULL;
  }

  r = gpr_malloc(sizeof(gpr_subprocess));
  memset(r, 0, sizeof(*r));
  r->pi = pi;
  return r;
}

void gpr_subprocess_destroy(gpr_subprocess *p) {
  if (!p->joined) {
    gpr_subprocess_interrupt();
    gpr_subprocess_join(p);
  }

  CloseHandle( p->pi.hProcess );
  CloseHandle( p->pi.hThread );
  gpr_free(p);
}

int gpr_subprocess_join(gpr_subprocess *p) {
  if(WaitForSingleObject(pi->hProcess, INFINITE) == WAIT_OBJECT_0) {
    return 0;
  }
  return -1;
}

void gpr_subprocess_interrupt(gpr_subprocess *p) {
  if (!p->joined) {
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, p->pi.hprocess)
  }
  return;
}

#endif /* GPR_WINDOWS_SUBPROCESS */
