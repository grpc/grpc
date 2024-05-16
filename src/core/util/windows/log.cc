//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_LOG

#include <stdarg.h>
#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/examine_stack.h"
#include "src/core/util/string.h"

void gpr_log(const char* file, int line, gpr_log_severity severity,
             const char* format, ...) {
  // Avoid message construction if gpr_log_message won't log
  if (gpr_should_log(severity) == 0) {
    return;
  }

  char* message = NULL;
  va_list args;
  int ret;

  // Determine the length.
  va_start(args, format);
  ret = _vscprintf(format, args);
  va_end(args);
  if (ret < 0) {
    message = NULL;
  } else {
    // Allocate a new buffer, with space for the NUL terminator.
    size_t strp_buflen = (size_t)ret + 1;
    message = (char*)gpr_malloc(strp_buflen);

    // Print to the buffer.
    va_start(args, format);
    ret = vsnprintf_s(message, strp_buflen, _TRUNCATE, format, args);
    va_end(args);
    if ((size_t)ret != strp_buflen - 1) {
      // This should never happen.
      gpr_free(message);
      message = NULL;
    }
  }

  gpr_log_message(file, line, severity, message);
  gpr_free(message);
}

#endif  // GPR_WINDOWS_LOG
