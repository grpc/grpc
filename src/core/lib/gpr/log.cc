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

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/global_config.h"

#include <stdio.h>
#include <string.h>

GPR_GLOBAL_CONFIG_DEFINE_STRING(grpc_verbosity, "ERROR",
                                "Default gRPC logging verbosity")

void gpr_default_log(gpr_log_func_args* args);
static gpr_atm g_log_func = (gpr_atm)gpr_default_log;
static gpr_atm g_min_severity_to_print = GPR_LOG_VERBOSITY_UNSET;

const char* gpr_log_severity_string(gpr_log_severity severity) {
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

int gpr_should_log(gpr_log_severity severity) {
  return static_cast<gpr_atm>(severity) >=
                 gpr_atm_no_barrier_load(&g_min_severity_to_print)
             ? 1
             : 0;
}

void gpr_log_message(const char* file, int line, gpr_log_severity severity,
                     const char* message) {
  if (gpr_should_log(severity) == 0) {
    return;
  }

  gpr_log_func_args lfargs;
  memset(&lfargs, 0, sizeof(lfargs));
  lfargs.file = file;
  lfargs.line = line;
  lfargs.severity = severity;
  lfargs.message = message;
  ((gpr_log_func)gpr_atm_no_barrier_load(&g_log_func))(&lfargs);
}

void gpr_set_log_verbosity(gpr_log_severity min_severity_to_print) {
  gpr_atm_no_barrier_store(&g_min_severity_to_print,
                           (gpr_atm)min_severity_to_print);
}

void gpr_log_verbosity_init() {
  grpc_core::UniquePtr<char> verbosity = GPR_GLOBAL_CONFIG_GET(grpc_verbosity);

  gpr_atm min_severity_to_print = GPR_LOG_SEVERITY_ERROR;
  if (strlen(verbosity.get()) > 0) {
    if (gpr_stricmp(verbosity.get(), "DEBUG") == 0) {
      min_severity_to_print = static_cast<gpr_atm>(GPR_LOG_SEVERITY_DEBUG);
    } else if (gpr_stricmp(verbosity.get(), "INFO") == 0) {
      min_severity_to_print = static_cast<gpr_atm>(GPR_LOG_SEVERITY_INFO);
    } else if (gpr_stricmp(verbosity.get(), "ERROR") == 0) {
      min_severity_to_print = static_cast<gpr_atm>(GPR_LOG_SEVERITY_ERROR);
    }
  }
  if ((gpr_atm_no_barrier_load(&g_min_severity_to_print)) ==
      GPR_LOG_VERBOSITY_UNSET) {
    gpr_atm_no_barrier_store(&g_min_severity_to_print, min_severity_to_print);
  }
}

void gpr_set_log_function(gpr_log_func f) {
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)(f ? f : gpr_default_log));
}
