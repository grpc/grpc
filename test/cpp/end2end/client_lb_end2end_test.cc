/*
 *
 * Copyright 2016 gRPC authors.
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

#include <algorithm>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using std::chrono::system_clock;

// defined in tcp_client.cc
extern grpc_tcp_client_vtable* grpc_tcp_client_impl;

static grpc_tcp_client_vtable* default_client_impl;

namespace grpc {
namespace testing {
namespace {

gpr_atm g_connection_delay_ms;

void tcp_client_connect_with_delay(grpc_closure* closure, grpc_endpoint** ep,
                                   grpc_pollset_set* interested_parties,
                                   const grpc_channel_args* channel_args,
                                   const grpc_resolved_address* addr,
                                   grpc_millis deadline) {
  const int delay_ms = gpr_atm_acq_load(&g_connection_delay_ms);
  if (delay_ms > 0) {
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
  }
  default_client_impl->connect(closure, ep, interested_parties, channel_args,
                               addr, deadline + delay_ms);
}

grpc_tcp_client_vtable delayed_connect = {tcp_client_connect_with_delay};

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo RPC.
class MyTestServiceImpl : public TestServiceImpl {
 public:
  MyTestServiceImpl() : request_count_(0) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    {
      std::unique_lock<std::mutex> lock(mu_);
      ++request_count_;
    }
    return TestServiceImpl::Echo(context, request, response);
  }

  int request_count() {
    std::unique_lock<std::mutex> lock(mu_);
    return request_count_;
  }

  void ResetCounters() {
    std::unique_lock<std::mutex> lock(mu_);
    request_count_ = 0;
  }

 private:
  std::mutex mu_;
  int request_count_;
};

class ClientLbEnd2endTest : public ::testing::Test {
 protected:
  ClientLbEnd2endTest()
      : server_host_("localhost"),
        kRequestMessage_("Live long and prosper."),
        creds_(new SecureChannelCredentials(
            grpc_fake_transport_security_credentials_create())) {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    gpr_setenv("GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS", "1");
  }

  void SetUp() override {
    grpc_init();
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  }

  void TearDown() override {
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
    grpc_shutdown();
  }

  void CreateServers(size_t num_servers,
                     std::vector<int> ports = std::vector<int>()) {
    servers_.clear();
    for (size_t i = 0; i < num_servers; ++i) {
      int port = 0;
      if (ports.size() == num_servers) port = ports[i];
      servers_.emplace_back(new ServerData(port));
    }
  }

  void StartServer(size_t index) { servers_[index]->Start(server_host_); }

  void StartServers(size_t num_servers,
                    std::vector<int> ports = std::vector<int>()) {
    CreateServers(num_servers, std::move(ports));
    for (size_t i = 0; i < num_servers; ++i) {
      StartServer(i);
    }
  }

  grpc_channel_args* BuildFakeResults(const std::vector<int>& ports) {
    grpc_core::ServerAddressList addresses;
    for (const int& port : ports) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", port);
      grpc_uri* lb_uri = grpc_uri_parse(lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(lb_uri, &address));
      addresses.emplace_back(address.addr, address.len, nullptr /* args */);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    const grpc_arg fake_addresses =
        CreateServerAddressListChannelArg(&addresses);
    grpc_channel_args* fake_results =
        grpc_channel_args_copy_and_add(nullptr, &fake_addresses, 1);
    return fake_results;
  }

  void SetNextResolution(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args* fake_results = BuildFakeResults(ports);
    response_generator_->SetResponse(fake_results);
    grpc_channel_args_destroy(fake_results);
  }

  void SetNextResolutionUponError(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args* fake_results = BuildFakeResults(ports);
    response_generator_->SetReresolutionResponse(fake_results);
    grpc_channel_args_destroy(fake_results);
  }

  void SetFailureOnReresolution() {
    grpc_core::ExecCtx exec_ctx;
    response_generator_->SetFailureOnReresolution();
  }

  std::vector<int> GetServersPorts() {
    std::vector<int> ports;
    for (const auto& server : servers_) ports.push_back(server->port_);
    return ports;
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub(
      const std::shared_ptr<Channel>& channel) {
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  std::shared_ptr<Channel> BuildChannel(
      const grpc::string& lb_policy_name,
      ChannelArguments args = ChannelArguments()) {
    if (lb_policy_name.size() > 0) {
      args.SetLoadBalancingPolicyName(lb_policy_name);
    }  // else, default to pick first
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    return CreateCustomChannel("fake:///", creds_, args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      EchoResponse* response = nullptr, int timeout_ms = 1000,
      Status* result = nullptr, bool wait_for_ready = false) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub->Echo(&context, request, response);
    if (result != nullptr) *result = status;
    if (local_response) delete response;
    return status.ok();
  }

  void CheckRpcSendOk(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      const grpc_core::DebugLocation& location, bool wait_for_ready = false) {
    EchoResponse response;
    Status status;
    const bool success =
        SendRpc(stub, &response, 2000, &status, wait_for_ready);
    ASSERT_TRUE(success) << "From " << location.file() << ":" << location.line()
                         << "\n"
                         << "Error: " << status.error_message() << " "
                         << status.error_details();
    ASSERT_EQ(response.message(), kRequestMessage_)
        << "From " << location.file() << ":" << location.line();
    if (!success) abort();
  }

  void CheckRpcSendFailure(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub) {
    const bool success = SendRpc(stub);
    EXPECT_FALSE(success);
  }

  struct ServerData {
    int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;
    bool server_ready_ = false;
    bool started_ = false;

    explicit ServerData(int port = 0) {
      port_ = port > 0 ? port : grpc_pick_unused_port_or_die();
    }

    void Start(const grpc::string& server_host) {
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      started_ = true;
      std::mutex mu;
      std::unique_lock<std::mutex> lock(mu);
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerData::Serve, this, server_host, &mu, &cond)));
      cond.wait(lock, [this] { return server_ready_; });
      server_ready_ = false;
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Serve(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), std::move(creds));
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      std::lock_guard<std::mutex> lock(*mu);
      server_ready_ = true;
      cond->notify_one();
    }

    void Shutdown() {
      if (!started_) return;
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      started_ = false;
    }

    void SetServingStatus(const grpc::string& service, bool serving) {
      server_->GetHealthCheckService()->SetServingStatus(service, serving);
    }
  };

  void ResetCounters() {
    for (const auto& server : servers_) server->service_.ResetCounters();
  }

  void WaitForServer(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      size_t server_idx, const grpc_core::DebugLocation& location,
      bool ignore_failure = false) {
    do {
      if (ignore_failure) {
        SendRpc(stub);
      } else {
        CheckRpcSendOk(stub, location, true);
      }
    } while (servers_[server_idx]->service_.request_count() == 0);
    ResetCounters();
  }

  bool WaitForChannelNotReady(Channel* channel, int timeout_seconds = 5) {
    const gpr_timespec deadline =
        grpc_timeout_seconds_to_deadline(timeout_seconds);
    grpc_connectivity_state state;
    while ((state = channel->GetState(false /* try_to_connect */)) ==
           GRPC_CHANNEL_READY) {
      if (!channel->WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

  bool WaitForChannelReady(Channel* channel, int timeout_seconds = 5) {
    const gpr_timespec deadline =
        grpc_timeout_seconds_to_deadline(timeout_seconds);
    grpc_connectivity_state state;
    while ((state = channel->GetState(true /* try_to_connect */)) !=
           GRPC_CHANNEL_READY) {
      if (!channel->WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

  bool SeenAllServers() {
    for (const auto& server : servers_) {
      if (server->service_.request_count() == 0) return false;
    }
    return true;
  }

  // Updates \a connection_order by appending to it the index of the newly
  // connected server. Must be called after every single RPC.
  void UpdateConnectionOrder(
      const std::vector<std::unique_ptr<ServerData>>& servers,
      std::vector<int>* connection_order) {
    for (size_t i = 0; i < servers.size(); ++i) {
      if (servers[i]->service_.request_count() == 1) {
        // Was the server index known? If not, update connection_order.
        const auto it =
            std::find(connection_order->begin(), connection_order->end(), i);
        if (it == connection_order->end()) {
          connection_order->push_back(i);
          return;
        }
      }
    }
  }

  const grpc::string server_host_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerData>> servers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  const grpc::string kRequestMessage_;
  std::shared_ptr<ChannelCredentials> creds_;
};

TEST_F(ClientLbEnd2endTest, PickFirst) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("");  // test that pick first is the default.
  auto stub = BuildStub(channel);
  SetNextResolution(GetServersPorts());
  for (size_t i = 0; i < servers_.size(); ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // All requests should have gone to a single server.
  bool found = false;
  for (size_t i = 0; i < servers_.size(); ++i) {
    const int request_count = servers_[i]->service_.request_count();
    if (request_count == kNumServers) {
      found = true;
    } else {
      EXPECT_EQ(0, request_count);
    }
  }
  EXPECT_TRUE(found);
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstProcessPending) {
  StartServers(1);                  // Single server
  auto channel = BuildChannel("");  // test that pick first is the default.
  auto stub = BuildStub(channel);
  SetNextResolution({servers_[0]->port_});
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Create a new channel and its corresponding PF LB policy, which will pick
  // the subchannels in READY state from the previous RPC against the same
  // target (even if it happened over a different channel, because subchannels
  // are globally reused). Progress should happen without any transition from
  // this READY state.
  auto second_channel = BuildChannel("");
  auto second_stub = BuildStub(second_channel);
  SetNextResolution({servers_[0]->port_});
  CheckRpcSendOk(second_stub, DEBUG_LOCATION);
}

TEST_F(ClientLbEnd2endTest, PickFirstSelectsReadyAtStartup) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 5000;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  // Create 2 servers, but start only the second one.
  std::vector<int> ports = {grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die()};
  CreateServers(2, ports);
  StartServer(1);
  auto channel1 = BuildChannel("pick_first", args);
  auto stub1 = BuildStub(channel1);
  SetNextResolution(ports);
  // Wait for second server to be ready.
  WaitForServer(stub1, 1, DEBUG_LOCATION);
  // Create a second channel with the same addresses.  Its PF instance
  // should immediately pick the second subchannel, since it's already
  // in READY state.
  auto channel2 = BuildChannel("pick_first", args);
  SetNextResolution(ports);
  // Check that the channel reports READY without waiting for the
  // initial backoff.
  EXPECT_TRUE(WaitForChannelReady(channel2.get(), 1 /* timeout_seconds */));
}

TEST_F(ClientLbEnd2endTest, PickFirstBackOffInitialReconnect) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 100;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  // The channel won't become connected (there's no server).
  ASSERT_FALSE(channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 2)));
  // Bring up a server on the chosen port.
  StartServers(1, ports);
  // Now it will.
  ASSERT_TRUE(channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 2)));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_millis waited_ms = gpr_time_to_millis(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited_ms);
  // We should have waited at least kInitialBackOffMs. We substract one to
  // account for test and precision accuracy drift.
  EXPECT_GE(waited_ms, kInitialBackOffMs - 1);
  // But not much more.
  EXPECT_GT(
      gpr_time_cmp(
          grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 1.10), t1),
      0);
}

TEST_F(ClientLbEnd2endTest, PickFirstBackOffMinReconnect) {
  ChannelArguments args;
  constexpr int kMinReconnectBackOffMs = 1000;
  args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, kMinReconnectBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  // Make connection delay a 10% longer than it's willing to in order to make
  // sure we are hitting the codepath that waits for the min reconnect backoff.
  gpr_atm_rel_store(&g_connection_delay_ms, kMinReconnectBackOffMs * 1.10);
  default_client_impl = grpc_tcp_client_impl;
  grpc_set_tcp_client_impl(&delayed_connect);
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kMinReconnectBackOffMs * 2));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_millis waited_ms = gpr_time_to_millis(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " ms", waited_ms);
  // We should have waited at least kMinReconnectBackOffMs. We substract one to
  // account for test and precision accuracy drift.
  EXPECT_GE(waited_ms, kMinReconnectBackOffMs - 1);
  gpr_atm_rel_store(&g_connection_delay_ms, 0);
}

TEST_F(ClientLbEnd2endTest, PickFirstResetConnectionBackoff) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 1000;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  // The channel won't become connected (there's no server).
  EXPECT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(10)));
  // Bring up a server on the chosen port.
  StartServers(1, ports);
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  // Wait for connect, but not long enough.  This proves that we're
  // being throttled by initial backoff.
  EXPECT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(10)));
  // Reset connection backoff.
  experimental::ChannelResetConnectionBackoff(channel.get());
  // Wait for connect.  Should happen ~immediately.
  EXPECT_TRUE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(10)));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_millis waited_ms = gpr_time_to_millis(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited_ms);
  // We should have waited less than kInitialBackOffMs.
  EXPECT_LT(waited_ms, kInitialBackOffMs);
}

TEST_F(ClientLbEnd2endTest,
       PickFirstResetConnectionBackoffNextAttemptStartsImmediately) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 1000;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  auto channel = BuildChannel("pick_first", args);
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  // Wait for connect, which should fail ~immediately, because the server
  // is not up.
  gpr_log(GPR_INFO, "=== INITIAL CONNECTION ATTEMPT");
  EXPECT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(10)));
  // Reset connection backoff.
  // Note that the time at which the third attempt will be started is
  // actually computed at this point, so we record the start time here.
  gpr_log(GPR_INFO, "=== RESETTING BACKOFF");
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  experimental::ChannelResetConnectionBackoff(channel.get());
  // Trigger a second connection attempt.  This should also fail
  // ~immediately, but the retry should be scheduled for
  // kInitialBackOffMs instead of applying the multiplier.
  gpr_log(GPR_INFO, "=== POLLING FOR SECOND CONNECTION ATTEMPT");
  EXPECT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(10)));
  // Bring up a server on the chosen port.
  gpr_log(GPR_INFO, "=== STARTING BACKEND");
  StartServers(1, ports);
  // Wait for connect.  Should happen within kInitialBackOffMs.
  // Give an extra 100ms to account for the time spent in the second and
  // third connection attempts themselves (since what we really want to
  // measure is the time between the two).  As long as this is less than
  // the 1.6x increase we would see if the backoff state was not reset
  // properly, the test is still proving that the backoff was reset.
  constexpr int kWaitMs = kInitialBackOffMs + 100;
  gpr_log(GPR_INFO, "=== POLLING FOR THIRD CONNECTION ATTEMPT");
  EXPECT_TRUE(channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kWaitMs)));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_millis waited_ms = gpr_time_to_millis(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited_ms);
  EXPECT_LT(waited_ms, kWaitMs);
}

TEST_F(ClientLbEnd2endTest, PickFirstUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);

  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);

  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  ports.clear();
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET none *******");
  grpc_connectivity_state channel_state;
  do {
    channel_state = channel->GetState(true /* try to connect */);
  } while (channel_state == GRPC_CHANNEL_READY);
  ASSERT_NE(channel_state, GRPC_CHANNEL_READY);
  servers_[0]->service_.ResetCounters();

  // Next update introduces servers_[1], making the channel recover.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [1] *******");
  WaitForServer(stub, 1, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);

  // And again for servers_[2]
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [2] *******");
  WaitForServer(stub, 2, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);
  EXPECT_EQ(servers_[1]->service_.request_count(), 0);

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstUpdateSuperset) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);

  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);
  servers_[0]->service_.ResetCounters();

  // Send and superset update
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET superset *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // We stick to the previously connected server.
  WaitForServer(stub, 0, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[1]->service_.request_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

class ClientLbEnd2endWithParamTest
    : public ClientLbEnd2endTest,
      public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    grpc_subchannel_index_test_only_set_force_creation(GetParam());
    ClientLbEnd2endTest::SetUp();
  }

  void TearDown() override {
    ClientLbEnd2endTest::TearDown();
    grpc_subchannel_index_test_only_set_force_creation(false);
  }
};

TEST_P(ClientLbEnd2endWithParamTest, PickFirstManyUpdates) {
  gpr_log(GPR_INFO, "subchannel force creation: %d", GetParam());
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);
  std::vector<int> ports = GetServersPorts();
  for (size_t i = 0; i < 1000; ++i) {
    std::shuffle(ports.begin(), ports.end(),
                 std::mt19937(std::random_device()()));
    SetNextResolution(ports);
    // We should re-enter core at the end of the loop to give the resolution
    // setting closure a chance to run.
    if ((i + 1) % 10 == 0) CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

INSTANTIATE_TEST_CASE_P(SubchannelForceCreation, ClientLbEnd2endWithParamTest,
                        ::testing::Bool());

TEST_F(ClientLbEnd2endTest, PickFirstReresolutionNoSelected) {
  // Prepare the ports for up servers and down servers.
  const int kNumServers = 3;
  const int kNumAliveServers = 1;
  StartServers(kNumAliveServers);
  std::vector<int> alive_ports, dead_ports;
  for (size_t i = 0; i < kNumServers; ++i) {
    if (i < kNumAliveServers) {
      alive_ports.emplace_back(servers_[i]->port_);
    } else {
      dead_ports.emplace_back(grpc_pick_unused_port_or_die());
    }
  }
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);
  // The initial resolution only contains dead ports. There won't be any
  // selected subchannel. Re-resolution will return the same result.
  SetNextResolution(dead_ports);
  gpr_log(GPR_INFO, "****** INITIAL RESOLUTION SET *******");
  for (size_t i = 0; i < 10; ++i) CheckRpcSendFailure(stub);
  // Set a re-resolution result that contains reachable ports, so that the
  // pick_first LB policy can recover soon.
  SetNextResolutionUponError(alive_ports);
  gpr_log(GPR_INFO, "****** RE-RESOLUTION SET *******");
  WaitForServer(stub, 0, DEBUG_LOCATION, true /* ignore_failure */);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstReconnectWithoutNewResolverResult) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  StartServers(1, ports);
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** INITIAL CONNECTION *******");
  WaitForServer(stub, 0, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** STOPPING SERVER ******");
  servers_[0]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  gpr_log(GPR_INFO, "****** RESTARTING SERVER ******");
  StartServers(1, ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

TEST_F(ClientLbEnd2endTest,
       PickFirstReconnectWithoutNewResolverResultStartsFromTopOfList) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die()};
  CreateServers(2, ports);
  StartServer(1);
  auto channel = BuildChannel("pick_first");
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** INITIAL CONNECTION *******");
  WaitForServer(stub, 1, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** STOPPING SERVER ******");
  servers_[1]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  gpr_log(GPR_INFO, "****** STARTING BOTH SERVERS ******");
  StartServers(2, ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

TEST_F(ClientLbEnd2endTest, PickFirstCheckStateBeforeStartWatch) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  StartServers(1, ports);
  auto channel_1 = BuildChannel("pick_first");
  auto stub_1 = BuildStub(channel_1);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** RESOLUTION SET FOR CHANNEL 1 *******");
  WaitForServer(stub_1, 0, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** CHANNEL 1 CONNECTED *******");
  servers_[0]->Shutdown();
  // Channel 1 will receive a re-resolution containing the same server. It will
  // create a new subchannel and hold a ref to it.
  StartServers(1, ports);
  gpr_log(GPR_INFO, "****** SERVER RESTARTED *******");
  auto channel_2 = BuildChannel("pick_first");
  auto stub_2 = BuildStub(channel_2);
  // TODO(juanlishen): This resolution result will only be visible to channel 2
  // since the response generator is only associated with channel 2 now. We
  // should change the response generator to be able to deliver updates to
  // multiple channels at once.
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** RESOLUTION SET FOR CHANNEL 2 *******");
  WaitForServer(stub_2, 0, DEBUG_LOCATION, true);
  gpr_log(GPR_INFO, "****** CHANNEL 2 CONNECTED *******");
  servers_[0]->Shutdown();
  // Wait until the disconnection has triggered the connectivity notification.
  // Otherwise, the subchannel may be picked for next call but will fail soon.
  EXPECT_TRUE(WaitForChannelNotReady(channel_2.get()));
  // Channel 2 will also receive a re-resolution containing the same server.
  // Both channels will ref the same subchannel that failed.
  StartServers(1, ports);
  gpr_log(GPR_INFO, "****** SERVER RESTARTED AGAIN *******");
  gpr_log(GPR_INFO, "****** CHANNEL 2 STARTING A CALL *******");
  // The first call after the server restart will succeed.
  CheckRpcSendOk(stub_2, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** CHANNEL 2 FINISHED A CALL *******");
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_1->GetLoadBalancingPolicyName());
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_2->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstIdleOnDisconnect) {
  // Start server, send RPC, and make sure channel is READY.
  const int kNumServers = 1;
  StartServers(kNumServers);
  auto channel = BuildChannel("");  // pick_first is the default.
  auto stub = BuildStub(channel);
  SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  // Stop server.  Channel should go into state IDLE.
  SetFailureOnReresolution();
  servers_[0]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_IDLE);
  servers_.clear();
}

TEST_F(ClientLbEnd2endTest, RoundRobin) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  SetNextResolution(GetServersPorts());
  // Wait until all backends are ready.
  do {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  } while (!SeenAllServers());
  ResetCounters();
  // "Sync" to the end of the list. Next sequence of picks will start at the
  // first server (index 0).
  WaitForServer(stub, servers_.size() - 1, DEBUG_LOCATION);
  std::vector<int> connection_order;
  for (size_t i = 0; i < servers_.size(); ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
    UpdateConnectionOrder(servers_, &connection_order);
  }
  // Backends should be iterated over in the order in which the addresses were
  // given.
  const auto expected = std::vector<int>{0, 1, 2};
  EXPECT_EQ(expected, connection_order);
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinProcessPending) {
  StartServers(1);  // Single server
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  SetNextResolution({servers_[0]->port_});
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Create a new channel and its corresponding RR LB policy, which will pick
  // the subchannels in READY state from the previous RPC against the same
  // target (even if it happened over a different channel, because subchannels
  // are globally reused). Progress should happen without any transition from
  // this READY state.
  auto second_channel = BuildChannel("round_robin");
  auto second_stub = BuildStub(second_channel);
  SetNextResolution({servers_[0]->port_});
  CheckRpcSendOk(second_stub, DEBUG_LOCATION);
}

TEST_F(ClientLbEnd2endTest, RoundRobinUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  std::vector<int> ports;

  // Start with a single server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Send RPCs. They should all go servers_[0]
  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  servers_[0]->service_.ResetCounters();

  // And now for the second server.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  WaitForServer(stub, 1, DEBUG_LOCATION);

  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(10, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  servers_[1]->service_.ResetCounters();

  // ... and for the last server.
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 2, DEBUG_LOCATION);

  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(10, servers_[2]->service_.request_count());
  servers_[2]->service_.ResetCounters();

  // Back to all servers.
  ports.clear();
  ports.emplace_back(servers_[0]->port_);
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
  WaitForServer(stub, 1, DEBUG_LOCATION);
  WaitForServer(stub, 2, DEBUG_LOCATION);

  // Send three RPCs, one per server.
  for (size_t i = 0; i < 3; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(1, servers_[0]->service_.request_count());
  EXPECT_EQ(1, servers_[1]->service_.request_count());
  EXPECT_EQ(1, servers_[2]->service_.request_count());

  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  ports.clear();
  SetNextResolution(ports);
  grpc_connectivity_state channel_state;
  do {
    channel_state = channel->GetState(true /* try to connect */);
  } while (channel_state == GRPC_CHANNEL_READY);
  ASSERT_NE(channel_state, GRPC_CHANNEL_READY);
  servers_[0]->service_.ResetCounters();

  // Next update introduces servers_[1], making the channel recover.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 1, DEBUG_LOCATION);
  channel_state = channel->GetState(false /* try to connect */);
  ASSERT_EQ(channel_state, GRPC_CHANNEL_READY);

  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinUpdateInError) {
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  std::vector<int> ports;

  // Start with a single server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Send RPCs. They should all go to servers_[0]
  for (size_t i = 0; i < 10; ++i) SendRpc(stub);
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  servers_[0]->service_.ResetCounters();

  // Shutdown one of the servers to be sent in the update.
  servers_[1]->Shutdown();
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
  WaitForServer(stub, 2, DEBUG_LOCATION);

  // Send three RPCs, one per server.
  for (size_t i = 0; i < kNumServers; ++i) SendRpc(stub);
  // The server in shutdown shouldn't receive any.
  EXPECT_EQ(0, servers_[1]->service_.request_count());
}

TEST_F(ClientLbEnd2endTest, RoundRobinManyUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  std::vector<int> ports = GetServersPorts();
  for (size_t i = 0; i < 1000; ++i) {
    std::shuffle(ports.begin(), ports.end(),
                 std::mt19937(std::random_device()()));
    SetNextResolution(ports);
    if (i % 10 == 0) CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinConcurrentUpdates) {
  // TODO(dgq): replicate the way internal testing exercises the concurrent
  // update provisions of RR.
}

TEST_F(ClientLbEnd2endTest, RoundRobinReresolve) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  std::vector<int> first_ports;
  std::vector<int> second_ports;
  first_ports.reserve(kNumServers);
  for (int i = 0; i < kNumServers; ++i) {
    first_ports.push_back(grpc_pick_unused_port_or_die());
  }
  second_ports.reserve(kNumServers);
  for (int i = 0; i < kNumServers; ++i) {
    second_ports.push_back(grpc_pick_unused_port_or_die());
  }
  StartServers(kNumServers, first_ports);
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  SetNextResolution(first_ports);
  // Send a number of RPCs, which succeed.
  for (size_t i = 0; i < 100; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Kill all servers
  gpr_log(GPR_INFO, "****** ABOUT TO KILL SERVERS *******");
  for (size_t i = 0; i < servers_.size(); ++i) {
    servers_[i]->Shutdown();
  }
  gpr_log(GPR_INFO, "****** SERVERS KILLED *******");
  gpr_log(GPR_INFO, "****** SENDING DOOMED REQUESTS *******");
  // Client requests should fail. Send enough to tickle all subchannels.
  for (size_t i = 0; i < servers_.size(); ++i) CheckRpcSendFailure(stub);
  gpr_log(GPR_INFO, "****** DOOMED REQUESTS SENT *******");
  // Bring servers back up on a different set of ports. We need to do this to be
  // sure that the eventual success is *not* due to subchannel reconnection
  // attempts and that an actual re-resolution has happened as a result of the
  // RR policy going into transient failure when all its subchannels become
  // unavailable (in transient failure as well).
  gpr_log(GPR_INFO, "****** RESTARTING SERVERS *******");
  StartServers(kNumServers, second_ports);
  // Don't notify of the update. Wait for the LB policy's re-resolution to
  // "pull" the new ports.
  SetNextResolutionUponError(second_ports);
  gpr_log(GPR_INFO, "****** SERVERS RESTARTED *******");
  gpr_log(GPR_INFO, "****** SENDING REQUEST TO SUCCEED *******");
  // Client request should eventually (but still fairly soon) succeed.
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  while (gpr_time_cmp(deadline, now) > 0) {
    if (SendRpc(stub)) break;
    now = gpr_now(GPR_CLOCK_MONOTONIC);
  }
  ASSERT_GT(gpr_time_cmp(deadline, now), 0);
}

TEST_F(ClientLbEnd2endTest, RoundRobinSingleReconnect) {
  const int kNumServers = 3;
  StartServers(kNumServers);
  const auto ports = GetServersPorts();
  auto channel = BuildChannel("round_robin");
  auto stub = BuildStub(channel);
  SetNextResolution(ports);
  for (size_t i = 0; i < kNumServers; ++i)
    WaitForServer(stub, i, DEBUG_LOCATION);
  for (size_t i = 0; i < servers_.size(); ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
    EXPECT_EQ(1, servers_[i]->service_.request_count()) << "for backend #" << i;
  }
  // One request should have gone to each server.
  for (size_t i = 0; i < servers_.size(); ++i) {
    EXPECT_EQ(1, servers_[i]->service_.request_count());
  }
  const auto pre_death = servers_[0]->service_.request_count();
  // Kill the first server.
  servers_[0]->Shutdown();
  // Client request still succeed. May need retrying if RR had returned a pick
  // before noticing the change in the server's connectivity.
  while (!SendRpc(stub)) {
  }  // Retry until success.
  // Send a bunch of RPCs that should succeed.
  for (int i = 0; i < 10 * kNumServers; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  const auto post_death = servers_[0]->service_.request_count();
  // No requests have gone to the deceased server.
  EXPECT_EQ(pre_death, post_death);
  // Bring the first server back up.
  servers_[0].reset(new ServerData(ports[0]));
  StartServer(0);
  // Requests should start arriving at the first server either right away (if
  // the server managed to start before the RR policy retried the subchannel) or
  // after the subchannel retry delay otherwise (RR's subchannel retried before
  // the server was fully back up).
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

// If health checking is required by client but health checking service
// is not running on the server, the channel should be treated as healthy.
TEST_F(ClientLbEnd2endTest,
       RoundRobinServersHealthCheckingUnimplementedTreatedAsHealthy) {
  StartServers(1);  // Single server
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto channel = BuildChannel("round_robin", args);
  auto stub = BuildStub(channel);
  SetNextResolution({servers_[0]->port_});
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  CheckRpcSendOk(stub, DEBUG_LOCATION);
}

TEST_F(ClientLbEnd2endTest, RoundRobinWithHealthChecking) {
  EnableDefaultHealthCheckService(true);
  // Start servers.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto channel = BuildChannel("round_robin", args);
  auto stub = BuildStub(channel);
  SetNextResolution(GetServersPorts());
  // Channel should not become READY, because health checks should be failing.
  gpr_log(GPR_INFO,
          "*** initial state: unknown health check service name for "
          "all servers");
  EXPECT_FALSE(WaitForChannelReady(channel.get(), 1));
  // Now set one of the servers to be healthy.
  // The channel should become healthy and all requests should go to
  // the healthy server.
  gpr_log(GPR_INFO, "*** server 0 healthy");
  servers_[0]->SetServingStatus("health_check_service_name", true);
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  for (int i = 0; i < 10; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  // Now set a second server to be healthy.
  gpr_log(GPR_INFO, "*** server 2 healthy");
  servers_[2]->SetServingStatus("health_check_service_name", true);
  WaitForServer(stub, 2, DEBUG_LOCATION);
  for (int i = 0; i < 10; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  EXPECT_EQ(5, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(5, servers_[2]->service_.request_count());
  // Now set the remaining server to be healthy.
  gpr_log(GPR_INFO, "*** server 1 healthy");
  servers_[1]->SetServingStatus("health_check_service_name", true);
  WaitForServer(stub, 1, DEBUG_LOCATION);
  for (int i = 0; i < 9; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  EXPECT_EQ(3, servers_[0]->service_.request_count());
  EXPECT_EQ(3, servers_[1]->service_.request_count());
  EXPECT_EQ(3, servers_[2]->service_.request_count());
  // Now set one server to be unhealthy again.  Then wait until the
  // unhealthiness has hit the client.  We know that the client will see
  // this when we send kNumServers requests and one of the remaining servers
  // sees two of the requests.
  gpr_log(GPR_INFO, "*** server 0 unhealthy");
  servers_[0]->SetServingStatus("health_check_service_name", false);
  do {
    ResetCounters();
    for (int i = 0; i < kNumServers; ++i) {
      CheckRpcSendOk(stub, DEBUG_LOCATION);
    }
  } while (servers_[1]->service_.request_count() != 2 &&
           servers_[2]->service_.request_count() != 2);
  // Now set the remaining two servers to be unhealthy.  Make sure the
  // channel leaves READY state and that RPCs fail.
  gpr_log(GPR_INFO, "*** all servers unhealthy");
  servers_[1]->SetServingStatus("health_check_service_name", false);
  servers_[2]->SetServingStatus("health_check_service_name", false);
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  CheckRpcSendFailure(stub);
  // Clean up.
  EnableDefaultHealthCheckService(false);
}

TEST_F(ClientLbEnd2endTest, RoundRobinWithHealthCheckingInhibitPerChannel) {
  EnableDefaultHealthCheckService(true);
  // Start server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  // Create a channel with health-checking enabled.
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto channel1 = BuildChannel("round_robin", args);
  auto stub1 = BuildStub(channel1);
  std::vector<int> ports = GetServersPorts();
  SetNextResolution(ports);
  // Create a channel with health checking enabled but inhibited.
  args.SetInt(GRPC_ARG_INHIBIT_HEALTH_CHECKING, 1);
  auto channel2 = BuildChannel("round_robin", args);
  auto stub2 = BuildStub(channel2);
  SetNextResolution(ports);
  // First channel should not become READY, because health checks should be
  // failing.
  EXPECT_FALSE(WaitForChannelReady(channel1.get(), 1));
  CheckRpcSendFailure(stub1);
  // Second channel should be READY.
  EXPECT_TRUE(WaitForChannelReady(channel2.get(), 1));
  CheckRpcSendOk(stub2, DEBUG_LOCATION);
  // Clean up.
  EnableDefaultHealthCheckService(false);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
