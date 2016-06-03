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

#ifdef GPR_WINDOWS_ENV

#include <windows.h>

#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/string_windows.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

char *gpr_getenv(const char *name) {
  char *result = NULL;
  DWORD size;
  LPTSTR tresult = NULL;
  LPTSTR tname = gpr_char_to_tchar(name);
  DWORD ret;

  ret = GetEnvironmentVariable(tname, NULL, 0);
  if (ret == 0) return NULL;
  size = ret * (DWORD)sizeof(TCHAR);
  tresult = gpr_malloc(size);
  ret = GetEnvironmentVariable(tname, tresult, size);
  gpr_free(tname);
  if (ret == 0) {
    gpr_free(tresult);
    return NULL;
  }
  result = gpr_tchar_to_char(tresult);
  gpr_free(tresult);
  return result;
}

void gpr_setenv(const char *name, const char *value) {
  LPTSTR tname = gpr_char_to_tchar(name);
  LPTSTR tvalue = gpr_char_to_tchar(value);
  BOOL res = SetEnvironmentVariable(tname, tvalue);
  gpr_free(tname);
  gpr_free(tvalue);
  GPR_ASSERT(res);
}

#endif /* GPR_WINDOWS_ENV */
