// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/http/client_authority_filter.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "test/core/filters/filter_test.h"

using ::testing::StrictMock;

namespace grpc_core {
namespace {

ChannelArgs TestChannelArgs(absl::string_view default_authority) {
  return ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, default_authority);
}

TEST(ClientAuthorityFilterTest, DefaultFails) {
  EXPECT_FALSE(
      ClientAuthorityFilter::Create(ChannelArgs(), ChannelFilter::Args()).ok());
}

TEST(ClientAuthorityFilterTest, WithArgSucceeds) {
  EXPECT_EQ(ClientAuthorityFilter::Create(TestChannelArgs("foo.test.google.au"),
                                          ChannelFilter::Args())
                .status(),
            absl::OkStatus());
}

TEST(ClientAuthorityFilterTest, NonStringArgFails) {
  EXPECT_FALSE(ClientAuthorityFilter::Create(
                   ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, 123),
                   ChannelFilter::Args())
                   .ok());
}

TEST(ClientAuthorityFilterTest, PromiseCompletesImmediatelyAndSetsAuthority) {
  StrictMock<FilterTest::Call> call(FilterTest(*ClientAuthorityFilter::Create(
      TestChannelArgs("foo.test.google.au"), ChannelFilter::Args())));
  EXPECT_CALL(call,
              Started(HasMetadataKeyValue(":authority", "foo.test.google.au")));
  call.Start(call.NewClientMetadata());
}

TEST(ClientAuthorityFilterTest,
     PromiseCompletesImmediatelyAndDoesNotSetAuthority) {
  StrictMock<FilterTest::Call> call(FilterTest(*ClientAuthorityFilter::Create(
      TestChannelArgs("foo.test.google.au"), ChannelFilter::Args())));
  EXPECT_CALL(call,
              Started(HasMetadataKeyValue(":authority", "bar.test.google.au")));
  call.Start(call.NewClientMetadata({{":authority", "bar.test.google.au"}}));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // TODO(ctiller): promise_based_call currently demands to instantiate an event
  // engine which needs grpc to be initialized.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
