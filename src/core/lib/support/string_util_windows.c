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

/* Posix code for gpr snprintf support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

/* Some platforms (namely msys) need wchar to be included BEFORE
   anything else, especially strsafe.h. */
#include <wchar.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strsafe.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/string.h"

#if defined UNICODE || defined _UNICODE
LPTSTR
gpr_char_to_tchar(LPCSTR input) {
  LPTSTR ret;
  int needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
  if (needed <= 0) return NULL;
  ret = gpr_malloc((unsigned)needed * sizeof(TCHAR));
  MultiByteToWideChar(CP_UTF8, 0, input, -1, ret, needed);
  return ret;
}

LPSTR
gpr_tchar_to_char(LPCTSTR input) {
  LPSTR ret;
  int needed = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
  if (needed <= 0) return NULL;
  ret = gpr_malloc((unsigned)needed);
  WideCharToMultiByte(CP_UTF8, 0, input, -1, ret, needed, NULL, NULL);
  return ret;
}
#else
char *gpr_tchar_to_char(LPTSTR input) { return gpr_strdup(input); }

char *gpr_char_to_tchar(LPTSTR input) { return gpr_strdup(input); }
#endif

char *gpr_format_message(int messageid) {
  LPTSTR tmessage;
  char *message;
  DWORD status = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, (DWORD)messageid, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
      (LPTSTR)(&tmessage), 0, NULL);
  if (status == 0) return gpr_strdup("Unable to retrieve error string");
  message = gpr_tchar_to_char(tmessage);
  LocalFree(tmessage);
  return message;
}

#endif /* GPR_WINDOWS */
