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

#ifndef GRPC_TEST_CPP_SLEUTH_TOOL_TEST_H
#define GRPC_TEST_CPP_SLEUTH_TOOL_TEST_H

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_sleuth {

absl::StatusOr<std::string> TestTool(absl::string_view tool_name,
                                     std::vector<std::string> args);

}  // namespace grpc_sleuth

#endif  // GRPC_TEST_CPP_SLEUTH_TOOL_TEST_H
