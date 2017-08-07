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
            client_load_reporting_interval_seconds) {}

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

  void ResetStub() {
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_);
    std::ostringstream uri;
    uri << "fake:///servername_not_used";
    channel_ =
        CreateCustomChannel(uri.str(), InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  ClientStats WaitForLoadReports() {
    ClientStats client_stats;
    for (const auto& balancer : balancers_) {
      client_stats += balancer->WaitForLoadReport();
    }
    return client_stats;
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

  const std::vector<int> GetBackendPorts() const {
    std::vector<int> backend_ports;
    for (const auto& bs : backend_servers_) {
      backend_ports.push_back(bs.port_);
    }
    return backend_ports;
  }

  void ScheduleResponseForBalancer(size_t i,
                                   const LoadBalanceResponse& response,
                                   int delay_ms) {
    balancers_.at(i)->add_response(response, delay_ms);
  }

  std::vector<std::pair<Status, EchoResponse>> SendRpc(const string& message,
                                                       int num_rpcs,
                                                       int timeout_ms = 1000) {
    std::vector<std::pair<Status, EchoResponse>> results;
    EchoRequest request;
    EchoResponse response;
    request.set_message(message);
    for (int i = 0; i < num_rpcs; i++) {
      ClientContext context;
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
      Status status = stub_->Echo(&context, request, &response);
      results.push_back(std::make_pair(status, response));
    }
    return results;
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
};

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
  // Send 100 RPCs per server.
  const auto& statuses_and_responses =
      SendRpc(kMessage_, kNumRpcsPerAddress * num_backends_);

  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }

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
  const auto& statuses_and_responses =
      SendRpc(kMessage_, num_backends_, kCallDeadlineMs);
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
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // and sent two responses.
  EXPECT_EQ(2U, balancer_servers_[0].service_->response_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, RepeatedServerlist) {
  constexpr int kServerlistDelayMs = 100;

  // Send a serverlist right away.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  // ... and the same one a bit later.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      kServerlistDelayMs);

  // Send num_backends/2 requests.
  auto statuses_and_responses = SendRpc(kMessage_, num_backends_ / 2);
  // only the first half of the backends will receive them.
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (i < backends_.size() / 2)
      EXPECT_EQ(1U, backend_servers_[i].service_->request_count())
          << "for backend #" << i;
    else
      EXPECT_EQ(0U, backend_servers_[i].service_->request_count())
          << "for backend #" << i;
  }
  EXPECT_EQ(statuses_and_responses.size(), num_backends_ / 2);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }

  // Wait for the (duplicated) serverlist update.
  gpr_sleep_until(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(kServerlistDelayMs * 1.1, GPR_TIMESPAN)));

  // Verify the LB has sent two responses.
  EXPECT_EQ(2U, balancer_servers_[0].service_->response_count());

  // Some more calls to complete the total number of backends.
  statuses_and_responses = SendRpc(
      kMessage_,
      num_backends_ / 2 + (num_backends_ & 0x1) /* extra one if num_bes odd */);
  // Because a duplicated serverlist should have no effect, all backends must
  // have been hit once now.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(1U, backend_servers_[i].service_->request_count());
  }
  EXPECT_EQ(statuses_and_responses.size(), num_backends_ / 2);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }
  balancers_[0]->NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancer_servers_[0].service_->request_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, BackendsRestart) {
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // Send 100 RPCs per server.
  auto statuses_and_responses =
      SendRpc(kMessage_, kNumRpcsPerAddress * num_backends_);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }
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
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->Shutdown()) backend_servers_[i].Shutdown();
  }
  statuses_and_responses = SendRpc(kMessage_, 1);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    EXPECT_FALSE(status.ok());
  }
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
  statuses_and_responses = SendRpc(kMessage_, 1);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    EXPECT_FALSE(status.ok());
  }
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
  auto statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
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
  do {
    auto statuses_and_responses = SendRpc(kMessage_, 1);
    for (const auto& status_and_response : statuses_and_responses) {
      EXPECT_TRUE(status_and_response.first.ok());
      EXPECT_EQ(status_and_response.second.message(), kMessage_);
    }
  } while (backend_servers_[1].service_->request_count() == 0);

  backend_servers_[1].service_->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
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
  auto statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
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
    statuses_and_responses = SendRpc(kMessage_, 1);
    for (const auto& status_and_response : statuses_and_responses) {
      EXPECT_TRUE(status_and_response.first.ok());
      EXPECT_EQ(status_and_response.second.message(), kMessage_);
    }
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
    statuses_and_responses = SendRpc(kMessage_, 1);
    for (const auto& status_and_response : statuses_and_responses) {
      EXPECT_TRUE(status_and_response.first.ok());
      EXPECT_EQ(status_and_response.second.message(), kMessage_);
    }
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
  auto statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backend_servers_[0].service_->request_count());

  // Kill balancer 0
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->NotifyDoneWithServerlists();
  if (balancers_[0]->Shutdown()) balancer_servers_[0].Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");

  // This is serviced by the existing RR policy
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
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
  do {
    auto statuses_and_responses = SendRpc(kMessage_, 1);
    for (const auto& status_and_response : statuses_and_responses) {
      EXPECT_TRUE(status_and_response.first.ok());
      EXPECT_EQ(status_and_response.second.message(), kMessage_);
    }
  } while (backend_servers_[1].service_->request_count() == 0);

  // This is serviced by the existing RR policy
  backend_servers_[1].service_->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  statuses_and_responses = SendRpc(kMessage_, 10);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }
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
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(), {{"rate_limiting", 1}, {"load_balancing", 2}}),
      0);
  // Send 100 RPCs for each server and drop address.
  const auto& statuses_and_responses =
      SendRpc(kMessage_, kNumRpcsPerAddress * (num_backends_ + 3));

  size_t num_drops = 0;
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kMessage_);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * 3, num_drops);

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
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             {}, {{"rate_limiting", 1}, {"load_balancing", 1}}),
      0);
  const auto& statuses_and_responses = SendRpc(kMessage_, 1);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_message(), "Call dropped by load balancing policy");
  }
}

TEST_F(SingleBalancerTest, DropAll) {
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts(), {}),
      0);
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             {}, {{"rate_limiting", 1}, {"load_balancing", 1}}),
      1000);

  // First call succeeds.
  auto statuses_and_responses = SendRpc(kMessage_, 1);
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }
  // But eventually, the update with only dropped servers is processed and calls
  // fail.
  do {
    statuses_and_responses = SendRpc(kMessage_, 1);
    ASSERT_EQ(statuses_and_responses.size(), 1UL);
  } while (statuses_and_responses[0].first.ok());
  const Status& status = statuses_and_responses[0].first;
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
  // Send 100 RPCs per server.
  const auto& statuses_and_responses =
      SendRpc(kMessage_, kNumRpcsPerAddress * num_backends_);

  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
    EXPECT_EQ(response.message(), kMessage_);
  }

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
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_, client_stats.num_calls_started);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_,
            client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_,
            client_stats.num_calls_finished_known_received);
  EXPECT_THAT(client_stats.drop_token_counts, ::testing::ElementsAre());
}

TEST_F(SingleBalancerWithClientLoadReportingTest, Drop) {
  const size_t kNumRpcsPerAddress = 3;
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(
             GetBackendPorts(), {{"rate_limiting", 2}, {"load_balancing", 1}}),
      0);
  // Send 100 RPCs for each server and drop address.
  const auto& statuses_and_responses =
      SendRpc(kMessage_, kNumRpcsPerAddress * (num_backends_ + 3));

  size_t num_drops = 0;
  for (const auto& status_and_response : statuses_and_responses) {
    const Status& status = status_and_response.first;
    const EchoResponse& response = status_and_response.second;
    if (!status.ok() &&
        status.error_message() == "Call dropped by load balancing policy") {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kMessage_);
    }
  }
  EXPECT_EQ(kNumRpcsPerAddress * 3, num_drops);

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
  EXPECT_EQ(kNumRpcsPerAddress * (num_backends_ + 3),
            client_stats.num_calls_started);
  EXPECT_EQ(kNumRpcsPerAddress * (num_backends_ + 3),
            client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_,
            client_stats.num_calls_finished_known_received);
  EXPECT_THAT(client_stats.drop_token_counts,
              ::testing::ElementsAre(
                  ::testing::Pair("load_balancing", kNumRpcsPerAddress),
                  ::testing::Pair("rate_limiting", kNumRpcsPerAddress * 2)));
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
