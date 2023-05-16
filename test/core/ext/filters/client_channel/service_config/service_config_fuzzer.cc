// Copyright 2023 The gRPC Authors
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

#include <string>

#include "absl/status/statusor.h"
#include "google/protobuf/json/json.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "src/proto/grpc/service_config/service_config.pb.h"
#include "test/core/ext/filters/client_channel/service_config/service_config_fuzzer.pb.h"

constexpr char g_grpc_config_prefix[] = "grpc_config=";

DEFINE_PROTO_FUZZER(const service_config_fuzzer::Msg& msg) {
  std::string payload;
  if (msg.has_fuzzed_service_config()) {
    ::google::protobuf::json::PrintOptions print_options;
    auto status = MessageToJsonString(msg.fuzzed_service_config(), &payload,
                                      print_options);
    // Sometimes LLVM will generate protos that can't be dumped to JSON
    // (Durations out of bounds, for example). These are ignored.
    if (!status.ok()) {
      return;
    }
  } else if (msg.has_random_data()) {
    switch (msg.random_data().enumerated_value()) {
      case service_config_fuzzer::ServiceConfigType::RANDOM:
        payload = msg.random_data().arbitrary_text();
        break;
      case service_config_fuzzer::ServiceConfigType::RANDOM_PREFIXED_CONFIG:
        payload = g_grpc_config_prefix + msg.random_data().arbitrary_text();
        break;
      default:
        // ignore sentinel values
        return;
    }
  } else {
    // an empty example
  }
  std::ignore = grpc_core::ChooseServiceConfig(payload);
}
