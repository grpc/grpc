//
// Copyright 2023 gRPC authors.
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
//

#include "src/core/lib/gprpp/directory.h"

#include <stdio.h>

#include "gtest/gtest.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

const char prefix[] = "file_test";

std::string DirectoryPathFromFilePath(const std::string& path) {
  int last_separator = path.find_last_of("/\\");
  return path.substr(0, last_separator);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
