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

#include "src/core/client_channel/client_channel.h"
#include "src/core/client_channel/local_subchannel_pool.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/experiments/experiments.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/test_util/scoped_env_var.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestPath = "/test_method";
const absl::string_view kTestAddress = "ipv4:127.0.0.1:1234";
const absl::string_view kDefaultAuthority = "test-authority";
}  // namespace

class SubchannelTest : public YodelTest {
 protected:
  class Watcher final : public Subchannel::ConnectivityStateWatcherInterface {
   public:
    explicit Watcher(uint32_t max_connections_per_subchannel)
        : max_connections_per_subchannel_(max_connections_per_subchannel) {}

    void OnConnectivityStateChange(grpc_connectivity_state state,
                                   const absl::Status&) override {
      state_ = state;
    }

    void OnKeepaliveUpdate(Duration) override {}

    uint32_t max_connections_per_subchannel() const override {
      return max_connections_per_subchannel_;
    }

    grpc_pollset_set* interested_parties() override { return nullptr; }

    grpc_connectivity_state state() const { return state_; }

   private:
    const uint32_t max_connections_per_subchannel_;
    grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  };

  class TestTransport final : public ClientTransport {
   public:
    explicit TestTransport(SubchannelTest* test) : test_(test) {
      if (test->max_concurrent_streams_for_next_transport_.has_value()) {
        max_concurrent_streams_ =
            *test->max_concurrent_streams_for_next_transport_;
        test->max_concurrent_streams_for_next_transport_.reset();
      } else {
        max_concurrent_streams_ = std::numeric_limits<uint32_t>::max();
      }
    }

    void Orphan() override {
      state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN, absl::OkStatus(),
                              "transport-orphaned");
      if (watcher_ != nullptr) NotifyWatcherOfDisconnect();
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
    void StartWatch(RefCountedPtr<StateWatcher> watcher) override {
      GRPC_CHECK(watcher_ == nullptr);
      watcher_ = std::move(watcher);
      NotifyWatcherOfMaxConcurrentStreams();
    }
    void StopWatch(RefCountedPtr<StateWatcher> watcher) override {
      if (watcher_ == watcher) watcher_.reset();
    }

    void StartCall(CallHandler call_handler) override {
      test_->handlers_.push(std::move(call_handler));
    }

    RefCountedPtr<channelz::SocketNode> GetSocketNode() const override {
      return nullptr;
    }

    void SetMaxConcurrentStreams(uint32_t max_concurrent_streams) {
      max_concurrent_streams_ = max_concurrent_streams;
      if (watcher_ != nullptr) NotifyWatcherOfMaxConcurrentStreams();
    }

    uint32_t max_concurrent_streams() const { return max_concurrent_streams_; }

   private:
    void NotifyWatcherOfMaxConcurrentStreams() {
      test_->event_engine()->Run(
          [watcher = watcher_,
           max_concurrent_streams = max_concurrent_streams_]() mutable {
            ExecCtx exec_ctx;
            watcher->OnPeerMaxConcurrentStreamsUpdate(max_concurrent_streams,
                                                      nullptr);
            watcher.reset();
          });
    }

    void NotifyWatcherOfDisconnect() {
      test_->event_engine()->Run([watcher = std::move(watcher_)]() mutable {
        ExecCtx exec_ctx;
        watcher->OnDisconnect(absl::UnavailableError("disconnected"), {});
        watcher.reset();
      });
    }

    SubchannelTest* const test_;
    uint32_t max_concurrent_streams_;
    ConnectivityStateTracker state_tracker_{"test-transport"};
    RefCountedPtr<StateWatcher> watcher_;
  };

  using YodelTest::YodelTest;

  RefCountedPtr<Subchannel> InitSubchannel(const ChannelArgs& args) {
    grpc_resolved_address addr;
    CHECK(grpc_parse_uri(URI::Parse(kTestAddress).value(), &addr));
    return Subchannel::Create(MakeOrphanable<TestConnector>(this), addr,
                              CompleteArgs(args));
  }

  RefCountedPtr<Watcher> StartWatch(Subchannel* subchannel,
                                    uint32_t max_connections_per_subchannel) {
    auto watcher = MakeRefCounted<Watcher>(max_connections_per_subchannel);
    ExecCtx exec_ctx;
    subchannel->WatchConnectivityState(watcher);
    return watcher;
  }

  void WaitForConnection(Subchannel* subchannel, Watcher* watcher) {
    {
      ExecCtx exec_ctx;
      subchannel->RequestConnection();
    }
    TickUntil<Empty>([watcher]() -> Poll<Empty> {
      if (watcher->state() == GRPC_CHANNEL_READY) return Empty{};
      return Pending();
    });
  }

  void set_max_concurrent_streams_for_next_transport(
      uint32_t max_concurrent_streams) {
    max_concurrent_streams_for_next_transport_ = max_concurrent_streams;
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

  bool NoCallsStarted() const { return handlers_.empty(); }

 private:
  class TestConnector final : public SubchannelConnector {
   public:
    explicit TestConnector(SubchannelTest* test) : test_(test) {}

    void Connect(const Args& args, Result* result,
                 grpc_closure* notify) override {
      result->channel_args = args.channel_args;
      auto transport = MakeOrphanable<TestTransport>(test_).release();
      result->max_concurrent_streams = transport->max_concurrent_streams();
      result->transport = transport;
      ExecCtx::Run(DEBUG_LOCATION, notify, absl::OkStatus());
    }

    void Shutdown(grpc_error_handle) override {}

   private:
    SubchannelTest* const test_;
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
  std::optional<uint32_t> max_concurrent_streams_for_next_transport_;
};

#define SUBCHANNEL_TEST(name) YODEL_TEST(SubchannelTest, name)

SUBCHANNEL_TEST(Connects) {
  auto subchannel = InitSubchannel(ChannelArgs());
  auto watcher =
      StartWatch(subchannel.get(), /*max_connections_per_subchannel=*/1);
  WaitForConnection(subchannel.get(), watcher.get());
}

SUBCHANNEL_TEST(StartCall) {
  auto subchannel = InitSubchannel(ChannelArgs());
  auto watcher =
      StartWatch(subchannel.get(), /*max_connections_per_subchannel=*/1);
  WaitForConnection(subchannel.get(), watcher.get());
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call.handler, "start-call",
               [subchannel, handler = call.handler]() mutable {
                 subchannel->call_destination()->StartCall(std::move(handler));
               });
  auto handler = TickUntilCallStarted();
  WaitForAllPendingWork();
}

SUBCHANNEL_TEST(MaxConcurrentStreams) {
  if (!IsSubchannelConnectionScalingEnabled()) {
    GTEST_SKIP()
        << "this test requires the subchannel_connection_scaling experiment";
  }
  testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_MAX_CONCURRENT_STREAMS_CONNECTION_SCALING");
  auto subchannel = InitSubchannel(ChannelArgs());
  auto watcher =
      StartWatch(subchannel.get(), /*max_connections_per_subchannel=*/1);
  set_max_concurrent_streams_for_next_transport(2);
  WaitForConnection(subchannel.get(), watcher.get());
  // Start two calls, which will be sent to the transport.
  LOG(INFO) << "STARTING CALL 1...";
  auto call1 = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call1.handler, "start-call1",
               [subchannel, handler = std::move(call1.handler)]() mutable {
                 subchannel->call_destination()->StartCall(std::move(handler));
               });
  LOG(INFO) << "WAITING FOR CALL 1 TO BE STARTED ON TRANSPORT...";
  /*auto handler1 =*/TickUntilCallStarted();
  LOG(INFO) << "STARTING CALL 2...";
  auto call2 = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call2.handler, "start-call2",
               [subchannel, handler = std::move(call2.handler)]() mutable {
                 subchannel->call_destination()->StartCall(std::move(handler));
               });
  LOG(INFO) << "WAITING FOR CALL 2 TO BE STARTED ON TRANSPORT...";
  auto handler2 = TickUntilCallStarted();
  // Now start a third call, which will be queued.
  LOG(INFO) << "STARTING CALL 3...";
  auto call3 = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call3.handler, "start-call3",
               [subchannel, handler = std::move(call3.handler)]() mutable {
                 subchannel->call_destination()->StartCall(std::move(handler));
               });
  LOG(INFO) << "WAITING FOR ALL PENDING WORK...";
  WaitForAllPendingWork();
  EXPECT_TRUE(NoCallsStarted());
  // Now cancel one of the existing RPCs.
  LOG(INFO) << "CANCELING CALL 1...";
  SpawnTestSeq(call1.initiator, "cancel-call1",
               [initiator = std::move(call1.initiator)]() mutable {
                 initiator.Cancel();
               });
  // This should allow the third call to start.
  LOG(INFO) << "WAITING FOR CALL 3 TO BE STARTED ON TRANSPOR...";
  auto handler3 = TickUntilCallStarted();
  LOG(INFO) << "WAITING FOR ALL PENDING WORK...";
  WaitForAllPendingWork();
}

}  // namespace grpc_core
