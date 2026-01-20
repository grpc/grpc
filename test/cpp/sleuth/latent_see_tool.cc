// Copyright 2025 gRPC authors.
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

#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "src/core/util/latent_see.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/tool_options.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_sleuth {

SLEUTH_TOOL(fetch_latent_see_json, "target=... [destination=...]",
            "Fetch latent see data and format as json. If destination is not "
            "specified, dumps to stdout.") {
  auto target = args.TryGetFlag<std::string>("target");
  if (!target.ok()) return target.status();
  auto sample_time_seconds = args.TryGetFlag<double>("sample_time_seconds");

  std::ofstream file_out;
  std::stringstream ss;
  std::ostream* out = &ss;
  auto destination = args.TryGetFlag<std::string>("destination");
  if (destination.ok()) {
    file_out.open(*destination);
    if (!file_out.is_open()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to open file: ", *destination));
    }
    out = &file_out;
  }

  grpc_core::latent_see::JsonOutput output(*out);
  std::optional<std::string> channel_creds_type;
  auto channel_creds_type_arg =
      args.TryGetFlag<std::string>("channel_creds_type");
  if (channel_creds_type_arg.ok()) {
    channel_creds_type = *channel_creds_type_arg;
  }
  auto channelz_protocol = args.TryGetFlag<std::string>("channelz_protocol");
  auto client = Client(
      *target,
      ToolClientOptions(channelz_protocol.ok() ? *channelz_protocol : "h2",
                        channel_creds_type));
  auto status = client.FetchLatentSee(
      sample_time_seconds.ok() ? *sample_time_seconds : 1.0, &output);
  if (!destination.ok()) {
    print_fn(ss.str());
  }
  return status;
}

}  // namespace grpc_sleuth
