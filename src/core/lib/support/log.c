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

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"

#include <stdio.h>
#include <string.h>

extern void gpr_default_log(gpr_log_func_args *args);
static gpr_log_func g_log_func = gpr_default_log;
static gpr_atm g_min_severity_to_print = GPR_LOG_VERBOSITY_UNSET;

const char *gpr_log_severity_string(gpr_log_severity severity) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      return "D";
    case GPR_LOG_SEVERITY_INFO:
      return "I";
    case GPR_LOG_SEVERITY_ERROR:
      return "E";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

void gpr_log_message(const char *file, int line, gpr_log_severity severity,
                     const char *message) {
  if ((gpr_atm)severity < gpr_atm_no_barrier_load(&g_min_severity_to_print)) {
    return;
  }

  gpr_log_func_args lfargs;
  memset(&lfargs, 0, sizeof(lfargs));
  lfargs.file = file;
  lfargs.line = line;
  lfargs.severity = severity;
  lfargs.message = message;
  g_log_func(&lfargs);
}

void gpr_set_log_verbosity(gpr_log_severity min_severity_to_print) {
  gpr_atm_no_barrier_store(&g_min_severity_to_print,
                           (gpr_atm)min_severity_to_print);
}

void gpr_log_verbosity_init() {
  char *verbosity = gpr_getenv("GRPC_VERBOSITY");

  gpr_atm min_severity_to_print = GPR_LOG_SEVERITY_ERROR;
  if (verbosity != NULL) {
    if (gpr_stricmp(verbosity, "DEBUG") == 0) {
      min_severity_to_print = (gpr_atm)GPR_LOG_SEVERITY_DEBUG;
    } else if (gpr_stricmp(verbosity, "INFO") == 0) {
      min_severity_to_print = (gpr_atm)GPR_LOG_SEVERITY_INFO;
    } else if (gpr_stricmp(verbosity, "ERROR") == 0) {
      min_severity_to_print = (gpr_atm)GPR_LOG_SEVERITY_ERROR;
    }
    gpr_free(verbosity);
  }
  if ((gpr_atm_no_barrier_load(&g_min_severity_to_print)) ==
      GPR_LOG_VERBOSITY_UNSET) {
    gpr_atm_no_barrier_store(&g_min_severity_to_print, min_severity_to_print);
  }
}

void gpr_set_log_function(gpr_log_func f) {
  g_log_func = f ? f : gpr_default_log;
}
