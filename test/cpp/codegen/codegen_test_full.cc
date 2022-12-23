//
//
// Copyright 2017 gRPC authors.
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
//

#include <gtest/gtest.h>

#include <grpc/support/time.h>
#include <grpcpp/completion_queue.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace {

class CodegenTestFull : public ::testing::Test {};

TEST_F(CodegenTestFull, Init) {
  grpc::CompletionQueue cq;
  void* tag = nullptr;
  bool ok = false;
  cq.AsyncNext(&tag, &ok, gpr_time_0(GPR_CLOCK_REALTIME));
  ASSERT_FALSE(ok);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
