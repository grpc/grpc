//
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
//

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(MakeSubchannelArgs, UsesChannelDefaultAuthorityByDefault) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs(), ChannelArgs(), nullptr, "foo.example.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_DEFAULT_AUTHORITY), "foo.example.com");
}

TEST(MakeSubchannelArgs, DefaultAuthorityFromChannelArgs) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, "bar.example.com"),
      ChannelArgs(), nullptr, "foo.example.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_DEFAULT_AUTHORITY), "bar.example.com");
}

TEST(MakeSubchannelArgs, DefaultAuthorityFromResolver) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs(),
      ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, "bar.example.com"), nullptr,
      "foo.example.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_DEFAULT_AUTHORITY), "bar.example.com");
}

TEST(MakeSubchannelArgs,
     DefaultAuthorityFromChannelArgsOverridesValueFromResolver) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, "bar.example.com"),
      ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, "baz.example.com"), nullptr,
      "foo.example.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_DEFAULT_AUTHORITY), "bar.example.com");
}

TEST(MakeSubchannelArgs, ArgsFromChannelTrumpPerAddressArgs) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs().Set("foo", 1), ChannelArgs().Set("foo", 2), nullptr,
      "foo.example.com");
  EXPECT_EQ(args.GetInt("foo"), 1);
}

TEST(MakeSubchannelArgs, StripsOutNoSubchannelArgs) {
  ChannelArgs args = ClientChannelFilter::MakeSubchannelArgs(
      ChannelArgs().Set(GRPC_ARG_NO_SUBCHANNEL_PREFIX "foo", 1),
      ChannelArgs().Set(GRPC_ARG_NO_SUBCHANNEL_PREFIX "bar", 1), nullptr,
      "foo.example.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_NO_SUBCHANNEL_PREFIX "foo"), absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_NO_SUBCHANNEL_PREFIX "bar"), absl::nullopt);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
