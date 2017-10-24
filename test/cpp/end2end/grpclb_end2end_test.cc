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

// TODO(dgq): Other scenarios in need of testing:
// - Send a serverlist with faulty ip:port addresses (port > 2^16, etc).
// - Test reception of invalid serverlist
// - Test pinging
// - Test against a non-LB server.
// - Random LB server closing the stream unexpectedly.
// - Test using DNS-resolvable names (localhost?)
// - Test handling of creation of faulty RR instance by having the LB return a
//   serverlist with non-existent backends after having initially returned a
//   valid one.
//
// Findings from end to end testing to be covered here:
// - Handling of LB servers restart, including reconnection after backing-off
//   retries.
// - Destruction of load balanced channel (and therefore of grpclb instance)
//   while:
//   1) the internal LB call is still active. This should work by virtue
//   of the weak reference the LB call holds. The call should be terminated as
//   part of the grpclb shutdown process.
//   2) the retry timer is active. Again, the weak reference it holds should
//   prevent a premature call to \a glb_destroy.
// - Restart of backend servers with no changes to serverlist. This exercises
//   the RR handover mechanism.

using std::chrono::system_clock;

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;
using grpc::lb::v1::LoadBalancer;

namespace grpc {
namespace testing {
namespace {

class SingleBalancerTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerTest() : GrpclbEnd2endTest(4, 1, 0) {}
};

TEST_F(SingleBalancerTest, Vanilla) {
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);

  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backend_servers_[i].service_->request_count());
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, InitiallyEmptyServerlist) {
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const int kCallDeadlineMs = 1000 * grpc_test_slowdown_factor();

  // First response is an empty serverlist, sent right away.
  ScheduleResponseForBalancer(0, LoadBalanceResponse(), 0);
  // Send non-empty serverlist only after kServerlistDelayMs
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      kServerlistDelayMs);

  const auto t0 = system_clock::now();
  // Client will block: LB will initially send empty serverlist.
  CheckRpcSendOk(num_backends_);
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline.
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
  EXPECT_LT(ellapsed_ms.count(), kCallDeadlineMs);

  // Each backend should have gotten 1 request.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancer_servers_[0].service_->response_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, Fallback) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendInResolution = backends_.size() / 2;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    addresses.emplace_back(AddressData{backend_servers_[i].port_, false, ""});
  }
  SetNextResolution(addresses);

  // Send non-empty serverlist only after kServerlistDelayMs.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(kNumBackendInResolution /* start_index */), {}),
      kServerlistDelayMs);

  // Wait until all the fallback backends are reachable.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    WaitForBackend(i);
  }

  // The first request.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }

  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    WaitForBackend(i);
  }

  // Send out the second request.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");

  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }

  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
}

TEST_F(SingleBalancerTest, FallbackUpdate) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendInResolution = backends_.size() / 3;
  const size_t kNumBackendInResolutionUpdate = backends_.size() / 3;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    addresses.emplace_back(AddressData{backend_servers_[i].port_, false, ""});
  }
  SetNextResolution(addresses);

  // Send non-empty serverlist only after kServerlistDelayMs.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(kNumBackendInResolution +
                             kNumBackendInResolutionUpdate /* start_index */),
             {}),
      kServerlistDelayMs);

  // Wait until all the fallback backends are reachable.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    WaitForBackend(i);
  }

  // The first request.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }

  addresses.clear();
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  for (size_t i = kNumBackendInResolution;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    addresses.emplace_back(AddressData{backend_servers_[i].port_, false, ""});
  }
  SetNextResolution(addresses);

  // Wait until the resolution update has been processed and all the new
  // fallback backends are reachable.
  for (size_t i = kNumBackendInResolution;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    WaitForBackend(i);
  }

  // Send out the second request.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(kNumBackendInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");

  // The resolution update is used: each backend in the resolution update should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution + kNumBackendInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }

  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  for (size_t i = kNumBackendInResolution + kNumBackendInResolutionUpdate;
       i < backends_.size(); ++i) {
    WaitForBackend(i);
  }

  // Send out the third request.
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendInResolution -
                 kNumBackendInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");

  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    EXPECT_EQ(0U, backend_servers_[i].service_->request_count());
  }
  for (size_t i = kNumBackendInResolution + kNumBackendInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }

  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
}

TEST_F(SingleBalancerTest, BackendsRestart) {
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->Shutdown()) backend_servers_[i].Shutdown();
  }
  CheckRpcSendFailure();
  for (size_t i = 0; i < num_backends_; ++i) {
    backends_.emplace_back(new BackendServiceImpl());
    backend_servers_.emplace_back(ServerThread<BackendService>(
        "backend", server_host_, backends_.back().get()));
  }
  // The following RPC will fail due to the backend ports having changed. It
  // will nonetheless exercise the grpclb-roundrobin handling of the RR policy
  // having gone into shutdown.
  // TODO(dgq): implement the "backend restart" component as well. We need extra
  // machinery to either update the LB responses "on the fly" or instruct
  // backends which ports to restart on.
  CheckRpcSendFailure();
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

class UpdatesTest : public GrpclbEnd2endTest {
 public:
  UpdatesTest() : GrpclbEnd2endTest(4, 3, 0) {}
};

TEST_F(UpdatesTest, UpdateBalancers) {
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(first_backend, {}), 0);
  ScheduleResponseForBalancer(
      1, BalancerServiceImpl::BuildResponseForBackends(second_backend, {}), 0);

  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backend_servers_[0].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  balancers_[1]->NotifyDoneWithServerlists();
  balancers_[2]->NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  WaitForBackend(1);

  backend_servers_[1].service_->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backend_servers_[1].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  balancers_[1]->NotifyDoneWithServerlists();
  balancers_[2]->NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(1U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(1U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

// Send an update with the same set of LBs as the one in SetUp() in order to
// verify that the LB channel inside grpclb keeps the initial connection (which
// by definition is also present in the update).
TEST_F(UpdatesTest, UpdateBalancersRepeated) {
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[0]};

  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(first_backend, {}), 0);
  ScheduleResponseForBalancer(
      1, BalancerServiceImpl::BuildResponseForBackends(second_backend, {}), 0);

  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backend_servers_[0].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  addresses.emplace_back(AddressData{balancer_servers_[2].port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // grpclb continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  balancers_[0]->NotifyDoneWithServerlists();

  addresses.clear();
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 2 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 2 DONE ==========");

  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                          gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // grpclb continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  balancers_[0]->NotifyDoneWithServerlists();

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(UpdatesTest, UpdateBalancersDeadUpdate) {
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};

  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(first_backend, {}), 0);
  ScheduleResponseForBalancer(
      1, BalancerServiceImpl::BuildResponseForBackends(second_backend, {}), 0);

  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backend_servers_[0].service_->request_count());

  // Kill balancer 0
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->NotifyDoneWithServerlists();
  if (balancers_[0]->Shutdown()) balancer_servers_[0].Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");

  // This is serviced by the existing RR policy
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should again have gone to the first backend.
  EXPECT_EQ(20U, backend_servers_[0].service_->request_count());
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  balancers_[1]->NotifyDoneWithServerlists();
  balancers_[2]->NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request. In the meantime, the client continues to be serviced
  // (by the first backend) without interruption.
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  WaitForBackend(1);

  // This is serviced by the existing RR policy
  backend_servers_[1].service_->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backend_servers_[1].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  balancers_[1]->NotifyDoneWithServerlists();
  balancers_[2]->NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(1U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(1U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, Drop) {
  const size_t kNumRpcsPerAddress = 100;
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 2;
  const int num_of_drop_addresses = num_of_drop_by_rate_limiting_addresses +
                                    num_of_drop_by_load_balancing_addresses;
  const int num_total_addresses = num_backends_ + num_of_drop_addresses;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(),
             {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
              {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      0);
  // Wait until all backends are ready.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs for each server and drop address.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcsPerAddress * num_total_addresses; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * num_of_drop_addresses, num_drops);

  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backend_servers_[i].service_->request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
}

TEST_F(SingleBalancerTest, DropAllFirst) {
  // All registered addresses are marked as "drop".
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 1;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             {}, {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
                  {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      0);
  const Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
}

TEST_F(SingleBalancerTest, DropAll) {
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 1;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             {}, {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
                  {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      1000);

  // First call succeeds.
  CheckRpcSendOk();
  // But eventually, the update with only dropped servers is processed and calls
  // fail.
  Status status;
  do {
    status = SendRpc();
  } while (status.ok());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
}

class SingleBalancerWithClientLoadReportingTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerWithClientLoadReportingTest() : GrpclbEnd2endTest(4, 1, 2) {}
};

TEST_F(SingleBalancerWithClientLoadReportingTest, Vanilla) {
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backend_servers_[i].service_->request_count());
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());

  const ClientStats client_stats = WaitForLoadReports();
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats.num_calls_started);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + (num_ok + num_drops),
            client_stats.num_calls_finished_known_received);
  EXPECT_THAT(client_stats.drop_token_counts, ::testing::ElementsAre());
}

TEST_F(SingleBalancerWithClientLoadReportingTest, Drop) {
  const size_t kNumRpcsPerAddress = 3;
  const int num_of_drop_by_rate_limiting_addresses = 2;
  const int num_of_drop_by_load_balancing_addresses = 1;
  const int num_of_drop_addresses = num_of_drop_by_rate_limiting_addresses +
                                    num_of_drop_by_load_balancing_addresses;
  const int num_total_addresses = num_backends_ + num_of_drop_addresses;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(),
             {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
              {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      0);
  // Wait until all backends are ready.
  int num_warmup_ok = 0;
  int num_warmup_failure = 0;
  int num_warmup_drops = 0;
  std::tie(num_warmup_ok, num_warmup_failure, num_warmup_drops) =
      WaitForAllBackends(num_total_addresses /* num_requests_multiple_of */);
  const int num_total_warmup_requests =
      num_warmup_ok + num_warmup_failure + num_warmup_drops;
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcsPerAddress * num_total_addresses; ++i) {
    EchoResponse response;
    const Status status = SendRpc(&response);
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * num_of_drop_addresses, num_drops);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backend_servers_[i].service_->request_count());
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());

  const ClientStats client_stats = WaitForLoadReports();
  EXPECT_EQ(
      kNumRpcsPerAddress * num_total_addresses + num_total_warmup_requests,
      client_stats.num_calls_started);
  EXPECT_EQ(
      kNumRpcsPerAddress * num_total_addresses + num_total_warmup_requests,
      client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_warmup_ok,
            client_stats.num_calls_finished_known_received);
  // The number of warmup request is a multiple of the number of addresses.
  // Therefore, all addresses in the scheduled balancer response are hit the
  // same number of times.
  const int num_times_drop_addresses_hit =
      num_warmup_drops / num_of_drop_addresses;
  EXPECT_THAT(
      client_stats.drop_token_counts,
      ::testing::ElementsAre(
          ::testing::Pair("load_balancing",
                          (kNumRpcsPerAddress + num_times_drop_addresses_hit)),
          ::testing::Pair(
              "rate_limiting",
              (kNumRpcsPerAddress + num_times_drop_addresses_hit) * 2)));
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
