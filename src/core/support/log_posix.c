/*
 *
 * Copyright 2014, Google Inc.
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

#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <grpc/support/port_platform.h>

#if defined(GPR_POSIX_LOG)

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static long gettid() { return pthread_self(); }

void gpr_vlog(const char *file, int line, gpr_log_severity severity,
              const char *format, va_list args) {
  char *final_slash;
  const char *display_file;
  char time_buffer[64];
  gpr_timespec now = gpr_now();
  struct tm tm;

  final_slash = strrchr(file, '/');
  if (final_slash == NULL)
    display_file = file;
  else
    display_file = final_slash + 1;

  if (!localtime_r(&now.tv_sec, &tm)) {
    strcpy(time_buffer, "error:localtime");
  } else if (0 ==
             strftime(time_buffer, sizeof(time_buffer), "%m%d %H:%M:%S", &tm)) {
    strcpy(time_buffer, "error:strftime");
  }

  flockfile(stderr);
  fprintf(stderr, "%s%s.%09d %7ld %s:%d] ", gpr_log_severity_string(severity),
          time_buffer, (int)(now.tv_nsec), gettid(), display_file, line);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  funlockfile(stderr);
}

#endif /* defined(GPR_POSIX_LOG) */
