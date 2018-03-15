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

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX_LOG

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static long gettid(void) { return syscall(__NR_gettid); }

void gpr_log(const char* file, int line, gpr_log_severity severity,
             const char* format, ...) {
  char* message = nullptr;
  va_list args;
  va_start(args, format);
  if (vasprintf(&message, format, args) == -1) {
    va_end(args);
    return;
  }
  va_end(args);
  gpr_log_message(file, line, severity, message);
  /* message has been allocated by vasprintf above, and needs free */
  free(message);
}

void gpr_default_log(gpr_log_func_args* args) {
  const char* final_slash;
  char* prefix;
  const char* display_file;
  char time_buffer[64];
  time_t timer;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  struct tm tm;
  static __thread long tid = 0;
  if (tid == 0) tid = gettid();

  timer = static_cast<time_t>(now.tv_sec);
  final_slash = strrchr(args->file, '/');
  if (final_slash == nullptr)
    display_file = args->file;
  else
    display_file = final_slash + 1;

  if (!localtime_r(&timer, &tm)) {
    strcpy(time_buffer, "error:localtime");
  } else if (0 ==
             strftime(time_buffer, sizeof(time_buffer), "%m%d %H:%M:%S", &tm)) {
    strcpy(time_buffer, "error:strftime");
  }

  gpr_asprintf(&prefix, "%s%s.%09" PRId32 " %7ld %s:%d]",
               gpr_log_severity_string(args->severity), time_buffer,
               now.tv_nsec, tid, display_file, args->line);

  fprintf(stderr, "%-60s %s\n", prefix, args->message);
  gpr_free(prefix);
}

#endif /* GPR_LINUX_LOG */
