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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/json/json.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "src/proto/grpc/service_config/service_config.pb.h"
#include "test/core/service_config/service_config_fuzzer.pb.h"

constexpr char g_grpc_config_prefix[] = "grpc_config=";

namespace {
const google::protobuf::json::PrintOptions g_print_options;
}  // namespace

// TODO(yijiem): the redundant serialization & deserialization is unnecessary.
// We should probably change ChooseServiceConfig() to return the chosen config
// as a Json object, and then have the DNS resolvers use this override of
// ServiceConfigImpl::Create() that accepts a Json object.)

std::vector<std::string> ServiceConfigTXTRecordToJSON(
    const service_config_fuzzer::ServiceConfigTXTRecord& txt_record) {
  if (txt_record.service_config_choices_size() == 0) return {};
  std::vector<std::string> config_choices;
  for (const auto& choice : txt_record.service_config_choices()) {
    std::string payload;
    auto status = MessageToJsonString(choice, &payload, g_print_options);
    // Sometimes LLVM will generate protos that can't be dumped to JSON
    // (Durations out of bounds, for example). These are ignored.
    if (!status.ok()) {
      continue;
    }
    config_choices.push_back(payload);
  }
  return config_choices;
}

DEFINE_PROTO_FUZZER(const service_config_fuzzer::Msg& msg) {
  // std::ignore = grpc_core::CoreConfiguration::Get();
  std::string choose_serivce_config_payload;
  if (msg.has_service_config_txt_record()) {
    std::vector<std::string> config_choice_json_strings =
        ServiceConfigTXTRecordToJSON(msg.service_config_txt_record());
    choose_serivce_config_payload =
        absl::StrCat(g_grpc_config_prefix, "[",
                     absl::StrJoin(config_choice_json_strings, ","), "]");
    // Test each ServiceConfig against ServiceConfigImpl::Create
    grpc_core::ChannelArgs channel_args;
    for (const auto& config_choice :
         msg.service_config_txt_record().service_config_choices()) {
      std::string sub_config;
      auto status = MessageToJsonString(config_choice.service_config(),
                                        &sub_config, g_print_options);
      if (!status.ok()) continue;
      std::ignore =
          grpc_core::ServiceConfigImpl::Create(channel_args, sub_config);
    }
  } else if (msg.has_arbitrary_txt_record()) {
    choose_serivce_config_payload = msg.arbitrary_txt_record();
  } else {
    // an empty example
  }
  std::ignore = grpc_core::ChooseServiceConfig(choose_serivce_config_payload);
}
