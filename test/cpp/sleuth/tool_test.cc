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

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "test/cpp/sleuth/tool.h"

namespace grpc_sleuth {

absl::StatusOr<std::string> TestTool(absl::string_view tool_name,
                                     std::vector<std::string> args) {
  auto* tool_registry = ToolRegistry::Get();
  auto tool = tool_registry->GetTool(tool_name);
  if (tool == nullptr) {
    return absl::NotFoundError(absl::StrCat("Tool not found: ", tool_name));
  }

  std::stringstream buffer;
  auto old_cout_rdbuf = std::cout.rdbuf();
  std::cout.rdbuf(buffer.rdbuf());

  absl::Status status = tool(std::move(args));

  std::cout.rdbuf(old_cout_rdbuf);

  if (!status.ok()) {
    return status;
  }

  return buffer.str();
}

}  // namespace grpc_sleuth
