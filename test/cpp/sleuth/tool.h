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

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_sleuth {

// Prints to stdout. New-line is not appended.
inline void PrintStdout(std::string s) { std::cout << s; }

class ToolArgs {
 public:
  static absl::StatusOr<std::unique_ptr<ToolArgs>> TryCreate(
      const std::vector<std::string>& args);

  template <typename T>
  absl::StatusOr<T> TryGetFlag(
      absl::string_view key,
      std::optional<T> default_value = std::nullopt) const {
    auto it = map_.find(key);
    if (it != map_.end()) {
      return ConvertValue<T>(key, it->second);
    }
    if (default_value.has_value()) {
      return *default_value;
    }
    return absl::InvalidArgumentError(absl::StrCat(key, " is required"));
  }

 private:
  explicit ToolArgs(absl::flat_hash_map<std::string, std::string> map)
      : map_(std::move(map)) {}

  template <typename T>
  static absl::StatusOr<T> ConvertValue(absl::string_view key,
                                        absl::string_view value);

  absl::flat_hash_map<std::string, std::string> map_;
};

// The caller must ensure the second arg remains valid until this returns.
using ToolFn = absl::Status (*)(
    const ToolArgs&, const absl::AnyInvocable<void(std::string) const>&);

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
  absl::Status name(                                                      \
      const grpc_sleuth::ToolArgs& args,                                  \
      const absl::AnyInvocable<void(std::string) const>& print_fn);       \
  namespace {                                                             \
  int registration_for_tool_##name =                                      \
      grpc_sleuth::ToolRegistry::Get()->Register(#name, args_description, \
                                                 description, name);      \
  }                                                                       \
  absl::Status name(                                                      \
      GRPC_UNUSED const grpc_sleuth::ToolArgs& args,                      \
      GRPC_UNUSED const absl::AnyInvocable<void(std::string) const>& print_fn)

#endif  // GRPC_TEST_CPP_SLEUTH_TOOL_H
