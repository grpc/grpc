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

#include "src/core/call/server_call.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>

#include <atomic>

#include "absl/status/status.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/telemetry/call_tracer.h"
#include "test/core/call/batch_builder.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
const absl::string_view kDefaultPath = "/foo/bar";
}

class ServerCallTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  ~ServerCallTest() override {
    grpc_metadata_array_destroy(&publish_initial_metadata_);
  }

  grpc_call* InitCall(ClientMetadataHandle client_initial_metadata) {
    CHECK_EQ(call_, nullptr);
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    auto call =
        MakeCallPair(std::move(client_initial_metadata), std::move(arena));
    call.initiator.SpawnGuarded(
        "initial_metadata",
        [this, handler = call.handler.StartCall()]() mutable {
          return TrySeq(
              handler.PullClientInitialMetadata(),
              [this,
               handler](ClientMetadataHandle client_initial_metadata) mutable {
                call_.store(MakeServerCall(std::move(handler),
                                           std::move(client_initial_metadata),
                                           &test_server_, cq_,
                                           &publish_initial_metadata_),
                            std::memory_order_release);
                return absl::OkStatus();
              });
        });
    while (true) {
      auto* result = call_.load(std::memory_order_acquire);
      if (result != nullptr) return result;
    }
  }

  ClientMetadataHandle MakeClientInitialMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          md) {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kDefaultPath));
    for (const auto& pair : md) {
      client_initial_metadata->Append(
          pair.first, Slice::FromCopiedBuffer(pair.second),
          [](absl::string_view error, const Slice&) { Crash(error); });
    }
    return client_initial_metadata;
  }

  std::optional<std::string> GetClientInitialMetadata(absl::string_view key) {
    CHECK_NE(call_.load(std::memory_order_acquire), nullptr);
    return FindInMetadataArray(publish_initial_metadata_, key);
  }

 private:
  class TestServer final : public ServerInterface {
   public:
    const ChannelArgs& channel_args() const override { return channel_args_; }
    channelz::ServerNode* channelz_node() const override { return nullptr; }
    ServerCallTracerFactory* server_call_tracer_factory() const override {
      return nullptr;
    }
    grpc_compression_options compression_options() const override {
      return {
          1,
          {0, GRPC_COMPRESS_LEVEL_NONE},
          {0, GRPC_COMPRESS_NONE},
      };
    }

   private:
    ChannelArgs channel_args_;
  };

  void InitTest() override {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
  }

  void Shutdown() override {
    auto* call = call_.load(std::memory_order_acquire);
    if (call != nullptr) {
      grpc_call_unref(call);
    }
    grpc_completion_queue_shutdown(cq_);
    auto ev = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
  }

  grpc_completion_queue* cq_{nullptr};
  std::atomic<grpc_call*> call_{nullptr};
  CallInitiator call_initiator_;
  TestServer test_server_;
  grpc_metadata_array publish_initial_metadata_{0, 0, nullptr};
};

#define SERVER_CALL_TEST(name) YODEL_TEST(ServerCallTest, name)

SERVER_CALL_TEST(NoOp) { InitCall(MakeClientInitialMetadata({})); }

SERVER_CALL_TEST(InitialMetadataPassedThrough) {
  InitCall(MakeClientInitialMetadata({{"foo", "bar"}}));
  EXPECT_EQ(GetClientInitialMetadata("foo"), "bar");
}

}  // namespace grpc_core
