// Copyright 2023 The gRPC Authors.
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

#include "test/cpp/util/get_grpc_test_runfile_dir.h"

#include "src/core/lib/gprpp/env.h"

namespace grpc {

absl::optional<std::string> GetGrpcTestRunFileDir() {
  absl::optional<std::string> test_srcdir = grpc_core::GetEnv("TEST_SRCDIR");
  if (!test_srcdir.has_value()) {
    return absl::nullopt;
  }
  return *test_srcdir + "/com_github_grpc_grpc";
}

}  // namespace grpc
