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
#include <ostream>

#include "src/core/util/latent_see.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/tool_options.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"

ABSL_FLAG(std::optional<std::string>, latent_see_target, std::nullopt,
          "Target to connect to for latent see");
ABSL_FLAG(double, latent_see_sample_time_seconds, 1.0,
          "Time to sample latent see data");

namespace grpc_sleuth {

SLEUTH_TOOL(fetch_latent_see_json, "file|-",
            "Fetch latent see data and format as json. file|- means output to "
            "stdout.") {
  if (args.size() != 1) {
    return absl::InvalidArgumentError(
        "Usage: fetch_latent_see_json file|-\n"
        "fetch_latent_see_json requires exactly one argument: the file to "
        "write to, or - for stdout.");
  }

  auto target = absl::GetFlag(FLAGS_latent_see_target);
  if (!target.has_value()) {
    return absl::InvalidArgumentError("--latent_see_target is required");
  }

  std::ofstream file_out;
  std::ostream* out = &std::cout;
  if (args[0] != "-") {
    file_out.open(args[0]);
    if (!file_out.is_open()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to open file: ", args[0]));
    }
    out = &file_out;
  }

  grpc_core::latent_see::JsonOutput output(*out);
  auto client = Client(*target, ToolClientOptions());
  auto status = client.FetchLatentSee(
      absl::GetFlag(FLAGS_latent_see_sample_time_seconds), &output);
  return status;
}

}  // namespace grpc_sleuth
