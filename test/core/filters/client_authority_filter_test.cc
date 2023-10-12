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

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>

#include "test/core/filters/filter_test.h"

using ::testing::StrictMock;

namespace grpc_core {
namespace {

using ClientAuthorityFilterTest = FilterTest<ClientAuthorityFilter>;

ChannelArgs TestChannelArgs(absl::string_view default_authority) {
  return ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, default_authority);
}

TEST_F(ClientAuthorityFilterTest, DefaultFails) {
  EXPECT_FALSE(MakeChannel(ChannelArgs()).ok());
}

TEST_F(ClientAuthorityFilterTest, WithArgSucceeds) {
  EXPECT_EQ(MakeChannel(TestChannelArgs("foo.test.google.au")).status(),
            absl::OkStatus());
}

TEST_F(ClientAuthorityFilterTest, NonStringArgFails) {
  EXPECT_FALSE(
      MakeChannel(ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, 123)).ok());
}

TEST_F(ClientAuthorityFilterTest, PromiseCompletesImmediatelyAndSetsAuthority) {
  StrictMock<FilterTest::Call> call(
      MakeChannel(TestChannelArgs("foo.test.google.au")).value());
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(":authority", "foo.test.google.au")));
  call.Start(call.NewClientMetadata());
}

TEST_F(ClientAuthorityFilterTest,
       PromiseCompletesImmediatelyAndDoesNotSetAuthority) {
  StrictMock<FilterTest::Call> call(
      MakeChannel(TestChannelArgs("foo.test.google.au")).value());
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(":authority", "bar.test.google.au")));
  call.Start(call.NewClientMetadata({{":authority", "bar.test.google.au"}}));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
