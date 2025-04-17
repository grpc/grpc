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

#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/call/call_arena_allocator.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

const grpc_channel_filter* FilterNamed(const char* name) {
  static auto* filters =
      new std::map<absl::string_view, const grpc_channel_filter*>;
  auto it = filters->find(name);
  if (it != filters->end()) return it->second;
  static auto* name_factories =
      new std::vector<std::unique_ptr<UniqueTypeName::Factory>>();
  name_factories->emplace_back(std::make_unique<UniqueTypeName::Factory>(name));
  auto unique_type_name = name_factories->back()->Create();
  return filters
      ->emplace(name,
                new grpc_channel_filter{nullptr, nullptr, 0, nullptr, nullptr,
                                        nullptr, 0, nullptr, nullptr, nullptr,
                                        nullptr, unique_type_name})
      .first->second;
}

std::vector<std::string> GetFilterNames(const ChannelInit& init,
                                        grpc_channel_stack_type type,
                                        const ChannelArgs& args) {
  ChannelStackBuilderImpl b("test", type, args);
  if (!init.CreateStack(&b)) return {};
  std::vector<std::string> names;
  for (auto f : b.stack()) {
    names.push_back(std::string(f->name.name()));
  }
  EXPECT_NE(names, std::vector<std::string>());
  return names;
}

TEST(ChannelInitTest, Empty) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"terminator"}));
}

TEST(ChannelInitTest, OneClientFilter) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  b.RegisterFilter(GRPC_SERVER_CHANNEL, FilterNamed("terminator")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "terminator"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_SERVER_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"terminator"}));
}

TEST(ChannelInitTest, DefaultLexicalOrdering) {
  // ChannelInit defaults to lexical ordering in the absence of other
  // constraints, to ensure that a stable ordering is produced between builds.
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"bar", "baz", "foo", "aaa"}));
}

TEST(ChannelInitTest, AfterConstraintsApply) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"))
      .After({FilterNamed("foo")->name});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"baz", "foo", "bar", "aaa"}));
}

TEST(ChannelInitTest, BeforeConstraintsApply) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"))
      .Before({FilterNamed("bar")->name});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"baz", "foo", "bar", "aaa"}));
}

TEST(ChannelInitTest, PredicatesCanFilter) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"))
      .IfChannelArg("foo", true);
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"))
      .IfChannelArg("bar", false);
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  auto init = b.Build();
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "aaa"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL,
                           ChannelArgs().Set("foo", false)),
            std::vector<std::string>({"aaa"}));
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("bar", true)),
      std::vector<std::string>({"bar", "foo", "aaa"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL,
                           ChannelArgs().Set("bar", true).Set("foo", false)),
            std::vector<std::string>({"bar", "aaa"}));
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
            std::vector<std::string>());
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("bar", true)),
      std::vector<std::string>({"foo", "bar"}));
  EXPECT_EQ(
      GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs().Set("baz", true)),
      std::vector<std::string>({"foo", "baz"}));
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL,
                           ChannelArgs().Set("bar", true).Set("baz", true)),
            std::vector<std::string>());
}

TEST(ChannelInitTest, CanAddBeforeAllOnce) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo")).BeforeAll();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "bar", "baz", "aaa"}));
}

TEST(ChannelInitDeathTest, CanAddBeforeAllTwice) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo")).BeforeAll();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("bar")).BeforeAll();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("baz"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  EXPECT_DEATH_IF_SUPPORTED(b.Build(), "Unresolvable graph of channel filters");
}

TEST(ChannelInitTest, CanPostProcessFilters) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("foo"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("aaa")).Terminal();
  int called_post_processor = 0;
  b.RegisterPostProcessor(
      GRPC_CLIENT_CHANNEL,
      ChannelInit::PostProcessorSlot::kXdsChannelStackModifier,
      [&called_post_processor](ChannelStackBuilder& b) {
        ++called_post_processor;
        b.mutable_stack()->push_back(FilterNamed("bar"));
      });
  auto init = b.Build();
  EXPECT_EQ(called_post_processor, 0);
  EXPECT_EQ(GetFilterNames(init, GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"foo", "aaa", "bar"}));
}

TEST(ChannelInitTest, OrderingConstraintsAreSatisfied) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).FloatToTop();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b"));
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("a")).SinkToBottom();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"c", "b", "a", "terminator"}));
}

TEST(ChannelInitTest, AmbiguousTopCrashes) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).FloatToTop();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b")).FloatToTop();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_DEATH_IF_SUPPORTED(b.Build(), "Ambiguous");
}

TEST(ChannelInitTest, ExplicitOrderingBetweenTopResolvesAmbiguity) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).FloatToTop();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b"))
      .FloatToTop()
      .After({FilterNamed("c")->name});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"c", "b", "terminator"}));
}

TEST(ChannelInitTest, AmbiguousBottomCrashes) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).SinkToBottom();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b")).SinkToBottom();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_DEATH_IF_SUPPORTED(b.Build(), "Ambiguous");
}

TEST(ChannelInitTest, ExplicitOrderingBetweenBottomResolvesAmbiguity) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).SinkToBottom();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b"))
      .SinkToBottom()
      .After({FilterNamed("c")->name});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"c", "b", "terminator"}));
}

TEST(ChannelInitTest, BottomCanComeBeforeTopWithExplicitOrdering) {
  ChannelInit::Builder b;
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("c")).FloatToTop();
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("b"))
      .SinkToBottom()
      .Before({FilterNamed("c")->name});
  b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterNamed("terminator")).Terminal();
  EXPECT_EQ(GetFilterNames(b.Build(), GRPC_CLIENT_CHANNEL, ChannelArgs()),
            std::vector<std::string>({"b", "c", "terminator"}));
}

class TestFilter1 {
 public:
  explicit TestFilter1(int* p) : p_(p) {}

  static absl::string_view TypeName() { return "TestFilter1"; }

  static absl::StatusOr<std::unique_ptr<TestFilter1>> Create(
      const ChannelArgs& args, ChannelFilter::Args) {
    EXPECT_EQ(args.GetInt("foo"), 1);
    return std::make_unique<TestFilter1>(args.GetPointer<int>("p"));
  }

  static const grpc_channel_filter kFilter;

  class Call {
   public:
    explicit Call(TestFilter1* filter) {
      EXPECT_EQ(*filter->x_, 0);
      *filter->x_ = 1;
      ++*filter->p_;
    }
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
  };

 private:
  std::unique_ptr<int> x_ = std::make_unique<int>(0);
  int* const p_;
};

const grpc_channel_filter TestFilter1::kFilter = {
    nullptr, nullptr, 0,       nullptr,
    nullptr, nullptr, 0,       nullptr,
    nullptr, nullptr, nullptr, GRPC_UNIQUE_TYPE_NAME_HERE("test_filter1")};
const NoInterceptor TestFilter1::Call::OnClientInitialMetadata;
const NoInterceptor TestFilter1::Call::OnServerInitialMetadata;
const NoInterceptor TestFilter1::Call::OnServerTrailingMetadata;
const NoInterceptor TestFilter1::Call::OnClientToServerMessage;
const NoInterceptor TestFilter1::Call::OnClientToServerHalfClose;
const NoInterceptor TestFilter1::Call::OnServerToClientMessage;
const NoInterceptor TestFilter1::Call::OnFinalize;

TEST(ChannelInitTest, CanCreateFilterWithCall) {
  grpc::testing::TestGrpcScope g;
  ChannelInit::Builder b;
  b.RegisterFilter<TestFilter1>(GRPC_CLIENT_CHANNEL);
  auto init = b.Build();
  int p = 0;
  InterceptionChainBuilder chain_builder{
      ChannelArgs().Set("foo", 1).Set("p", ChannelArgs::UnownedPointer(&p))};
  init.AddToInterceptionChainBuilder(GRPC_CLIENT_CHANNEL, chain_builder);
  int handled = 0;
  auto stack = chain_builder.Build(MakeCallDestinationFromHandlerFunction(
      [&handled](CallHandler) { ++handled; }));
  ASSERT_TRUE(stack.ok()) << stack.status();
  RefCountedPtr<CallArenaAllocator> allocator =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test"),
          1024);
  auto event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  auto arena = allocator->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine.get());
  auto call = MakeCallPair(Arena::MakePooledForOverwrite<ClientMetadata>(),
                           std::move(arena));
  (*stack)->StartCall(std::move(call.handler));
  EXPECT_EQ(p, 1);
  EXPECT_EQ(handled, 1);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
