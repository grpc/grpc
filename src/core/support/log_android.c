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

#ifdef GPR_ANDROID

#include <android/log.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static android_LogPriority severity_to_log_priority(gpr_log_severity severity) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      return ANDROID_LOG_DEBUG;
    case GPR_LOG_SEVERITY_INFO:
      return ANDROID_LOG_INFO;
    case GPR_LOG_SEVERITY_ERROR:
      return ANDROID_LOG_ERROR;
  }
  return ANDROID_LOG_DEFAULT;
}

void gpr_log(const char *file, int line, gpr_log_severity severity,
             const char *format, ...) {
  char *message = NULL;
  va_list args;
  va_start(args, format);
  vasprintf(&message, format, args);
  va_end(args);
  gpr_log_message(file, line, severity, message);
  free(message);
}

void gpr_default_log(gpr_log_func_args *args) {
  char *final_slash;
  const char *display_file;
  char *output = NULL;

  final_slash = strrchr(args->file, '/');
  if (final_slash == NULL)
    display_file = args->file;
  else
    display_file = final_slash + 1;

  asprintf(&output, "%s:%d] %s", display_file, args->line, args->message);

  __android_log_write(severity_to_log_priority(args->severity), "GRPC", output);

  /* allocated by asprintf => use free, not gpr_free */
  free(output);
}

#endif /* GPR_ANDROID */
