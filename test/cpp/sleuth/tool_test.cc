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

#include "test/cpp/sleuth/tool_test.h"

#include <string>
#include <utility>
#include <vector>

#include "test/cpp/sleuth/tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_sleuth {

absl::StatusOr<std::string> TestTool(absl::string_view tool_name,
                                     std::vector<std::string> args) {
  auto* tool_registry = ToolRegistry::Get();
  auto tool = tool_registry->GetTool(tool_name);
  if (tool == nullptr) {
    return absl::NotFoundError(absl::StrCat("Tool not found: ", tool_name));
  }

  std::string output;
  auto tool_args = ToolArgs::TryCreate(args);
  if (!tool_args.ok()) {
    return tool_args.status();
  }
  absl::Status status =
      tool(*tool_args.value(), [&output](std::string s) { output += s; });

  if (!status.ok()) {
    return status;
  }

  return output;
}

}  // namespace grpc_sleuth
