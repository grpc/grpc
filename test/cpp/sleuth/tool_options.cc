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

#include "test/cpp/sleuth/tool_options.h"

#include <optional>
#include <string>

#include "src/core/ext/transport/chaotic_good/chaotic_good.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool_credentials.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"

ABSL_DECLARE_FLAG(std::string, channel_creds_type);

namespace grpc_sleuth {

Client::Options ToolClientOptions(
    absl::string_view protocol,
    std::optional<std::string> channel_creds_type_opt) {
  // Ensure chaotic good is linked in.
  grpc_core::chaotic_good::WireFormatPreferences();
  if (channel_creds_type_opt.has_value()) {
    absl::SetFlag(&FLAGS_channel_creds_type, *channel_creds_type_opt);
  }
  return Client::Options{
      ToolCredentials(),
      std::string(protocol),
  };
}

}  // namespace grpc_sleuth
