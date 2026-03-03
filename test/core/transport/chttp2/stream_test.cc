//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/stream.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/ref_counted_ptr.h"
#include "testing/base/public/gunit.h"

namespace grpc_core {
namespace http2 {
namespace testing {

TEST(StreamTest, Minimal) {
  ExecCtx exec_ctx;
  chttp2::TransportFlowControl tfc("test", false, nullptr);
  RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  CallInitiatorAndHandler call_pair = MakeCallPair(
      Arena::MakePooledForOverwrite<ClientMetadata>(), std::move(arena));
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair.handler.StartCall(), tfc);
  stream->InitializeStream(123u, true, true);
  EXPECT_EQ(stream->GetStreamId(), 123u);
}

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
