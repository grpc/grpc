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

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/tool_credentials.h"

ABSL_FLAG(std::optional<std::string>, channelz_target, std::nullopt,
          "Target to connect to for channelz");

namespace grpc_sleuth {

SLEUTH_TOOL(dump_channelz, "[destination]",
            "Dumps all channelz data; if destination is not specified, dumps "
            "to stdout.") {
  if (args.size() > 1) {
    return absl::InvalidArgumentError("Too many arguments");
  }
  if (args.size() == 1) {
    return absl::UnimplementedError("Destination not implemented yet");
  }

  auto target = absl::GetFlag(FLAGS_channelz_target);
  if (!target.has_value()) {
    return absl::InvalidArgumentError("--channelz_target is required");
  }

  auto response = Client(*target, ToolCredentials()).QueryAllChannelzEntities();
  if (!response.ok()) return response.status();

  for (const auto& entity : *response) {
    std::cout << entity.DebugString() << "\n";
  }

  return absl::OkStatus();
}

}  // namespace grpc_sleuth
