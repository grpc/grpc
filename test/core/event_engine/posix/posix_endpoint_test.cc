// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"

#include <fcntl.h>
#include <poll.h>

#include <chrono>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/event_engine/test_suite/event_engine_test_utils.h"
#include "test/core/event_engine/test_suite/oracle_event_engine_posix.h"
#include "test/core/util/port.h"

GPR_GLOBAL_CONFIG_DECLARE_STRING(grpc_poll_strategy);

namespace grpc_event_engine {
namespace posix_engine {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Poller;
using ::grpc_event_engine::experimental::PosixEventEngine;
using ::grpc_event_engine::experimental::PosixOracleEventEngine;
using ::grpc_event_engine::experimental::Promise;
using ::grpc_event_engine::experimental::URIToResolvedAddress;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

namespace {

constexpr int kMinMessageSize = 1024;
constexpr int kMaxMessageSize = 8192;
constexpr int kNumExchangedMessages = 100;

// Returns a random message with bounded length.
std::string GetNextSendMessage() {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  static std::random_device rd;
  static std::seed_seq seed{rd()};
  static std::mt19937 gen(seed);
  static std::uniform_real_distribution<> dis(kMinMessageSize, kMaxMessageSize);
  static grpc_core::Mutex g_mu;
  std::string tmp_s;
  int len;
  {
    grpc_core::MutexLock lock(&g_mu);
    len = dis(gen);
  }
  tmp_s.reserve(len);
  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return tmp_s;
}

class TestScheduler : public Scheduler {
 public:
  explicit TestScheduler(EventEngine* engine) : engine_(engine) {}
  void Run(EventEngine::Closure* closure) override { engine_->Run(closure); }

  void Run(absl::AnyInvocable<void()> cb) override {
    engine_->Run(std::move(cb));
  }

 private:
  EventEngine* engine_;
};

std::tuple<std::unique_ptr<EventEngine::Endpoint>,
           std::unique_ptr<EventEngine::Endpoint>>
CreateConnectedSocket(EventEngine* oracle_ee, Scheduler* scheduler,
                      PosixEventPoller* poller, bool is_zero_copy_enabled) {
  EXPECT_NE(oracle_ee, nullptr);
  EXPECT_NE(scheduler, nullptr);
  EXPECT_NE(poller, nullptr);
  auto memory_quota = absl::make_unique<grpc_core::MemoryQuota>("bar");
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  EventEngine::ResolvedAddress resolved_addr =
      URIToResolvedAddress(target_addr);
  Promise<std::unique_ptr<EventEngine::Endpoint>> client_endpoint_promise;
  Promise<std::unique_ptr<EventEngine::Endpoint>> server_endpoint_promise;

  Listener::AcceptCallback accept_cb =
      [&server_endpoint_promise](
          std::unique_ptr<Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint_promise.Set(std::move(ep));
      };
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  if (is_zero_copy_enabled) {
    args = args.Set(GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED, 1);
    args = args.Set(GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD,
                    kMinMessageSize);
  }
  ChannelArgsEndpointConfig config(args);
  auto status = oracle_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { ASSERT_TRUE(status.ok()); }, config,
      absl::make_unique<grpc_core::MemoryQuota>("foo"));
  EXPECT_TRUE(status.ok());

  std::unique_ptr<Listener> listener = std::move(*status);
  EXPECT_TRUE(listener->Bind(resolved_addr).ok());
  EXPECT_TRUE(listener->Start().ok());

  // Create client socket and connect to the target address.
  int client_fd;
  int one = 1;
  int flags;

  client_fd = socket(AF_INET6, SOCK_STREAM, 0);
  setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  // Make fd non-blocking.
  flags = fcntl(client_fd, F_GETFL, 0);
  EXPECT_EQ(fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0);

  if (connect(client_fd, const_cast<struct sockaddr*>(resolved_addr.address()),
              resolved_addr.size()) == -1) {
    if (errno == EINPROGRESS) {
      struct pollfd pfd;
      pfd.fd = client_fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (poll(&pfd, 1, -1) == -1) {
        gpr_log(GPR_ERROR, "poll() failed during connect; errno=%d", errno);
        abort();
      }
    } else {
      gpr_log(GPR_ERROR, "Failed to connect to the server (errno=%d)", errno);
      abort();
    }
  }
  EventHandle* handle =
      poller->CreateHandle(client_fd, "test", poller->CanTrackErrors());
  EXPECT_NE(handle, nullptr);
  auto server_endpoint = std::move(server_endpoint_promise.Get());
  EXPECT_NE(server_endpoint, nullptr);
  return std::make_tuple<std::unique_ptr<Endpoint>, std::unique_ptr<Endpoint>>(
      CreatePosixEndpoint(
          handle,
          PosixEngineClosure::TestOnlyToClosure(
              [poller](absl::Status /*status*/) { poller->Kick(); }),
          scheduler, config),
      std::move(server_endpoint));
}

}  // namespace

class TestParam {
 public:
  TestParam(std::string poller, bool is_zero_copy_enabled)
      : poller_(std::move(poller)),
        is_zero_copy_enabled_(is_zero_copy_enabled) {}

  std::string Poller() const { return poller_; }
  bool IsZeroCopyEnabled() const { return is_zero_copy_enabled_; }

 private:
  std::string poller_;
  bool is_zero_copy_enabled_;
};

std::string TestScenarioName(const ::testing::TestParamInfo<TestParam>& info) {
  return absl::StrCat("poller_type_", info.param.Poller(),
                      "_is_zero_copy_enabled_", info.param.IsZeroCopyEnabled());
}

// A helper class to create Fds and drive the polling for these Fds. It
// repeatedly calls the Work(..) method on the poller to get pet pending events,
// then schedules another parallel Work(..) instantiation and processes these
// pending events. This continues until all Fds have orphaned themselves.
class Worker {
 public:
  Worker(Scheduler* scheduler, PosixEventPoller* poller)
      : scheduler_(scheduler), poller_(poller) {}
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      promise.Set(true);
    }
  }

  void Start() {
    // Start executing Work(..).
    scheduler_->Run([this]() { Work(); });
  }

  void Wait() { EXPECT_TRUE(promise.Get()); }

 private:
  void Work() {
    auto result = poller_->Work(24h);
    if (absl::holds_alternative<Poller::Events>(result)) {
      // Schedule next work instantiation immediately and take a Ref for
      // the next instantiation.
      Ref();
      scheduler_->Run([this]() { Work(); });
      // Process pending events of current Work(..) instantiation.
      auto pending_events = absl::get<Poller::Events>(result);
      for (auto it = pending_events.begin(); it != pending_events.end(); ++it) {
        (*it)->Run();
      }
      pending_events.clear();
      // Corresponds to the Ref taken for the current instantiation.
      Unref();
    } else {
      // The poller got kicked. This can only happen when all the Fds have
      // orphaned themselves.
      EXPECT_TRUE(absl::holds_alternative<Poller::Kicked>(result));
      Unref();
    }
  }
  Scheduler* scheduler_;
  PosixEventPoller* poller_;
  Promise<bool> promise;
  std::atomic<int> ref_count_{1};
};

class PosixEndpointTest : public ::testing::TestWithParam<TestParam> {
  void SetUp() override {
    oracle_ee_ = absl::make_unique<
        grpc_event_engine::experimental::PosixOracleEventEngine>();
    posix_ee_ =
        absl::make_unique<grpc_event_engine::experimental::PosixEventEngine>();
    EXPECT_NE(posix_ee_, nullptr);
    scheduler_ =
        absl::make_unique<grpc_event_engine::posix_engine::TestScheduler>(
            posix_ee_.get());
    EXPECT_NE(scheduler_, nullptr);
    GPR_GLOBAL_CONFIG_SET(grpc_poll_strategy, GetParam().Poller().c_str());
    poller_ = GetDefaultPoller(scheduler_.get());
    if (poller_ != nullptr) {
      EXPECT_EQ(poller_->Name(), GetParam().Poller());
    }
  }

  void TearDown() override {
    if (poller_ != nullptr) {
      poller_->Shutdown();
    }
  }

 public:
  TestScheduler* Scheduler() { return scheduler_.get(); }

  PosixOracleEventEngine* OracleEE() { return oracle_ee_.get(); }

  PosixEventPoller* PosixPoller() { return poller_; }

  PosixEventEngine* PosixEE() { return posix_ee_.get(); }

 private:
  std::unique_ptr<PosixOracleEventEngine> oracle_ee_;
  PosixEventPoller* poller_;
  std::unique_ptr<PosixEventEngine> posix_ee_;
  std::unique_ptr<TestScheduler> scheduler_;
};

TEST_P(PosixEndpointTest, ConnectExchangeBidiDataTransferTest) {
  auto oracle_ee = std::make_unique<experimental::PosixOracleEventEngine>();
  auto test_ee = std::make_unique<experimental::PosixEventEngine>();
  if (PosixPoller() == nullptr) {
    return;
  }
  Worker worker(Scheduler(), PosixPoller());
  worker.Start();
  {
    auto endpoints = CreateConnectedSocket(
        OracleEE(), Scheduler(), PosixPoller(), GetParam().IsZeroCopyEnabled());

    auto client_endpoint = std::move(std::get<0>(endpoints));
    auto server_endpoint = std::move(std::get<1>(endpoints));
    EXPECT_TRUE(client_endpoint != nullptr);
    EXPECT_TRUE(server_endpoint != nullptr);

    // Alternate message exchanges between client -- server and server --
    // client.
    for (int i = 0; i < kNumExchangedMessages; i++) {
      // Send from client to server and verify data read at the server.
      ASSERT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                      client_endpoint.get(),
                                      server_endpoint.get())
                      .ok());

      // Send from server to client and verify data read at the client.
      ASSERT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                      server_endpoint.get(),
                                      client_endpoint.get())
                      .ok());
    }
  }
  worker.Wait();
}

INSTANTIATE_TEST_SUITE_P(
    PosixEventPoller, PosixEndpointTest,
    ::testing::ValuesIn({TestParam(std::string("poll"), false),
                         TestParam(std::string("poll"), true)}),
    &TestScenarioName);

}  // namespace posix_engine
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}