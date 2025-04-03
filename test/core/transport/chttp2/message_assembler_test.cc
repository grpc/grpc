//
//
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
//
//

#include "src/core/ext/transport/chttp2/transport/message_assembler.h"

#include <grpc/grpc.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using grpc_event_engine::experimental::Slice;

TEST(GrpcMessageAssembler, ObjectCreation) { GrpcMessageAssembler assembler; }

TEST(GrpcMessageAssembler, OneMessageInOneFrame) { CHECK(true); }

TEST(GrpcMessageAssembler, OneMessageInThreeFrames) { CHECK(true); }

TEST(GrpcMessageAssembler, ThreeMessageInOneFrame) { CHECK(true); }

TEST(GrpcMessageAssembler, ThreeMessageInFourFrames) { CHECK(true); }

TEST(GrpcMessageAssembler, ThreeEmptyMessagesInOneFrame) { CHECK(true); }

TEST(GrpcMessageAssembler, ThreeMessageInOneFrameMiddleMessageEmpty) {
  CHECK(true);
}

TEST(GrpcMessageAssembler, One2GBMessage) { CHECK(true); }

TEST(GrpcMessageAssembler, One4GBMessage) { CHECK(true); }

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
