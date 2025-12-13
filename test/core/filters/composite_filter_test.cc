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

#include "src/core/filter/composite/composite_filter.h"

#include <memory>
#include <utility>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

class CompositeFilterTest : public FilterTest<CompositeFilter> {
 protected:
  RefCountedPtr<const FilterConfig> MakeConfig() {
    auto config = MakeRefCounted<CompositeFilter::Config>();
    // FIXME: populate
    return config;
  }
};

TEST_F(CompositeFilterTest, CreateSucceeds) {
  auto filter = CompositeFilter::Create(
      channel_args, ChannelFilter::Args(/*instance_id=*/0, MakeConfig()));
  EXPECT_TRUE(filter.ok()) << filter.status();
}

// FIXME: flesh this out, and add other tests
TEST_F(CompositeFilterTest, BasicCall) {
  Call call(MakeChannel(channel_args, MakeConfig()).value());
  EXPECT_EVENT(Started(&call, ::testing::_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata({{"grpc-status", "0"}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  Step();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
