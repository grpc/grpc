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

#include <grpc/census.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/surface/call.h"

void grpc_census_call_set_context(grpc_call* call, census_context* context) {
  GRPC_TRACE_LOG(api, INFO) << "grpc_census_call_set_context(call=" << call
                            << ", census_context=" << context << ")";
  if (context != nullptr) {
    grpc_call_get_arena(call)->SetContext<census_context>(context);
  }
}

census_context* grpc_census_call_get_context(grpc_call* call) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_census_call_get_context(call=" << call << ")";
  return grpc_call_get_arena(call)->GetContext<census_context>();
}
