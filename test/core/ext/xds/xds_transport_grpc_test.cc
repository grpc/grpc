// Copyright 2021 gRPC authors.
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

#include "src/core/ext/xds/xds_transport_grpc.h"

#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

using EventHandlerEvent = absl::variant<bool, absl::Status, std::string>;

class TestEventHandler
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit TestEventHandler(std::vector<EventHandlerEvent>* events)
      : events_(events) {}

  void OnRequestSent(bool ok) override { events_->emplace_back(ok); }

  bool OnRecvMessage(absl::string_view payload) override {
    events_->emplace_back(std::string(payload));
    return true;
  }

  void OnStatusReceived(absl::Status status) override {
    events_->emplace_back(std::move(status));
  }

 private:
  std::vector<EventHandlerEvent>* events_;
};

class AdsServer {
 public:
  AdsServer() : server_thread_(ServerThread, this) {
    WaitForState<State::kReady>();
  }

  ~AdsServer() {
    set_state(State::kStopping);
    server_thread_.join();
  }

 private:
  enum class State {
    kNew,
    kReady,
    kStopping,
  };

  static void ServerThread(AdsServer* ads_server) { ads_server->Run(); }

  void Run() {
    set_state(State::kReady);
    WaitForState<State::kStopping>();
  }

  template <State state>
  void WaitForState() {
    MutexLock lock(&mu_);
    mu_.AwaitWithTimeout(
        absl::Condition(
            +[](AdsServer* server) ABSL_NO_THREAD_SAFETY_ANALYSIS {
              return server->state_ != state;
            },
            this),
        absl::Seconds(15));
  }

  void set_state(State state) {
    MutexLock lock(&mu_);
    state_ = state;
  }

  std::thread server_thread_;
  Mutex mu_;
  State state_ ABSL_GUARDED_BY(mu_) = State::kNew;
};

TEST(GrpcTransportTest, WaitsWithAdsRead) {
  AdsServer ads_server;
  ExecCtx exec_ctx;
  ChannelArgs args;
  auto factory = MakeOrphanable<GrpcXdsTransportFactory>(args);
  GrpcXdsBootstrap::GrpcXdsServer server;
  absl::Status status;
  std::vector<absl::Status> statuses;
  auto transport = factory->Create(
      server, [&statuses](auto s) { statuses.emplace_back(std::move(s)); },
      &status);
  // ASSERT_TRUE(status.ok()) << status;
  // std::vector<EventHandlerEvent> events;
  // auto call = transport->CreateStreamingCall(
  //     "boop", std::make_unique<TestEventHandler>(&events));
  // call->SendMessage("booop");
  // auto deadline = absl::Now() + absl::Seconds(45);
  // while (events.empty() && deadline > absl::Now() && statuses.empty() &&
  //        status.ok()) {
  //   absl::SleepFor(absl::Seconds(1));
  // }
  // EXPECT_THAT(events, ::testing::IsEmpty());
  // EXPECT_THAT(statuses, ::testing::IsEmpty());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
