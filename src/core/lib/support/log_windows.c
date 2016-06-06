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

#ifdef GPR_WINDOWS_LOG

#include <stdarg.h>
#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/support/string.h"
#include "src/core/lib/support/string_windows.h"

void gpr_log(const char *file, int line, gpr_log_severity severity,
             const char *format, ...) {
  char *message = NULL;
  va_list args;
  int ret;

  /* Determine the length. */
  va_start(args, format);
  ret = _vscprintf(format, args);
  va_end(args);
  if (ret < 0) {
    message = NULL;
  } else {
    /* Allocate a new buffer, with space for the NUL terminator. */
    size_t strp_buflen = (size_t)ret + 1;
    message = gpr_malloc(strp_buflen);

    /* Print to the buffer. */
    va_start(args, format);
    ret = vsnprintf_s(message, strp_buflen, _TRUNCATE, format, args);
    va_end(args);
    if ((size_t)ret != strp_buflen - 1) {
      /* This should never happen. */
      gpr_free(message);
      message = NULL;
    }
  }

  gpr_log_message(file, line, severity, message);
  gpr_free(message);
}

/* Simple starter implementation */
void gpr_default_log(gpr_log_func_args *args) {
  char *final_slash;
  const char *display_file;
  char time_buffer[64];
  time_t timer;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  struct tm tm;

  timer = (time_t)now.tv_sec;
  final_slash = strrchr(args->file, '\\');
  if (final_slash == NULL)
    display_file = args->file;
  else
    display_file = final_slash + 1;

  if (localtime_s(&tm, &timer)) {
    strcpy(time_buffer, "error:localtime");
  } else if (0 ==
             strftime(time_buffer, sizeof(time_buffer), "%m%d %H:%M:%S", &tm)) {
    strcpy(time_buffer, "error:strftime");
  }

  fprintf(stderr, "%s%s.%09u %5lu %s:%d] %s\n",
          gpr_log_severity_string(args->severity), time_buffer,
          (int)(now.tv_nsec), GetCurrentThreadId(), display_file, args->line,
          args->message);
  fflush(stderr);
}

#endif /* GPR_WINDOWS_LOG */
