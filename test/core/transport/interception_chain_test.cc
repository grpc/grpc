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

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {
namespace {

///////////////////////////////////////////////////////////////////////////////
// Mutate metadata by annotating that it passed through a filter "x"

void AnnotatePassedThrough(ClientMetadata& md, int x) {
  md.Append(absl::StrCat("passed-through-", x), Slice::FromCopiedString("true"),
            [](absl::string_view, const Slice&) { Crash("unreachable"); });
}

///////////////////////////////////////////////////////////////////////////////
// CreationLog helps us reason about filter creation order by logging a small
// record of each filter's creation.

struct CreationLogEntry {
  size_t filter_instance_id;
  size_t type_tag;

  bool operator==(const CreationLogEntry& other) const {
    return filter_instance_id == other.filter_instance_id &&
           type_tag == other.type_tag;
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const CreationLogEntry& entry) {
    return os << "{filter_instance_id=" << entry.filter_instance_id
              << ", type_tag=" << entry.type_tag << "}";
  }
};

struct CreationLog {
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() { return "creation_log"; }
  std::vector<CreationLogEntry> entries;
};

void MaybeLogCreation(const ChannelArgs& channel_args,
                      ChannelFilter::Args filter_args, size_t type_tag) {
  auto* log = channel_args.GetPointer<CreationLog>("creation_log");
  if (log == nullptr) return;
  log->entries.push_back(CreationLogEntry{filter_args.instance_id(), type_tag});
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

  static absl::StatusOr<std::unique_ptr<TestFilter<I>>> Create(
      const ChannelArgs& channel_args, ChannelFilter::Args filter_args) {
    MaybeLogCreation(channel_args, filter_args, I);
    return std::make_unique<TestFilter<I>>();
  }

 private:
  std::unique_ptr<int> i_ = std::make_unique<int>(I);
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
// Test call filter that fails to instantiate

template <int I>
class FailsToInstantiateFilter {
 public:
  class Call {
   public:
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
  };

  static absl::StatusOr<std::unique_ptr<FailsToInstantiateFilter<I>>> Create(
      const ChannelArgs& channel_args, ChannelFilter::Args filter_args) {
    MaybeLogCreation(channel_args, filter_args, I);
    return absl::InternalError(absl::StrCat("👊 failed to instantiate ", I));
  }
};

template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnClientInitialMetadata;
template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnServerInitialMetadata;
template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnClientToServerMessage;
template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnServerToClientMessage;
template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnServerTrailingMetadata;
template <int I>
const NoInterceptor FailsToInstantiateFilter<I>::Call::OnFinalize;

///////////////////////////////////////////////////////////////////////////////
// Test call interceptor - consumes calls

template <int I>
class TestConsumingInterceptor final : public Interceptor {
 public:
  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    Consume(std::move(unstarted_call_handler))
        .PushServerTrailingMetadata(
            ServerMetadataFromStatus(absl::InternalError("👊 consumed")));
  }
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<TestConsumingInterceptor<I>>> Create(
      const ChannelArgs& channel_args, ChannelFilter::Args filter_args) {
    MaybeLogCreation(channel_args, filter_args, I);
    return MakeRefCounted<TestConsumingInterceptor<I>>();
  }
};

///////////////////////////////////////////////////////////////////////////////
// Test call interceptor - fails to instantiate

template <int I>
class TestFailingInterceptor final : public Interceptor {
 public:
  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    Crash("unreachable");
  }
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<TestFailingInterceptor<I>>> Create(
      const ChannelArgs& channel_args, ChannelFilter::Args filter_args) {
    MaybeLogCreation(channel_args, filter_args, I);
    return absl::InternalError(absl::StrCat("👊 failed to instantiate ", I));
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
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<TestHijackingInterceptor<I>>> Create(
      const ChannelArgs& channel_args, ChannelFilter::Args filter_args) {
    MaybeLogCreation(channel_args, filter_args, I);
    return MakeRefCounted<TestHijackingInterceptor<I>>();
  }
};

///////////////////////////////////////////////////////////////////////////////
// Test fixture

class InterceptionChainTest : public ::testing::Test {
 protected:
  InterceptionChainTest() {}
  ~InterceptionChainTest() override {}

  RefCountedPtr<UnstartedCallDestination> destination() { return destination_; }

  struct FinishedCall {
    CallInitiator call;
    ClientMetadataHandle client_metadata;
    ServerMetadataHandle server_metadata;
  };

  // Run a call through a UnstartedCallDestination until it's complete.
  FinishedCall RunCall(UnstartedCallDestination* destination) {
    auto* arena = call_arena_allocator_->MakeArena();
    auto call = MakeCallPair(Arena::MakePooled<ClientMetadata>(), nullptr,
                             arena, call_arena_allocator_, nullptr);
    Poll<ServerMetadataHandle> trailing_md;
    call.initiator.SpawnInfallible(
        "run_call", [destination, &call, &trailing_md]() mutable {
          gpr_log(GPR_INFO, "👊 start call");
          destination->StartCall(std::move(call.handler));
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
  class Destination final : public UnstartedCallDestination {
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
      unstarted_call_handler.PushServerTrailingMetadata(
          ServerMetadataFromStatus(absl::InternalError("👊 cancelled")));
    }

    void Orphaned() override {}

    ClientMetadataHandle TakeMetadata() { return std::move(metadata_); }

   private:
    ClientMetadataHandle metadata_;
  };
  RefCountedPtr<Destination> destination_ = MakeRefCounted<Destination>();
  RefCountedPtr<CallArenaAllocator> call_arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test"),
          1024);
};

///////////////////////////////////////////////////////////////////////////////
// Tests begin

TEST_F(InterceptionChainTest, Empty) {
  auto r = InterceptionChainBuilder(ChannelArgs()).Build(destination());
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
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestConsumingInterceptor<1>>()
               .Build(destination());
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
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestHijackingInterceptor<1>>()
               .Build(destination());
  ASSERT_TRUE(r.ok()) << r.status();
  auto finished_call = RunCall(r.value().get());
  EXPECT_EQ(finished_call.server_metadata->get(GrpcStatusMetadata()),
            GRPC_STATUS_INTERNAL);
  EXPECT_EQ(finished_call.server_metadata->get_pointer(GrpcMessageMetadata())
                ->as_string_view(),
            "👊 cancelled");
  EXPECT_NE(finished_call.client_metadata, nullptr);
}

TEST_F(InterceptionChainTest, FiltersThenHijacked) {
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestFilter<1>>()
               .Add<TestHijackingInterceptor<2>>()
               .Build(destination());
  ASSERT_TRUE(r.ok()) << r.status();
  auto finished_call = RunCall(r.value().get());
  EXPECT_EQ(finished_call.server_metadata->get(GrpcStatusMetadata()),
            GRPC_STATUS_INTERNAL);
  EXPECT_EQ(finished_call.server_metadata->get_pointer(GrpcMessageMetadata())
                ->as_string_view(),
            "👊 cancelled");
  EXPECT_NE(finished_call.client_metadata, nullptr);
  std::string backing;
  EXPECT_EQ(finished_call.client_metadata->GetStringValue("passed-through-1",
                                                          &backing),
            "true");
}

TEST_F(InterceptionChainTest, FailsToInstantiateInterceptor) {
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestFailingInterceptor<1>>()
               .Build(destination());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(r.status().message(), "👊 failed to instantiate 1");
}

TEST_F(InterceptionChainTest, FailsToInstantiateInterceptor2) {
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestFilter<1>>()
               .Add<TestFailingInterceptor<2>>()
               .Build(destination());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(r.status().message(), "👊 failed to instantiate 2");
}

TEST_F(InterceptionChainTest, FailsToInstantiateFilter) {
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<FailsToInstantiateFilter<1>>()
               .Build(destination());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(r.status().message(), "👊 failed to instantiate 1");
}

TEST_F(InterceptionChainTest, FailsToInstantiateFilter2) {
  auto r = InterceptionChainBuilder(ChannelArgs())
               .Add<TestFilter<1>>()
               .Add<FailsToInstantiateFilter<2>>()
               .Build(destination());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(r.status().message(), "👊 failed to instantiate 2");
}

TEST_F(InterceptionChainTest, CreationOrderCorrect) {
  CreationLog log;
  auto r = InterceptionChainBuilder(ChannelArgs().SetObject(&log))
               .Add<TestFilter<1>>()
               .Add<TestFilter<2>>()
               .Add<TestFilter<3>>()
               .Add<TestConsumingInterceptor<4>>()
               .Add<TestFilter<1>>()
               .Add<TestFilter<2>>()
               .Add<TestFilter<3>>()
               .Add<TestConsumingInterceptor<4>>()
               .Add<TestFilter<1>>()
               .Build(destination());
  EXPECT_THAT(log.entries, ::testing::ElementsAre(
                               CreationLogEntry{0, 1}, CreationLogEntry{0, 2},
                               CreationLogEntry{0, 3}, CreationLogEntry{0, 4},
                               CreationLogEntry{1, 1}, CreationLogEntry{1, 2},
                               CreationLogEntry{1, 3}, CreationLogEntry{1, 4},
                               CreationLogEntry{2, 1}));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_tracer_init();
  gpr_log_verbosity_init();
  return RUN_ALL_TESTS();
}
