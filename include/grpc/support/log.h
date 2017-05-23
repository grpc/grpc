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

#ifndef GRPC_SUPPORT_LOG_H
#define GRPC_SUPPORT_LOG_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h> /* for abort() */

#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GPR log API.

   Usage (within grpc):

   int argument1 = 3;
   char* argument2 = "hello";
   gpr_log(GPR_DEBUG, "format string %d", argument1);
   gpr_log(GPR_INFO, "hello world");
   gpr_log(GPR_ERROR, "%d %s!!", argument1, argument2); */

/** The severity of a log message - use the #defines below when calling into
   gpr_log to additionally supply file and line data */
typedef enum gpr_log_severity {
  GPR_LOG_SEVERITY_DEBUG,
  GPR_LOG_SEVERITY_INFO,
  GPR_LOG_SEVERITY_ERROR
} gpr_log_severity;

#define GPR_LOG_VERBOSITY_UNSET -1

/** Returns a string representation of the log severity */
GPRAPI const char *gpr_log_severity_string(gpr_log_severity severity);

/** Macros to build log contexts at various severity levels */
#define GPR_DEBUG __FILE__, __LINE__, GPR_LOG_SEVERITY_DEBUG
#define GPR_INFO __FILE__, __LINE__, GPR_LOG_SEVERITY_INFO
#define GPR_ERROR __FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR

/** Log a message. It's advised to use GPR_xxx above to generate the context
 * for each message */
GPRAPI void gpr_log(const char *file, int line, gpr_log_severity severity,
                    const char *format, ...) GPR_PRINT_FORMAT_CHECK(4, 5);

GPRAPI void gpr_log_message(const char *file, int line,
                            gpr_log_severity severity, const char *message);

/** Set global log verbosity */
GPRAPI void gpr_set_log_verbosity(gpr_log_severity min_severity_to_print);

GPRAPI void gpr_log_verbosity_init();

/** Log overrides: applications can use this API to intercept logging calls
   and use their own implementations */

typedef struct {
  const char *file;
  int line;
  gpr_log_severity severity;
  const char *message;
} gpr_log_func_args;

typedef void (*gpr_log_func)(gpr_log_func_args *args);
GPRAPI void gpr_set_log_function(gpr_log_func func);

/** abort() the process if x is zero, having written a line to the log.

   Intended for internal invariants.  If the error can be recovered from,
   without the possibility of corruption, or might best be reflected via
   an exception in a higher-level language, consider returning error code.  */
#define GPR_ASSERT(x)                                 \
  do {                                                \
    if (!(x)) {                                       \
      gpr_log(GPR_ERROR, "assertion failed: %s", #x); \
      abort();                                        \
    }                                                 \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_LOG_H */
