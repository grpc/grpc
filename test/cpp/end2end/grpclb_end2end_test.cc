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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"

#include "src/cpp/client/secure_credentials.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
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

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  size_t request_count() {
    std::unique_lock<std::mutex> lock(mu_);
    return request_count_;
  }

  size_t response_count() {
    std::unique_lock<std::mutex> lock(mu_);
    return response_count_;
  }

  void IncreaseResponseCount() {
    std::unique_lock<std::mutex> lock(mu_);
    ++response_count_;
  }
  void IncreaseRequestCount() {
    std::unique_lock<std::mutex> lock(mu_);
    ++request_count_;
  }

  void ResetCounters() {
    std::unique_lock<std::mutex> lock(mu_);
    request_count_ = 0;
    response_count_ = 0;
  }

 protected:
  std::mutex mu_;

 private:
  size_t request_count_ = 0;
  size_t response_count_ = 0;
};

using BackendService = CountedService<TestServiceImpl>;
using BalancerService = CountedService<LoadBalancer::Service>;

const char g_kCallCredsMdKey[] = "Balancer should not ...";
const char g_kCallCredsMdValue[] = "... receive me";

class BackendServiceImpl : public BackendService {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    // Backend should receive the call credentials metadata.
    auto call_credentials_entry =
        context->client_metadata().find(g_kCallCredsMdKey);
    EXPECT_NE(call_credentials_entry, context->client_metadata().end());
    if (call_credentials_entry != context->client_metadata().end()) {
      EXPECT_EQ(call_credentials_entry->second, g_kCallCredsMdValue);
    }
    IncreaseRequestCount();
    const auto status = TestServiceImpl::Echo(context, request, response);
    IncreaseResponseCount();
    return status;
  }

  // Returns true on its first invocation, false otherwise.
  bool Shutdown() {
    std::unique_lock<std::mutex> lock(mu_);
    const bool prev = !shutdown_;
    shutdown_ = true;
    gpr_log(GPR_INFO, "Backend: shut down");
    return prev;
  }

 private:
  std::mutex mu_;
  bool shutdown_ = false;
};

grpc::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
  return grpc::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

struct ClientStats {
  size_t num_calls_started = 0;
  size_t num_calls_finished = 0;
  size_t num_calls_finished_with_client_failed_to_send = 0;
  size_t num_calls_finished_known_received = 0;
  std::map<grpc::string, size_t> drop_token_counts;

  ClientStats& operator+=(const ClientStats& other) {
    num_calls_started += other.num_calls_started;
    num_calls_finished += other.num_calls_finished;
    num_calls_finished_with_client_failed_to_send +=
        other.num_calls_finished_with_client_failed_to_send;
    num_calls_finished_known_received +=
        other.num_calls_finished_known_received;
    for (const auto& p : other.drop_token_counts) {
      drop_token_counts[p.first] += p.second;
    }
    return *this;
  }
};

class BalancerServiceImpl : public BalancerService {
 public:
  using Stream = ServerReaderWriter<LoadBalanceResponse, LoadBalanceRequest>;
  using ResponseDelayPair = std::pair<LoadBalanceResponse, int>;

  explicit BalancerServiceImpl(int client_load_reporting_interval_seconds)
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        shutdown_(false) {}

  Status BalanceLoad(ServerContext* context, Stream* stream) override {
    // Balancer shouldn't receive the call credentials metadata.
    EXPECT_EQ(context->client_metadata().find(g_kCallCredsMdKey),
              context->client_metadata().end());
    gpr_log(GPR_INFO, "LB[%p]: BalanceLoad", this);
    LoadBalanceRequest request;
    std::vector<ResponseDelayPair> responses_and_delays;

    if (!stream->Read(&request)) {
      goto done;
    }
    IncreaseRequestCount();
    gpr_log(GPR_INFO, "LB[%p]: received initial message '%s'", this,
            request.DebugString().c_str());

    // TODO(juanlishen): Initial response should always be the first response.
    if (client_load_reporting_interval_seconds_ > 0) {
      LoadBalanceResponse initial_response;
      initial_response.mutable_initial_response()
          ->mutable_client_stats_report_interval()
          ->set_seconds(client_load_reporting_interval_seconds_);
      stream->Write(initial_response);
    }

    {
      std::unique_lock<std::mutex> lock(mu_);
      responses_and_delays = responses_and_delays_;
    }
    for (const auto& response_and_delay : responses_and_delays) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        if (shutdown_) goto done;
      }
      SendResponse(stream, response_and_delay.first, response_and_delay.second);
    }
    {
      std::unique_lock<std::mutex> lock(mu_);
      if (shutdown_) goto done;
      serverlist_cond_.wait(lock, [this] { return serverlist_ready_; });
    }

    if (client_load_reporting_interval_seconds_ > 0) {
      request.Clear();
      if (stream->Read(&request)) {
        gpr_log(GPR_INFO, "LB[%p]: received client load report message '%s'",
                this, request.DebugString().c_str());
        GPR_ASSERT(request.has_client_stats());
        // We need to acquire the lock here in order to prevent the notify_one
        // below from firing before its corresponding wait is executed.
        std::lock_guard<std::mutex> lock(mu_);
        client_stats_.num_calls_started +=
            request.client_stats().num_calls_started();
        client_stats_.num_calls_finished +=
            request.client_stats().num_calls_finished();
        client_stats_.num_calls_finished_with_client_failed_to_send +=
            request.client_stats()
                .num_calls_finished_with_client_failed_to_send();
        client_stats_.num_calls_finished_known_received +=
            request.client_stats().num_calls_finished_known_received();
        for (const auto& drop_token_count :
             request.client_stats().calls_finished_with_drop()) {
          client_stats_
              .drop_token_counts[drop_token_count.load_balance_token()] +=
              drop_token_count.num_calls();
        }
        load_report_ready_ = true;
        load_report_cond_.notify_one();
      }
    }
  done:
    gpr_log(GPR_INFO, "LB[%p]: done", this);
    return Status::OK;
  }

  void add_response(const LoadBalanceResponse& response, int send_after_ms) {
    std::unique_lock<std::mutex> lock(mu_);
    responses_and_delays_.push_back(std::make_pair(response, send_after_ms));
  }

  // Returns true on its first invocation, false otherwise.
  bool Shutdown() {
    NotifyDoneWithServerlists();
    std::unique_lock<std::mutex> lock(mu_);
    const bool prev = !shutdown_;
    shutdown_ = true;
    gpr_log(GPR_INFO, "LB[%p]: shut down", this);
    return prev;
  }

  static LoadBalanceResponse BuildResponseForBackends(
      const std::vector<int>& backend_ports,
      const std::map<grpc::string, size_t>& drop_token_counts) {
    LoadBalanceResponse response;
    for (const auto& drop_token_count : drop_token_counts) {
      for (size_t i = 0; i < drop_token_count.second; ++i) {
        auto* server = response.mutable_server_list()->add_servers();
        server->set_drop(true);
        server->set_load_balance_token(drop_token_count.first);
      }
    }
    for (const int& backend_port : backend_ports) {
      auto* server = response.mutable_server_list()->add_servers();
      server->set_ip_address(Ip4ToPackedString("127.0.0.1"));
      server->set_port(backend_port);
    }
    return response;
  }

  const ClientStats& WaitForLoadReport() {
    std::unique_lock<std::mutex> lock(mu_);
    load_report_cond_.wait(lock, [this] { return load_report_ready_; });
    load_report_ready_ = false;
    return client_stats_;
  }

  void NotifyDoneWithServerlists() {
    std::lock_guard<std::mutex> lock(mu_);
    serverlist_ready_ = true;
    serverlist_cond_.notify_all();
  }

 private:
  void SendResponse(Stream* stream, const LoadBalanceResponse& response,
                    int delay_ms) {
    gpr_log(GPR_INFO, "LB[%p]: sleeping for %d ms...", this, delay_ms);
    if (delay_ms > 0) {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    }
    gpr_log(GPR_INFO, "LB[%p]: Woke up! Sending response '%s'", this,
            response.DebugString().c_str());
    IncreaseResponseCount();
    stream->Write(response);
  }

  const int client_load_reporting_interval_seconds_;
  std::vector<ResponseDelayPair> responses_and_delays_;
  std::mutex mu_;
  std::condition_variable load_report_cond_;
  bool load_report_ready_ = false;
  std::condition_variable serverlist_cond_;
  bool serverlist_ready_ = false;
  ClientStats client_stats_;
  bool shutdown_;
};

class GrpclbEnd2endTest : public ::testing::Test {
 protected:
  GrpclbEnd2endTest(int num_backends, int num_balancers,
                    int client_load_reporting_interval_seconds)
      : server_host_("localhost"),
        num_backends_(num_backends),
        num_balancers_(num_balancers),
        client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds) {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    gpr_setenv("GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS", "1");
  }

  void SetUp() override {
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
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
    ResetStub();
  }

  void TearDown() override {
    for (size_t i = 0; i < backends_.size(); ++i) {
      if (backends_[i]->Shutdown()) backend_servers_[i].Shutdown();
    }
    for (size_t i = 0; i < balancers_.size(); ++i) {
      if (balancers_[i]->Shutdown()) balancer_servers_[i].Shutdown();
    }
  }

  void SetNextResolutionAllBalancers() {
    std::vector<AddressData> addresses;
    for (size_t i = 0; i < balancer_servers_.size(); ++i) {
      addresses.emplace_back(AddressData{balancer_servers_[i].port_, true, ""});
    }
    SetNextResolution(addresses);
  }

  void ResetStub(int fallback_timeout = 0,
                 const grpc::string& expected_targets = "") {
    ChannelArguments args;
    args.SetGrpclbFallbackTimeout(fallback_timeout);
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    if (!expected_targets.empty()) {
      args.SetString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
    }
    std::ostringstream uri;
    uri << "fake:///" << kApplicationTargetName_;
    // TODO(dgq): templatize tests to run everything using both secure and
    // insecure channel credentials.
    grpc_channel_credentials* channel_creds =
        grpc_fake_transport_security_credentials_create();
    grpc_call_credentials* call_creds = grpc_md_only_test_credentials_create(
        g_kCallCredsMdKey, g_kCallCredsMdValue, false);
    std::shared_ptr<ChannelCredentials> creds(
        new SecureChannelCredentials(grpc_composite_channel_credentials_create(
            channel_creds, call_creds, nullptr)));
    grpc_call_credentials_unref(call_creds);
    grpc_channel_credentials_unref(channel_creds);
    channel_ = CreateCustomChannel(uri.str(), creds, args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void ResetBackendCounters() {
    for (const auto& backend : backends_) backend->ResetCounters();
  }

  ClientStats WaitForLoadReports() {
    ClientStats client_stats;
    for (const auto& balancer : balancers_) {
      client_stats += balancer->WaitForLoadReport();
    }
    return client_stats;
  }

  bool SeenAllBackends() {
    for (const auto& backend : backends_) {
      if (backend->request_count() == 0) return false;
    }
    return true;
  }

  void SendRpcAndCount(int* num_total, int* num_ok, int* num_failure,
                       int* num_drops) {
    const Status status = SendRpc();
    if (status.ok()) {
      ++*num_ok;
    } else {
      if (status.error_message() == "Call dropped by load balancing policy") {
        ++*num_drops;
      } else {
        ++*num_failure;
      }
    }
    ++*num_total;
  }

  std::tuple<int, int, int> WaitForAllBackends(
      int num_requests_multiple_of = 1) {
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    while (!SeenAllBackends()) {
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    while (num_total % num_requests_multiple_of != 0) {
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops);
    }
    ResetBackendCounters();
    gpr_log(GPR_INFO,
            "Performed %d warm up requests (a multiple of %d) against the "
            "backends. %d succeeded, %d failed, %d dropped.",
            num_total, num_requests_multiple_of, num_ok, num_failure,
            num_drops);
    return std::make_tuple(num_ok, num_failure, num_drops);
  }

  void WaitForBackend(size_t backend_idx) {
    do {
      (void)SendRpc();
    } while (backends_[backend_idx]->request_count() == 0);
    ResetBackendCounters();
  }

  struct AddressData {
    int port;
    bool is_balancer;
    grpc::string balancer_name;
  };

  grpc_lb_addresses* CreateLbAddressesFromAddressDataList(
      const std::vector<AddressData>& address_data) {
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(address_data.size(), nullptr);
    for (size_t i = 0; i < address_data.size(); ++i) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", address_data[i].port);
      grpc_uri* lb_uri = grpc_uri_parse(lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_lb_addresses_set_address_from_uri(
          addresses, i, lb_uri, address_data[i].is_balancer,
          address_data[i].balancer_name.c_str(), nullptr);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    return addresses;
  }

  void SetNextResolution(const std::vector<AddressData>& address_data) {
    grpc_core::ExecCtx exec_ctx;
    grpc_lb_addresses* addresses =
        CreateLbAddressesFromAddressDataList(address_data);
    grpc_arg fake_addresses = grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args fake_result = {1, &fake_addresses};
    response_generator_->SetResponse(&fake_result);
    grpc_lb_addresses_destroy(addresses);
  }

  void SetNextReresolutionResponse(
      const std::vector<AddressData>& address_data) {
    grpc_core::ExecCtx exec_ctx;
    grpc_lb_addresses* addresses =
        CreateLbAddressesFromAddressDataList(address_data);
    grpc_arg fake_addresses = grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args fake_result = {1, &fake_addresses};
    response_generator_->SetReresolutionResponse(&fake_result);
    grpc_lb_addresses_destroy(addresses);
  }

  const std::vector<int> GetBackendPorts(const size_t start_index = 0) const {
    std::vector<int> backend_ports;
    for (size_t i = start_index; i < backend_servers_.size(); ++i) {
      backend_ports.push_back(backend_servers_[i].port_);
    }
    return backend_ports;
  }

  void ScheduleResponseForBalancer(size_t i,
                                   const LoadBalanceResponse& response,
                                   int delay_ms) {
    balancers_.at(i)->add_response(response, delay_ms);
  }

  Status SendRpc(EchoResponse* response = nullptr, int timeout_ms = 1000) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    Status status = stub_->Echo(&context, request, response);
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1, const int timeout_ms = 1000) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(&response, timeout_ms);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage_);
    }
  }

  void CheckRpcSendFailure() {
    const Status status = SendRpc();
    EXPECT_FALSE(status.ok());
  }

  template <typename T>
  struct ServerThread {
    explicit ServerThread(const grpc::string& type,
                          const grpc::string& server_host, T* service)
        : type_(type), service_(service) {
      std::mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Start from firing before the wait below is hit.
      std::unique_lock<std::mutex> lock(mu);
      port_ = grpc_pick_unused_port_or_die();
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Start, this, server_host, &mu, &cond)));
      cond.wait(lock);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Start(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      std::lock_guard<std::mutex> lock(*mu);
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), creds);
      builder.RegisterService(service_);
      server_ = builder.BuildAndStart();
      cond->notify_one();
    }

    void Shutdown() {
      gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      gpr_log(GPR_INFO, "%s shutdown completed", type_.c_str());
    }

    int port_;
    grpc::string type_;
    std::unique_ptr<Server> server_;
    T* service_;
    std::unique_ptr<std::thread> thread_;
  };

  const grpc::string server_host_;
  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<BackendServiceImpl>> backends_;
  std::vector<std::unique_ptr<BalancerServiceImpl>> balancers_;
  std::vector<ServerThread<BackendService>> backend_servers_;
  std::vector<ServerThread<BalancerService>> balancer_servers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  const grpc::string kRequestMessage_ = "Live long and prosper.";
  const grpc::string kApplicationTargetName_ = "application_target_name";
};

class SingleBalancerTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerTest() : GrpclbEnd2endTest(4, 1, 0) {}
};

TEST_F(SingleBalancerTest, Vanilla) {
  SetNextResolutionAllBalancers();
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

TEST_F(SingleBalancerTest, SecureNaming) {
  ResetStub(0, kApplicationTargetName_ + ";lb");
  SetNextResolution({AddressData{balancer_servers_[0].port_, true, "lb"}});
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

TEST_F(SingleBalancerTest, SecureNamingDeathTest) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH(
      {
        ResetStub(0, kApplicationTargetName_ + ";lb");
        SetNextResolution(
            {AddressData{balancer_servers_[0].port_, true, "woops"}});
        channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(1));
      },
      "");
}

TEST_F(SingleBalancerTest, InitiallyEmptyServerlist) {
  SetNextResolutionAllBalancers();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const int kCallDeadlineMs = kServerlistDelayMs * 2;
  // First response is an empty serverlist, sent right away.
  ScheduleResponseForBalancer(0, LoadBalanceResponse(), 0);
  // Send non-empty serverlist only after kServerlistDelayMs
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      kServerlistDelayMs);

  const auto t0 = system_clock::now();
  // Client will block: LB will initially send empty serverlist.
  CheckRpcSendOk(1, kCallDeadlineMs);
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline (which is enforced by
  // the call's deadline).
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancer_servers_[0].service_->response_count());
}

TEST_F(SingleBalancerTest, Fallback) {
  SetNextResolutionAllBalancers();
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
      0,
      BalancerServiceImpl::BuildResponseForBackends(
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
  SetNextResolutionAllBalancers();
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
      0,
      BalancerServiceImpl::BuildResponseForBackends(
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
  SetNextResolutionAllBalancers();
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
}

class UpdatesTest : public GrpclbEnd2endTest {
 public:
  UpdatesTest() : GrpclbEnd2endTest(4, 3, 0) {}
};

TEST_F(UpdatesTest, UpdateBalancers) {
  SetNextResolutionAllBalancers();
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(first_backend, {}), 0);
  ScheduleResponseForBalancer(
      1, BalancerServiceImpl::BuildResponseForBackends(second_backend, {}), 0);

  // Wait until the first backend is ready.
  WaitForBackend(0);

  // Send 10 requests.
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
}

// Send an update with the same set of LBs as the one in SetUp() in order to
// verify that the LB channel inside grpclb keeps the initial connection (which
// by definition is also present in the update).
TEST_F(UpdatesTest, UpdateBalancersRepeated) {
  SetNextResolutionAllBalancers();
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[0]};

  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(first_backend, {}), 0);
  ScheduleResponseForBalancer(
      1, BalancerServiceImpl::BuildResponseForBackends(second_backend, {}), 0);

  // Wait until the first backend is ready.
  WaitForBackend(0);

  // Send 10 requests.
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
}

TEST_F(UpdatesTest, UpdateBalancersDeadUpdate) {
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  SetNextResolution(addresses);
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

  addresses.clear();
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request. In the meantime, the client continues to be serviced
  // (by the first backend) without interruption.
  EXPECT_EQ(0U, backend_servers_[1].service_->request_count());
  WaitForBackend(1);

  // This is serviced by the updated RR policy
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
  // The second balancer, published as part of the first update, may end up
  // getting two requests (that is, 1 <= #req <= 2) if the LB call retry timer
  // firing races with the arrival of the update containing the second
  // balancer.
  EXPECT_GE(balancer_servers_[1].service_->request_count(), 1U);
  EXPECT_GE(balancer_servers_[1].service_->response_count(), 1U);
  EXPECT_LE(balancer_servers_[1].service_->request_count(), 2U);
  EXPECT_LE(balancer_servers_[1].service_->response_count(), 2U);
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());
}

TEST_F(UpdatesTest, ReresolveDeadBackend) {
  ResetStub(500);
  // The first resolution contains the addresses of a balancer that never
  // responds, and a fallback backend.
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  addresses.emplace_back(AddressData{backend_servers_[0].port_, false, ""});
  SetNextResolution(addresses);
  // The re-resolution result will contain the addresses of the same balancer
  // and a new fallback backend.
  addresses.clear();
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  addresses.emplace_back(AddressData{backend_servers_[1].port_, false, ""});
  SetNextReresolutionResponse(addresses);

  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the fallback backend.
  EXPECT_EQ(10U, backend_servers_[0].service_->request_count());

  // Kill backend 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BACKEND 0 *************");
  if (backends_[0]->Shutdown()) backend_servers_[0].Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BACKEND 0 *************");

  // Wait until re-resolution has finished, as signaled by the second backend
  // receiving a request.
  WaitForBackend(1);

  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backend_servers_[1].service_->request_count());

  balancers_[0]->NotifyDoneWithServerlists();
  balancers_[1]->NotifyDoneWithServerlists();
  balancers_[2]->NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());
}

// TODO(juanlishen): Should be removed when the first response is always the
// initial response. Currently, if client load reporting is not enabled, the
// balancer doesn't send initial response. When the backend shuts down, an
// unexpected re-resolution will happen. This test configuration is a workaround
// for test ReresolveDeadBalancer.
class UpdatesWithClientLoadReportingTest : public GrpclbEnd2endTest {
 public:
  UpdatesWithClientLoadReportingTest() : GrpclbEnd2endTest(4, 3, 2) {}
};

TEST_F(UpdatesWithClientLoadReportingTest, ReresolveDeadBalancer) {
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancer_servers_[0].port_, true, ""});
  SetNextResolution(addresses);
  addresses.clear();
  addresses.emplace_back(AddressData{balancer_servers_[1].port_, true, ""});
  SetNextReresolutionResponse(addresses);
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

  // Kill backend 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BACKEND 0 *************");
  if (backends_[0]->Shutdown()) backend_servers_[0].Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BACKEND 0 *************");

  CheckRpcSendFailure();

  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[1].service_->response_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());

  // Kill balancer 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  if (balancers_[0]->Shutdown()) balancer_servers_[0].Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");

  // Wait until re-resolution has finished, as signaled by the second backend
  // receiving a request.
  WaitForBackend(1);

  // This is serviced by the new serverlist.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backend_servers_[1].service_->request_count());

  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  EXPECT_EQ(1U, balancer_servers_[0].service_->response_count());
  // After balancer 0 is killed, we restart an LB call immediately (because we
  // disconnect to a previously connected balancer). Although we will cancel
  // this call when the re-resolution update is done and another LB call restart
  // is needed, this old call may still succeed reaching the LB server if
  // re-resolution is slow. So balancer 1 may have received 2 requests and sent
  // 2 responses.
  EXPECT_GE(balancer_servers_[1].service_->request_count(), 1U);
  EXPECT_GE(balancer_servers_[1].service_->response_count(), 1U);
  EXPECT_LE(balancer_servers_[1].service_->request_count(), 2U);
  EXPECT_LE(balancer_servers_[1].service_->response_count(), 2U);
  EXPECT_EQ(0U, balancer_servers_[2].service_->request_count());
  EXPECT_EQ(0U, balancer_servers_[2].service_->response_count());
}

TEST_F(SingleBalancerTest, Drop) {
  SetNextResolutionAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 2;
  const int num_of_drop_addresses = num_of_drop_by_rate_limiting_addresses +
                                    num_of_drop_by_load_balancing_addresses;
  const int num_total_addresses = num_backends_ + num_of_drop_addresses;
  ScheduleResponseForBalancer(
      0,
      BalancerServiceImpl::BuildResponseForBackends(
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
  SetNextResolutionAllBalancers();
  // All registered addresses are marked as "drop".
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 1;
  ScheduleResponseForBalancer(
      0,
      BalancerServiceImpl::BuildResponseForBackends(
          {}, {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
               {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      0);
  const Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
}

TEST_F(SingleBalancerTest, DropAll) {
  SetNextResolutionAllBalancers();
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 1;
  ScheduleResponseForBalancer(
      0,
      BalancerServiceImpl::BuildResponseForBackends(
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
  SetNextResolutionAllBalancers();
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
  SetNextResolutionAllBalancers();
  const size_t kNumRpcsPerAddress = 3;
  const int num_of_drop_by_rate_limiting_addresses = 2;
  const int num_of_drop_by_load_balancing_addresses = 1;
  const int num_of_drop_addresses = num_of_drop_by_rate_limiting_addresses +
                                    num_of_drop_by_load_balancing_addresses;
  const int num_total_addresses = num_backends_ + num_of_drop_addresses;
  ScheduleResponseForBalancer(
      0,
      BalancerServiceImpl::BuildResponseForBackends(
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
