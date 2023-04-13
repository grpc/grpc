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

#include "src/core/lib/surface/channel_init.h"

#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

const grpc_channel_filter* FilterNamed(const char* name) {
  static auto* filters =
      new std::map<absl::string_view, const grpc_channel_filter*>;
  auto it = filters->find(name);
  if (it != filters->end()) return it->second;
  return filters
      ->emplace(name,
                new grpc_channel_filter{nullptr, nullptr, nullptr, 0, nullptr,
                                        nullptr, nullptr, 0, nullptr, nullptr,
                                        nullptr, nullptr, name})
      .first->second;
}

std::vector<std::string> GetFilterNames(const ChannelInit& init,
                                        grpc_channel_stack_type type,
                                        const ChannelArgs& args) {
  ChannelStackBuilderImpl b("test", type, args);
  init.CreateStack(&b);
  std::vector<std::string> names;
  for (auto f : b.stack()) {
    names.push_back(f->name);
  }
  return names;
}

TEST(ChannelInitTest, Empty) {
  EXPECT_EQ(GetFilterNames(ChannelInit::Builder().Build(), GRPC_CLIENT_CHANNEL,
                           ChannelArgs()),
            std::vector<std::string>());
}

TEST(ChannelInitTest, OneClientFilter) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_SERVER_CHANNEL, ChannelArgs()),
            std::vector<std::string>());
}

TEST(ChannelInitTest, DefaultLexicalOrdering) {
  // ChannelInit defaults to lexical ordering in the absense of other
  // constraints, to ensure that a stable ordering is produced between builds.
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"bar", "baz", "foo"}));
}

TEST(ChannelInitTest, AfterConstraintsApply) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"))
      .After({FilterNamed("foo")});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"baz", "foo", "bar"}));
}

TEST(ChannelInitTest, BeforeConstraintsApply) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"))
      .Before({FilterNamed("bar")});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"baz", "foo", "bar"}));
}

TEST(ChannelInitTest, PredicatesCanFilter) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"))
      .IfChannelArg("foo", true);
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"))
      .IfChannelArg("bar", false);
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL,
                           ChannelArgs().Set("foo", false)),
            std::vector<std::string>({}));
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("bar", true)),
      std::vector<std::string>({"bar", "foo"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL,
                           ChannelArgs().Set("bar", true).Set("foo", false)),
            std::vector<std::string>({"bar"}));
}

TEST(ChannelInitTest, CanAddTerminalFilter) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "bar"}));
}

TEST(ChannelInitTest, CanAddMultipleTerminalFilters) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"))
      .Terminal()
      .IfChannelArg("bar", false);
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"))
      .Terminal()
      .IfChannelArg("baz", false);
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo"}));
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("bar", true)),
      std::vector<std::string>({"foo", "bar"}));
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("baz", true)),
      std::vector<std::string>({"foo", "baz"}));
}

TEST(ChannelInitTest, CanAddBeforeAllOnce) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo")).BeforeAll();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "bar", "baz"}));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
