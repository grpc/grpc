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

#include "absl/log/log.h"

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/util/crash.h"
#include "src/core/util/string.h"

void gpr_unreachable_code(const char* reason, const char* file, int line) {
  grpc_core::Crash(absl::StrCat("UNREACHABLE CODE: ", reason),
                   grpc_core::SourceLocation(file, line));
}

GPRAPI void grpc_absl_log(const char* file, int line, gpr_log_severity severity,
                          const char* message_str) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      VLOG(2).AtLocation(file, line) << message_str;
      return;
    case GPR_LOG_SEVERITY_INFO:
      LOG(INFO).AtLocation(file, line) << message_str;
      return;
    case GPR_LOG_SEVERITY_ERROR:
      LOG(ERROR).AtLocation(file, line) << message_str;
      return;
    default:
      DCHECK(false) << "Invalid severity";
  }
}

GPRAPI void grpc_absl_log_int(const char* file, int line,
                              gpr_log_severity severity,
                              const char* message_str, intptr_t num) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      VLOG(2).AtLocation(file, line) << message_str << num;
      return;
    case GPR_LOG_SEVERITY_INFO:
      LOG(INFO).AtLocation(file, line) << message_str << num;
      return;
    case GPR_LOG_SEVERITY_ERROR:
      LOG(ERROR).AtLocation(file, line) << message_str << num;
      return;
    default:
      DCHECK(false) << "Invalid severity";
  }
}

GPRAPI void grpc_absl_log_str(const char* file, int line,
                              gpr_log_severity severity,
                              const char* message_str1,
                              const char* message_str2) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      VLOG(2).AtLocation(file, line) << message_str1 << message_str2;
      return;
    case GPR_LOG_SEVERITY_INFO:
      LOG(INFO).AtLocation(file, line) << message_str1 << message_str2;
      return;
    case GPR_LOG_SEVERITY_ERROR:
      LOG(ERROR).AtLocation(file, line) << message_str1 << message_str2;
      return;
    default:
      DCHECK(false) << "Invalid severity";
  }
}

void gpr_log_verbosity_init(void) {
#ifndef GRPC_VERBOSITY_MACRO
  // SetMinLogLevel sets the value for the entire binary, not just gRPC.
  // This setting will change things for other libraries/code that is unrelated
  // to grpc.
  absl::string_view verbosity = grpc_core::ConfigVars::Get().Verbosity();
  if (absl::EqualsIgnoreCase(verbosity, "INFO")) {
    LOG_FIRST_N(WARNING, 1)
        << "Log level INFO is not suitable for production. Prefer WARNING or "
           "ERROR. However if you see this message in a debug environment or "
           "test environment it is safe to ignore this message.";
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  } else if (absl::EqualsIgnoreCase(verbosity, "DEBUG")) {
    LOG_FIRST_N(WARNING, 1)
        << "Log level DEBUG is not suitable for production. Prefer WARNING or "
           "ERROR. However if you see this message in a debug environment or "
           "test environment it is safe to ignore this message.";
    absl::SetVLogLevel("*grpc*/*", 2);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  } else if (absl::EqualsIgnoreCase(verbosity, "ERROR")) {
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kError);
  } else if (absl::EqualsIgnoreCase(verbosity, "NONE")) {
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
  } else if (verbosity.empty()) {
    // Do not alter absl settings if GRPC_VERBOSITY flag is not set.
  } else {
    LOG(ERROR) << "Unknown log verbosity: " << verbosity;
  }
#endif  // GRPC_VERBOSITY_MACRO
}
