//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/util/examine_stack.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

gpr_current_stack_trace_func g_current_stack_trace_provider = nullptr;

gpr_current_stack_trace_func GetCurrentStackTraceProvider() {
  return g_current_stack_trace_provider;
}

void SetCurrentStackTraceProvider(
    gpr_current_stack_trace_func current_stack_trace_provider) {
  g_current_stack_trace_provider = current_stack_trace_provider;
}

absl::optional<std::string> GetCurrentStackTrace() {
  if (g_current_stack_trace_provider != nullptr) {
    return g_current_stack_trace_provider();
  }
  return absl::nullopt;
}

}  // namespace grpc_core
