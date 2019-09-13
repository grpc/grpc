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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_ENV

#include <windows.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/string_windows.h"

char* gpr_getenv(const char* name) {
  char* result = NULL;
  DWORD size;
  LPTSTR tresult = NULL;
  LPTSTR tname = gpr_char_to_tchar(name);
  DWORD ret;

  ret = GetEnvironmentVariable(tname, NULL, 0);
  if (ret == 0) {
    gpr_free(tname);
    return NULL;
  }
  size = ret * (DWORD)sizeof(TCHAR);
  tresult = (LPTSTR)gpr_malloc(size);
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

void gpr_setenv(const char* name, const char* value) {
  LPTSTR tname = gpr_char_to_tchar(name);
  LPTSTR tvalue = gpr_char_to_tchar(value);
  BOOL res = SetEnvironmentVariable(tname, tvalue);
  gpr_free(tname);
  gpr_free(tvalue);
  GPR_ASSERT(res);
}

void gpr_unsetenv(const char* name) {
  LPTSTR tname = gpr_char_to_tchar(name);
  BOOL res = SetEnvironmentVariable(tname, NULL);
  gpr_free(tname);
  GPR_ASSERT(res);
}

#endif /* GPR_WINDOWS_ENV */
