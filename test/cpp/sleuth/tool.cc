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

#include "test/cpp/sleuth/tool.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_sleuth {

absl::StatusOr<std::unique_ptr<ToolArgs>> ToolArgs::TryCreate(
    const std::vector<std::string>& args) {
  absl::flat_hash_map<std::string, std::string> map;
  for (const auto& arg : args) {
    std::vector<std::string> parts =
        absl::StrSplit(arg, absl::MaxSplits('=', 1));
    if (parts.size() != 2) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid argument format: ", arg, "; expected key=value"));
    }
    const std::string& key = parts[0];
    const std::string& value = parts[1];
    if (key.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Key cannot be empty in argument: ", arg));
    }
    if (!map.insert({key, value}).second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate key: ", key));
    }
  }
  return std::unique_ptr<ToolArgs>(new ToolArgs(std::move(map)));
}

template <>
absl::StatusOr<std::string> ToolArgs::ConvertValue<std::string>(
    absl::string_view /*key*/, absl::string_view value) {
  return std::string(value);
}

template <>
absl::StatusOr<double> ToolArgs::ConvertValue<double>(absl::string_view key,
                                                      absl::string_view value) {
  double result;
  if (!absl::SimpleAtod(value, &result)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid value for ", key, ": '", value, "' is not a double"));
  }
  return result;
}

template <>
absl::StatusOr<int64_t> ToolArgs::ConvertValue<int64_t>(
    absl::string_view key, absl::string_view value) {
  int64_t result;
  if (!absl::SimpleAtoi(value, &result)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid value for ", key, ": '", value, "' is not an integer"));
  }
  return result;
}

}  // namespace grpc_sleuth
