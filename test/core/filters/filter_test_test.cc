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

#include "test/core/filters/filter_test.h"

#include <functional>
#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

using ::testing::_;
using ::testing::StrictMock;

namespace grpc_core {
namespace {

class NoOpFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(CallArgs args,
                                                     NextPromiseFactory next) {
    return next(std::move(args));
  }
};

TEST(FilterTestTest, NoOp) { FilterTest test{NoOpFilter()}; }

TEST(FilterTestTest, MakeCall) {
  StrictMock<FilterTest::Call> call(FilterTest{NoOpFilter()});
}

TEST(FilterTestTest, MakeClientMetadata) {
  StrictMock<FilterTest::Call> call(FilterTest{NoOpFilter()});
  auto md = call.NewClientMetadata({{":path", "foo.bar"}});
  EXPECT_EQ(md->get_pointer(HttpPathMetadata())->as_string_view(), "foo.bar");
}

TEST(FilterTestTest, MakeServerMetadata) {
  StrictMock<FilterTest::Call> call(FilterTest{NoOpFilter()});
  auto md = call.NewServerMetadata({{":status", "200"}});
  EXPECT_EQ(md->get(HttpStatusMetadata()), 200);
}

TEST(FilterTestTest, CanStart) {
  StrictMock<FilterTest::Call> call(FilterTest{NoOpFilter()});
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.Step();
}

TEST(FilterTestTest, CanFinish) {
  StrictMock<FilterTest::Call> call(FilterTest{NoOpFilter()});
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata());
  EXPECT_CALL(call, Finished(_));
  call.Step();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
