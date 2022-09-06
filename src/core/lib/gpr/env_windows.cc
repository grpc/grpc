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

#include <memory>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/tchar.h"

char* gpr_getenv(const char* name) {
  char* result = NULL;
  LPTSTR tresult = NULL;
  auto tname = grpc_core::CharToTchar(name);
  DWORD ret;

  ret = GetEnvironmentVariable(tname.c_str(), NULL, 0);
  if (ret == 0) return NULL;
  std::unique_ptr<TCHAR[]> tresult(new TCHAR[ret]);
  ret = GetEnvironmentVariable(tname.c_str(), tresult.get(), size);
  if (ret == 0) return NULL;
  return gpr_strdup(grpc_core::TcharToChar(tresult.get()).c_str());
}

void gpr_setenv(const char* name, const char* value) {
  BOOL res = SetEnvironmentVariable(grpc_core::CharToTchar(name).c_str(),
                                    grpc_core::CharToTchar(value).c_str());
  GPR_ASSERT(res);
}

void gpr_unsetenv(const char* name) {
  BOOL res = SetEnvironmentVariable(grpc_core::CharToTchar(name).c_str(), NULL);
  GPR_ASSERT(res);
}

#endif /* GPR_WINDOWS_ENV */
