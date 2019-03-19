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
#include <set>
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

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"

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
// - Test against a non-LB server.
// - Random LB server closing the stream unexpectedly.
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
    AddClient(context->peer());
    return status;
  }

  void Start() {}

  void Shutdown() {}

  std::set<grpc::string> clients() {
    std::unique_lock<std::mutex> lock(clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const grpc::string& client) {
    std::unique_lock<std::mutex> lock(clients_mu_);
    clients_.insert(client);
  }

  std::mutex mu_;
  std::mutex clients_mu_;
  std::set<grpc::string> clients_;
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

  void Reset() {
    num_calls_started = 0;
    num_calls_finished = 0;
    num_calls_finished_with_client_failed_to_send = 0;
    num_calls_finished_known_received = 0;
    drop_token_counts.clear();
  }
};

class BalancerServiceImpl : public BalancerService {
 public:
  using Stream = ServerReaderWriter<LoadBalanceResponse, LoadBalanceRequest>;
  using ResponseDelayPair = std::pair<LoadBalanceResponse, int>;

  explicit BalancerServiceImpl(int client_load_reporting_interval_seconds)
      : client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds) {}

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
      SendResponse(stream, response_and_delay.first, response_and_delay.second);
    }
    {
      std::unique_lock<std::mutex> lock(mu_);
      serverlist_cond_.wait(lock, [this] { return serverlist_done_; });
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

  void Start() {
    std::lock_guard<std::mutex> lock(mu_);
    serverlist_done_ = false;
    load_report_ready_ = false;
    responses_and_delays_.clear();
    client_stats_.Reset();
  }

  void Shutdown() {
    NotifyDoneWithServerlists();
    gpr_log(GPR_INFO, "LB[%p]: shut down", this);
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
      static int token_count = 0;
      char* token;
      gpr_asprintf(&token, "token%03d", ++token_count);
      server->set_load_balance_token(token);
      gpr_free(token);
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
    if (!serverlist_done_) {
      serverlist_done_ = true;
      serverlist_cond_.notify_all();
    }
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
  bool serverlist_done_ = false;
  ClientStats client_stats_;
};

class GrpclbEnd2endTest : public ::testing::Test {
 protected:
  GrpclbEnd2endTest(size_t num_backends, size_t num_balancers,
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
      backends_.emplace_back(new ServerThread<BackendServiceImpl>("backend"));
      backends_.back()->Start(server_host_);
    }
    // Start the load balancers.
    for (size_t i = 0; i < num_balancers_; ++i) {
      balancers_.emplace_back(new ServerThread<BalancerServiceImpl>(
          "balancer", client_load_reporting_interval_seconds_));
      balancers_.back()->Start(server_host_);
    }
    ResetStub();
  }

  void TearDown() override {
    ShutdownAllBackends();
    for (auto& balancer : balancers_) balancer->Shutdown();
  }

  void StartAllBackends() {
    for (auto& backend : backends_) backend->Start(server_host_);
  }

  void StartBackend(size_t index) { backends_[index]->Start(server_host_); }

  void ShutdownAllBackends() {
    for (auto& backend : backends_) backend->Shutdown();
  }

  void ShutdownBackend(size_t index) { backends_[index]->Shutdown(); }

  void ResetStub(int fallback_timeout = 0,
                 const grpc::string& expected_targets = "") {
    ChannelArguments args;
    if (fallback_timeout > 0) args.SetGrpclbFallbackTimeout(fallback_timeout);
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
    call_creds->Unref();
    channel_creds->Unref();
    channel_ = CreateCustomChannel(uri.str(), creds, args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void ResetBackendCounters() {
    for (auto& backend : backends_) backend->service_.ResetCounters();
  }

  ClientStats WaitForLoadReports() {
    ClientStats client_stats;
    for (auto& balancer : balancers_) {
      client_stats += balancer->service_.WaitForLoadReport();
    }
    return client_stats;
  }

  bool SeenAllBackends(size_t start_index = 0, size_t stop_index = 0) {
    if (stop_index == 0) stop_index = backends_.size();
    for (size_t i = start_index; i < stop_index; ++i) {
      if (backends_[i]->service_.request_count() == 0) return false;
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

  std::tuple<int, int, int> WaitForAllBackends(int num_requests_multiple_of = 1,
                                               size_t start_index = 0,
                                               size_t stop_index = 0) {
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    while (!SeenAllBackends(start_index, stop_index)) {
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
    } while (backends_[backend_idx]->service_.request_count() == 0);
    ResetBackendCounters();
  }

  struct AddressData {
    int port;
    bool is_balancer;
    grpc::string balancer_name;
  };

  grpc_core::ServerAddressList CreateLbAddressesFromAddressDataList(
      const std::vector<AddressData>& address_data) {
    grpc_core::ServerAddressList addresses;
    for (const auto& addr : address_data) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", addr.port);
      grpc_uri* lb_uri = grpc_uri_parse(lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(lb_uri, &address));
      std::vector<grpc_arg> args_to_add;
      if (addr.is_balancer) {
        args_to_add.emplace_back(grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_ADDRESS_IS_BALANCER), 1));
        args_to_add.emplace_back(grpc_channel_arg_string_create(
            const_cast<char*>(GRPC_ARG_ADDRESS_BALANCER_NAME),
            const_cast<char*>(addr.balancer_name.c_str())));
      }
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, args_to_add.data(), args_to_add.size());
      addresses.emplace_back(address.addr, address.len, args);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    return addresses;
  }

  void SetNextResolutionAllBalancers(
      const char* service_config_json = nullptr) {
    std::vector<AddressData> addresses;
    for (size_t i = 0; i < balancers_.size(); ++i) {
      addresses.emplace_back(AddressData{balancers_[i]->port_, true, ""});
    }
    SetNextResolution(addresses, service_config_json);
  }

  void SetNextResolution(const std::vector<AddressData>& address_data,
                         const char* service_config_json = nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::ServerAddressList addresses =
        CreateLbAddressesFromAddressDataList(address_data);
    std::vector<grpc_arg> args = {
        CreateServerAddressListChannelArg(&addresses),
    };
    if (service_config_json != nullptr) {
      args.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_SERVICE_CONFIG),
          const_cast<char*>(service_config_json)));
    }
    grpc_channel_args fake_result = {args.size(), args.data()};
    response_generator_->SetResponse(&fake_result);
  }

  void SetNextReresolutionResponse(
      const std::vector<AddressData>& address_data) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::ServerAddressList addresses =
        CreateLbAddressesFromAddressDataList(address_data);
    grpc_arg fake_addresses = CreateServerAddressListChannelArg(&addresses);
    grpc_channel_args fake_result = {1, &fake_addresses};
    response_generator_->SetReresolutionResponse(&fake_result);
  }

  const std::vector<int> GetBackendPorts(size_t start_index = 0,
                                         size_t stop_index = 0) const {
    if (stop_index == 0) stop_index = backends_.size();
    std::vector<int> backend_ports;
    for (size_t i = start_index; i < stop_index; ++i) {
      backend_ports.push_back(backends_[i]->port_);
    }
    return backend_ports;
  }

  void ScheduleResponseForBalancer(size_t i,
                                   const LoadBalanceResponse& response,
                                   int delay_ms) {
    balancers_[i]->service_.add_response(response, delay_ms);
  }

  Status SendRpc(EchoResponse* response = nullptr, int timeout_ms = 1000,
                 bool wait_for_ready = false) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    ClientContext context;
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub_->Echo(&context, request, response);
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1, const int timeout_ms = 1000,
                      bool wait_for_ready = false) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(&response, timeout_ms, wait_for_ready);
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
    template <typename... Args>
    explicit ServerThread(const grpc::string& type, Args&&... args)
        : port_(grpc_pick_unused_port_or_die()),
          type_(type),
          service_(std::forward<Args>(args)...) {}

    void Start(const grpc::string& server_host) {
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      service_.Start();
      std::mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      std::unique_lock<std::mutex> lock(mu);
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Serve, this, server_host, &mu, &cond)));
      cond.wait(lock);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Serve(const grpc::string& server_host, std::mutex* mu,
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
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      cond->notify_one();
    }

    void Shutdown() {
      if (!running_) return;
      gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
      service_.Shutdown();
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      gpr_log(GPR_INFO, "%s shutdown completed", type_.c_str());
      running_ = false;
    }

    const int port_;
    grpc::string type_;
    T service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
  };

  const grpc::string server_host_;
  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerThread<BackendServiceImpl>>> backends_;
  std::vector<std::unique_ptr<ServerThread<BalancerServiceImpl>>> balancers_;
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
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service_.request_count());
  }
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, SelectGrpclbWithMigrationServiceConfig) {
  SetNextResolutionAllBalancers(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"grpclb\":{} }\n"
      "  ]\n"
      "}");
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  CheckRpcSendOk(1, 1000 /* timeout_ms */, true /* wait_for_ready */);
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest,
       SelectGrpclbWithMigrationServiceConfigAndNoAddresses) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  SetNextResolution({},
                    "{\n"
                    "  \"loadBalancingConfig\":[\n"
                    "    { \"does_not_exist\":{} },\n"
                    "    { \"grpclb\":{} }\n"
                    "  ]\n"
                    "}");
  // Try to connect.
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(true));
  // Should go into state TRANSIENT_FAILURE when we enter fallback mode.
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  grpc_connectivity_state state;
  while ((state = channel_->GetState(false)) !=
         GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ASSERT_TRUE(channel_->WaitForStateChange(state, deadline));
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest,
       SelectGrpclbWithMigrationServiceConfigAndNoBalancerAddresses) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Resolution includes fallback address but no balancers.
  SetNextResolution({AddressData{backends_[0]->port_, false, ""}},
                    "{\n"
                    "  \"loadBalancingConfig\":[\n"
                    "    { \"does_not_exist\":{} },\n"
                    "    { \"grpclb\":{} }\n"
                    "  ]\n"
                    "}");
  CheckRpcSendOk(1, 1000 /* timeout_ms */, true /* wait_for_ready */);
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, UsePickFirstChildPolicy) {
  SetNextResolutionAllBalancers(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"pick_first\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  const size_t kNumRpcs = num_backends_ * 2;
  CheckRpcSendOk(kNumRpcs, 1000 /* timeout_ms */, true /* wait_for_ready */);
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // Check that all requests went to the first backend.  This verifies
  // that we used pick_first instead of round_robin as the child policy.
  EXPECT_EQ(backends_[0]->service_.request_count(), kNumRpcs);
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(backends_[i]->service_.request_count(), 0UL);
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, SwapChildPolicy) {
  SetNextResolutionAllBalancers(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"pick_first\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  const size_t kNumRpcs = num_backends_ * 2;
  CheckRpcSendOk(kNumRpcs, 1000 /* timeout_ms */, true /* wait_for_ready */);
  // Check that all requests went to the first backend.  This verifies
  // that we used pick_first instead of round_robin as the child policy.
  EXPECT_EQ(backends_[0]->service_.request_count(), kNumRpcs);
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(backends_[i]->service_.request_count(), 0UL);
  }
  // Send new resolution that removes child policy from service config.
  SetNextResolutionAllBalancers("{}");
  WaitForAllBackends();
  CheckRpcSendOk(kNumRpcs, 1000 /* timeout_ms */, true /* wait_for_ready */);
  // Check that every backend saw the same number of requests.  This verifies
  // that we used round_robin.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(backends_[i]->service_.request_count(), 2UL);
  }
  // Done.
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, UpdatesGoToMostRecentChildPolicy) {
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  int unreachable_balancer_port = grpc_pick_unused_port_or_die();
  int unreachable_backend_port = grpc_pick_unused_port_or_die();
  // Phase 1: Start with RR pointing to first backend.
  gpr_log(GPR_INFO, "PHASE 1: Initial setup with RR with first backend");
  SetNextResolution(
      {
          // Unreachable balancer.
          {unreachable_balancer_port, true, ""},
          // Fallback address: first backend.
          {backends_[0]->port_, false, ""},
      },
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"round_robin\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  // RPCs should go to first backend.
  WaitForBackend(0);
  // Phase 2: Switch to PF pointing to unreachable backend.
  gpr_log(GPR_INFO, "PHASE 2: Update to use PF with unreachable backend");
  SetNextResolution(
      {
          // Unreachable balancer.
          {unreachable_balancer_port, true, ""},
          // Fallback address: unreachable backend.
          {unreachable_backend_port, false, ""},
      },
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"pick_first\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  // RPCs should continue to go to the first backend, because the new
  // PF child policy will never go into state READY.
  WaitForBackend(0);
  // Phase 3: Switch back to RR pointing to second and third backends.
  // This ensures that we create a new policy rather than updating the
  // pending PF policy.
  gpr_log(GPR_INFO, "PHASE 3: Update to use RR again with two backends");
  SetNextResolution(
      {
          // Unreachable balancer.
          {unreachable_balancer_port, true, ""},
          // Fallback address: second and third backends.
          {backends_[1]->port_, false, ""},
          {backends_[2]->port_, false, ""},
      },
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"childPolicy\":[\n"
      "        { \"round_robin\":{} }\n"
      "      ]\n"
      "    } }\n"
      "  ]\n"
      "}");
  // RPCs should go to the second and third backends.
  WaitForBackend(1);
  WaitForBackend(2);
}

TEST_F(SingleBalancerTest, SameBackendListedMultipleTimes) {
  SetNextResolutionAllBalancers();
  // Same backend listed twice.
  std::vector<int> ports;
  ports.push_back(backends_[0]->port_);
  ports.push_back(backends_[0]->port_);
  const size_t kNumRpcsPerAddress = 10;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(ports, {}), 0);
  // We need to wait for the backend to come online.
  WaitForBackend(0);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * ports.size());
  // Backend should have gotten 20 requests.
  EXPECT_EQ(kNumRpcsPerAddress * 2, backends_[0]->service_.request_count());
  // And they should have come from a single client port, because of
  // subchannel sharing.
  EXPECT_EQ(1UL, backends_[0]->service_.clients().size());
  balancers_[0]->service_.NotifyDoneWithServerlists();
}

TEST_F(SingleBalancerTest, SecureNaming) {
  ResetStub(0, kApplicationTargetName_ + ";lb");
  SetNextResolution({AddressData{balancers_[0]->port_, true, "lb"}});
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
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service_.request_count());
  }
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
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
        SetNextResolution({AddressData{balancers_[0]->port_, true, "woops"}});
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
  CheckRpcSendOk(1, kCallDeadlineMs, true /* wait_for_ready */);
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline (which is enforced by
  // the call's deadline).
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancers_[0]->service_.response_count());
}

TEST_F(SingleBalancerTest, AllServersUnreachableFailFast) {
  SetNextResolutionAllBalancers();
  const size_t kNumUnreachableServers = 5;
  std::vector<int> ports;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    ports.push_back(grpc_pick_unused_port_or_die());
  }
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(ports, {}), 0);
  const Status status = SendRpc();
  // The error shouldn't be DEADLINE_EXCEEDED.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
}

TEST_F(SingleBalancerTest, Fallback) {
  SetNextResolutionAllBalancers();
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendInResolution = backends_.size() / 2;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    addresses.emplace_back(AddressData{backends_[i]->port_, false, ""});
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
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
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
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }

  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
}

TEST_F(SingleBalancerTest, FallbackUpdate) {
  SetNextResolutionAllBalancers();
  const int kFallbackTimeoutMs = 200 * grpc_test_slowdown_factor();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const size_t kNumBackendInResolution = backends_.size() / 3;
  const size_t kNumBackendInResolutionUpdate = backends_.size() / 3;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  for (size_t i = 0; i < kNumBackendInResolution; ++i) {
    addresses.emplace_back(AddressData{backends_[i]->port_, false, ""});
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
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }

  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  for (size_t i = kNumBackendInResolution;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    addresses.emplace_back(AddressData{backends_[i]->port_, false, ""});
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
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution;
       i < kNumBackendInResolution + kNumBackendInResolutionUpdate; ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution + kNumBackendInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
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
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendInResolution + kNumBackendInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }

  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
}

TEST_F(SingleBalancerTest,
       FallbackAfterStartup_LoseContactWithBalancerThenBackends) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = backends_.size() - kNumFallbackBackends;
  std::vector<AddressData> addresses;
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    addresses.emplace_back(AddressData{backends_[i]->port_, false, ""});
  }
  for (size_t i = 0; i < balancers_.size(); ++i) {
    addresses.emplace_back(AddressData{balancers_[i]->port_, true, ""});
  }
  SetNextResolution(addresses);
  ScheduleResponseForBalancer(0,
                              BalancerServiceImpl::BuildResponseForBackends(
                                  GetBackendPorts(kNumFallbackBackends), {}),
                              0);
  // Try to connect.
  channel_->GetState(true /* try_to_connect */);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
  // Stop balancer.  RPCs should continue going to backends from balancer.
  balancers_[0]->Shutdown();
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    EXPECT_EQ(100UL, backends_[i]->service_.request_count());
  }
  // Stop backends from balancer.  This should put us in fallback mode.
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    ShutdownBackend(i);
  }
  WaitForAllBackends(1 /* num_requests_multiple_of */, 0 /* start_index */,
                     kNumFallbackBackends /* stop_index */);
  // Restart the backends from the balancer.  We should *not* start
  // sending traffic back to them at this point (although the behavior
  // in xds may be different).
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    StartBackend(i);
  }
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    EXPECT_EQ(100UL, backends_[i]->service_.request_count());
  }
  // Now start the balancer again.  This should cause us to exit
  // fallback mode.
  balancers_[0]->Start(server_host_);
  ScheduleResponseForBalancer(0,
                              BalancerServiceImpl::BuildResponseForBackends(
                                  GetBackendPorts(kNumFallbackBackends), {}),
                              0);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
}

TEST_F(SingleBalancerTest,
       FallbackAfterStartup_LoseContactWithBackendsThenBalancer) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = backends_.size() - kNumFallbackBackends;
  std::vector<AddressData> addresses;
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    addresses.emplace_back(AddressData{backends_[i]->port_, false, ""});
  }
  for (size_t i = 0; i < balancers_.size(); ++i) {
    addresses.emplace_back(AddressData{balancers_[i]->port_, true, ""});
  }
  SetNextResolution(addresses);
  ScheduleResponseForBalancer(0,
                              BalancerServiceImpl::BuildResponseForBackends(
                                  GetBackendPorts(kNumFallbackBackends), {}),
                              0);
  // Try to connect.
  channel_->GetState(true /* try_to_connect */);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
  // Stop backends from balancer.  Since we are still in contact with
  // the balancer at this point, RPCs should be failing.
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    ShutdownBackend(i);
  }
  CheckRpcSendFailure();
  // Stop balancer.  This should put us in fallback mode.
  balancers_[0]->Shutdown();
  WaitForAllBackends(1 /* num_requests_multiple_of */, 0 /* start_index */,
                     kNumFallbackBackends /* stop_index */);
  // Restart the backends from the balancer.  We should *not* start
  // sending traffic back to them at this point (although the behavior
  // in xds may be different).
  for (size_t i = kNumFallbackBackends; i < backends_.size(); ++i) {
    StartBackend(i);
  }
  CheckRpcSendOk(100 * kNumBalancerBackends);
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    EXPECT_EQ(100UL, backends_[i]->service_.request_count());
  }
  // Now start the balancer again.  This should cause us to exit
  // fallback mode.
  balancers_[0]->Start(server_host_);
  ScheduleResponseForBalancer(0,
                              BalancerServiceImpl::BuildResponseForBackends(
                                  GetBackendPorts(kNumFallbackBackends), {}),
                              0);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
}

TEST_F(SingleBalancerTest, FallbackEarlyWhenBalancerChannelFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return an unreachable balancer and one fallback backend.
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{grpc_pick_unused_port_or_die(), true, ""});
  addresses.emplace_back(AddressData{backends_[0]->port_, false, ""});
  SetNextResolution(addresses);
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

TEST_F(SingleBalancerTest, FallbackEarlyWhenBalancerCallFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return an unreachable balancer and one fallback backend.
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  addresses.emplace_back(AddressData{backends_[0]->port_, false, ""});
  SetNextResolution(addresses);
  // Balancer drops call without sending a serverlist.
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
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
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  CheckRpcSendFailure();
  // Restart backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1 /* times */, 2000 /* timeout_ms */,
                 true /* wait_for_ready */);
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
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
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  balancers_[1]->service_.NotifyDoneWithServerlists();
  balancers_[2]->service_.NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[1]->port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  WaitForBackend(1);

  backends_[1]->service_.ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  balancers_[1]->service_.NotifyDoneWithServerlists();
  balancers_[2]->service_.NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(1U, balancers_[1]->service_.request_count());
  EXPECT_EQ(1U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());
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
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  addresses.emplace_back(AddressData{balancers_[1]->port_, true, ""});
  addresses.emplace_back(AddressData{balancers_[2]->port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // grpclb continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  balancers_[0]->service_.NotifyDoneWithServerlists();

  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  addresses.emplace_back(AddressData{balancers_[1]->port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 2 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 2 DONE ==========");

  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                          gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // grpclb continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  balancers_[0]->service_.NotifyDoneWithServerlists();
}

TEST_F(UpdatesTest, UpdateBalancersDeadUpdate) {
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
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
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  // Kill balancer 0
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");

  // This is serviced by the existing RR policy
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should again have gone to the first backend.
  EXPECT_EQ(20U, backends_[0]->service_.request_count());
  EXPECT_EQ(0U, backends_[1]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  balancers_[1]->service_.NotifyDoneWithServerlists();
  balancers_[2]->service_.NotifyDoneWithServerlists();
  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[1]->port_, true, ""});
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request. In the meantime, the client continues to be serviced
  // (by the first backend) without interruption.
  EXPECT_EQ(0U, backends_[1]->service_.request_count());
  WaitForBackend(1);

  // This is serviced by the updated RR policy
  backends_[1]->service_.ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  balancers_[1]->service_.NotifyDoneWithServerlists();
  balancers_[2]->service_.NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  // The second balancer, published as part of the first update, may end up
  // getting two requests (that is, 1 <= #req <= 2) if the LB call retry timer
  // firing races with the arrival of the update containing the second
  // balancer.
  EXPECT_GE(balancers_[1]->service_.request_count(), 1U);
  EXPECT_GE(balancers_[1]->service_.response_count(), 1U);
  EXPECT_LE(balancers_[1]->service_.request_count(), 2U);
  EXPECT_LE(balancers_[1]->service_.response_count(), 2U);
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());
}

TEST_F(UpdatesTest, ReresolveDeadBackend) {
  ResetStub(500);
  // The first resolution contains the addresses of a balancer that never
  // responds, and a fallback backend.
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  addresses.emplace_back(AddressData{backends_[0]->port_, false, ""});
  SetNextResolution(addresses);
  // The re-resolution result will contain the addresses of the same balancer
  // and a new fallback backend.
  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  addresses.emplace_back(AddressData{backends_[1]->port_, false, ""});
  SetNextReresolutionResponse(addresses);

  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the fallback backend.
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  // Kill backend 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BACKEND 0 *************");
  backends_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BACKEND 0 *************");

  // Wait until re-resolution has finished, as signaled by the second backend
  // receiving a request.
  WaitForBackend(1);

  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->service_.request_count());

  balancers_[0]->service_.NotifyDoneWithServerlists();
  balancers_[1]->service_.NotifyDoneWithServerlists();
  balancers_[2]->service_.NotifyDoneWithServerlists();
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  EXPECT_EQ(0U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());
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
  addresses.emplace_back(AddressData{balancers_[0]->port_, true, ""});
  SetNextResolution(addresses);
  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[1]->port_, true, ""});
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
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  // Kill backend 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BACKEND 0 *************");
  backends_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BACKEND 0 *************");

  CheckRpcSendFailure();

  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  // Kill balancer 0.
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");

  // Wait until re-resolution has finished, as signaled by the second backend
  // receiving a request.
  WaitForBackend(1);

  // This is serviced by the new serverlist.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->service_.request_count());

  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  // After balancer 0 is killed, we restart an LB call immediately (because we
  // disconnect to a previously connected balancer). Although we will cancel
  // this call when the re-resolution update is done and another LB call restart
  // is needed, this old call may still succeed reaching the LB server if
  // re-resolution is slow. So balancer 1 may have received 2 requests and sent
  // 2 responses.
  EXPECT_GE(balancers_[1]->service_.request_count(), 1U);
  EXPECT_GE(balancers_[1]->service_.response_count(), 1U);
  EXPECT_LE(balancers_[1]->service_.request_count(), 2U);
  EXPECT_LE(balancers_[1]->service_.response_count(), 2U);
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());
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
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service_.request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
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
  const Status status = SendRpc(nullptr, 1000, true);
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
    status = SendRpc(nullptr, 1000, true);
  } while (status.ok());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
}

class SingleBalancerWithClientLoadReportingTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerWithClientLoadReportingTest() : GrpclbEnd2endTest(4, 1, 3) {}
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
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service_.request_count());
  }
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());

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

TEST_F(SingleBalancerWithClientLoadReportingTest, BalancerRestart) {
  SetNextResolutionAllBalancers();
  const size_t kNumBackendsFirstPass = 2;
  const size_t kNumBackendsSecondPass =
      backends_.size() - kNumBackendsFirstPass;
  // Balancer returns backends starting at index 1.
  ScheduleResponseForBalancer(
      0,
      BalancerServiceImpl::BuildResponseForBackends(
          GetBackendPorts(0, kNumBackendsFirstPass), {}),
      0);
  // Wait until all backends returned by the balancer are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* num_requests_multiple_of */ 1, /* start_index */ 0,
                         /* stop_index */ kNumBackendsFirstPass);
  balancers_[0]->service_.NotifyDoneWithServerlists();
  ClientStats client_stats = WaitForLoadReports();
  EXPECT_EQ(static_cast<size_t>(num_ok), client_stats.num_calls_started);
  EXPECT_EQ(static_cast<size_t>(num_ok), client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(static_cast<size_t>(num_ok),
            client_stats.num_calls_finished_known_received);
  EXPECT_THAT(client_stats.drop_token_counts, ::testing::ElementsAre());
  // Shut down the balancer.
  balancers_[0]->Shutdown();
  // Send 10 more requests per backend.  This will continue using the
  // last serverlist we received from the balancer before it was shut down.
  ResetBackendCounters();
  CheckRpcSendOk(kNumBackendsFirstPass);
  // Each backend should have gotten 1 request.
  for (size_t i = 0; i < kNumBackendsFirstPass; ++i) {
    EXPECT_EQ(1UL, backends_[i]->service_.request_count());
  }
  // Now restart the balancer, this time pointing to all backends.
  balancers_[0]->Start(server_host_);
  ScheduleResponseForBalancer(0,
                              BalancerServiceImpl::BuildResponseForBackends(
                                  GetBackendPorts(kNumBackendsFirstPass), {}),
                              0);
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  do {
    CheckRpcSendOk();
  } while (backends_[2]->service_.request_count() == 0 &&
           backends_[3]->service_.request_count() == 0);
  // Send one RPC per backend.
  CheckRpcSendOk(kNumBackendsSecondPass);
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // Check client stats.
  client_stats = WaitForLoadReports();
  EXPECT_EQ(kNumBackendsSecondPass + 1, client_stats.num_calls_started);
  EXPECT_EQ(kNumBackendsSecondPass + 1, client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumBackendsSecondPass + 1,
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
    EXPECT_EQ(kNumRpcsPerAddress, backends_[i]->service_.request_count());
  }
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());

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
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
