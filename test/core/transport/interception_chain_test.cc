// Copyright 2024 gRPC authors.
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

#include "src/core/lib/transport/interception_chain.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {
namespace {

MATCHER(IsPending, "") {
  if (arg.ready()) {
    *result_listener << "is ready";
    return false;
  }
  return true;
}

MATCHER(IsReady, "") {
  if (arg.pending()) {
    *result_listener << "is pending";
    return false;
  }
  return true;
}

MATCHER_P(IsReady, value, "") {
  if (arg.pending()) {
    *result_listener << "is pending";
    return false;
  }
  if (arg.value() != value) {
    *result_listener << "is " << ::testing::PrintToString(arg.value());
    return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Mutate metadata by annotating that it passed through a filter "x"

void AnnotatePassedThrough(ClientMetadata& md, int x) {
  md.Append(absl::StrCat("passed-through-", x), Slice::FromCopiedString("true"),
            [](absl::string_view, const Slice&) { Crash("unreachable"); });
}

///////////////////////////////////////////////////////////////////////////////
// Test call filter

template <int I>
class TestFilter {
 public:
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md) {
      AnnotatePassedThrough(md, I);
    }
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
  };
};

template <int I>
const NoInterceptor TestFilter<I>::Call::OnServerInitialMetadata;
template <int I>
const NoInterceptor TestFilter<I>::Call::OnClientToServerMessage;
template <int I>
const NoInterceptor TestFilter<I>::Call::OnServerToClientMessage;
template <int I>
const NoInterceptor TestFilter<I>::Call::OnServerTrailingMetadata;
template <int I>
const NoInterceptor TestFilter<I>::Call::OnFinalize;

///////////////////////////////////////////////////////////////////////////////
// Test call interceptor - consumes calls

template <int I>
class TestConsumingInterceptor final : public Interceptor {
 public:
  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    Consume(std::move(unstarted_call_handler))
        .Cancel(ServerMetadataFromStatus(absl::InternalError("👊 consumed")));
  }
  static absl::StatusOr<TestConsumingInterceptor<I>> Create(
      const ChannelArgs& args, Empty) {
    return TestConsumingInterceptor<I>{};
  }
};

///////////////////////////////////////////////////////////////////////////////
// Test call interceptor - hijacks calls

template <int I>
class TestHijackingInterceptor final : public Interceptor {
 public:
  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.SpawnInfallible(
        "hijack", [this, unstarted_call_handler]() mutable {
          return Map(Hijack(std::move(unstarted_call_handler)),
                     [](ValueOrFailure<HijackedCall> hijacked_call) {
                       ForwardCall(
                           hijacked_call.value().original_call_handler(),
                           hijacked_call.value().MakeCall());
                       return Empty{};
                     });
        });
  }
  static absl::StatusOr<TestHijackingInterceptor<I>> Create(
      const ChannelArgs& args, Empty) {
    return TestHijackingInterceptor<I>{};
  }
};

///////////////////////////////////////////////////////////////////////////////
// Test fixture

class InterceptionChainTest : public ::testing::Test {
 protected:
  InterceptionChainTest() {}
  ~InterceptionChainTest() override {}

  std::shared_ptr<CallDestination> destination() { return destination_; }

  struct FinishedCall {
    CallInitiator call;
    ClientMetadataHandle client_metadata;
    ServerMetadataHandle server_metadata;
  };

  // Run a call through a CallDestination until it's complete.
  FinishedCall RunCall(CallDestination* destination) {
    auto* arena = Arena::Create(1024, &memory_allocator_);
    auto call =
        MakeCallPair(Arena::MakePooled<ClientMetadata>(), nullptr, arena, true);
    Poll<ServerMetadataHandle> trailing_md;
    call.initiator.SpawnInfallible(
        "run_call", [destination, &call, &trailing_md]() mutable {
          gpr_log(GPR_INFO, "👊 start call");
          destination->StartCall(std::move(call.unstarted_handler));
          return Map(call.initiator.PullServerTrailingMetadata(),
                     [&trailing_md](ServerMetadataHandle md) {
                       trailing_md = std::move(md);
                       return Empty{};
                     });
        });
    EXPECT_THAT(trailing_md, IsReady());
    return FinishedCall{std::move(call.initiator), destination_->TakeMetadata(),
                        std::move(trailing_md.value())};
  }

 private:
  class Destination : public CallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      gpr_log(GPR_INFO, "👊 started call: metadata=%s",
              unstarted_call_handler.UnprocessedClientInitialMetadata()
                  .DebugString()
                  .c_str());
      EXPECT_EQ(metadata_.get(), nullptr);
      metadata_ = Arena::MakePooled<ClientMetadata>();
      *metadata_ =
          unstarted_call_handler.UnprocessedClientInitialMetadata().Copy();
      unstarted_call_handler.Cancel(
          ServerMetadataFromStatus(absl::InternalError("👊 cancelled")));
    }

    ClientMetadataHandle TakeMetadata() { return std::move(metadata_); }

   private:
    ClientMetadataHandle metadata_;
  };
  std::shared_ptr<Destination> destination_ = std::make_shared<Destination>();
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

///////////////////////////////////////////////////////////////////////////////
// Tests begin

TEST_F(InterceptionChainTest, Empty) {
  auto r = InterceptionChain::Builder(destination()).Build(ChannelArgs());
  ASSERT_TRUE(r.ok()) << r.status();
  auto finished_call = RunCall(r.value().get());
  EXPECT_EQ(finished_call.server_metadata->get(GrpcStatusMetadata()),
            GRPC_STATUS_INTERNAL);
  EXPECT_EQ(finished_call.server_metadata->get_pointer(GrpcMessageMetadata())
                ->as_string_view(),
            "👊 cancelled");
  EXPECT_NE(finished_call.client_metadata, nullptr);
}

TEST_F(InterceptionChainTest, Consumed) {
  auto r = InterceptionChain::Builder(destination())
               .Add<TestConsumingInterceptor<1>>()
               .Build(ChannelArgs());
  ASSERT_TRUE(r.ok()) << r.status();
  auto finished_call = RunCall(r.value().get());
  EXPECT_EQ(finished_call.server_metadata->get(GrpcStatusMetadata()),
            GRPC_STATUS_INTERNAL);
  EXPECT_EQ(finished_call.server_metadata->get_pointer(GrpcMessageMetadata())
                ->as_string_view(),
            "👊 consumed");
  EXPECT_EQ(finished_call.client_metadata, nullptr);
}

TEST_F(InterceptionChainTest, Hijacked) {
  auto r = InterceptionChain::Builder(destination())
               .Add<TestHijackingInterceptor<1>>()
               .Build(ChannelArgs());
  ASSERT_TRUE(r.ok()) << r.status();
  auto finished_call = RunCall(r.value().get());
  EXPECT_EQ(finished_call.server_metadata->get(GrpcStatusMetadata()),
            GRPC_STATUS_INTERNAL);
  EXPECT_EQ(finished_call.server_metadata->get_pointer(GrpcMessageMetadata())
                ->as_string_view(),
            "👊 cancelled");
  EXPECT_NE(finished_call.client_metadata, nullptr);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_tracer_init();
  gpr_log_verbosity_init();
  return RUN_ALL_TESTS();
}
