/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include <gtest/gtest.h>

extern "C" {
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/end2end/fake_resolver.h"
}

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"

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

using grpc::lb::v1::LoadBalanceResponse;
using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalancer;

namespace grpc {
namespace testing {
namespace {

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  int request_count() {
    std::unique_lock<std::mutex> lock(mu_);
    return request_count_;
  }

  int response_count() {
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

 protected:
  std::mutex mu_;

 private:
  int request_count_ = 0;
  int response_count_ = 0;
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
};

grpc::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
  return grpc::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

struct ClientStats {
  size_t num_calls_started = 0;
  size_t num_calls_finished = 0;
  size_t num_calls_finished_with_drop_for_rate_limiting = 0;
  size_t num_calls_finished_with_drop_for_load_balancing = 0;
  size_t num_calls_finished_with_client_failed_to_send = 0;
  size_t num_calls_finished_known_received = 0;

  ClientStats& operator+=(const ClientStats& other) {
    num_calls_started += other.num_calls_started;
    num_calls_finished += other.num_calls_finished;
    num_calls_finished_with_drop_for_rate_limiting +=
        other.num_calls_finished_with_drop_for_rate_limiting;
    num_calls_finished_with_drop_for_load_balancing +=
        other.num_calls_finished_with_drop_for_load_balancing;
    num_calls_finished_with_client_failed_to_send +=
        other.num_calls_finished_with_client_failed_to_send;
    num_calls_finished_known_received +=
        other.num_calls_finished_known_received;
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
    LoadBalanceRequest request;
    stream->Read(&request);
    IncreaseRequestCount();
    gpr_log(GPR_INFO, "LB: recv msg '%s'", request.DebugString().c_str());

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
      if (shutdown_) break;
      SendResponse(stream, response_and_delay.first, response_and_delay.second);
    }

    if (client_load_reporting_interval_seconds_ > 0) {
      request.Clear();
      stream->Read(&request);
      gpr_log(GPR_INFO, "LB: recv client load report msg: '%s'",
              request.DebugString().c_str());
      GPR_ASSERT(request.has_client_stats());
      client_stats_.num_calls_started +=
          request.client_stats().num_calls_started();
      client_stats_.num_calls_finished +=
          request.client_stats().num_calls_finished();
      client_stats_.num_calls_finished_with_drop_for_rate_limiting +=
          request.client_stats()
              .num_calls_finished_with_drop_for_rate_limiting();
      client_stats_.num_calls_finished_with_drop_for_load_balancing +=
          request.client_stats()
              .num_calls_finished_with_drop_for_load_balancing();
      client_stats_.num_calls_finished_with_client_failed_to_send +=
          request.client_stats()
              .num_calls_finished_with_client_failed_to_send();
      client_stats_.num_calls_finished_known_received +=
          request.client_stats().num_calls_finished_known_received();
      std::lock_guard<std::mutex> lock(mu_);
      cond_.notify_one();
    }

    return Status::OK;
  }

  void add_response(const LoadBalanceResponse& response, int send_after_ms) {
    std::unique_lock<std::mutex> lock(mu_);
    responses_and_delays_.push_back(std::make_pair(response, send_after_ms));
  }

  void Shutdown() {
    std::unique_lock<std::mutex> lock(mu_);
    shutdown_ = true;
  }

  static LoadBalanceResponse BuildResponseForBackends(
      const std::vector<int>& backend_ports) {
    LoadBalanceResponse response;
    for (const int backend_port : backend_ports) {
      auto* server = response.mutable_server_list()->add_servers();
      server->set_ip_address(Ip4ToPackedString("127.0.0.1"));
      server->set_port(backend_port);
    }
    return response;
  }

  const ClientStats& WaitForLoadReport() {
    std::unique_lock<std::mutex> lock(mu_);
    cond_.wait(lock);
    return client_stats_;
  }

 private:
  void SendResponse(Stream* stream, const LoadBalanceResponse& response,
                    int delay_ms) {
    gpr_log(GPR_INFO, "LB: sleeping for %d ms...", delay_ms);
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(delay_ms, GPR_TIMESPAN)));
    gpr_log(GPR_INFO, "LB: Woke up! Sending response '%s'",
            response.DebugString().c_str());
    stream->Write(response);
    IncreaseResponseCount();
  }

  const int client_load_reporting_interval_seconds_;
  std::vector<ResponseDelayPair> responses_and_delays_;
  std::mutex mu_;
  std::condition_variable cond_;
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
      backend_servers_[i].Shutdown();
    }
    for (size_t i = 0; i < balancers_.size(); ++i) {
      balancers_[i]->Shutdown();
      balancer_servers_[i].Shutdown();
    }
    grpc_fake_resolver_response_generator_unref(response_generator_);
  }

  void ResetStub() {
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_);
    std::ostringstream uri;
    uri << "test:///servername_not_used";
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
      port_ = grpc_pick_unused_port_or_die();
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      std::mutex mu;
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Start, this, server_host, &mu, &cond)));
      std::unique_lock<std::mutex> lock(mu);
      cond.wait(lock);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Start(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
      builder.RegisterService(service_);
      server_ = builder.BuildAndStart();
      std::lock_guard<std::mutex> lock(*mu);
      cond->notify_one();
    }

    void Shutdown() {
      gpr_log(GPR_INFO, "%s about to shutdown", type_.c_str());
      server_->Shutdown();
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
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts()), 0);
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // Start servers and send 100 RPCs per server.
  const auto& statuses_and_responses = SendRpc(kMessage_, 100 * num_backends_);

  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }

  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(100, backend_servers_[i].service_->request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1, balancer_servers_[0].service_->response_count());

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
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts()),
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
    EXPECT_EQ(1, backend_servers_[i].service_->request_count());
  }
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }

  // The balancer got a single request.
  EXPECT_EQ(1, balancer_servers_[0].service_->request_count());
  // and sent two responses.
  EXPECT_EQ(2, balancer_servers_[0].service_->response_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

TEST_F(SingleBalancerTest, RepeatedServerlist) {
  constexpr int kServerlistDelayMs = 100;

  // Send a serverlist right away.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts()), 0);
  // ... and the same one a bit later.
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts()),
      kServerlistDelayMs);

  // Send num_backends/2 requests.
  auto statuses_and_responses = SendRpc(kMessage_, num_backends_ / 2);
  // only the first half of the backends will receive them.
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (i < backends_.size() / 2)
      EXPECT_EQ(1, backend_servers_[i].service_->request_count());
    else
      EXPECT_EQ(0, backend_servers_[i].service_->request_count());
  }
  EXPECT_EQ(statuses_and_responses.size(), num_backends_ / 2);
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }

  // Wait for the (duplicated) serverlist update.
  gpr_sleep_until(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(kServerlistDelayMs * 1.1, GPR_TIMESPAN)));

  // Verify the LB has sent two responses.
  EXPECT_EQ(2, balancer_servers_[0].service_->response_count());

  // Some more calls to complete the total number of backends.
  statuses_and_responses = SendRpc(
      kMessage_,
      num_backends_ / 2 + (num_backends_ & 0x1) /* extra one if num_bes odd */);
  // Because a duplicated serverlist should have no effect, all backends must
  // have been hit once now.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(1, backend_servers_[i].service_->request_count());
  }
  EXPECT_EQ(statuses_and_responses.size(), num_backends_ / 2);
  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }

  // The balancer got a single request.
  EXPECT_EQ(1, balancer_servers_[0].service_->request_count());
  // Check LB policy name for the channel.
  EXPECT_EQ("grpclb", channel_->GetLoadBalancingPolicyName());
}

class SingleBalancerWithClientLoadReportingTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerWithClientLoadReportingTest() : GrpclbEnd2endTest(4, 1, 2) {}
};

TEST_F(SingleBalancerWithClientLoadReportingTest, Vanilla) {
  ScheduleResponseForBalancer(
      0, BalancerServiceImpl::BuildResponseForBackends(GetBackendPorts()), 0);
  // Start servers and send 100 RPCs per server.
  const auto& statuses_and_responses = SendRpc(kMessage_, 100 * num_backends_);

  for (const auto& status_and_response : statuses_and_responses) {
    EXPECT_TRUE(status_and_response.first.ok());
    EXPECT_EQ(status_and_response.second.message(), kMessage_);
  }

  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(100, backend_servers_[i].service_->request_count());
  }
  // The balancer got a single request.
  EXPECT_EQ(1, balancer_servers_[0].service_->request_count());
  // and sent a single response.
  EXPECT_EQ(1, balancer_servers_[0].service_->response_count());

  const ClientStats client_stats = WaitForLoadReports();
  EXPECT_EQ(100 * num_backends_, client_stats.num_calls_started);
  EXPECT_EQ(100 * num_backends_, client_stats.num_calls_finished);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_drop_for_rate_limiting);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_drop_for_load_balancing);
  EXPECT_EQ(0U, client_stats.num_calls_finished_with_client_failed_to_send);
  EXPECT_EQ(100 * num_backends_,
            client_stats.num_calls_finished_known_received);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
