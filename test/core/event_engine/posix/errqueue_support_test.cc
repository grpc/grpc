// Copyright 2025 The gRPC Authors
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

#include "gtest/gtest.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"

namespace grpc_event_engine {
namespace experimental {
namespace {

#ifdef GRPC_POSIX_SOCKET_TCP
TEST(KernelSupportsErrqueueTest, Basic) {
#ifdef GRPC_LINUX_ERRQUEUE
  int expected_value = true;
#else
  int expected_value = false;
#endif
  EXPECT_EQ(KernelSupportsErrqueue(), expected_value);
}
#endif /* GRPC_POSIX_SOCKET_TCP */

}  // namespace
}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int r = RUN_ALL_TESTS();
  return r;
}
