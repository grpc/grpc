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

#ifndef GRPC_TEST_CPP_SLEUTH_TOOL_H
#define GRPC_TEST_CPP_SLEUTH_TOOL_H

#include <grpc/support/port_platform.h>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_sleuth {

using ToolFn = absl::Status (*)(std::vector<std::string>);

class ToolRegistry {
 public:
  struct ToolMetadata {
    absl::string_view name;
    absl::string_view args;
    absl::string_view description;
    ToolFn tool;
  };

  int Register(absl::string_view name, absl::string_view args,
               absl::string_view description, ToolFn tool) {
    for (const auto& tool : tool_metadata_) {
      CHECK_NE(tool.name, name) << "Tool name collision: " << name;
    }
    tool_metadata_.push_back({name, args, description, tool});
    return tool_metadata_.size();
  }

  static ToolRegistry* Get() {
    static ToolRegistry* instance = new ToolRegistry();
    return instance;
  }

  absl::Span<const ToolMetadata> GetMetadata() {
    std::sort(tool_metadata_.begin(), tool_metadata_.end(),
              [](const ToolMetadata& a, const ToolMetadata& b) {
                return a.name < b.name;
              });
    return tool_metadata_;
  }

  ToolFn GetTool(absl::string_view name) {
    for (const auto& tool : tool_metadata_) {
      if (tool.name == name) return tool.tool;
    }
    return nullptr;
  }

 private:
  ToolRegistry() = default;

  std::vector<ToolMetadata> tool_metadata_;
};

}  // namespace grpc_sleuth

#define SLEUTH_TOOL(name, args_description, description)                  \
  absl::Status name(std::vector<std::string> args);                       \
  namespace {                                                             \
  int registration_for_tool_##name =                                      \
      grpc_sleuth::ToolRegistry::Get()->Register(#name, args_description, \
                                                 description, name);      \
  }                                                                       \
  absl::Status name(GRPC_UNUSED std::vector<std::string> args)

#endif  // GRPC_TEST_CPP_SLEUTH_TOOL_H
