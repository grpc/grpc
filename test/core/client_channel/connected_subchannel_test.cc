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

#include <grpc/grpc.h>

#include <atomic>
#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/client_channel.h"
#include "src/core/client_channel/local_subchannel_pool.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestPath = "/test_method";
const absl::string_view kTestAddress = "ipv4:127.0.0.1:1234";
const absl::string_view kDefaultAuthority = "test-authority";
}  // namespace

class ConnectedSubchannelTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  RefCountedPtr<ConnectedSubchannel> InitChannel(const ChannelArgs& args) {
    grpc_resolved_address addr;
    CHECK(grpc_parse_uri(URI::Parse(kTestAddress).value(), &addr));
    auto subchannel = Subchannel::Create(MakeOrphanable<TestConnector>(this),
                                         addr, CompleteArgs(args));
    {
      ExecCtx exec_ctx;
      subchannel->RequestConnection();
    }
    return TickUntil<RefCountedPtr<ConnectedSubchannel>>(
        [subchannel]() -> Poll<RefCountedPtr<ConnectedSubchannel>> {
          auto connected_subchannel = subchannel->connected_subchannel();
          if (connected_subchannel != nullptr) return connected_subchannel;
          return Pending();
        });
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kTestPath));
    return client_initial_metadata;
  }

  CallInitiatorAndHandler MakeCall(
      ClientMetadataHandle client_initial_metadata) {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    return MakeCallPair(std::move(client_initial_metadata), std::move(arena));
  }

  CallHandler TickUntilCallStarted() {
    return TickUntil<CallHandler>([this]() -> Poll<CallHandler> {
      auto handler = PopHandler();
      if (handler.has_value()) return std::move(*handler);
      return Pending();
    });
  }

 private:
  class TestTransport final : public ClientTransport {
   public:
    explicit TestTransport(ConnectedSubchannelTest* test) : test_(test) {}

    void Orphan() override {
      state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN, absl::OkStatus(),
                              "transport-orphaned");
      Unref();
    }

    FilterStackTransport* filter_stack_transport() override { return nullptr; }
    ClientTransport* client_transport() override { return this; }
    ServerTransport* server_transport() override { return nullptr; }
    absl::string_view GetTransportName() const override { return "test"; }
    void SetPollset(grpc_stream*, grpc_pollset*) override {}
    void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
    void PerformOp(grpc_transport_op* op) override {
      LOG(INFO) << "PerformOp: " << grpc_transport_op_string(op);
      if (op->start_connectivity_watch != nullptr) {
        state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                                  std::move(op->start_connectivity_watch));
      }
      ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
    }

    void StartCall(CallHandler call_handler) override {
      test_->handlers_.push(std::move(call_handler));
    }

   private:
    ConnectedSubchannelTest* const test_;
    ConnectivityStateTracker state_tracker_{"test-transport"};
  };

  class TestConnector final : public SubchannelConnector {
   public:
    explicit TestConnector(ConnectedSubchannelTest* test) : test_(test) {}

    void Connect(const Args& args, Result* result,
                 grpc_closure* notify) override {
      result->channel_args = args.channel_args;
      result->transport = MakeOrphanable<TestTransport>(test_).release();
      ExecCtx::Run(DEBUG_LOCATION, notify, absl::OkStatus());
    }

    void Shutdown(grpc_error_handle) override {}

   private:
    ConnectedSubchannelTest* const test_;
  };

  ChannelArgs CompleteArgs(const ChannelArgs& args) {
    return args.SetObject(ResourceQuota::Default())
        .SetObject(std::static_pointer_cast<EventEngine>(event_engine()))
        .SetObject(MakeRefCounted<LocalSubchannelPool>())
        .Set(GRPC_ARG_DEFAULT_AUTHORITY, kDefaultAuthority);
  }

  void InitCoreConfiguration() override {}

  void Shutdown() override {}

  std::optional<CallHandler> PopHandler() {
    if (handlers_.empty()) return std::nullopt;
    auto handler = std::move(handlers_.front());
    handlers_.pop();
    return handler;
  }

  std::queue<CallHandler> handlers_;
};

#define CONNECTED_SUBCHANNEL_CHANNEL_TEST(name) \
  YODEL_TEST(ConnectedSubchannelTest, name)

CONNECTED_SUBCHANNEL_CHANNEL_TEST(NoOp) { InitChannel(ChannelArgs()); }

CONNECTED_SUBCHANNEL_CHANNEL_TEST(StartCall) {
  auto channel = InitChannel(ChannelArgs());
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(
      call.handler, "start-call", [channel, handler = call.handler]() mutable {
        channel->unstarted_call_destination()->StartCall(std::move(handler));
      });
  auto handler = TickUntilCallStarted();
  WaitForAllPendingWork();
}

}  // namespace grpc_core
