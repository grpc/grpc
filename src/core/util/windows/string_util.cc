//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

// Posix code for gpr snprintf support.

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

// Some platforms (namely msys) need wchar to be included BEFORE
// anything else, especially strsafe.h.
#include <wchar.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strsafe.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/tchar.h"
#include "src/core/util/string.h"

char* gpr_format_message(int messageid) {
  LPTSTR tmessage;
  DWORD status = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, (DWORD)messageid, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
      (LPTSTR)(&tmessage), 0, NULL);
  if (status == 0) return gpr_strdup("Unable to retrieve error string");
  auto message = grpc_core::TcharToChar(tmessage);
  LocalFree(tmessage);
  return gpr_strdup(message.c_str());
}

#endif  // GPR_WINDOWS
