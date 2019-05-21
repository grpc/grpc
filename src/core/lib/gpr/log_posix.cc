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

#ifdef GPR_POSIX_LOG

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static intptr_t gettid(void) { return (intptr_t)pthread_self(); }

void gpr_log(const char* file, int line, gpr_log_severity severity,
             const char* format, ...) {
  /* Avoid message construction if gpr_log_message won't log */
  if (gpr_should_log(severity) == 0) {
    return;
  }
  char buf[64];
  char* allocated = nullptr;
  char* message = nullptr;
  int ret;
  va_list args;
  va_start(args, format);
  ret = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (ret < 0) {
    message = nullptr;
  } else if ((size_t)ret <= sizeof(buf) - 1) {
    message = buf;
  } else {
    message = allocated = (char*)gpr_malloc((size_t)ret + 1);
    va_start(args, format);
    vsnprintf(message, (size_t)(ret + 1), format, args);
    va_end(args);
  }
  gpr_log_message(file, line, severity, message);
  gpr_free(allocated);
}

void gpr_default_log(gpr_log_func_args* args) {
  const char* final_slash;
  const char* display_file;
  char time_buffer[64];
  time_t timer;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  struct tm tm;

  timer = (time_t)now.tv_sec;
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

  char* prefix;
  gpr_asprintf(&prefix, "%s%s.%09d %7" PRIdPTR " %s:%d]",
               gpr_log_severity_string(args->severity), time_buffer,
               (int)(now.tv_nsec), gettid(), display_file, args->line);

  fprintf(stderr, "%-70s %s\n", prefix, args->message);
  gpr_free(prefix);
}

#endif /* defined(GPR_POSIX_LOG) */
