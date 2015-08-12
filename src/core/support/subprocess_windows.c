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
#include <tchar.h>

#include <grpc/support/subprocess.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

struct gpr_subprocess {
  PROCESS_INFORMATION pi;
  int joined;
  int interrupted;	//because ctrl-c can't be sent and ctrl-break is used instead, this allows ignoring the error code
};

const char *gpr_subprocess_binary_extension() { return ".exe"; }

gpr_subprocess *gpr_subprocess_create(int argc, const char **argv) {
  gpr_subprocess *r;

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));

  //put all argv into one string
  char args_concat[1000];
  strcpy(args_concat, argv[0]);
  for (int i = 1; i < argc; i++) {
	  strcat(args_concat, " ");
	  strcat(args_concat, argv[i]);
  }

  //createprocess has buth a utf8 and a unicode version.  convert as needed
  TCHAR *tstrTo;
  int tstrLen;
#ifdef UNICODE
  tstrLen = MultiByteToWideChar(CP_UTF8, 0, args_concat, strlen(args_concat) + 1, NULL, 0);
  tstrTo = (TCHAR*)malloc(tstrLen * sizeof(TCHAR));
  MultiByteToWideChar(CP_UTF8, 0, args_concat, strlen(args_concat) + 1, tstrTo, tstrLen);
#else
  tstrTo = strdup(args_concat);
  tstrLen = strlen(tstrTo);
#endif
  
  if( !CreateProcess( NULL, // No module name (use command line)
      tstrTo,              // Command line
      NULL,                 // Process handle not inheritable
      NULL,                 // Thread handle not inheritable
      FALSE,                // Set handle inheritance to FALSE
      CREATE_NEW_PROCESS_GROUP,               // creation flags.  this is required to be able to send ctrl-break
      NULL,                 // Use parent's environment block
      NULL,                 // Use parent's starting directory
      &si,                  // Pointer to STARTUPINFO structure
      &pi ))                // Pointer to PROCESS_INFORMATION structure
  {
    free(tstrTo);
    return NULL;
  }
  free(tstrTo);

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
	if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode)){
		if (dwExitCode == STILL_ACTIVE) {
			if (WaitForSingleObject(p->pi.hProcess, INFINITE) == WAIT_OBJECT_0) {
				p->joined = 1;
				goto getExitCode;
			}
			return -1;	//failed to join
		}
		else {
			goto getExitCode;
		}
	}
	else {
		return -1;	//failed to get exit code
	}

getExitCode:
	if (p->interrupted)
	{
		return 0;
	}
	if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode))
	{
		return dwExitCode;
	}
	else
	{
		return -1;	//failed to get exit code
	}
}

void gpr_subprocess_interrupt(gpr_subprocess *p) {
  DWORD dwExitCode;
  if (GetExitCodeProcess(p->pi.hProcess, &dwExitCode)){
    if (dwExitCode == STILL_ACTIVE) {
      gpr_log(GPR_INFO, "sending ctrl-break");		//this method does not support sending ctrl-c
	  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, p->pi.dwProcessId);
	  p->joined = 1;
	  p->interrupted = 1;
    }
  }
  return;
}

#endif /* GPR_WINDOWS_SUBPROCESS */
