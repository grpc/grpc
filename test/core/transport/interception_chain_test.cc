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

#include "gtest/gtest.h"

#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {
namespace {

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
};

///////////////////////////////////////////////////////////////////////////////
// Test call interceptor - hijacks calls

template <int I>
class TestHijackingInterceptor final : public Interceptor {
 public:
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
        MakeCall(Arena::MakePooled<ClientMetadata>(arena), nullptr, arena);
    destination->StartCall(std::move(call.unstarted_handler));
    auto trailing_md_promise = call.initiator.PullServerTrailingMetadata();
    Poll<ServerMetadataHandle> trailing_md;
    do {
      trailing_md = trailing_md_promise();
    } while (trailing_md.pending());
    return FinishedCall{std::move(call.initiator), destination_->TakeMetadata(),
                        std::move(trailing_md.value())};
  }

 private:
  class Destination : public CallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      EXPECT_EQ(metadata_.get(), nullptr);
      metadata_ =
          Arena::MakePooled<ClientMetadata>(unstarted_call_handler.arena());
      *metadata_ =
          unstarted_call_handler.UnprocessedClientInitialMetadata().Copy();
    }

    ClientMetadataHandle TakeMetadata() {
      EXPECT_NE(metadata_.get(), nullptr);
      return std::move(metadata_);
    }

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
  int num_destination_calls = 0;
  auto r = InterceptionChain::Builder(destination()).Build(ChannelArgs());
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(num_destination_calls, 0);
  auto finished_call = RunCall(r.value().get());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
