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

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX_LOG

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/examine_stack.h"

void gpr_log(const char* file, int line, gpr_log_severity severity,
             const char* format, ...) {
  // Avoid message construction if gpr_log_message won't log
  if (gpr_should_log(severity) == 0) {
    return;
  }
  char* message = nullptr;
  va_list args;
  va_start(args, format);
  if (vasprintf(&message, format, args) == -1) {
    va_end(args);
    return;
  }
  va_end(args);
  gpr_log_message(file, line, severity, message);
  // message has been allocated by vasprintf above, and needs free
  free(message);
}

#endif  // GPR_LINUX_LOG
