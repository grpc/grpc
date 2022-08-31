// Copyright 2022 gRPC authors.
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

// Automatically generated by tools/codegen/core/gen_experiments.py

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/experiments.h"

#include "src/core/lib/gprpp/global_config.h"

namespace {
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const description_promise_based_client_call =
    "If set, use the new gRPC promise based call code when it's appropriate "
    "(ie when all filters in a stack are promise based)";
}  // namespace

GPR_GLOBAL_CONFIG_DEFINE_BOOL(grpc_experimental_enable_tcp_frame_size_tuning,
                              false, description_tcp_frame_size_tuning);
GPR_GLOBAL_CONFIG_DEFINE_BOOL(
    grpc_experimental_enable_promise_based_client_call, false,
    description_promise_based_client_call);

namespace grpc_core {

bool IsTcpFrameSizeTuningEnabled() {
  static const bool enabled =
      GPR_GLOBAL_CONFIG_GET(grpc_experimental_enable_tcp_frame_size_tuning);
  return enabled;
}
bool IsPromiseBasedClientCallEnabled() {
  static const bool enabled =
      GPR_GLOBAL_CONFIG_GET(grpc_experimental_enable_promise_based_client_call);
  return enabled;
}

const ExperimentMetadata g_experiment_metadata[] = {
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning, false,
     IsTcpFrameSizeTuningEnabled},
    {"promise_based_client_call", description_promise_based_client_call, false,
     IsPromiseBasedClientCallEnabled},
};

}  // namespace grpc_core
