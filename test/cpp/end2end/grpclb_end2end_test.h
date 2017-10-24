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

class BackendServiceImpl : public BackendService {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
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
    gpr_log(GPR_INFO, "LB[%p]: BalanceLoad", this);
    LoadBalanceRequest request;
    stream->Read(&request);
    IncreaseRequestCount();
    gpr_log(GPR_INFO, "LB[%p]: recv msg '%s'", this,
            request.DebugString().c_str());

    if (client_load_reporting_interval_seconds_ > 0) {
      LoadBalanceResponse initial_response;
      initial_response.mutable_initial_response()
          ->mutable_client_stats_report_interval()
          ->set_seconds(client_load_reporting_interval_seconds_);
      stream->Write(initial_response);
    }

    std::vector<ResponseDelayPair> responses_and_delays;
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
      serverlist_ready_ = false;
    }

    if (client_load_reporting_interval_seconds_ > 0) {
      request.Clear();
      stream->Read(&request);
      gpr_log(GPR_INFO, "LB[%p]: recv client load report msg: '%s'", this,
              request.DebugString().c_str());
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
    serverlist_cond_.notify_one();
  }

 private:
  void SendResponse(Stream* stream, const LoadBalanceResponse& response,
                    int delay_ms) {
    gpr_log(GPR_INFO, "LB[%p]: sleeping for %d ms...", this, delay_ms);
    if (delay_ms > 0) {
      gpr_sleep_until(
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_millis(delay_ms, GPR_TIMESPAN)));
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
            client_load_reporting_interval_seconds),
        kRequestMessage_("Live long and prosper.") {}

  void SetUp() override {
    response_generator_ = grpc_fake_resolver_response_generator_create();
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
    std::vector<AddressData> addresses;
    for (size_t i = 0; i < balancer_servers_.size(); ++i) {
      addresses.emplace_back(AddressData{balancer_servers_[i].port_, true, ""});
    }
    SetNextResolution(addresses);
  }

  void TearDown() override {
    for (size_t i = 0; i < backends_.size(); ++i) {
      if (backends_[i]->Shutdown()) backend_servers_[i].Shutdown();
    }
    for (size_t i = 0; i < balancers_.size(); ++i) {
      if (balancers_[i]->Shutdown()) balancer_servers_[i].Shutdown();
    }
    grpc_fake_resolver_response_generator_unref(response_generator_);
  }

  void ResetStub(int fallback_timeout = 0) {
    ChannelArguments args;
    args.SetGrpclbFallbackTimeout(fallback_timeout);
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_);
    std::ostringstream uri;
    uri << "fake:///servername_not_used";
    channel_ =
        CreateCustomChannel(uri.str(), InsecureChannelCredentials(), args);
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
      CheckRpcSendOk();
    } while (backends_[backend_idx]->request_count() == 0);
    ResetBackendCounters();
  }

  struct AddressData {
    int port;
    bool is_balancer;
    grpc::string balancer_name;
  };

  void SetNextResolution(const std::vector<AddressData>& address_data) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(address_data.size(), nullptr);
    for (size_t i = 0; i < address_data.size(); ++i) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", address_data[i].port);
      grpc_uri* lb_uri = grpc_uri_parse(&exec_ctx, lb_uri_str, true);
      GPR_ASSERT(lb_uri != nullptr);
      grpc_lb_addresses_set_address_from_uri(
          addresses, i, lb_uri, address_data[i].is_balancer,
          address_data[i].balancer_name.c_str(), nullptr);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    grpc_arg fake_addresses = grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args fake_result = {1, &fake_addresses};
    grpc_fake_resolver_response_generator_set_response(
        &exec_ctx, response_generator_, &fake_result);
    grpc_lb_addresses_destroy(&exec_ctx, addresses);
    grpc_exec_ctx_finish(&exec_ctx);
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

  void CheckRpcSendOk(const size_t times = 1) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(&response);
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
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
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

  const grpc::string kMessage_ = "Live long and prosper.";
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
  grpc_fake_resolver_response_generator* response_generator_;
  const grpc::string kRequestMessage_;
};

}  // namespace
}  // namespace testing
}  // namespace grpc
