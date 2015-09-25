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

/* Posix code for gpr snprintf support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WIN32

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/support/string.h"

int gpr_asprintf(char **strp, const char *format, ...) {
  va_list args;
  int ret;
  size_t strp_buflen;

  /* Determine the length. */
  va_start(args, format);
  ret = _vscprintf(format, args);
  va_end(args);
  if (ret < 0) {
    *strp = NULL;
    return -1;
  }

  /* Allocate a new buffer, with space for the NUL terminator. */
  strp_buflen = (size_t)ret + 1;
  if ((*strp = gpr_malloc(strp_buflen)) == NULL) {
    /* This shouldn't happen, because gpr_malloc() calls abort(). */
    return -1;
  }

  /* Print to the buffer. */
  va_start(args, format);
  ret = vsnprintf_s(*strp, strp_buflen, _TRUNCATE, format, args);
  va_end(args);
  if ((size_t)ret == strp_buflen - 1) {
    return ret;
  }

  /* This should never happen. */
  gpr_free(*strp);
  *strp = NULL;
  return -1;
}

#if defined UNICODE || defined _UNICODE
LPTSTR
gpr_char_to_tchar(LPCSTR input) {
  LPTSTR ret;
  int needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
  if (needed == 0) return NULL;
  ret = gpr_malloc(needed * sizeof(TCHAR));
  MultiByteToWideChar(CP_UTF8, 0, input, -1, ret, needed);
  return ret;
}

LPSTR
gpr_tchar_to_char(LPCTSTR input) {
  LPSTR ret;
  int needed = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
  if (needed == 0) return NULL;
  ret = gpr_malloc(needed);
  WideCharToMultiByte(CP_UTF8, 0, input, -1, ret, needed, NULL, NULL);
  return ret;
}
#else
char *gpr_tchar_to_char(LPTSTR input) { return gpr_strdup(input); }

char *gpr_char_to_tchar(LPTSTR input) { return gpr_strdup(input); }
#endif

#endif /* GPR_WIN32 */
