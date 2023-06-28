//
// Copyright 2017 gRPC authors.
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
//

#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/counted_service.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_config.h"

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

using grpc::lb::v1::LoadBalancer;
using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;

namespace grpc {
namespace testing {
namespace {

constexpr char kDefaultServiceConfig[] =
    "{\n"
    "  \"loadBalancingConfig\":[\n"
    "    { \"grpclb\":{} }\n"
    "  ]\n"
    "}";

using BackendService = CountedService<TestServiceImpl>;
using BalancerService = CountedService<LoadBalancer::Service>;

const char g_kCallCredsMdKey[] = "call-creds";
const char g_kCallCredsMdValue[] = "should not be received by balancer";

// A test user agent string sent by the client only to the grpclb loadbalancer.
// The backend should not see this user-agent string.
constexpr char kGrpclbSpecificUserAgentString[] = "grpc-grpclb-test-user-agent";

class BackendServiceImpl : public BackendService {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    // The backend should not see a test user agent configured at the client
    // using GRPC_ARG_GRPCLB_CHANNEL_ARGS.
    auto it = context->client_metadata().find("user-agent");
    if (it != context->client_metadata().end()) {
      EXPECT_FALSE(it->second.starts_with(kGrpclbSpecificUserAgentString));
    }
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

  std::set<std::string> clients() {
    grpc::internal::MutexLock lock(&clients_mu_);
    return clients_;
  }

 private:
  void AddClient(const std::string& client) {
    grpc::internal::MutexLock lock(&clients_mu_);
    clients_.insert(client);
  }

  grpc::internal::Mutex clients_mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(&clients_mu_);
};

std::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
  return std::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

std::string Ip6ToPackedString(const char* ip_str) {
  struct in6_addr ip6;
  GPR_ASSERT(inet_pton(AF_INET6, ip_str, &ip6) == 1);
  return std::string(reinterpret_cast<const char*>(&ip6), sizeof(ip6));
}

struct ClientStats {
  size_t num_calls_started = 0;
  size_t num_calls_finished = 0;
  size_t num_calls_finished_with_client_failed_to_send = 0;
  size_t num_calls_finished_known_received = 0;
  std::map<std::string, size_t> drop_token_counts;

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
    gpr_log(GPR_INFO, "LB[%p]: BalanceLoad", this);
    {
      grpc::internal::MutexLock lock(&mu_);
      if (serverlist_done_) goto done;
    }
    {
      // The loadbalancer should see a test user agent because it was
      // specifically configured at the client using
      // GRPC_ARG_GRPCLB_CHANNEL_ARGS
      auto it = context->client_metadata().find("user-agent");
      EXPECT_TRUE(it != context->client_metadata().end());
      if (it != context->client_metadata().end()) {
        EXPECT_THAT(std::string(it->second.data(), it->second.length()),
                    ::testing::StartsWith(kGrpclbSpecificUserAgentString));
      }
      // Balancer shouldn't receive the call credentials metadata.
      EXPECT_EQ(context->client_metadata().find(g_kCallCredsMdKey),
                context->client_metadata().end());
      LoadBalanceRequest request;
      std::vector<ResponseDelayPair> responses_and_delays;

      if (!stream->Read(&request)) {
        goto done;
      } else {
        if (request.has_initial_request()) {
          grpc::internal::MutexLock lock(&mu_);
          service_names_.push_back(request.initial_request().name());
        }
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
        grpc::internal::MutexLock lock(&mu_);
        responses_and_delays = responses_and_delays_;
      }
      for (const auto& response_and_delay : responses_and_delays) {
        SendResponse(stream, response_and_delay.first,
                     response_and_delay.second);
      }
      {
        grpc::internal::MutexLock lock(&mu_);
        while (!serverlist_done_) {
          serverlist_cond_.Wait(&mu_);
        }
      }

      if (client_load_reporting_interval_seconds_ > 0) {
        request.Clear();
        while (stream->Read(&request)) {
          gpr_log(GPR_INFO, "LB[%p]: received client load report message '%s'",
                  this, request.DebugString().c_str());
          GPR_ASSERT(request.has_client_stats());
          ClientStats load_report;
          load_report.num_calls_started =
              request.client_stats().num_calls_started();
          load_report.num_calls_finished =
              request.client_stats().num_calls_finished();
          load_report.num_calls_finished_with_client_failed_to_send =
              request.client_stats()
                  .num_calls_finished_with_client_failed_to_send();
          load_report.num_calls_finished_known_received =
              request.client_stats().num_calls_finished_known_received();
          for (const auto& drop_token_count :
               request.client_stats().calls_finished_with_drop()) {
            load_report
                .drop_token_counts[drop_token_count.load_balance_token()] =
                drop_token_count.num_calls();
          }
          // We need to acquire the lock here in order to prevent the notify_one
          // below from firing before its corresponding wait is executed.
          grpc::internal::MutexLock lock(&mu_);
          load_report_queue_.emplace_back(std::move(load_report));
          load_report_cond_.Signal();
        }
      }
    }
  done:
    gpr_log(GPR_INFO, "LB[%p]: done", this);
    return Status::OK;
  }

  void add_response(const LoadBalanceResponse& response, int send_after_ms) {
    grpc::internal::MutexLock lock(&mu_);
    responses_and_delays_.push_back(std::make_pair(response, send_after_ms));
  }

  void Start() {
    grpc::internal::MutexLock lock(&mu_);
    serverlist_done_ = false;
    responses_and_delays_.clear();
    load_report_queue_.clear();
  }

  void Shutdown() {
    NotifyDoneWithServerlists();
    gpr_log(GPR_INFO, "LB[%p]: shut down", this);
  }

  ClientStats WaitForLoadReport() {
    grpc::internal::MutexLock lock(&mu_);
    if (load_report_queue_.empty()) {
      while (load_report_queue_.empty()) {
        load_report_cond_.Wait(&mu_);
      }
    }
    ClientStats load_report = std::move(load_report_queue_.front());
    load_report_queue_.pop_front();
    return load_report;
  }

  void NotifyDoneWithServerlists() {
    grpc::internal::MutexLock lock(&mu_);
    if (!serverlist_done_) {
      serverlist_done_ = true;
      serverlist_cond_.SignalAll();
    }
  }

  std::vector<std::string> service_names() {
    grpc::internal::MutexLock lock(&mu_);
    return service_names_;
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
  std::vector<std::string> service_names_;

  grpc::internal::Mutex mu_;
  grpc::internal::CondVar serverlist_cond_;
  bool serverlist_done_ ABSL_GUARDED_BY(mu_) = false;
  grpc::internal::CondVar load_report_cond_;
  std::deque<ClientStats> load_report_queue_ ABSL_GUARDED_BY(mu_);
};

class GrpclbEnd2endTest : public ::testing::Test {
 protected:
  GrpclbEnd2endTest(size_t num_backends, size_t num_balancers,
                    int client_load_reporting_interval_seconds)
      : server_host_("localhost"),
        num_backends_(num_backends),
        num_balancers_(num_balancers),
        client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds) {}

  static void SetUpTestSuite() {
    // Make the backup poller poll very frequently in order to pick up
    // updates from all the subchannels's FDs.
    grpc_core::ConfigVars::Overrides overrides;
    overrides.client_channel_backup_poll_interval_ms = 1;
    grpc_core::ConfigVars::SetOverrides(overrides);
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    grpc_core::SetEnv("grpc_cfstream", "0");
#endif
    grpc_init();
  }

  static void TearDownTestSuite() { grpc_shutdown(); }

  void SetUp() override {
    bool localhost_resolves_to_ipv4 = false;
    bool localhost_resolves_to_ipv6 = false;
    grpc_core::LocalhostResolves(&localhost_resolves_to_ipv4,
                                 &localhost_resolves_to_ipv6);
    ipv6_only_ = !localhost_resolves_to_ipv4 && localhost_resolves_to_ipv6;
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
                 const std::string& expected_targets = "",
                 int subchannel_cache_delay_ms = 0) {
    // Send a separate user agent string for the grpclb load balancer alone.
    grpc_core::ChannelArgs grpclb_channel_args;
    // Set a special user agent string for the grpclb load balancer. It
    // will be verified at the load balancer.
    grpclb_channel_args = grpclb_channel_args.Set(
        GRPC_ARG_PRIMARY_USER_AGENT_STRING, kGrpclbSpecificUserAgentString);
    ChannelArguments args;
    if (fallback_timeout > 0) args.SetGrpclbFallbackTimeout(fallback_timeout);
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    if (!expected_targets.empty()) {
      args.SetString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
      grpclb_channel_args = grpclb_channel_args.Set(
          GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_targets);
    }
    if (subchannel_cache_delay_ms > 0) {
      args.SetInt(GRPC_ARG_GRPCLB_SUBCHANNEL_CACHE_INTERVAL_MS,
                  subchannel_cache_delay_ms * grpc_test_slowdown_factor());
    }
    static const grpc_arg_pointer_vtable channel_args_vtable = {
        // copy
        [](void* p) -> void* {
          return grpc_channel_args_copy(static_cast<grpc_channel_args*>(p));
        },
        // destroy
        [](void* p) {
          grpc_channel_args_destroy(static_cast<grpc_channel_args*>(p));
        },
        // compare
        [](void* p1, void* p2) {
          return grpc_channel_args_compare(static_cast<grpc_channel_args*>(p1),
                                           static_cast<grpc_channel_args*>(p2));
        },
    };
    // Specify channel args for the channel to the load balancer.
    args.SetPointerWithVtable(
        GRPC_ARG_EXPERIMENTAL_GRPCLB_CHANNEL_ARGS,
        const_cast<grpc_channel_args*>(grpclb_channel_args.ToC().get()),
        &channel_args_vtable);
    std::ostringstream uri;
    uri << "fake:///" << kApplicationTargetName_;
    // TODO(dgq): templatize tests to run everything using both secure and
    // insecure channel credentials.
    grpc_channel_credentials* channel_creds =
        grpc_fake_transport_security_credentials_create();
    grpc_call_credentials* call_creds = grpc_md_only_test_credentials_create(
        g_kCallCredsMdKey, g_kCallCredsMdValue);
    std::shared_ptr<ChannelCredentials> creds(
        new SecureChannelCredentials(grpc_composite_channel_credentials_create(
            channel_creds, call_creds, nullptr)));
    call_creds->Unref();
    channel_creds->Unref();
    channel_ = grpc::CreateCustomChannel(uri.str(), creds, args);
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
      if (status.error_message() == "drop directed by grpclb balancer") {
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
    std::string balancer_name;
  };

  grpc_core::EndpointAddressesList CreateLbAddressesFromAddressDataList(
      const std::vector<AddressData>& address_data) {
    grpc_core::EndpointAddressesList addresses;
    for (const auto& addr : address_data) {
      absl::StatusOr<grpc_core::URI> lb_uri =
          grpc_core::URI::Parse(absl::StrCat(
              ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", addr.port));
      GPR_ASSERT(lb_uri.ok());
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(*lb_uri, &address));
      addresses.emplace_back(
          address, grpc_core::ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY,
                                                addr.balancer_name));
    }
    return addresses;
  }

  grpc_core::Resolver::Result MakeResolverResult(
      const std::vector<AddressData>& balancer_address_data,
      const std::vector<AddressData>& backend_address_data = {},
      const char* service_config_json = kDefaultServiceConfig) {
    grpc_core::Resolver::Result result;
    result.addresses =
        CreateLbAddressesFromAddressDataList(backend_address_data);
    result.service_config = grpc_core::ServiceConfigImpl::Create(
        grpc_core::ChannelArgs(), service_config_json);
    GPR_ASSERT(result.service_config.ok());
    grpc_core::EndpointAddressesList balancer_addresses =
        CreateLbAddressesFromAddressDataList(balancer_address_data);
    result.args = grpc_core::SetGrpcLbBalancerAddresses(
        grpc_core::ChannelArgs(), std::move(balancer_addresses));
    return result;
  }

  void SetNextResolutionAllBalancers(
      const char* service_config_json = kDefaultServiceConfig) {
    std::vector<AddressData> addresses;
    for (size_t i = 0; i < balancers_.size(); ++i) {
      addresses.emplace_back(AddressData{balancers_[i]->port_, ""});
    }
    SetNextResolution(addresses, {}, service_config_json);
  }

  void SetNextResolution(
      const std::vector<AddressData>& balancer_address_data,
      const std::vector<AddressData>& backend_address_data = {},
      const char* service_config_json = kDefaultServiceConfig) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = MakeResolverResult(
        balancer_address_data, backend_address_data, service_config_json);
    response_generator_->SetResponse(std::move(result));
  }

  void SetNextReresolutionResponse(
      const std::vector<AddressData>& balancer_address_data,
      const std::vector<AddressData>& backend_address_data = {},
      const char* service_config_json = kDefaultServiceConfig) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result = MakeResolverResult(
        balancer_address_data, backend_address_data, service_config_json);
    response_generator_->SetReresolutionResponse(std::move(result));
  }

  std::vector<int> GetBackendPorts(size_t start_index = 0,
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

  LoadBalanceResponse BuildResponseForBackends(
      const std::vector<int>& backend_ports,
      const std::map<std::string, size_t>& drop_token_counts) {
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
      server->set_ip_address(ipv6_only_ ? Ip6ToPackedString("::1")
                                        : Ip4ToPackedString("127.0.0.1"));
      server->set_port(backend_port);
      static int token_count = 0;
      server->set_load_balance_token(
          absl::StrFormat("token%03d", ++token_count));
    }
    return response;
  }

  Status SendRpc(EchoResponse* response = nullptr, int timeout_ms = 1000,
                 bool wait_for_ready = false,
                 const Status& expected_status = Status::OK) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage_);
    if (!expected_status.ok()) {
      auto* error = request.mutable_param()->mutable_expected_error();
      error->set_code(expected_status.error_code());
      error->set_error_message(expected_status.error_message());
    }
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
    explicit ServerThread(const std::string& type, Args&&... args)
        : port_(grpc_pick_unused_port_or_die()),
          type_(type),
          service_(std::forward<Args>(args)...) {}

    void Start(const std::string& server_host) {
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      service_.Start();
      grpc::internal::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc::internal::MutexLock lock(&mu);
      grpc::internal::CondVar cond;
      thread_ = std::make_unique<std::thread>(
          std::bind(&ServerThread::Serve, this, server_host, &mu, &cond));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Serve(const std::string& server_host, grpc::internal::Mutex* mu,
               grpc::internal::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc::internal::MutexLock lock(mu);
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      std::shared_ptr<ServerCredentials> creds(new SecureServerCredentials(
          grpc_fake_transport_security_server_credentials_create()));
      builder.AddListeningPort(server_address.str(), creds);
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      cond->Signal();
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
    std::string type_;
    T service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
  };

  const std::string server_host_;
  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  bool ipv6_only_ = false;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerThread<BackendServiceImpl>>> backends_;
  std::vector<std::unique_ptr<ServerThread<BalancerServiceImpl>>> balancers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  const std::string kRequestMessage_ = "Live long and prosper.";
  const std::string kApplicationTargetName_ = "application_target_name";
};

class SingleBalancerTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerTest() : GrpclbEnd2endTest(4, 1, 0) {}
};

TEST_F(SingleBalancerTest, Vanilla) {
  SetNextResolutionAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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

TEST_F(SingleBalancerTest, SubchannelCaching) {
  ResetStub(/*fallback_timeout=*/0, /*expected_targets=*/"",
            /*subchannel_cache_delay_ms=*/1500);
  SetNextResolutionAllBalancers();
  // Initially send all backends.
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
  // Then remove backends 0 and 1.
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(2), {}), 1000);
  // Now re-add backend 1.
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(1), {}), 1000);
  // Wait for all backends to come online.
  WaitForAllBackends();
  // Send RPCs for long enough to get all responses.
  gpr_timespec deadline = grpc_timeout_milliseconds_to_deadline(3000);
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
  // Backend 0 should have received less traffic than the others.
  // Backend 1 would have received less traffic than 2 and 3.
  gpr_log(GPR_INFO, "BACKEND 0: %" PRIuPTR " requests",
          backends_[0]->service_.request_count());
  EXPECT_GT(backends_[0]->service_.request_count(), 0);
  for (size_t i = 1; i < backends_.size(); ++i) {
    gpr_log(GPR_INFO, "BACKEND %" PRIuPTR ": %" PRIuPTR " requests", i,
            backends_[i]->service_.request_count());
    EXPECT_GT(backends_[i]->service_.request_count(),
              backends_[0]->service_.request_count())
        << "backend " << i;
    if (i >= 2) {
      EXPECT_GT(backends_[i]->service_.request_count(),
                backends_[1]->service_.request_count())
          << "backend " << i;
    }
  }
  // Backend 1 should never have lost its connection from the client.
  EXPECT_EQ(1UL, backends_[1]->service_.clients().size());
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // The balancer got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // And sent 3 responses.
  EXPECT_EQ(3U, balancers_[0]->service_.response_count());
}

TEST_F(SingleBalancerTest, ReturnServerStatus) {
  SetNextResolutionAllBalancers();
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send a request that the backend will fail, and make sure we get
  // back the right status.
  Status expected(StatusCode::INVALID_ARGUMENT, "He's dead, Jim!");
  Status actual = SendRpc(/*response=*/nullptr, /*timeout_ms=*/1000,
                          /*wait_for_ready=*/false, expected);
  EXPECT_EQ(actual.error_code(), expected.error_code());
  EXPECT_EQ(actual.error_message(), expected.error_message());
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
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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
  SetNextResolution({}, {},
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
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
  const size_t kNumRpcs = num_backends_ * 2;
  CheckRpcSendOk(kNumRpcs, 1000 /* timeout_ms */, true /* wait_for_ready */);
  // Check that all requests went to the first backend.  This verifies
  // that we used pick_first instead of round_robin as the child policy.
  EXPECT_EQ(backends_[0]->service_.request_count(), kNumRpcs);
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(backends_[i]->service_.request_count(), 0UL);
  }
  // Send new resolution that removes child policy from service config.
  SetNextResolutionAllBalancers();
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

TEST_F(SingleBalancerTest, SameBackendListedMultipleTimes) {
  SetNextResolutionAllBalancers();
  // Same backend listed twice.
  std::vector<int> ports;
  ports.push_back(backends_[0]->port_);
  ports.push_back(backends_[0]->port_);
  const size_t kNumRpcsPerAddress = 10;
  ScheduleResponseForBalancer(0, BuildResponseForBackends(ports, {}), 0);
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
  SetNextResolution({AddressData{balancers_[0]->port_, "lb"}});
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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

TEST_F(SingleBalancerTest, InitiallyEmptyServerlist) {
  SetNextResolutionAllBalancers();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const int kCallDeadlineMs = kServerlistDelayMs * 10;
  // First response is an empty serverlist, sent right away.
  ScheduleResponseForBalancer(0, LoadBalanceResponse(), 0);
  // Send non-empty serverlist only after kServerlistDelayMs
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), kServerlistDelayMs);
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
  ScheduleResponseForBalancer(0, BuildResponseForBackends(ports, {}), 0);
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
  const size_t kNumBackendsInResolution = backends_.size() / 2;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    backend_addresses.emplace_back(AddressData{backends_[i]->port_, ""});
  }
  SetNextResolution(balancer_addresses, backend_addresses);

  // Send non-empty serverlist only after kServerlistDelayMs.
  ScheduleResponseForBalancer(
      0,
      BuildResponseForBackends(
          GetBackendPorts(kNumBackendsInResolution /* start_index */), {}),
      kServerlistDelayMs);

  // Wait until all the fallback backends are reachable.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    WaitForBackend(i);
  }

  // The first request.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }

  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    WaitForBackend(i);
  }

  // Send out the second request.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");

  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
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
  const size_t kNumBackendsInResolution = backends_.size() / 3;
  const size_t kNumBackendsInResolutionUpdate = backends_.size() / 3;

  ResetStub(kFallbackTimeoutMs);
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    backend_addresses.emplace_back(AddressData{backends_[i]->port_, ""});
  }
  SetNextResolution(balancer_addresses, backend_addresses);

  // Send non-empty serverlist only after kServerlistDelayMs.
  ScheduleResponseForBalancer(
      0,
      BuildResponseForBackends(
          GetBackendPorts(kNumBackendsInResolution +
                          kNumBackendsInResolutionUpdate /* start_index */),
          {}),
      kServerlistDelayMs);

  // Wait until all the fallback backends are reachable.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    WaitForBackend(i);
  }

  // The first request.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolution);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // Fallback is used: each backend returned by the resolver should have
  // gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution; i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }

  balancer_addresses.clear();
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  backend_addresses.clear();
  for (size_t i = kNumBackendsInResolution;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    backend_addresses.emplace_back(AddressData{backends_[i]->port_, ""});
  }
  SetNextResolution(balancer_addresses, backend_addresses);

  // Wait until the resolution update has been processed and all the new
  // fallback backends are reachable.
  for (size_t i = kNumBackendsInResolution;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    WaitForBackend(i);
  }

  // Send out the second request.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(kNumBackendsInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");

  // The resolution update is used: each backend in the resolution update should
  // have gotten one request.
  for (size_t i = 0; i < kNumBackendsInResolution; ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    EXPECT_EQ(1U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution + kNumBackendsInResolutionUpdate;
       i < backends_.size(); ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }

  // Wait until the serverlist reception has been processed and all backends
  // in the serverlist are reachable.
  for (size_t i = kNumBackendsInResolution + kNumBackendsInResolutionUpdate;
       i < backends_.size(); ++i) {
    WaitForBackend(i);
  }

  // Send out the third request.
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(backends_.size() - kNumBackendsInResolution -
                 kNumBackendsInResolutionUpdate);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");

  // Serverlist is used: each backend returned by the balancer should
  // have gotten one request.
  for (size_t i = 0;
       i < kNumBackendsInResolution + kNumBackendsInResolutionUpdate; ++i) {
    EXPECT_EQ(0U, backends_[i]->service_.request_count());
  }
  for (size_t i = kNumBackendsInResolution + kNumBackendsInResolutionUpdate;
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
       FallbackAfterStartupLoseContactWithBalancerThenBackends) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = backends_.size() - kNumFallbackBackends;
  std::vector<AddressData> backend_addresses;
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    backend_addresses.emplace_back(AddressData{backends_[i]->port_, ""});
  }
  std::vector<AddressData> balancer_addresses;
  for (size_t i = 0; i < balancers_.size(); ++i) {
    balancer_addresses.emplace_back(AddressData{balancers_[i]->port_, ""});
  }
  SetNextResolution(balancer_addresses, backend_addresses);
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}),
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
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}),
      0);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
}

TEST_F(SingleBalancerTest,
       FallbackAfterStartupLoseContactWithBackendsThenBalancer) {
  // First two backends are fallback, last two are pointed to by balancer.
  const size_t kNumFallbackBackends = 2;
  const size_t kNumBalancerBackends = backends_.size() - kNumFallbackBackends;
  std::vector<AddressData> backend_addresses;
  for (size_t i = 0; i < kNumFallbackBackends; ++i) {
    backend_addresses.emplace_back(AddressData{backends_[i]->port_, ""});
  }
  std::vector<AddressData> balancer_addresses;
  for (size_t i = 0; i < balancers_.size(); ++i) {
    balancer_addresses.emplace_back(AddressData{balancers_[i]->port_, ""});
  }
  SetNextResolution(balancer_addresses, backend_addresses);
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}),
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
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(kNumFallbackBackends), {}),
      0);
  WaitForAllBackends(1 /* num_requests_multiple_of */,
                     kNumFallbackBackends /* start_index */);
}

TEST_F(SingleBalancerTest, FallbackEarlyWhenBalancerChannelFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return an unreachable balancer and one fallback backend.
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(
      AddressData{grpc_pick_unused_port_or_die(), ""});
  std::vector<AddressData> backend_addresses;
  backend_addresses.emplace_back(AddressData{backends_[0]->port_, ""});
  SetNextResolution(balancer_addresses, backend_addresses);
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

TEST_F(SingleBalancerTest, FallbackEarlyWhenBalancerCallFails) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return one balancer and one fallback backend.
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  backend_addresses.emplace_back(AddressData{backends_[0]->port_, ""});
  SetNextResolution(balancer_addresses, backend_addresses);
  // Balancer drops call without sending a serverlist.
  balancers_[0]->service_.NotifyDoneWithServerlists();
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

TEST_F(SingleBalancerTest, FallbackControlledByBalancerBeforeFirstServerlist) {
  const int kFallbackTimeoutMs = 10000 * grpc_test_slowdown_factor();
  ResetStub(kFallbackTimeoutMs);
  // Return one balancer and one fallback backend.
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  backend_addresses.emplace_back(AddressData{backends_[0]->port_, ""});
  SetNextResolution(balancer_addresses, backend_addresses);
  // Balancer explicitly tells client to fallback.
  LoadBalanceResponse resp;
  resp.mutable_fallback_response();
  ScheduleResponseForBalancer(0, resp, 0);
  // Send RPC with deadline less than the fallback timeout and make sure it
  // succeeds.
  CheckRpcSendOk(/* times */ 1, /* timeout_ms */ 1000,
                 /* wait_for_ready */ false);
}

TEST_F(SingleBalancerTest, FallbackControlledByBalancerAfterFirstServerlist) {
  // Return one balancer and one fallback backend (backend 0).
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  backend_addresses.emplace_back(AddressData{backends_[0]->port_, ""});
  SetNextResolution(balancer_addresses, backend_addresses);
  // Balancer initially sends serverlist, then tells client to fall back,
  // then sends the serverlist again.
  // The serverlist points to backend 1.
  LoadBalanceResponse serverlist_resp =
      BuildResponseForBackends({backends_[1]->port_}, {});
  LoadBalanceResponse fallback_resp;
  fallback_resp.mutable_fallback_response();
  ScheduleResponseForBalancer(0, serverlist_resp, 0);
  ScheduleResponseForBalancer(0, fallback_resp, 100);
  ScheduleResponseForBalancer(0, serverlist_resp, 100);
  // Requests initially go to backend 1, then go to backend 0 in
  // fallback mode, then go back to backend 1 when we exit fallback.
  WaitForBackend(1);
  WaitForBackend(0);
  WaitForBackend(1);
}

TEST_F(SingleBalancerTest, BackendsRestart) {
  SetNextResolutionAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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

TEST_F(SingleBalancerTest, ServiceNameFromLbPolicyConfig) {
  constexpr char kServiceConfigWithTarget[] =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"grpclb\":{\n"
      "      \"serviceName\":\"test_service\"\n"
      "    }}\n"
      "  ]\n"
      "}";

  SetNextResolutionAllBalancers(kServiceConfigWithTarget);
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  EXPECT_EQ(balancers_[0]->service_.service_names().back(), "test_service");
}

// This death test is kept separate from the rest to ensure that it's run before
// any others. See https://github.com/grpc/grpc/pull/32269 for details.
using SingleBalancerDeathTest = SingleBalancerTest;

TEST_F(SingleBalancerDeathTest, SecureNaming) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED(
      {
        ResetStub(0, kApplicationTargetName_ + ";lb");
        SetNextResolution({AddressData{balancers_[0]->port_, "woops"}});
        channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(1));
      },
      "");
}

class UpdatesTest : public GrpclbEnd2endTest {
 public:
  UpdatesTest() : GrpclbEnd2endTest(4, 3, 0) {}
};

TEST_F(UpdatesTest, UpdateBalancersButKeepUsingOriginalBalancer) {
  SetNextResolutionAllBalancers();
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};
  ScheduleResponseForBalancer(0, BuildResponseForBackends(first_backend, {}),
                              0);
  ScheduleResponseForBalancer(1, BuildResponseForBackends(second_backend, {}),
                              0);

  // Wait until the first backend is ready.
  WaitForBackend(0);

  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");

  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->service_.request_count());

  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[1]->port_, ""});
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
  // The current LB call is still working, so grpclb continued using it to the
  // first balancer, which doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->service_.request_count());

  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
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

  ScheduleResponseForBalancer(0, BuildResponseForBackends(first_backend, {}),
                              0);
  ScheduleResponseForBalancer(1, BuildResponseForBackends(second_backend, {}),
                              0);

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
  addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  addresses.emplace_back(AddressData{balancers_[1]->port_, ""});
  addresses.emplace_back(AddressData{balancers_[2]->port_, ""});
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
  addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  addresses.emplace_back(AddressData{balancers_[1]->port_, ""});
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
  addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  SetNextResolution(addresses);
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};

  ScheduleResponseForBalancer(0, BuildResponseForBackends(first_backend, {}),
                              0);
  ScheduleResponseForBalancer(1, BuildResponseForBackends(second_backend, {}),
                              0);

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

  // Balancer 0 got a single request.
  EXPECT_EQ(1U, balancers_[0]->service_.request_count());
  // and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->service_.response_count());
  EXPECT_EQ(0U, balancers_[1]->service_.request_count());
  EXPECT_EQ(0U, balancers_[1]->service_.response_count());
  EXPECT_EQ(0U, balancers_[2]->service_.request_count());
  EXPECT_EQ(0U, balancers_[2]->service_.response_count());

  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[1]->port_, ""});
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
  std::vector<AddressData> balancer_addresses;
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  std::vector<AddressData> backend_addresses;
  backend_addresses.emplace_back(AddressData{backends_[0]->port_, ""});
  SetNextResolution(balancer_addresses, backend_addresses);
  // Ask channel to connect to trigger resolver creation.
  channel_->GetState(true);
  // The re-resolution result will contain the addresses of the same balancer
  // and a new fallback backend.
  balancer_addresses.clear();
  balancer_addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  backend_addresses.clear();
  backend_addresses.emplace_back(AddressData{backends_[1]->port_, ""});
  SetNextReresolutionResponse(balancer_addresses, backend_addresses);

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
  const std::vector<int> first_backend{GetBackendPorts()[0]};
  const std::vector<int> second_backend{GetBackendPorts()[1]};
  ScheduleResponseForBalancer(0, BuildResponseForBackends(first_backend, {}),
                              0);
  ScheduleResponseForBalancer(1, BuildResponseForBackends(second_backend, {}),
                              0);

  // Ask channel to connect to trigger resolver creation.
  channel_->GetState(true);
  std::vector<AddressData> addresses;
  addresses.emplace_back(AddressData{balancers_[0]->port_, ""});
  SetNextResolution(addresses);
  addresses.clear();
  addresses.emplace_back(AddressData{balancers_[1]->port_, ""});
  SetNextReresolutionResponse(addresses);

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
      BuildResponseForBackends(
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
        status.error_message() == "drop directed by grpclb balancer") {
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
      BuildResponseForBackends(
          {}, {{"rate_limiting", num_of_drop_by_rate_limiting_addresses},
               {"load_balancing", num_of_drop_by_load_balancing_addresses}}),
      0);
  const Status status = SendRpc(nullptr, 1000, true);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "drop directed by grpclb balancer");
}

TEST_F(SingleBalancerTest, DropAll) {
  SetNextResolutionAllBalancers();
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
  const int num_of_drop_by_rate_limiting_addresses = 1;
  const int num_of_drop_by_load_balancing_addresses = 1;
  ScheduleResponseForBalancer(
      0,
      BuildResponseForBackends(
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
  EXPECT_EQ(status.error_message(), "drop directed by grpclb balancer");
}

class SingleBalancerWithClientLoadReportingTest : public GrpclbEnd2endTest {
 public:
  SingleBalancerWithClientLoadReportingTest() : GrpclbEnd2endTest(4, 1, 3) {}
};

TEST_F(SingleBalancerWithClientLoadReportingTest, Vanilla) {
  SetNextResolutionAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(), {}), 0);
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

  ClientStats client_stats;
  do {
    client_stats += WaitForLoadReports();
  } while (client_stats.num_calls_finished !=
           kNumRpcsPerAddress * num_backends_ + num_ok);
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
      BuildResponseForBackends(GetBackendPorts(0, kNumBackendsFirstPass), {}),
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
  ScheduleResponseForBalancer(
      0, BuildResponseForBackends(GetBackendPorts(kNumBackendsFirstPass), {}),
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
      BuildResponseForBackends(
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
        status.error_message() == "drop directed by grpclb balancer") {
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
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
