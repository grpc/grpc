//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H
#define GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H

#include <grpc/grpc.h>

#include "src/core/lib/experiments/experiments.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"

namespace grpc {
namespace testing {

// TODO(tjagtap) : [PH2][P3] : Remove once all the PH2 E2E tests are fixed.
inline void DisableLoggingForPH2Tests() {
  if (grpc_core::IsPromiseBasedHttp2ClientTransportEnabled()) {
    grpc_tracer_set_enabled("http", false);
    grpc_tracer_set_enabled("channel", false);
    grpc_tracer_set_enabled("subchannel", false);
    grpc_tracer_set_enabled("client_channel", false);
    grpc_tracer_set_enabled("http2_ph2_transport", false);
    grpc_tracer_set_enabled("call", false);
    grpc_tracer_set_enabled("call_state", false);
    grpc_tracer_set_enabled("promise_primitives", false);
    absl::SetGlobalVLogLevel(-1);
  }
}

// TODO(tjagtap) : [PH2][P3] : Remove once all the PH2 E2E tests are fixed.
inline void EnableLoggingForPH2Tests() {
  if (grpc_core::IsPromiseBasedHttp2ClientTransportEnabled()) {
    grpc_tracer_set_enabled("http", 1);
    grpc_tracer_set_enabled("channel", 1);
    grpc_tracer_set_enabled("subchannel", 1);
    grpc_tracer_set_enabled("client_channel", 1);
    grpc_tracer_set_enabled("http2_ph2_transport", 1);
    grpc_tracer_set_enabled("call", 1);
    grpc_tracer_set_enabled("call_state", 1);
    grpc_tracer_set_enabled("promise_primitives", 1);
    absl::SetGlobalVLogLevel(2);
  }
}

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H
