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

#include "src/core/ext/transport/chttp2/transport/flow_control_manager.h"

#include <cstdint>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"

namespace grpc_core {
namespace chttp2 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

MATCHER_P2(WindowUpdateFrame, stream_id, increment, "") {
  return arg.stream_id == stream_id && arg.increment == increment;
}

TEST(FlowControlManagerTest, NoUpdates) {
  FlowControlManager manager;
  EXPECT_FALSE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(), IsEmpty());
}

TEST(FlowControlManagerTest, TransportWindowUpdate) {
  FlowControlManager manager;
  manager.IncrementTransportWindow(100);
  EXPECT_TRUE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(),
              ElementsAre(VariantWith<Http2WindowUpdateFrame>(
                  WindowUpdateFrame(0, 100))));
  EXPECT_FALSE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(), IsEmpty());
}

TEST(FlowControlManagerTest, StreamWindowUpdate) {
  FlowControlManager manager;
  manager.IncrementStreamWindow(1, 100);
  EXPECT_TRUE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(),
              ElementsAre(VariantWith<Http2WindowUpdateFrame>(
                  WindowUpdateFrame(1, 100))));
  EXPECT_FALSE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(), IsEmpty());
}

TEST(FlowControlManagerTest, MultipleStreamWindowUpdates) {
  FlowControlManager manager;
  manager.IncrementStreamWindow(1, 100);
  manager.IncrementStreamWindow(2, 200);
  manager.IncrementStreamWindow(1, 50);
  EXPECT_TRUE(manager.HasWindowUpdates());
  EXPECT_THAT(
      manager.GetWindowUpdates(),
      UnorderedElementsAre(
          VariantWith<Http2WindowUpdateFrame>(WindowUpdateFrame(1, 150)),
          VariantWith<Http2WindowUpdateFrame>(WindowUpdateFrame(2, 200))));
  EXPECT_FALSE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(), IsEmpty());
}

TEST(FlowControlManagerTest, TransportAndStreamWindowUpdates) {
  FlowControlManager manager;
  manager.IncrementTransportWindow(500);
  manager.IncrementStreamWindow(1, 100);
  manager.IncrementStreamWindow(2, 200);
  EXPECT_TRUE(manager.HasWindowUpdates());
  EXPECT_THAT(
      manager.GetWindowUpdates(),
      UnorderedElementsAre(
          VariantWith<Http2WindowUpdateFrame>(WindowUpdateFrame(0, 500)),
          VariantWith<Http2WindowUpdateFrame>(WindowUpdateFrame(1, 100)),
          VariantWith<Http2WindowUpdateFrame>(WindowUpdateFrame(2, 200))));
  EXPECT_FALSE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(), IsEmpty());
}

TEST(FlowControlManagerTest, RemoveStream) {
  FlowControlManager manager;
  manager.IncrementStreamWindow(1, 100);
  manager.IncrementStreamWindow(2, 200);
  manager.RemoveStream(1);
  EXPECT_TRUE(manager.HasWindowUpdates());
  EXPECT_THAT(manager.GetWindowUpdates(),
              ElementsAre(VariantWith<Http2WindowUpdateFrame>(
                  WindowUpdateFrame(2, 200))));
  EXPECT_FALSE(manager.HasWindowUpdates());
}

}  // namespace
}  // namespace chttp2
}  // namespace grpc_core
