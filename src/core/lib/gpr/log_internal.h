// Copyright 2021 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#ifndef GRPC_CORE_LIB_GPR_LOG_INTERNAL_H
#define GRPC_CORE_LIB_GPR_LOG_INTERNAL_H

#include "grpc/support/log.h"

/// Log a message, accepting a variadic argument list. See also \a gpr_log.
void gpr_vlog(const char* file, int line, gpr_log_severity severity,
              const char* format, va_list args);

#endif  // GRPC_CORE_LIB_GPR_LOG_INTERNAL_H
