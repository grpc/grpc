/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

extern "C" {
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/iomgr/sockaddr.h"
}

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/grpclb_end2end_test.h"
#include "test/cpp/end2end/test_service_impl.h"

#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using std::chrono::system_clock;

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;
using grpc::lb::v1::LoadBalancer;

namespace grpc {
namespace testing {
namespace {

const int kTestDurationSec = 5;
const size_t kNumBackends = 4;
const size_t kNumBalancers = 2;
const size_t kNumClientThreads = 2;
const int kResolutionUpdateIntervalMs = 20;
const std::pair<int, int> kServerlistUpdateIntervalRangeMs{10, 20};
const std::pair<int, int> kRequestSendingIntervalRangeMs{10, 20};

class End2endStressTest : public GrpclbEnd2endTest {
 public:
  End2endStressTest()
      : GrpclbEnd2endTest(kNumBackends, kNumBalancers, 0),
        num_client_threads_(kNumClientThreads),
        resolution_update_interval_ms_(kResolutionUpdateIntervalMs),
        serverlist_update_interval_range_ms_(kServerlistUpdateIntervalRangeMs),
        request_sending_interval_range_ms_(kRequestSendingIntervalRangeMs) {
    assert(serverlist_update_interval_range_ms_.second >=
        serverlist_update_interval_range_ms_.first);
    assert(request_sending_interval_range_ms_.second >=
        request_sending_interval_range_ms_.first);
  }

  void SetUp() override {
    // Start the backends.
    for (size_t i = 0; i < num_backends_; ++i) {
      backends_.emplace_back(new BackendServiceImpl());
      backend_servers_.emplace_back(ServerThread<BackendService>(
          "backend", server_host_, backends_.back().get()));
    }
    // Start the load balancers.
    for (size_t i = 0; i < num_balancers_; ++i) {
      balancers_.emplace_back(
          new BalancerServiceImpl(client_load_reporting_interval_seconds_));
      balancer_servers_.emplace_back(ServerThread<BalancerService>(
          "balancer", server_host_, balancers_.back().get()));
    }
    // Start updating resolution.
    response_generator_ = grpc_fake_resolver_response_generator_create();
    ResetStub();
    resolver_thread_ = std::thread(
        &End2endStressTest::PeriodicallyUpdateRandomResolution, this);
    // Start scheduling serverlist.
    for (size_t i = 0; i < num_balancers_; ++i) {
      response_scheduler_threads_.emplace_back(std::thread(
          &End2endStressTest::PeriodicallyScheduleResponseForBalancer, this,
          i));
    }
    // Start sending RPCs in multiple threads.
    for (size_t i = 0; i < num_client_threads_; ++i) {
      client_threads_.emplace_back(
          std::thread(&End2endStressTest::PeriodicallySendRequests, this));
    }
  }

  void TearDown() override {
    resolver_thread_.join();
    for (size_t i = 0; i < num_client_threads_; ++i) {
      client_threads_[i].join();
    }
    for (size_t i = 0; i < num_balancers_; ++i) {
      response_scheduler_threads_[i].join();
    }
    for (size_t i = 0; i < backends_.size(); ++i) {
      if (backends_[i]->Shutdown()) backend_servers_[i].Shutdown();
    }
    for (size_t i = 0; i < balancers_.size(); ++i) {
      if (balancers_[i]->Shutdown()) balancer_servers_[i].Shutdown();
    }
    grpc_fake_resolver_response_generator_unref(response_generator_);
  }

  std::atomic_bool shutdown_{false};

 private:
  LoadBalanceResponse BuildRandomResponseForBackends() {
    std::vector<int> all_backend_ports = GetBackendPorts();
    size_t num_non_drop_entry = rand() % (all_backend_ports.size() + 1);
    size_t num_drop_entry = rand() % (all_backend_ports.size() + 1);
    std::vector<int> random_backend_ports;
    for (size_t i = 0; i < num_non_drop_entry; ++i) {
      random_backend_ports.push_back(
          all_backend_ports[rand() % all_backend_ports.size()]);
    }
    return BalancerServiceImpl::BuildResponseForBackends(
        random_backend_ports, {{"load_balancing", num_drop_entry}});
  }

  void PeriodicallyUpdateRandomResolution() {
    const auto wait_duration =
        std::chrono::milliseconds(resolution_update_interval_ms_);
    std::vector<AddressData> addresses;
    while (!shutdown_) {
      // Generate a random list of balancers.
      addresses.clear();
      const size_t num_addresses = std::rand() % (balancer_servers_.size() + 1);
      for (size_t i = 0; i < num_addresses; ++i) {
        addresses.emplace_back(AddressData{
            balancer_servers_[std::rand() % balancer_servers_.size()].port_,
            true, ""});
      }
      SetNextResolution(addresses);
      std::this_thread::sleep_for(wait_duration);
    }
  }

  void PeriodicallyScheduleResponseForBalancer(size_t i) {
    while (!shutdown_) {
      ScheduleResponseForBalancer(i, BuildRandomResponseForBackends(), 0);
      const auto low = serverlist_update_interval_range_ms_.first;
      const auto high = serverlist_update_interval_range_ms_.second;
      std::this_thread::sleep_for(std::chrono::milliseconds(
          high > low ? low + std::rand() % (high - low + 1) : low));
    }
  }

  void PeriodicallySendRequests() {
    while (!shutdown_) {
      SendRpc();
      const auto low = request_sending_interval_range_ms_.first;
      const auto high = request_sending_interval_range_ms_.second;
      std::this_thread::sleep_for(std::chrono::milliseconds(
          high > low ? low + std::rand() % (high - low + 1) : low));
    }
  }

  const size_t num_client_threads_;
  const int resolution_update_interval_ms_;
  const std::pair<int, int> serverlist_update_interval_range_ms_;
  const std::pair<int, int> request_sending_interval_range_ms_;
  std::thread resolver_thread_;
  std::vector<std::thread> response_scheduler_threads_;
  std::vector<std::thread> client_threads_;
};

TEST_F(End2endStressTest, Vanilla) {
  std::this_thread::sleep_for(std::chrono::seconds(kTestDurationSec));
  shutdown_ = true;
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
