// Copyright 2016 gRPC authors.
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

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/surface/server.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/orca_load_report.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_lb_policies.h"
#include "test/cpp/end2end/connection_delay_injector.h"
#include "test/cpp/end2end/test_service_impl.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {
namespace {

constexpr char kRequestMessage[] = "Live long and prosper.";

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo RPC.
class MyTestServiceImpl : public TestServiceImpl {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    {
      grpc::internal::MutexLock lock(&mu_);
      ++request_count_;
    }
    AddClient(context->peer());
    if (request->has_param() && request->param().has_backend_metrics()) {
      load_report_ = request->param().backend_metrics();
      auto* recorder = context->ExperimentalGetCallMetricRecorder();
      EXPECT_NE(recorder, nullptr);
      recorder->RecordCpuUtilizationMetric(load_report_.cpu_utilization())
          .RecordMemoryUtilizationMetric(load_report_.mem_utilization());
      for (const auto& p : load_report_.request_cost()) {
        recorder->RecordRequestCostMetric(p.first, p.second);
      }
      for (const auto& p : load_report_.utilization()) {
        recorder->RecordUtilizationMetric(p.first, p.second);
      }
    }
    return TestServiceImpl::Echo(context, request, response);
  }

  int request_count() {
    grpc::internal::MutexLock lock(&mu_);
    return request_count_;
  }

  void ResetCounters() {
    grpc::internal::MutexLock lock(&mu_);
    request_count_ = 0;
  }

  std::set<std::string> clients() {
    grpc::internal::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const std::string& client) {
    grpc::internal::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  grpc::internal::Mutex mu_;
  int request_count_ = 0;
  grpc::internal::Mutex clients_mu_;
  std::set<std::string> clients_;
  // For strings storage.
  xds::data::orca::v3::OrcaLoadReport load_report_;
};

class FakeResolverResponseGeneratorWrapper {
 public:
  explicit FakeResolverResponseGeneratorWrapper(bool ipv6_only)
      : ipv6_only_(ipv6_only),
        response_generator_(grpc_core::MakeRefCounted<
                            grpc_core::FakeResolverResponseGenerator>()) {}

  FakeResolverResponseGeneratorWrapper(
      FakeResolverResponseGeneratorWrapper&& other) noexcept {
    ipv6_only_ = other.ipv6_only_;
    response_generator_ = std::move(other.response_generator_);
  }

  void SetNextResolution(
      const std::vector<int>& ports, const char* service_config_json = nullptr,
      const char* attribute_key = nullptr,
      std::unique_ptr<grpc_core::ServerAddress::AttributeInterface> attribute =
          nullptr) {
    grpc_core::ExecCtx exec_ctx;
    response_generator_->SetResponse(
        BuildFakeResults(ipv6_only_, ports, service_config_json, attribute_key,
                         std::move(attribute)));
  }

  void SetNextResolutionUponError(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    response_generator_->SetReresolutionResponse(
        BuildFakeResults(ipv6_only_, ports));
  }

  void SetFailureOnReresolution() {
    grpc_core::ExecCtx exec_ctx;
    response_generator_->SetFailureOnReresolution();
  }

  grpc_core::FakeResolverResponseGenerator* Get() const {
    return response_generator_.get();
  }

 private:
  static grpc_core::Resolver::Result BuildFakeResults(
      bool ipv6_only, const std::vector<int>& ports,
      const char* service_config_json = nullptr,
      const char* attribute_key = nullptr,
      std::unique_ptr<grpc_core::ServerAddress::AttributeInterface> attribute =
          nullptr) {
    grpc_core::Resolver::Result result;
    result.addresses = grpc_core::ServerAddressList();
    for (const int& port : ports) {
      absl::StatusOr<grpc_core::URI> lb_uri = grpc_core::URI::Parse(
          absl::StrCat(ipv6_only ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", port));
      GPR_ASSERT(lb_uri.ok());
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(*lb_uri, &address));
      std::map<const char*,
               std::unique_ptr<grpc_core::ServerAddress::AttributeInterface>>
          attributes;
      if (attribute != nullptr) {
        attributes[attribute_key] = attribute->Copy();
      }
      result.addresses->emplace_back(address.addr, address.len,
                                     nullptr /* args */, std::move(attributes));
    }
    if (service_config_json != nullptr) {
      grpc_error_handle error = GRPC_ERROR_NONE;
      result.service_config = grpc_core::ServiceConfigImpl::Create(
          nullptr, service_config_json, &error);
      GPR_ASSERT(*result.service_config != nullptr);
    }
    return result;
  }

  bool ipv6_only_ = false;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
};

class ClientLbEnd2endTest : public ::testing::Test {
 protected:
  ClientLbEnd2endTest()
      : server_host_("localhost"),
        creds_(new SecureChannelCredentials(
            grpc_fake_transport_security_credentials_create())) {}

  static void SetUpTestCase() {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    gpr_setenv("grpc_cfstream", "0");
#endif
  }

  void SetUp() override {
    grpc_init();
    bool localhost_resolves_to_ipv4 = false;
    bool localhost_resolves_to_ipv6 = false;
    grpc_core::LocalhostResolves(&localhost_resolves_to_ipv4,
                                 &localhost_resolves_to_ipv6);
    ipv6_only_ = !localhost_resolves_to_ipv4 && localhost_resolves_to_ipv6;
  }

  void TearDown() override {
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
    servers_.clear();
    creds_.reset();
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

  std::vector<int> GetServersPorts(size_t start_index = 0,
                                   size_t stop_index = 0) {
    if (stop_index == 0) stop_index = servers_.size();
    std::vector<int> ports;
    for (size_t i = start_index; i < stop_index; ++i) {
      ports.push_back(servers_[i]->port_);
    }
    return ports;
  }

  FakeResolverResponseGeneratorWrapper BuildResolverResponseGenerator() {
    return FakeResolverResponseGeneratorWrapper(ipv6_only_);
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub(
      const std::shared_ptr<Channel>& channel) {
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  std::shared_ptr<Channel> BuildChannel(
      const std::string& lb_policy_name,
      const FakeResolverResponseGeneratorWrapper& response_generator,
      ChannelArguments args = ChannelArguments()) {
    if (!lb_policy_name.empty()) {
      args.SetLoadBalancingPolicyName(lb_policy_name);
    }  // else, default to pick first
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator.Get());
    return grpc::CreateCustomChannel("fake:///", creds_, args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      EchoResponse* response = nullptr, int timeout_ms = 1000,
      Status* result = nullptr, bool wait_for_ready = false,
      EchoRequest* request = nullptr) {
    EchoResponse local_response;
    if (response == nullptr) response = &local_response;
    EchoRequest local_request;
    if (request == nullptr) request = &local_request;
    request->set_message(kRequestMessage);
    request->mutable_param()->set_echo_metadata(true);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    context.AddMetadata("foo", "1");
    context.AddMetadata("bar", "2");
    context.AddMetadata("baz", "3");
    Status status = stub->Echo(&context, *request, response);
    if (result != nullptr) *result = status;
    return status.ok();
  }

  void CheckRpcSendOk(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      const grpc_core::DebugLocation& location, bool wait_for_ready = false,
      xds::data::orca::v3::OrcaLoadReport* load_report = nullptr,
      int timeout_ms = 2000) {
    EchoResponse response;
    Status status;
    EchoRequest request;
    EchoRequest* request_ptr = nullptr;
    if (load_report != nullptr) {
      request_ptr = &request;
      auto params = request.mutable_param();
      auto backend_metrics = params->mutable_backend_metrics();
      *backend_metrics = *load_report;
    }
    const bool success = SendRpc(stub, &response, timeout_ms, &status,
                                 wait_for_ready, request_ptr);
    ASSERT_TRUE(success) << "From " << location.file() << ":" << location.line()
                         << "\nError: " << status.error_message() << " "
                         << status.error_details();
    ASSERT_EQ(response.message(), kRequestMessage)
        << "From " << location.file() << ":" << location.line();
    if (!success) abort();
  }

  void CheckRpcSendFailure(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub) {
    const bool success = SendRpc(stub);
    EXPECT_FALSE(success);
  }

  struct ServerData {
    const int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;
    experimental::OrcaService orca_service_;
    std::unique_ptr<std::thread> thread_;

    grpc::internal::Mutex mu_;
    grpc::internal::CondVar cond_;
    bool server_ready_ ABSL_GUARDED_BY(mu_) = false;
    bool started_ ABSL_GUARDED_BY(mu_) = false;

    explicit ServerData(int port = 0)
        : port_(port > 0 ? port : grpc_pick_unused_port_or_die()),
          orca_service_(experimental::OrcaService::Options()) {}

    void Start(const std::string& server_host) {
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      grpc::internal::MutexLock lock(&mu_);
      started_ = true;
      thread_ = absl::make_unique<std::thread>(
          std::bind(&ServerData::Serve, this, server_host));
      while (!server_ready_) {
        cond_.Wait(&mu_);
      }
      server_ready_ = false;
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Serve(const std::string& server_host) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      experimental::EnableCallMetricRecording(&builder);
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), std::move(creds));
      builder.RegisterService(&service_);
      builder.RegisterService(&orca_service_);
      server_ = builder.BuildAndStart();
      grpc::internal::MutexLock lock(&mu_);
      server_ready_ = true;
      cond_.Signal();
    }

    void Shutdown() {
      grpc::internal::MutexLock lock(&mu_);
      if (!started_) return;
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      started_ = false;
    }

    void SetServingStatus(const std::string& service, bool serving) {
      server_->GetHealthCheckService()->SetServingStatus(service, serving);
    }
  };

  void ResetCounters() {
    for (const auto& server : servers_) server->service_.ResetCounters();
  }

  bool SeenAllServers(size_t start_index, size_t stop_index) {
    for (size_t i = start_index; i < stop_index; ++i) {
      if (servers_[i]->service_.request_count() == 0) return false;
    }
    return true;
  }

  void WaitForServers(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      size_t start_index, size_t stop_index,
      const grpc_core::DebugLocation& location, bool ignore_failure = false,
      absl::Duration timeout = absl::Seconds(30)) {
    auto deadline = absl::Now() + (timeout * grpc_test_slowdown_factor());
    gpr_log(GPR_INFO,
            "========= WAITING FOR BACKENDS [%" PRIuPTR ", %" PRIuPTR
            ") ==========",
            start_index, stop_index);
    while (!SeenAllServers(start_index, stop_index)) {
      if (ignore_failure) {
        SendRpc(stub);
      } else {
        CheckRpcSendOk(stub, location, true);
      }
      EXPECT_LE(absl::Now(), deadline)
          << " at " << location.file() << ":" << location.line();
      if (absl::Now() >= deadline) break;
    }
    ResetCounters();
  }

  void WaitForServer(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      size_t server_index, const grpc_core::DebugLocation& location,
      bool ignore_failure = false) {
    WaitForServers(stub, server_index, server_index + 1, location,
                   ignore_failure);
  }

  bool WaitForChannelState(
      Channel* channel,
      const std::function<bool(grpc_connectivity_state)>& predicate,
      bool try_to_connect = false, int timeout_seconds = 5) {
    const gpr_timespec deadline =
        grpc_timeout_seconds_to_deadline(timeout_seconds);
    while (true) {
      grpc_connectivity_state state = channel->GetState(try_to_connect);
      if (predicate(state)) break;
      if (!channel->WaitForStateChange(state, deadline)) return false;
    }
    return true;
  }

  bool WaitForChannelNotReady(Channel* channel, int timeout_seconds = 5) {
    auto predicate = [](grpc_connectivity_state state) {
      return state != GRPC_CHANNEL_READY;
    };
    return WaitForChannelState(channel, predicate, false, timeout_seconds);
  }

  bool WaitForChannelReady(Channel* channel, int timeout_seconds = 5) {
    auto predicate = [](grpc_connectivity_state state) {
      return state == GRPC_CHANNEL_READY;
    };
    return WaitForChannelState(channel, predicate, true, timeout_seconds);
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

  const std::string server_host_;
  std::vector<std::unique_ptr<ServerData>> servers_;
  std::shared_ptr<ChannelCredentials> creds_;
  bool ipv6_only_ = false;
};

TEST_F(ClientLbEnd2endTest, ChannelStateConnectingWhenResolving) {
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("", response_generator);
  auto stub = BuildStub(channel);
  // Initial state should be IDLE.
  EXPECT_EQ(channel->GetState(false /* try_to_connect */), GRPC_CHANNEL_IDLE);
  // Tell the channel to try to connect.
  // Note that this call also returns IDLE, since the state change has
  // not yet occurred; it just gets triggered by this call.
  EXPECT_EQ(channel->GetState(true /* try_to_connect */), GRPC_CHANNEL_IDLE);
  // Now that the channel is trying to connect, we should be in state
  // CONNECTING.
  EXPECT_EQ(channel->GetState(false /* try_to_connect */),
            GRPC_CHANNEL_CONNECTING);
  // Return a resolver result, which allows the connection attempt to proceed.
  response_generator.SetNextResolution(GetServersPorts());
  // We should eventually transition into state READY.
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
}

TEST_F(ClientLbEnd2endTest, ChannelIdleness) {
  // Start server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  // Set max idle time and build the channel.
  ChannelArguments args;
  args.SetInt(GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS, 1000);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("", response_generator, args);
  auto stub = BuildStub(channel);
  // The initial channel state should be IDLE.
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_IDLE);
  // After sending RPC, channel state should be READY.
  gpr_log(GPR_INFO, "*** SENDING RPC, CHANNEL SHOULD CONNECT ***");
  response_generator.SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  // After a period time not using the channel, the channel state should switch
  // to IDLE.
  gpr_log(GPR_INFO, "*** WAITING FOR CHANNEL TO GO IDLE ***");
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1200));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_IDLE);
  // Sending a new RPC should awake the IDLE channel.
  gpr_log(GPR_INFO, "*** SENDING ANOTHER RPC, CHANNEL SHOULD RECONNECT ***");
  response_generator.SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
}

//
// pick_first tests
//

using PickFirstTest = ClientLbEnd2endTest;

TEST_F(PickFirstTest, Basic) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel(
      "", response_generator);  // test that pick first is the default.
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
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

TEST_F(PickFirstTest, ProcessPending) {
  StartServers(1);  // Single server
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel(
      "", response_generator);  // test that pick first is the default.
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution({servers_[0]->port_});
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Create a new channel and its corresponding PF LB policy, which will pick
  // the subchannels in READY state from the previous RPC against the same
  // target (even if it happened over a different channel, because subchannels
  // are globally reused). Progress should happen without any transition from
  // this READY state.
  auto second_response_generator = BuildResolverResponseGenerator();
  auto second_channel = BuildChannel("", second_response_generator);
  auto second_stub = BuildStub(second_channel);
  second_response_generator.SetNextResolution({servers_[0]->port_});
  CheckRpcSendOk(second_stub, DEBUG_LOCATION);
}

TEST_F(PickFirstTest, SelectsReadyAtStartup) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 5000;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  // Create 2 servers, but start only the second one.
  std::vector<int> ports = {grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die()};
  CreateServers(2, ports);
  StartServer(1);
  auto response_generator1 = BuildResolverResponseGenerator();
  auto channel1 = BuildChannel("pick_first", response_generator1, args);
  auto stub1 = BuildStub(channel1);
  response_generator1.SetNextResolution(ports);
  // Wait for second server to be ready.
  WaitForServer(stub1, 1, DEBUG_LOCATION);
  // Create a second channel with the same addresses.  Its PF instance
  // should immediately pick the second subchannel, since it's already
  // in READY state.
  auto response_generator2 = BuildResolverResponseGenerator();
  auto channel2 = BuildChannel("pick_first", response_generator2, args);
  response_generator2.SetNextResolution(ports);
  // Check that the channel reports READY without waiting for the
  // initial backoff.
  EXPECT_TRUE(WaitForChannelReady(channel2.get(), 1 /* timeout_seconds */));
}

TEST_F(PickFirstTest, BackOffInitialReconnect) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 100;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  // The channel won't become connected (there's no server).
  ASSERT_FALSE(channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 2)));
  // Bring up a server on the chosen port.
  StartServers(1, ports);
  // Now it will.
  ASSERT_TRUE(channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 2)));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_core::Duration waited =
      grpc_core::Duration::FromTimespec(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited.millis());
  // We should have waited at least kInitialBackOffMs. We substract one to
  // account for test and precision accuracy drift.
  EXPECT_GE(waited.millis(), kInitialBackOffMs - 1);
  // But not much more.
  EXPECT_GT(
      gpr_time_cmp(
          grpc_timeout_milliseconds_to_deadline(kInitialBackOffMs * 1.10), t1),
      0);
}

TEST_F(PickFirstTest, BackOffMinReconnect) {
  ChannelArguments args;
  constexpr int kMinReconnectBackOffMs = 1000;
  args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, kMinReconnectBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  // Make connection delay a 10% longer than it's willing to in order to make
  // sure we are hitting the codepath that waits for the min reconnect backoff.
  ConnectionDelayInjector delay_injector(
      grpc_core::Duration::Milliseconds(kMinReconnectBackOffMs * 1.10));
  delay_injector.Start();
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  channel->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kMinReconnectBackOffMs * 2));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_core::Duration waited =
      grpc_core::Duration::FromTimespec(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited.millis());
  // We should have waited at least kMinReconnectBackOffMs. We substract one to
  // account for test and precision accuracy drift.
  EXPECT_GE(waited.millis(), kMinReconnectBackOffMs - 1);
}

TEST_F(PickFirstTest, ResetConnectionBackoff) {
  ChannelArguments args;
  constexpr int kInitialBackOffMs = 1000;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  const std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
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
  // Wait for connect.  Should happen as soon as the client connects to
  // the newly started server, which should be before the initial
  // backoff timeout elapses.
  EXPECT_TRUE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(20)));
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_core::Duration waited =
      grpc_core::Duration::FromTimespec(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited.millis());
  // We should have waited less than kInitialBackOffMs.
  EXPECT_LT(waited.millis(), kInitialBackOffMs);
}

TEST_F(ClientLbEnd2endTest,
       ResetConnectionBackoffNextAttemptStartsImmediately) {
  // A connection attempt injector that allows us to control timing of a
  // connection attempt.
  class ConnectionInjector : public ConnectionAttemptInjector {
   public:
    explicit ConnectionInjector(int port) : port_(port) {}

    void InterceptNextAttempt(grpc_core::CondVar* cv) {
      grpc_core::MutexLock lock(&mu_);
      cv_ = cv;
    }

    void WaitForAttemptToStart(grpc_core::CondVar* cv) {
      grpc_core::MutexLock lock(&mu_);
      while (queued_attempt_ == nullptr) {
        cv->Wait(&mu_);
      }
    }

    void ResumeAttempt() {
      grpc_core::ExecCtx exec_ctx;
      std::unique_ptr<QueuedAttempt> attempt;
      {
        grpc_core::MutexLock lock(&mu_);
        attempt = std::move(queued_attempt_);
      }
      attempt->Resume();
    }

    void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                          grpc_pollset_set* interested_parties,
                          const grpc_channel_args* channel_args,
                          const grpc_resolved_address* addr,
                          grpc_core::Timestamp deadline) override {
      const int port = grpc_sockaddr_get_port(addr);
      gpr_log(GPR_INFO, "==> HandleConnection(): port=%d", port);
      if (port == port_) {
        grpc_core::MutexLock lock(&mu_);
        if (cv_ != nullptr) {
          gpr_log(GPR_INFO, "*** INTERCEPTING CONNECTION ATTEMPT");
          GPR_ASSERT(queued_attempt_ == nullptr);
          queued_attempt_ = absl::make_unique<QueuedAttempt>(
              closure, ep, interested_parties, channel_args, addr, deadline);
          cv_->Signal();
          cv_ = nullptr;
          return;
        }
      }
      AttemptConnection(closure, ep, interested_parties, channel_args, addr,
                        deadline);
    }

   private:
    const int port_;

    grpc_core::Mutex mu_;
    grpc_core::CondVar* cv_ = nullptr;
    std::unique_ptr<QueuedAttempt> queued_attempt_ ABSL_GUARDED_BY(mu_);
  };
  // Get an unused port and start connection injector.
  const int port = grpc_pick_unused_port_or_die();
  ConnectionInjector injector(port);
  injector.Start();
  // Create client.
  ChannelArguments args;
  const int kInitialBackOffMs = 5000 * grpc_test_slowdown_factor();
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, kInitialBackOffMs);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution({port});
  // Intercept initial connection attempt.
  grpc_core::CondVar cv1;
  injector.InterceptNextAttempt(&cv1);
  gpr_log(GPR_INFO, "=== TRIGGERING INITIAL CONNECTION ATTEMPT");
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel->GetState(/*try_to_connect=*/true));
  injector.WaitForAttemptToStart(&cv1);
  EXPECT_EQ(GRPC_CHANNEL_CONNECTING,
            channel->GetState(/*try_to_connect=*/false));
  // Reset backoff.
  gpr_log(GPR_INFO, "=== RESETTING BACKOFF");
  experimental::ChannelResetConnectionBackoff(channel.get());
  // Intercept next attempt.  Do this before resuming the first attempt,
  // just in case the client makes progress faster than this thread.
  grpc_core::CondVar cv2;
  injector.InterceptNextAttempt(&cv2);
  // Fail current attempt and wait for next one to start.
  gpr_log(GPR_INFO, "=== RESUMING INITIAL ATTEMPT");
  const gpr_timespec t0 = gpr_now(GPR_CLOCK_MONOTONIC);
  injector.ResumeAttempt();
  gpr_log(GPR_INFO, "=== WAITING FOR SECOND ATTEMPT");
  // This WaitForStateChange() call just makes sure we're doing some polling.
  EXPECT_TRUE(channel->WaitForStateChange(GRPC_CHANNEL_CONNECTING,
                                          grpc_timeout_seconds_to_deadline(1)));
  injector.WaitForAttemptToStart(&cv2);
  const gpr_timespec t1 = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_log(GPR_INFO, "=== RESUMING SECOND ATTEMPT");
  injector.ResumeAttempt();
  // Elapsed time should be very short, much less than kInitialBackOffMs.
  const grpc_core::Duration waited =
      grpc_core::Duration::FromTimespec(gpr_time_sub(t1, t0));
  gpr_log(GPR_DEBUG, "Waited %" PRId64 " milliseconds", waited.millis());
  EXPECT_LT(waited.millis(), 1000 * grpc_test_slowdown_factor());
}

TEST_F(PickFirstTest, Updates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);

  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);

  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  ports.clear();
  response_generator.SetNextResolution(ports);
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
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [1] *******");
  WaitForServer(stub, 1, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);

  // And again for servers_[2]
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [2] *******");
  WaitForServer(stub, 2, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);
  EXPECT_EQ(servers_[1]->service_.request_count(), 0);

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(PickFirstTest, UpdateSuperset) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);

  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);
  servers_[0]->service_.ResetCounters();

  // Send and superset update
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[0]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET superset *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // We stick to the previously connected server.
  WaitForServer(stub, 0, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[1]->service_.request_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(PickFirstTest, UpdateToUnconnected) {
  const int kNumServers = 2;
  CreateServers(kNumServers);
  StartServer(0);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);

  std::vector<int> ports;

  // Try to send rpcs against a list where the server is available.
  ports.emplace_back(servers_[0]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  CheckRpcSendOk(stub, DEBUG_LOCATION);

  // Send resolution for which all servers are currently unavailable. Eventually
  // this triggers replacing the existing working subchannel_list with the new
  // currently unresponsive list.
  ports.clear();
  ports.emplace_back(grpc_pick_unused_port_or_die());
  ports.emplace_back(servers_[1]->port_);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [unavailable] *******");
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));

  // Ensure that the last resolution was installed correctly by verifying that
  // the channel becomes ready once one of if its endpoints becomes available.
  gpr_log(GPR_INFO, "****** StartServer(1) *******");
  StartServer(1);
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
}

TEST_F(PickFirstTest, GlobalSubchannelPool) {
  // Start one server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  std::vector<int> ports = GetServersPorts();
  // Create two channels that (by default) use the global subchannel pool.
  auto response_generator1 = BuildResolverResponseGenerator();
  auto channel1 = BuildChannel("pick_first", response_generator1);
  auto stub1 = BuildStub(channel1);
  response_generator1.SetNextResolution(ports);
  auto response_generator2 = BuildResolverResponseGenerator();
  auto channel2 = BuildChannel("pick_first", response_generator2);
  auto stub2 = BuildStub(channel2);
  response_generator2.SetNextResolution(ports);
  WaitForServer(stub1, 0, DEBUG_LOCATION);
  // Send one RPC on each channel.
  CheckRpcSendOk(stub1, DEBUG_LOCATION);
  CheckRpcSendOk(stub2, DEBUG_LOCATION);
  // The server receives two requests.
  EXPECT_EQ(2, servers_[0]->service_.request_count());
  // The two requests are from the same client port, because the two channels
  // share subchannels via the global subchannel pool.
  EXPECT_EQ(1UL, servers_[0]->service_.clients().size());
}

TEST_F(PickFirstTest, LocalSubchannelPool) {
  // Start one server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  std::vector<int> ports = GetServersPorts();
  // Create two channels that use local subchannel pool.
  ChannelArguments args;
  args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
  auto response_generator1 = BuildResolverResponseGenerator();
  auto channel1 = BuildChannel("pick_first", response_generator1, args);
  auto stub1 = BuildStub(channel1);
  response_generator1.SetNextResolution(ports);
  auto response_generator2 = BuildResolverResponseGenerator();
  auto channel2 = BuildChannel("pick_first", response_generator2, args);
  auto stub2 = BuildStub(channel2);
  response_generator2.SetNextResolution(ports);
  WaitForServer(stub1, 0, DEBUG_LOCATION);
  // Send one RPC on each channel.
  CheckRpcSendOk(stub1, DEBUG_LOCATION);
  CheckRpcSendOk(stub2, DEBUG_LOCATION);
  // The server receives two requests.
  EXPECT_EQ(2, servers_[0]->service_.request_count());
  // The two requests are from two client ports, because the two channels didn't
  // share subchannels with each other.
  EXPECT_EQ(2UL, servers_[0]->service_.clients().size());
}

TEST_F(PickFirstTest, ManyUpdates) {
  const int kNumUpdates = 1000;
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);
  std::vector<int> ports = GetServersPorts();
  for (size_t i = 0; i < kNumUpdates; ++i) {
    std::shuffle(ports.begin(), ports.end(),
                 std::mt19937(std::random_device()()));
    response_generator.SetNextResolution(ports);
    // We should re-enter core at the end of the loop to give the resolution
    // setting closure a chance to run.
    if ((i + 1) % 10 == 0) CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(PickFirstTest, ReresolutionNoSelected) {
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
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);
  // The initial resolution only contains dead ports. There won't be any
  // selected subchannel. Re-resolution will return the same result.
  response_generator.SetNextResolution(dead_ports);
  gpr_log(GPR_INFO, "****** INITIAL RESOLUTION SET *******");
  for (size_t i = 0; i < 10; ++i) CheckRpcSendFailure(stub);
  // Set a re-resolution result that contains reachable ports, so that the
  // pick_first LB policy can recover soon.
  response_generator.SetNextResolutionUponError(alive_ports);
  gpr_log(GPR_INFO, "****** RE-RESOLUTION SET *******");
  WaitForServer(stub, 0, DEBUG_LOCATION, true /* ignore_failure */);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel->GetLoadBalancingPolicyName());
}

TEST_F(PickFirstTest, ReconnectWithoutNewResolverResult) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  StartServers(1, ports);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** INITIAL CONNECTION *******");
  WaitForServer(stub, 0, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** STOPPING SERVER ******");
  servers_[0]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  gpr_log(GPR_INFO, "****** RESTARTING SERVER ******");
  StartServers(1, ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

TEST_F(PickFirstTest, ReconnectWithoutNewResolverResultStartsFromTopOfList) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die()};
  CreateServers(2, ports);
  StartServer(1);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("pick_first", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** INITIAL CONNECTION *******");
  WaitForServer(stub, 1, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** STOPPING SERVER ******");
  servers_[1]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  gpr_log(GPR_INFO, "****** STARTING BOTH SERVERS ******");
  StartServers(2, ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

TEST_F(PickFirstTest, CheckStateBeforeStartWatch) {
  std::vector<int> ports = {grpc_pick_unused_port_or_die()};
  StartServers(1, ports);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel_1 = BuildChannel("pick_first", response_generator);
  auto stub_1 = BuildStub(channel_1);
  response_generator.SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** RESOLUTION SET FOR CHANNEL 1 *******");
  WaitForServer(stub_1, 0, DEBUG_LOCATION);
  gpr_log(GPR_INFO, "****** CHANNEL 1 CONNECTED *******");
  servers_[0]->Shutdown();
  // Channel 1 will receive a re-resolution containing the same server. It will
  // create a new subchannel and hold a ref to it.
  StartServers(1, ports);
  gpr_log(GPR_INFO, "****** SERVER RESTARTED *******");
  auto response_generator_2 = BuildResolverResponseGenerator();
  auto channel_2 = BuildChannel("pick_first", response_generator_2);
  auto stub_2 = BuildStub(channel_2);
  response_generator_2.SetNextResolution(ports);
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

TEST_F(PickFirstTest, IdleOnDisconnect) {
  // Start server, send RPC, and make sure channel is READY.
  const int kNumServers = 1;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("", response_generator);  // pick_first is the default.
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  // Stop server.  Channel should go into state IDLE.
  response_generator.SetFailureOnReresolution();
  servers_[0]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_IDLE);
  servers_.clear();
}

TEST_F(PickFirstTest, PendingUpdateAndSelectedSubchannelFails) {
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("", response_generator);  // pick_first is the default.
  auto stub = BuildStub(channel);
  // Create a number of servers, but only start 1 of them.
  CreateServers(10);
  StartServer(0);
  // Initially resolve to first server and make sure it connects.
  gpr_log(GPR_INFO, "Phase 1: Connect to first server.");
  response_generator.SetNextResolution({servers_[0]->port_});
  CheckRpcSendOk(stub, DEBUG_LOCATION, true /* wait_for_ready */);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  // Send a resolution update with the remaining servers, none of which are
  // running yet, so the update will stay pending.  Note that it's important
  // to have multiple servers here, or else the test will be flaky; with only
  // one server, the pending subchannel list has already gone into
  // TRANSIENT_FAILURE due to hitting the end of the list by the time we
  // check the state.
  gpr_log(GPR_INFO,
          "Phase 2: Resolver update pointing to remaining "
          "(not started) servers.");
  response_generator.SetNextResolution(GetServersPorts(1 /* start_index */));
  // RPCs will continue to be sent to the first server.
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // Now stop the first server, so that the current subchannel list
  // fails.  This should cause us to immediately swap over to the
  // pending list, even though it's not yet connected.  The state should
  // be set to CONNECTING, since that's what the pending subchannel list
  // was doing when we swapped over.
  gpr_log(GPR_INFO, "Phase 3: Stopping first server.");
  servers_[0]->Shutdown();
  WaitForChannelNotReady(channel.get());
  // TODO(roth): This should always return CONNECTING, but it's flaky
  // between that and TRANSIENT_FAILURE.  I suspect that this problem
  // will go away once we move the backoff code out of the subchannel
  // and into the LB policies.
  EXPECT_THAT(channel->GetState(false),
              ::testing::AnyOf(GRPC_CHANNEL_CONNECTING,
                               GRPC_CHANNEL_TRANSIENT_FAILURE));
  // Now start the second server.
  gpr_log(GPR_INFO, "Phase 4: Starting second server.");
  StartServer(1);
  // The channel should go to READY state and RPCs should go to the
  // second server.
  WaitForChannelReady(channel.get());
  WaitForServer(stub, 1, DEBUG_LOCATION, true /* ignore_failure */);
}

TEST_F(PickFirstTest, StaysIdleUponEmptyUpdate) {
  // Start server, send RPC, and make sure channel is READY.
  const int kNumServers = 1;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("", response_generator);  // pick_first is the default.
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
  // Stop server.  Channel should go into state IDLE.
  servers_[0]->Shutdown();
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_IDLE);
  // Now send resolver update that includes no addresses.  Channel
  // should stay in state IDLE.
  response_generator.SetNextResolution({});
  EXPECT_FALSE(channel->WaitForStateChange(
      GRPC_CHANNEL_IDLE, grpc_timeout_seconds_to_deadline(3)));
  // Now bring the backend back up and send a non-empty resolver update,
  // and then try to send an RPC.  Channel should go back into state READY.
  StartServer(0);
  response_generator.SetNextResolution(GetServersPorts());
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(channel->GetState(false), GRPC_CHANNEL_READY);
}

TEST_F(PickFirstTest,
       StaysTransientFailureOnFailedConnectionAttemptUntilReady) {
  // Allocate 3 ports, with no servers running.
  std::vector<int> ports = {grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die(),
                            grpc_pick_unused_port_or_die()};
  // Create channel with a 1-second backoff.
  ChannelArguments args;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS,
              1000 * grpc_test_slowdown_factor());
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel->GetState(false));
  // Send an RPC, which should fail.
  CheckRpcSendFailure(stub);
  // Channel should be in TRANSIENT_FAILURE.
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel->GetState(false));
  // Now start a server on the last port.
  StartServers(1, {ports.back()});
  // Channel should remain in TRANSIENT_FAILURE until it transitions to READY.
  EXPECT_TRUE(channel->WaitForStateChange(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                          grpc_timeout_seconds_to_deadline(4)));
  EXPECT_EQ(GRPC_CHANNEL_READY, channel->GetState(false));
  CheckRpcSendOk(stub, DEBUG_LOCATION);
}

//
// round_robin tests
//

using RoundRobinTest = ClientLbEnd2endTest;

TEST_F(RoundRobinTest, Basic) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
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

TEST_F(RoundRobinTest, ProcessPending) {
  StartServers(1);  // Single server
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution({servers_[0]->port_});
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Create a new channel and its corresponding RR LB policy, which will pick
  // the subchannels in READY state from the previous RPC against the same
  // target (even if it happened over a different channel, because subchannels
  // are globally reused). Progress should happen without any transition from
  // this READY state.
  auto second_response_generator = BuildResolverResponseGenerator();
  auto second_channel = BuildChannel("round_robin", second_response_generator);
  auto second_stub = BuildStub(second_channel);
  second_response_generator.SetNextResolution({servers_[0]->port_});
  CheckRpcSendOk(second_stub, DEBUG_LOCATION);
}

TEST_F(RoundRobinTest, Updates) {
  // Start servers.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  // Start with a single server.
  gpr_log(GPR_INFO, "*** FIRST BACKEND ***");
  std::vector<int> ports = {servers_[0]->port_};
  response_generator.SetNextResolution(ports);
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Send RPCs. They should all go servers_[0]
  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  ResetCounters();
  // And now for the second server.
  gpr_log(GPR_INFO, "*** SECOND BACKEND ***");
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  response_generator.SetNextResolution(ports);
  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  WaitForServer(stub, 1, DEBUG_LOCATION);
  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(10, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  ResetCounters();
  // ... and for the last server.
  gpr_log(GPR_INFO, "*** THIRD BACKEND ***");
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  response_generator.SetNextResolution(ports);
  WaitForServer(stub, 2, DEBUG_LOCATION);
  for (size_t i = 0; i < 10; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(10, servers_[2]->service_.request_count());
  ResetCounters();
  // Back to all servers.
  gpr_log(GPR_INFO, "*** ALL BACKENDS ***");
  ports.clear();
  ports.emplace_back(servers_[0]->port_);
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[2]->port_);
  response_generator.SetNextResolution(ports);
  WaitForServers(stub, 0, 3, DEBUG_LOCATION);
  // Send three RPCs, one per server.
  for (size_t i = 0; i < 3; ++i) CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(1, servers_[0]->service_.request_count());
  EXPECT_EQ(1, servers_[1]->service_.request_count());
  EXPECT_EQ(1, servers_[2]->service_.request_count());
  ResetCounters();
  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  gpr_log(GPR_INFO, "*** NO BACKENDS ***");
  ports.clear();
  response_generator.SetNextResolution(ports);
  WaitForChannelNotReady(channel.get());
  CheckRpcSendFailure(stub);
  servers_[0]->service_.ResetCounters();
  // Next update introduces servers_[1], making the channel recover.
  gpr_log(GPR_INFO, "*** BACK TO SECOND BACKEND ***");
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  response_generator.SetNextResolution(ports);
  WaitForServer(stub, 1, DEBUG_LOCATION);
  EXPECT_EQ(GRPC_CHANNEL_READY, channel->GetState(/*try_to_connect=*/false));
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel->GetLoadBalancingPolicyName());
}

TEST_F(RoundRobinTest, UpdateInError) {
  StartServers(2);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  // Start with a single server.
  response_generator.SetNextResolution(GetServersPorts(0, 1));
  // Send RPCs. They should all go to server 0.
  for (size_t i = 0; i < 10; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION, /*wait_for_ready=*/false,
                   /*load_report=*/nullptr, /*timeout_ms=*/4000);
  }
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  servers_[0]->service_.ResetCounters();
  // Send an update adding an unreachable server and server 2.
  std::vector<int> ports = {servers_[0]->port_, grpc_pick_unused_port_or_die(),
                            servers_[1]->port_};
  response_generator.SetNextResolution(ports);
  WaitForServers(stub, 0, 2, DEBUG_LOCATION, /*ignore_failure=*/false,
                 /*timeout=*/absl::Seconds(60));
  // Send a bunch more RPCs.  They should all succeed and should be
  // split evenly between the two servers.
  for (size_t i = 0; i < 10; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION, /*wait_for_ready=*/false,
                   /*load_report=*/nullptr, /*timeout_ms=*/4000);
  }
  EXPECT_EQ(5, servers_[0]->service_.request_count());
  EXPECT_EQ(5, servers_[1]->service_.request_count());
}

TEST_F(RoundRobinTest, ManyUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  std::vector<int> ports = GetServersPorts();
  for (size_t i = 0; i < 1000; ++i) {
    std::shuffle(ports.begin(), ports.end(),
                 std::mt19937(std::random_device()()));
    response_generator.SetNextResolution(ports);
    if (i % 10 == 0) CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel->GetLoadBalancingPolicyName());
}

TEST_F(RoundRobinTest, ReresolveOnSubchannelConnectionFailure) {
  // Start 3 servers.
  StartServers(3);
  // Create channel.
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  // Initially, tell the channel about only the first two servers.
  std::vector<int> ports = {servers_[0]->port_, servers_[1]->port_};
  response_generator.SetNextResolution(ports);
  // Wait for both servers to be seen.
  WaitForServers(stub, 0, 2, DEBUG_LOCATION);
  // Tell the fake resolver to send an update that adds the last server, but
  // only when the LB policy requests re-resolution.
  ports.push_back(servers_[2]->port_);
  response_generator.SetNextResolutionUponError(ports);
  // Have server 0 send a GOAWAY.  This should trigger a re-resolution.
  gpr_log(GPR_INFO, "****** SENDING GOAWAY FROM SERVER 0 *******");
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Server::FromC(servers_[0]->server_->c_server())->SendGoaways();
  }
  // Wait for the client to see server 2.
  WaitForServer(stub, 2, DEBUG_LOCATION);
}

TEST_F(RoundRobinTest, TransientFailure) {
  // Start servers and create channel.  Channel should go to READY state.
  const int kNumServers = 3;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  // Now kill the servers.  The channel should transition to TRANSIENT_FAILURE.
  // TODO(roth): This test should ideally check that even when the
  // subchannels are in state CONNECTING for an extended period of time,
  // we will still report TRANSIENT_FAILURE.  Unfortunately, we don't
  // currently have a good way to get a subchannel to report CONNECTING
  // for a long period of time, since the servers in this test framework
  // are on the loopback interface, which will immediately return a
  // "Connection refused" error, so the subchannels will only be in
  // CONNECTING state very briefly.  When we have time, see if we can
  // find a way to fix this.
  for (size_t i = 0; i < servers_.size(); ++i) {
    servers_[i]->Shutdown();
  }
  auto predicate = [](grpc_connectivity_state state) {
    return state == GRPC_CHANNEL_TRANSIENT_FAILURE;
  };
  EXPECT_TRUE(WaitForChannelState(channel.get(), predicate));
}

TEST_F(RoundRobinTest, TransientFailureAtStartup) {
  // Create channel and return servers that don't exist.  Channel should
  // quickly transition into TRANSIENT_FAILURE.
  // TODO(roth): This test should ideally check that even when the
  // subchannels are in state CONNECTING for an extended period of time,
  // we will still report TRANSIENT_FAILURE.  Unfortunately, we don't
  // currently have a good way to get a subchannel to report CONNECTING
  // for a long period of time, since the servers in this test framework
  // are on the loopback interface, which will immediately return a
  // "Connection refused" error, so the subchannels will only be in
  // CONNECTING state very briefly.  When we have time, see if we can
  // find a way to fix this.
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution({
      grpc_pick_unused_port_or_die(),
      grpc_pick_unused_port_or_die(),
      grpc_pick_unused_port_or_die(),
  });
  for (size_t i = 0; i < servers_.size(); ++i) {
    servers_[i]->Shutdown();
  }
  auto predicate = [](grpc_connectivity_state state) {
    return state == GRPC_CHANNEL_TRANSIENT_FAILURE;
  };
  EXPECT_TRUE(WaitForChannelState(channel.get(), predicate, true));
}

TEST_F(RoundRobinTest, DoesNotFailRpcsUponDisconnection) {
  // A connection attempt injector that allows us to control timing.
  class ConnectionInjector : public ConnectionAttemptInjector {
   public:
    explicit ConnectionInjector(int port) : port_(port) {}

    void InterceptNextAttempt() {
      grpc_core::MutexLock lock(&mu_);
      intercept_next_attempt_ = true;
    }

    void WaitForAttemptToStart() {
      grpc_core::MutexLock lock(&mu_);
      while (queued_attempt_ == nullptr) {
        start_cond_.Wait(&mu_);
      }
    }

    void ResumeAttempt() {
      grpc_core::ExecCtx exec_ctx;
      std::unique_ptr<QueuedAttempt> attempt;
      {
        grpc_core::MutexLock lock(&mu_);
        attempt = std::move(queued_attempt_);
      }
      attempt->Resume();
    }

    void WaitForAttemptComplete() {
      grpc_core::MutexLock lock(&mu_);
      while (!attempt_complete_) {
        complete_cond_.Wait(&mu_);
      }
    }

    void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                          grpc_pollset_set* interested_parties,
                          const grpc_channel_args* channel_args,
                          const grpc_resolved_address* addr,
                          grpc_core::Timestamp deadline) override {
      const int port = grpc_sockaddr_get_port(addr);
      gpr_log(GPR_INFO, "==> HandleConnection(): port=%d", port);
      if (port == port_) {
        grpc_core::MutexLock lock(&mu_);
        if (intercept_next_attempt_) {
          gpr_log(GPR_INFO, "*** INTERCEPTING CONNECTION ATTEMPT");
          original_closure_ = closure;
          closure = GRPC_CLOSURE_INIT(&closure_, OnComplete, this, nullptr);
          intercept_next_attempt_ = false;
          queued_attempt_ = absl::make_unique<QueuedAttempt>(
              closure, ep, interested_parties, channel_args, addr, deadline);
          start_cond_.Signal();
          return;
        }
      }
      AttemptConnection(closure, ep, interested_parties, channel_args, addr,
                        deadline);
    }

   private:
    static void OnComplete(void* arg, grpc_error_handle error) {
      auto* self = static_cast<ConnectionInjector*>(arg);
      {
        grpc_core::MutexLock lock(&self->mu_);
        self->attempt_complete_ = true;
        self->complete_cond_.Signal();
      }
      grpc_core::Closure::Run(DEBUG_LOCATION, self->original_closure_,
                              GRPC_ERROR_REF(error));
    }

    const int port_;

    grpc_core::Mutex mu_;
    bool intercept_next_attempt_ ABSL_GUARDED_BY(mu_) = false;
    grpc_core::CondVar start_cond_;
    std::unique_ptr<QueuedAttempt> queued_attempt_ ABSL_GUARDED_BY(mu_);
    grpc_closure* original_closure_ = nullptr;
    grpc_closure closure_;
    grpc_core::CondVar complete_cond_;
    bool attempt_complete_ ABSL_GUARDED_BY(mu_) = false;
  };
  // Start server.
  StartServers(1);
  ConnectionInjector injector(servers_[0]->port_);
  injector.Start();
  // Create channel.
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  // Start a thread constantly sending RPCs in a loop.
  gpr_log(GPR_ERROR, "=== STARTING CLIENT THREAD ===");
  std::atomic<bool> shutdown{false};
  gpr_event ev;
  gpr_event_init(&ev);
  std::thread thd([&]() {
    gpr_log(GPR_INFO, "sending first RPC");
    CheckRpcSendOk(stub, DEBUG_LOCATION);
    gpr_event_set(&ev, reinterpret_cast<void*>(1));
    while (!shutdown.load()) {
      gpr_log(GPR_INFO, "sending RPC");
      CheckRpcSendOk(stub, DEBUG_LOCATION);
    }
  });
  // Wait for first RPC to complete.
  gpr_log(GPR_ERROR, "=== WAITING FOR FIRST RPC TO COMPLETE ===");
  ASSERT_EQ(reinterpret_cast<void*>(1),
            gpr_event_wait(&ev, grpc_timeout_seconds_to_deadline(1)));
  // Channel should now be READY.
  ASSERT_EQ(GRPC_CHANNEL_READY, channel->GetState(false));
  // Tell injector to intercept the next connection attempt.
  injector.InterceptNextAttempt();
  // Now kill the server.  The subchannel should report IDLE and be
  // immediately reconnected to, but this should not cause any test
  // failures.
  gpr_log(GPR_ERROR, "=== SHUTTING DOWN SERVER ===");
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Server::FromC(servers_[0]->server_->c_server())->SendGoaways();
  }
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  servers_[0]->Shutdown();
  // Wait for next attempt to start.
  gpr_log(GPR_ERROR, "=== WAITING FOR RECONNECTION ATTEMPT ===");
  injector.WaitForAttemptToStart();
  // Start server and allow attempt to continue.
  gpr_log(GPR_ERROR, "=== RESTARTING SERVER ===");
  StartServer(0);
  injector.ResumeAttempt();
  // Wait for next attempt to complete.
  gpr_log(GPR_ERROR, "=== WAITING FOR RECONNECTION ATTEMPT TO COMPLETE ===");
  injector.WaitForAttemptComplete();
  // Now shut down the thread.
  gpr_log(GPR_ERROR, "=== SHUTTING DOWN CLIENT THREAD ===");
  shutdown.store(true);
  thd.join();
}

TEST_F(RoundRobinTest, SingleReconnect) {
  const int kNumServers = 3;
  StartServers(kNumServers);
  const auto ports = GetServersPorts();
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(ports);
  for (size_t i = 0; i < kNumServers; ++i) {
    WaitForServer(stub, i, DEBUG_LOCATION);
  }
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
  StartServer(0);
  // Requests should start arriving at the first server either right away (if
  // the server managed to start before the RR policy retried the subchannel) or
  // after the subchannel retry delay otherwise (RR's subchannel retried before
  // the server was fully back up).
  WaitForServer(stub, 0, DEBUG_LOCATION);
}

// If health checking is required by client but health checking service
// is not running on the server, the channel should be treated as healthy.
TEST_F(RoundRobinTest, ServersHealthCheckingUnimplementedTreatedAsHealthy) {
  StartServers(1);  // Single server
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution({servers_[0]->port_});
  EXPECT_TRUE(WaitForChannelReady(channel.get()));
  CheckRpcSendOk(stub, DEBUG_LOCATION);
}

TEST_F(RoundRobinTest, HealthChecking) {
  EnableDefaultHealthCheckService(true);
  // Start servers.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
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

TEST_F(RoundRobinTest, HealthCheckingHandlesSubchannelFailure) {
  EnableDefaultHealthCheckService(true);
  // Start servers.
  const int kNumServers = 3;
  StartServers(kNumServers);
  servers_[0]->SetServingStatus("health_check_service_name", true);
  servers_[1]->SetServingStatus("health_check_service_name", true);
  servers_[2]->SetServingStatus("health_check_service_name", true);
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  WaitForServer(stub, 0, DEBUG_LOCATION);
  // Stop server 0 and send a new resolver result to ensure that RR
  // checks each subchannel's state.
  servers_[0]->Shutdown();
  response_generator.SetNextResolution(GetServersPorts());
  // Send a bunch more RPCs.
  for (size_t i = 0; i < 100; i++) {
    SendRpc(stub);
  }
}

TEST_F(RoundRobinTest, WithHealthCheckingInhibitPerChannel) {
  EnableDefaultHealthCheckService(true);
  // Start server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  // Create a channel with health-checking enabled.
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto response_generator1 = BuildResolverResponseGenerator();
  auto channel1 = BuildChannel("round_robin", response_generator1, args);
  auto stub1 = BuildStub(channel1);
  std::vector<int> ports = GetServersPorts();
  response_generator1.SetNextResolution(ports);
  // Create a channel with health checking enabled but inhibited.
  args.SetInt(GRPC_ARG_INHIBIT_HEALTH_CHECKING, 1);
  auto response_generator2 = BuildResolverResponseGenerator();
  auto channel2 = BuildChannel("round_robin", response_generator2, args);
  auto stub2 = BuildStub(channel2);
  response_generator2.SetNextResolution(ports);
  // First channel should not become READY, because health checks should be
  // failing.
  EXPECT_FALSE(WaitForChannelReady(channel1.get(), 1));
  CheckRpcSendFailure(stub1);
  // Second channel should be READY.
  EXPECT_TRUE(WaitForChannelReady(channel2.get(), 1));
  CheckRpcSendOk(stub2, DEBUG_LOCATION);
  // Enable health checks on the backend and wait for channel 1 to succeed.
  servers_[0]->SetServingStatus("health_check_service_name", true);
  CheckRpcSendOk(stub1, DEBUG_LOCATION, true /* wait_for_ready */);
  // Check that we created only one subchannel to the backend.
  EXPECT_EQ(1UL, servers_[0]->service_.clients().size());
  // Clean up.
  EnableDefaultHealthCheckService(false);
}

TEST_F(RoundRobinTest, HealthCheckingServiceNamePerChannel) {
  EnableDefaultHealthCheckService(true);
  // Start server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  // Create a channel with health-checking enabled.
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}");
  auto response_generator1 = BuildResolverResponseGenerator();
  auto channel1 = BuildChannel("round_robin", response_generator1, args);
  auto stub1 = BuildStub(channel1);
  std::vector<int> ports = GetServersPorts();
  response_generator1.SetNextResolution(ports);
  // Create a channel with health-checking enabled with a different
  // service name.
  ChannelArguments args2;
  args2.SetServiceConfigJSON(
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name2\"}}");
  auto response_generator2 = BuildResolverResponseGenerator();
  auto channel2 = BuildChannel("round_robin", response_generator2, args2);
  auto stub2 = BuildStub(channel2);
  response_generator2.SetNextResolution(ports);
  // Allow health checks from channel 2 to succeed.
  servers_[0]->SetServingStatus("health_check_service_name2", true);
  // First channel should not become READY, because health checks should be
  // failing.
  EXPECT_FALSE(WaitForChannelReady(channel1.get(), 1));
  CheckRpcSendFailure(stub1);
  // Second channel should be READY.
  EXPECT_TRUE(WaitForChannelReady(channel2.get(), 1));
  CheckRpcSendOk(stub2, DEBUG_LOCATION);
  // Enable health checks for channel 1 and wait for it to succeed.
  servers_[0]->SetServingStatus("health_check_service_name", true);
  CheckRpcSendOk(stub1, DEBUG_LOCATION, true /* wait_for_ready */);
  // Check that we created only one subchannel to the backend.
  EXPECT_EQ(1UL, servers_[0]->service_.clients().size());
  // Clean up.
  EnableDefaultHealthCheckService(false);
}

TEST_F(RoundRobinTest,
       HealthCheckingServiceNameChangesAfterSubchannelsCreated) {
  EnableDefaultHealthCheckService(true);
  // Start server.
  const int kNumServers = 1;
  StartServers(kNumServers);
  // Create a channel with health-checking enabled.
  const char* kServiceConfigJson =
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name\"}}";
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("round_robin", response_generator);
  auto stub = BuildStub(channel);
  std::vector<int> ports = GetServersPorts();
  response_generator.SetNextResolution(ports, kServiceConfigJson);
  servers_[0]->SetServingStatus("health_check_service_name", true);
  EXPECT_TRUE(WaitForChannelReady(channel.get(), 1 /* timeout_seconds */));
  // Send an update on the channel to change it to use a health checking
  // service name that is not being reported as healthy.
  const char* kServiceConfigJson2 =
      "{\"healthCheckConfig\": "
      "{\"serviceName\": \"health_check_service_name2\"}}";
  response_generator.SetNextResolution(ports, kServiceConfigJson2);
  EXPECT_TRUE(WaitForChannelNotReady(channel.get()));
  // Clean up.
  EnableDefaultHealthCheckService(false);
}

//
// LB policy pick args
//

class ClientLbPickArgsTest : public ClientLbEnd2endTest {
 protected:
  void SetUp() override {
    ClientLbEnd2endTest::SetUp();
    current_test_instance_ = this;
  }

  static void SetUpTestCase() {
    grpc_init();
    grpc_core::RegisterTestPickArgsLoadBalancingPolicy(SavePickArgs);
  }

  static void TearDownTestCase() { grpc_shutdown(); }

  std::vector<grpc_core::PickArgsSeen> args_seen_list() {
    grpc::internal::MutexLock lock(&mu_);
    return args_seen_list_;
  }

  static std::string ArgsSeenListString(
      const std::vector<grpc_core::PickArgsSeen>& args_seen_list) {
    std::vector<std::string> entries;
    for (const auto& args_seen : args_seen_list) {
      std::vector<std::string> metadata;
      for (const auto& p : args_seen.metadata) {
        metadata.push_back(absl::StrCat(p.first, "=", p.second));
      }
      entries.push_back(absl::StrFormat("{path=\"%s\", metadata=[%s]}",
                                        args_seen.path,
                                        absl::StrJoin(metadata, ", ")));
    }
    return absl::StrCat("[", absl::StrJoin(entries, ", "), "]");
  }

 private:
  static void SavePickArgs(const grpc_core::PickArgsSeen& args_seen) {
    ClientLbPickArgsTest* self = current_test_instance_;
    grpc::internal::MutexLock lock(&self->mu_);
    self->args_seen_list_.emplace_back(args_seen);
  }

  static ClientLbPickArgsTest* current_test_instance_;
  grpc::internal::Mutex mu_;
  std::vector<grpc_core::PickArgsSeen> args_seen_list_;
};

ClientLbPickArgsTest* ClientLbPickArgsTest::current_test_instance_ = nullptr;

TEST_F(ClientLbPickArgsTest, Basic) {
  const int kNumServers = 1;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("test_pick_args_lb", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  // Proactively connect the channel, so that the LB policy will always
  // be connected before it sees the pick.  Otherwise, the test would be
  // flaky because sometimes the pick would be seen twice (once in
  // CONNECTING and again in READY) and other times only once (in READY).
  ASSERT_TRUE(channel->WaitForConnected(gpr_inf_future(GPR_CLOCK_MONOTONIC)));
  // Check LB policy name for the channel.
  EXPECT_EQ("test_pick_args_lb", channel->GetLoadBalancingPolicyName());
  // Now send an RPC and check that the picker sees the expected data.
  CheckRpcSendOk(stub, DEBUG_LOCATION, /*wait_for_ready=*/true);
  auto pick_args_seen_list = args_seen_list();
  EXPECT_THAT(pick_args_seen_list,
              ::testing::ElementsAre(::testing::AllOf(
                  ::testing::Field(&grpc_core::PickArgsSeen::path,
                                   "/grpc.testing.EchoTestService/Echo"),
                  ::testing::Field(&grpc_core::PickArgsSeen::metadata,
                                   ::testing::UnorderedElementsAre(
                                       ::testing::Pair("foo", "1"),
                                       ::testing::Pair("bar", "2"),
                                       ::testing::Pair("baz", "3"))))))
      << ArgsSeenListString(pick_args_seen_list);
}

//
// tests that LB policies can get the call's trailing metadata
//

xds::data::orca::v3::OrcaLoadReport BackendMetricDataToOrcaLoadReport(
    const grpc_core::BackendMetricData& backend_metric_data) {
  xds::data::orca::v3::OrcaLoadReport load_report;
  load_report.set_cpu_utilization(backend_metric_data.cpu_utilization);
  load_report.set_mem_utilization(backend_metric_data.mem_utilization);
  for (const auto& p : backend_metric_data.request_cost) {
    std::string name(p.first);
    (*load_report.mutable_request_cost())[name] = p.second;
  }
  for (const auto& p : backend_metric_data.utilization) {
    std::string name(p.first);
    (*load_report.mutable_utilization())[name] = p.second;
  }
  return load_report;
}

class ClientLbInterceptTrailingMetadataTest : public ClientLbEnd2endTest {
 protected:
  void SetUp() override {
    ClientLbEnd2endTest::SetUp();
    current_test_instance_ = this;
  }

  static void SetUpTestCase() {
    grpc_init();
    grpc_core::RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
        ReportTrailerIntercepted);
  }

  static void TearDownTestCase() { grpc_shutdown(); }

  int trailers_intercepted() {
    grpc::internal::MutexLock lock(&mu_);
    return trailers_intercepted_;
  }

  absl::Status last_status() {
    grpc::internal::MutexLock lock(&mu_);
    return last_status_;
  }

  grpc_core::MetadataVector trailing_metadata() {
    grpc::internal::MutexLock lock(&mu_);
    return std::move(trailing_metadata_);
  }

  absl::optional<xds::data::orca::v3::OrcaLoadReport> backend_load_report() {
    grpc::internal::MutexLock lock(&mu_);
    return std::move(load_report_);
  }

 private:
  static void ReportTrailerIntercepted(
      const grpc_core::TrailingMetadataArgsSeen& args_seen) {
    const auto* backend_metric_data = args_seen.backend_metric_data;
    ClientLbInterceptTrailingMetadataTest* self = current_test_instance_;
    grpc::internal::MutexLock lock(&self->mu_);
    self->last_status_ = args_seen.status;
    self->trailers_intercepted_++;
    self->trailing_metadata_ = args_seen.metadata;
    if (backend_metric_data != nullptr) {
      self->load_report_ =
          BackendMetricDataToOrcaLoadReport(*backend_metric_data);
    }
  }

  static ClientLbInterceptTrailingMetadataTest* current_test_instance_;
  grpc::internal::Mutex mu_;
  int trailers_intercepted_ = 0;
  absl::Status last_status_;
  grpc_core::MetadataVector trailing_metadata_;
  absl::optional<xds::data::orca::v3::OrcaLoadReport> load_report_;
};

ClientLbInterceptTrailingMetadataTest*
    ClientLbInterceptTrailingMetadataTest::current_test_instance_ = nullptr;

TEST_F(ClientLbInterceptTrailingMetadataTest, StatusOk) {
  StartServers(1);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("intercept_trailing_metadata_lb", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  // Send an OK RPC.
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // Check LB policy name for the channel.
  EXPECT_EQ("intercept_trailing_metadata_lb",
            channel->GetLoadBalancingPolicyName());
  EXPECT_EQ(1, trailers_intercepted());
  EXPECT_EQ(absl::OkStatus(), last_status());
}

TEST_F(ClientLbInterceptTrailingMetadataTest, StatusFailed) {
  StartServers(1);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("intercept_trailing_metadata_lb", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  EchoRequest request;
  auto* expected_error = request.mutable_param()->mutable_expected_error();
  expected_error->set_code(GRPC_STATUS_PERMISSION_DENIED);
  expected_error->set_error_message("bummer, man");
  Status status;
  SendRpc(stub, /*response=*/nullptr, /*timeout_ms=*/1000, &status,
          /*wait_for_ready=*/false, &request);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "bummer, man");
  absl::Status status_seen_by_lb = last_status();
  EXPECT_EQ(status_seen_by_lb.code(), absl::StatusCode::kPermissionDenied);
  EXPECT_EQ(status_seen_by_lb.message(), "bummer, man");
}

TEST_F(ClientLbInterceptTrailingMetadataTest,
       StatusCancelledWithoutStartingRecvTrailingMetadata) {
  StartServers(1);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("intercept_trailing_metadata_lb", response_generator);
  response_generator.SetNextResolution(GetServersPorts());
  auto stub = BuildStub(channel);
  {
    // Start a stream (sends initial metadata) and then cancel without
    // calling Finish().
    ClientContext ctx;
    auto stream = stub->BidiStream(&ctx);
    ctx.TryCancel();
  }
  // Check status seen by LB policy.
  EXPECT_EQ(1, trailers_intercepted());
  absl::Status status_seen_by_lb = last_status();
  EXPECT_EQ(status_seen_by_lb.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(status_seen_by_lb.message(), "call cancelled");
}

TEST_F(ClientLbInterceptTrailingMetadataTest, InterceptsRetriesDisabled) {
  const int kNumServers = 1;
  const int kNumRpcs = 10;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_ENABLE_RETRIES, 0);
  auto channel = BuildChannel("intercept_trailing_metadata_lb",
                              response_generator, channel_args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  for (size_t i = 0; i < kNumRpcs; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("intercept_trailing_metadata_lb",
            channel->GetLoadBalancingPolicyName());
  EXPECT_EQ(kNumRpcs, trailers_intercepted());
  EXPECT_THAT(trailing_metadata(),
              ::testing::UnorderedElementsAre(
                  // TODO(roth): Should grpc-status be visible here?
                  ::testing::Pair("grpc-status", "0"),
                  ::testing::Pair("user-agent", ::testing::_),
                  ::testing::Pair("foo", "1"), ::testing::Pair("bar", "2"),
                  ::testing::Pair("baz", "3")));
  EXPECT_FALSE(backend_load_report().has_value());
}

TEST_F(ClientLbInterceptTrailingMetadataTest, InterceptsRetriesEnabled) {
  const int kNumServers = 1;
  const int kNumRpcs = 10;
  StartServers(kNumServers);
  ChannelArguments args;
  args.SetServiceConfigJSON(
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"grpc.testing.EchoTestService\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 3,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}");
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("intercept_trailing_metadata_lb", response_generator, args);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  for (size_t i = 0; i < kNumRpcs; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION);
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("intercept_trailing_metadata_lb",
            channel->GetLoadBalancingPolicyName());
  EXPECT_EQ(kNumRpcs, trailers_intercepted());
  EXPECT_THAT(trailing_metadata(),
              ::testing::UnorderedElementsAre(
                  // TODO(roth): Should grpc-status be visible here?
                  ::testing::Pair("grpc-status", "0"),
                  ::testing::Pair("user-agent", ::testing::_),
                  ::testing::Pair("foo", "1"), ::testing::Pair("bar", "2"),
                  ::testing::Pair("baz", "3")));
  EXPECT_FALSE(backend_load_report().has_value());
}

TEST_F(ClientLbInterceptTrailingMetadataTest, BackendMetricData) {
  const int kNumServers = 1;
  const int kNumRpcs = 10;
  StartServers(kNumServers);
  xds::data::orca::v3::OrcaLoadReport load_report;
  load_report.set_cpu_utilization(0.5);
  load_report.set_mem_utilization(0.75);
  auto* request_cost = load_report.mutable_request_cost();
  (*request_cost)["foo"] = 0.8;
  (*request_cost)["bar"] = 1.4;
  auto* utilization = load_report.mutable_utilization();
  (*utilization)["baz"] = 1.1;
  (*utilization)["quux"] = 0.9;
  auto response_generator = BuildResolverResponseGenerator();
  auto channel =
      BuildChannel("intercept_trailing_metadata_lb", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  for (size_t i = 0; i < kNumRpcs; ++i) {
    CheckRpcSendOk(stub, DEBUG_LOCATION, false, &load_report);
    auto actual = backend_load_report();
    ASSERT_TRUE(actual.has_value());
    // TODO(roth): Change this to use EqualsProto() once that becomes
    // available in OSS.
    EXPECT_EQ(actual->cpu_utilization(), load_report.cpu_utilization());
    EXPECT_EQ(actual->mem_utilization(), load_report.mem_utilization());
    EXPECT_EQ(actual->request_cost().size(), load_report.request_cost().size());
    for (const auto& p : actual->request_cost()) {
      auto it = load_report.request_cost().find(p.first);
      ASSERT_NE(it, load_report.request_cost().end());
      EXPECT_EQ(it->second, p.second);
    }
    EXPECT_EQ(actual->utilization().size(), load_report.utilization().size());
    for (const auto& p : actual->utilization()) {
      auto it = load_report.utilization().find(p.first);
      ASSERT_NE(it, load_report.utilization().end());
      EXPECT_EQ(it->second, p.second);
    }
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("intercept_trailing_metadata_lb",
            channel->GetLoadBalancingPolicyName());
  EXPECT_EQ(kNumRpcs, trailers_intercepted());
}

//
// tests that address attributes from the resolver are visible to the LB policy
//

class ClientLbAddressTest : public ClientLbEnd2endTest {
 protected:
  static const char* kAttributeKey;

  class Attribute : public grpc_core::ServerAddress::AttributeInterface {
   public:
    explicit Attribute(const std::string& str) : str_(str) {}

    std::unique_ptr<AttributeInterface> Copy() const override {
      return absl::make_unique<Attribute>(str_);
    }

    int Cmp(const AttributeInterface* other) const override {
      return str_.compare(static_cast<const Attribute*>(other)->str_);
    }

    std::string ToString() const override { return str_; }

   private:
    std::string str_;
  };

  void SetUp() override {
    ClientLbEnd2endTest::SetUp();
    current_test_instance_ = this;
  }

  static void SetUpTestCase() {
    grpc_init();
    grpc_core::RegisterAddressTestLoadBalancingPolicy(SaveAddress);
  }

  static void TearDownTestCase() { grpc_shutdown(); }

  const std::vector<std::string>& addresses_seen() {
    grpc::internal::MutexLock lock(&mu_);
    return addresses_seen_;
  }

 private:
  static void SaveAddress(const grpc_core::ServerAddress& address) {
    ClientLbAddressTest* self = current_test_instance_;
    grpc::internal::MutexLock lock(&self->mu_);
    self->addresses_seen_.emplace_back(address.ToString());
  }

  static ClientLbAddressTest* current_test_instance_;
  grpc::internal::Mutex mu_;
  std::vector<std::string> addresses_seen_;
};

const char* ClientLbAddressTest::kAttributeKey = "attribute_key";

ClientLbAddressTest* ClientLbAddressTest::current_test_instance_ = nullptr;

TEST_F(ClientLbAddressTest, Basic) {
  const int kNumServers = 1;
  StartServers(kNumServers);
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("address_test_lb", response_generator);
  auto stub = BuildStub(channel);
  // Addresses returned by the resolver will have attached attributes.
  response_generator.SetNextResolution(GetServersPorts(), nullptr,
                                       kAttributeKey,
                                       absl::make_unique<Attribute>("foo"));
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // Check LB policy name for the channel.
  EXPECT_EQ("address_test_lb", channel->GetLoadBalancingPolicyName());
  // Make sure that the attributes wind up on the subchannels.
  std::vector<std::string> expected;
  for (const int port : GetServersPorts()) {
    expected.emplace_back(
        absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", port,
                     " args={} attributes={", kAttributeKey, "=foo}"));
  }
  EXPECT_EQ(addresses_seen(), expected);
}

//
// tests OOB backend metric API
//

class OobBackendMetricTest : public ClientLbEnd2endTest {
 protected:
  using BackendMetricReport =
      std::pair<int /*port*/, xds::data::orca::v3::OrcaLoadReport>;

  void SetUp() override {
    ClientLbEnd2endTest::SetUp();
    current_test_instance_ = this;
  }

  static void SetUpTestCase() {
    grpc_init();
    grpc_core::RegisterOobBackendMetricTestLoadBalancingPolicy(
        BackendMetricCallback);
  }

  static void TearDownTestCase() { grpc_shutdown(); }

  absl::optional<BackendMetricReport> GetBackendMetricReport() {
    grpc::internal::MutexLock lock(&mu_);
    if (backend_metric_reports_.empty()) return absl::nullopt;
    auto result = std::move(backend_metric_reports_.front());
    backend_metric_reports_.pop_front();
    return result;
  }

 private:
  static void BackendMetricCallback(
      grpc_core::ServerAddress address,
      const grpc_core::BackendMetricData& backend_metric_data) {
    auto load_report = BackendMetricDataToOrcaLoadReport(backend_metric_data);
    int port = grpc_sockaddr_get_port(&address.address());
    grpc::internal::MutexLock lock(&current_test_instance_->mu_);
    current_test_instance_->backend_metric_reports_.push_back(
        {port, std::move(load_report)});
  }

  static OobBackendMetricTest* current_test_instance_;
  grpc::internal::Mutex mu_;
  std::deque<BackendMetricReport> backend_metric_reports_ ABSL_GUARDED_BY(&mu_);
};

OobBackendMetricTest* OobBackendMetricTest::current_test_instance_ = nullptr;

TEST_F(OobBackendMetricTest, Basic) {
  StartServers(1);
  // Set initial backend metric data on server.
  constexpr char kMetricName[] = "foo";
  servers_[0]->orca_service_.SetCpuUtilization(0.1);
  servers_[0]->orca_service_.SetMemoryUtilization(0.2);
  servers_[0]->orca_service_.SetNamedUtilization(kMetricName, 0.3);
  // Start client.
  auto response_generator = BuildResolverResponseGenerator();
  auto channel = BuildChannel("oob_backend_metric_test_lb", response_generator);
  auto stub = BuildStub(channel);
  response_generator.SetNextResolution(GetServersPorts());
  // Send an OK RPC.
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  // Check LB policy name for the channel.
  EXPECT_EQ("oob_backend_metric_test_lb",
            channel->GetLoadBalancingPolicyName());
  // Check report seen by client.
  for (size_t i = 0; i < 5; ++i) {
    auto report = GetBackendMetricReport();
    if (report.has_value()) {
      EXPECT_EQ(report->first, servers_[0]->port_);
      EXPECT_EQ(report->second.cpu_utilization(), 0.1);
      EXPECT_EQ(report->second.mem_utilization(), 0.2);
      EXPECT_THAT(
          report->second.utilization(),
          ::testing::UnorderedElementsAre(::testing::Pair(kMetricName, 0.3)));
      break;
    }
    gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  }
  // Now update the utilization data on the server.
  // Note that the server may send a new report while we're updating these,
  // so we set them in reverse order, so that we know we'll get all new
  // data once we see a report with the new CPU utilization value.
  servers_[0]->orca_service_.SetNamedUtilization(kMetricName, 0.6);
  servers_[0]->orca_service_.SetMemoryUtilization(0.5);
  servers_[0]->orca_service_.SetCpuUtilization(0.4);
  // Wait for client to see new report.
  for (size_t i = 0; i < 5; ++i) {
    auto report = GetBackendMetricReport();
    if (report.has_value()) {
      EXPECT_EQ(report->first, servers_[0]->port_);
      if (report->second.cpu_utilization() != 0.1) {
        EXPECT_EQ(report->second.cpu_utilization(), 0.4);
        EXPECT_EQ(report->second.mem_utilization(), 0.5);
        EXPECT_THAT(
            report->second.utilization(),
            ::testing::UnorderedElementsAre(::testing::Pair(kMetricName, 0.6)));
        break;
      }
    }
    gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
