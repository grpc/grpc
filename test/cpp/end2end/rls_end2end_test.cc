/*
 *
 * Copyright 2020 gRpc authors.
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

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>

#include "absl/types/optional.h"
#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/lb/v1/load_balancer.grpc.pb.h"
#include "src/proto/grpc/lb/v1/load_balancer.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/lookup/v1/rls.grpc.pb.h"
#include "src/proto/grpc/lookup/v1/rls.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <thread>

namespace grpc {
namespace testing {
namespace {

#define SECONDS(x) (int(x))
#define NANOSECONDS(x) (int(((x)-SECONDS(x)) * 1e9))

const grpc::string kTestKey = "testKey";
const char* kTestUrl = "test.google.fr";
const char* kTestRequestPath = "/grpc.testing.EchoTestService/Echo";
const grpc::string kServerHost = "localhost";
const grpc::string kRequestMessage = "Live long and prosper.";
const grpc::string kTarget = "test_target";
const grpc::string kDefaultTarget = "test_default_target";

template <typename ServiceType>
class CountedService : public ServiceType {
 public:
  size_t request_count() {
    grpc::internal::MutexLock lock(&mu_);
    return request_count_;
  }

  size_t response_count() {
    grpc::internal::MutexLock lock(&mu_);
    return response_count_;
  }

  void IncreaseResponseCount() {
    grpc::internal::MutexLock lock(&mu_);
    ++response_count_;
  }
  void IncreaseRequestCount() {
    grpc::internal::MutexLock lock(&mu_);
    ++request_count_;
  }

  void ResetCounters() {
    grpc::internal::MutexLock lock(&mu_);
    request_count_ = 0;
    response_count_ = 0;
  }

 protected:
  grpc::internal::Mutex mu_;

 private:
  size_t request_count_ = 0;
  size_t response_count_ = 0;
};

using BackendService = CountedService<TestServiceImpl>;
using RlsService =
    CountedService<grpc::lookup::v1::RouteLookupService::Service>;
using BalancerService = CountedService<grpc::lb::v1::LoadBalancer::Service>;

const char g_kCallCredsMdKey[] = "Balancer should not ...";

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
  using Stream = ServerReaderWriter<grpc::lb::v1::LoadBalanceResponse,
                                    grpc::lb::v1::LoadBalanceRequest>;
  using ResponseDelayPair = std::pair<
      std::unordered_map<std::string, grpc::lb::v1::LoadBalanceResponse>, int>;

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
      // Balancer shouldn't receive the call credentials metadata.
      EXPECT_EQ(context->client_metadata().find(g_kCallCredsMdKey),
                context->client_metadata().end());
      grpc::lb::v1::LoadBalanceRequest request;
      std::vector<ResponseDelayPair> responses_and_delays;

      if (!stream->Read(&request)) {
        goto done;
      }
      IncreaseRequestCount();
      gpr_log(GPR_INFO, "LB[%p]: received initial message '%s'", this,
              request.DebugString().c_str());
      auto& name = request.initial_request().name();

      // TODO(juanlishen): Initial response should always be the first response.
      if (client_load_reporting_interval_seconds_ > 0) {
        grpc::lb::v1::LoadBalanceResponse initial_response;
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
        auto it = response_and_delay.first.find(name);
        if (it != response_and_delay.first.end()) {
          SendResponse(stream, it->second, response_and_delay.second);
        }
      }
      {
        grpc::internal::MutexLock lock(&mu_);
        serverlist_cond_.WaitUntil(&mu_, [this] { return serverlist_done_; });
      }

      if (client_load_reporting_interval_seconds_ > 0) {
        request.Clear();
        if (stream->Read(&request)) {
          gpr_log(GPR_INFO, "LB[%p]: received client load report message '%s'",
                  this, request.DebugString().c_str());
          GPR_ASSERT(request.has_client_stats());
          // We need to acquire the lock here in order to prevent the notify_one
          // below from firing before its corresponding wait is executed.
          grpc::internal::MutexLock lock(&mu_);
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
          load_report_cond_.Signal();
        }
      }
    }
  done:
    gpr_log(GPR_INFO, "LB[%p]: done", this);
    return Status::OK;
  }

  void add_response(
      std::unordered_map<std::string, grpc::lb::v1::LoadBalanceResponse>
          response_map,
      int send_after_ms) {
    grpc::internal::MutexLock lock(&mu_);
    responses_and_delays_.push_back(
        std::make_pair(response_map, send_after_ms));
  }

  void Start() {
    grpc::internal::MutexLock lock(&mu_);
    serverlist_done_ = false;
    load_report_ready_ = false;
    responses_and_delays_.clear();
    client_stats_.Reset();
  }

  void Shutdown() {
    NotifyDoneWithServerlists();
    gpr_log(GPR_INFO, "LB[%p]: shut down", this);
  }

  static grpc::lb::v1::LoadBalanceResponse BuildResponseForBackends(
      const std::vector<int>& backend_ports,
      const std::map<grpc::string, size_t>& drop_token_counts) {
    grpc::lb::v1::LoadBalanceResponse response;
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
    grpc::internal::MutexLock lock(&mu_);
    load_report_cond_.WaitUntil(&mu_, [this] { return load_report_ready_; });
    load_report_ready_ = false;
    return client_stats_;
  }

  void NotifyDoneWithServerlists() {
    grpc::internal::MutexLock lock(&mu_);
    if (!serverlist_done_) {
      serverlist_done_ = true;
      serverlist_cond_.Broadcast();
    }
  }

 private:
  void SendResponse(Stream* stream,
                    const grpc::lb::v1::LoadBalanceResponse& response,
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
  grpc::internal::Mutex mu_;
  grpc::internal::CondVar load_report_cond_;
  bool load_report_ready_ = false;
  grpc::internal::CondVar serverlist_cond_;
  bool serverlist_done_ = false;
  ClientStats client_stats_;
};

class RlsServiceImpl : public RlsService {
 public:
  struct Request {
    std::string server;
    std::string path;
    std::map<std::string, std::string> key_map;
  };

  struct Response {
    grpc_status_code status;

    grpc::lookup::v1::RouteLookupResponse response;

    grpc_millis response_delay;

    absl::optional<Request> request_match;
  };

  ::grpc::Status RouteLookup(
      ::grpc::ServerContext* context,
      const ::grpc::lookup::v1::RouteLookupRequest* request,
      ::grpc::lookup::v1::RouteLookupResponse* response) override {
    IncreaseRequestCount();
    Response res;
    {
      grpc::internal::MutexLock lock(&mu_);
      if (!responses_.empty()) {
        res = std::move(responses_.front());
        responses_.pop_front();
      } else {
        return {INTERNAL, std::string("no response entry")};
      }
    }
    if (res.response_delay > 0) {
      gpr_sleep_until(
          grpc_timeout_milliseconds_to_deadline(res.response_delay));
    }
    bool make_response = true;
    if (res.request_match.has_value()) {
      auto& server = request->server();
      auto& path = request->path();
      auto key_map = std::map<std::string, std::string>(
          request->key_map().begin(), request->key_map().end());
      if (server != res.request_match->server ||
          path != res.request_match->path ||
          key_map != res.request_match->key_map) {
        make_response = false;
      }
    }
    if (make_response) {
      IncreaseResponseCount();
      if (res.status == GRPC_STATUS_OK) {
        *response = std::move(res.response);
        return {};
      } else {
        return {StatusCode(res.status),
                std::string("predefined response error code")};
      }
    } else {
      return {UNIMPLEMENTED, std::string("unmatched request key")};
    }
  }

  void Start() {}

  void Shutdown() {}

  void AddResponse(Response response) {
    grpc::internal::MutexLock lock(&mu_);
    responses_.emplace_back(std::move(response));
  }

 private:
  std::deque<Response> responses_;
};

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo Rpc.
class MyTestServiceImpl : public BackendService {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    IncreaseRequestCount();
    auto client_metadata = context->client_metadata();
    auto range = client_metadata.equal_range("X-Google-RLS-Data");
    for (auto it = range.first; it != range.second; ++it) {
      AddRlsHeaderData(it->second);
    }
    IncreaseResponseCount();
    return TestServiceImpl::Echo(context, request, response);
  }

  std::set<grpc::string> rls_data() {
    grpc::internal::MutexLock lock(&mu_);
    return rls_header_data_;
  }

  void ResetRlsData() { grpc::internal::MutexLock lock(&mu_); }

  void Start() {}

  void Shutdown() {}

 private:
  void AddRlsHeaderData(const grpc::string_ref ref) {
    grpc::internal::MutexLock lock(&mu_);
    rls_header_data_.insert(grpc::string(ref.begin(), ref.length()));
  }

  std::set<grpc::string> rls_header_data_;
};

class FakeResolverResponseGeneratorWrapper {
 public:
  FakeResolverResponseGeneratorWrapper()
      : response_generator_(grpc_core::MakeRefCounted<
                            grpc_core::FakeResolverResponseGenerator>()) {}

  FakeResolverResponseGeneratorWrapper(
      FakeResolverResponseGeneratorWrapper&& other) {
    response_generator_ = std::move(other.response_generator_);
  }

  void SetNextResolution(int balancer_port,
                         const char* service_config_json = nullptr) {
    grpc_core::ExecCtx exec_ctx;

    response_generator_->SetResponse(
        BuildFakeResults(balancer_port, service_config_json));
  }

  grpc_core::FakeResolverResponseGenerator* Get() const {
    return response_generator_.get();
  }

 private:
  static grpc_core::Resolver::Result BuildFakeResults(
      int balancer_port, const char* service_config_json = nullptr) {
    grpc_core::Resolver::Result result;
    GPR_ASSERT(balancer_port != 0);

    grpc_arg arg = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_ADDRESS_IS_BALANCER), 1);
    grpc_resolved_address addr;
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr.addr);
    addr.len = sizeof(sockaddr_in);
    addr_in->sin_family = AF_INET;
    addr_in->sin_port = htons(balancer_port);
    addr_in->sin_addr.s_addr = htonl(0x7F000001);  // localhost

    result.addresses.emplace_back(
        addr, grpc_channel_args_copy_and_add(nullptr, &arg, 1));
    if (service_config_json != nullptr) {
      result.service_config_error = GRPC_ERROR_NONE;
      result.service_config = grpc_core::ServiceConfig::Create(
          service_config_json, &result.service_config_error);
      GPR_ASSERT(result.service_config_error == GRPC_ERROR_NONE);
      GPR_ASSERT(result.service_config != nullptr);
    }
    return result;
  }

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
};

class RlsPolicyEnd2endTest : public ::testing::Test {
 protected:
  RlsPolicyEnd2endTest()
      : creds_(new SecureChannelCredentials(
            grpc_fake_transport_security_credentials_create())) {}

  static void SetUpTestCase() {
    GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
    grpc_init();
  }

  static void TearDownTestCase() { grpc_shutdown_blocking(); }

  void SetUp() override {
    rls_server_.reset(new ServerThread<RlsServiceImpl>("rls"));
    rls_server_->Start(kServerHost);
    balancer_.reset(new ServerThread<BalancerServiceImpl>("balancer", 0));
    balancer_->Start(kServerHost);
    resolver_response_generator_.reset(
        new FakeResolverResponseGeneratorWrapper());
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    resolver_response_generator_->Get());
    channel_ = ::grpc::CreateCustomChannel(
        absl::StrCat("fake:///", kTestUrl).c_str(), creds_, args);
  }

  void TearDown() override {
    channel_.reset();
    resolver_response_generator_.reset();
    ShutdownBackends();
    rls_server_->Shutdown();
    balancer_->Shutdown();
  }

  void ShutdownBackends() {
    for (auto& server : backends_) {
      server->Shutdown();
    }
  }

  void StartBackends(size_t num_servers,
                     std::vector<int> ports = std::vector<int>()) {
    backends_.clear();
    for (size_t i = 0; i < num_servers; ++i) {
      backends_.emplace_back(new ServerThread<MyTestServiceImpl>("backend"));
      backends_.back()->Start(kServerHost);
    }
  }

  FakeResolverResponseGeneratorWrapper BuildResolverResponseGenerator() {
    return FakeResolverResponseGeneratorWrapper();
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> BuildStub() {
    return grpc::testing::EchoTestService::NewStub(channel_);
  }

  std::shared_ptr<Channel> BuildChannel(
      const FakeResolverResponseGeneratorWrapper& response_generator,
      ChannelArguments args = ChannelArguments()) {
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator.Get());
    return ::grpc::CreateCustomChannel("fake:///", creds_, args);
  }

  bool SendRpc(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      EchoResponse* response = nullptr, int timeout_ms = 1000,
      Status* result = nullptr, bool wait_for_ready = false,
      const std::map<grpc::string, grpc::string>& initial_metadata = {}) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
    EchoRequest request;
    request.set_message(kRequestMessage);
    ClientContext context;
    for (auto& item : initial_metadata) {
      context.AddMetadata(item.first, item.second);
    }
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
    if (wait_for_ready) context.set_wait_for_ready(true);
    Status status = stub->Echo(&context, request, response);
    if (result != nullptr) *result = status;
    if (local_response) delete response;
    return status.ok();
  }

  void CheckRpcSendOk(
      const std::unique_ptr<grpc::testing::EchoTestService::Stub>& stub,
      const grpc_core::DebugLocation& location, bool wait_for_ready = false,
      int timeout_ms = 2000,
      const std::map<grpc::string, grpc::string>& initial_metadata = {}) {
    EchoResponse response;
    Status status;
    const bool success = SendRpc(stub, &response, timeout_ms, &status,
                                 wait_for_ready, initial_metadata);
    ASSERT_TRUE(success) << "From " << location.file() << ":" << location.line()
                         << "\n"
                         << "Error: " << status.error_message() << " "
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

  std::string BuildServiceConfig(
      double max_age = 10, double stale_age = 5,
      const std::string& default_target = kDefaultTarget,
      int request_processing_strategy = 0, double lookup_service_timeout = 10,
      int64_t cache_size_bytes = 10 * 1024 * 1024) {
    int lookup_service_port = rls_server_->port_;
    std::stringstream service_config;
    service_config << "{";
    service_config << "  \"loadBalancingConfig\":[{";
    service_config << "    \"rls\":{";
    service_config << "      \"routeLookupConfig\":{";
    service_config << "        \"grpcKeybuilders\":[{";
    service_config << "          \"names\":[{";
    service_config
        << "            \"service\":\"grpc.testing.EchoTestService\",";
    service_config << "            \"method\":\"Echo\"";
    service_config << "          }],";
    service_config << "          \"headers\":[";
    service_config << "            {";
    service_config << "              \"key\":\"" << kTestKey << "\",";
    service_config << "              \"names\":[";
    service_config << "                \"key1\",\"key2\",\"key3\"";
    service_config << "              ]";
    service_config << "            }";
    service_config << "          ]";
    service_config << "        }],";
    service_config << "        \"lookupService\":\"localhost:"
                   << lookup_service_port << "\",";
    service_config << "        \"lookupServiceTimeout\":{";
    service_config << "          \"seconds\":"
                   << SECONDS(lookup_service_timeout) << ",";
    service_config << "          \"nanoseconds\":"
                   << NANOSECONDS(lookup_service_timeout);
    service_config << "        },";
    service_config << "        \"maxAge\":{";
    service_config << "          \"seconds\":" << SECONDS(max_age) << ",";
    service_config << "          \"nanoseconds\":" << NANOSECONDS(max_age);
    service_config << "        },";
    service_config << "        \"staleAge\":{";
    service_config << "          \"seconds\":" << SECONDS(stale_age) << ",";
    service_config << "          \"nanoseconds\":" << NANOSECONDS(stale_age);
    service_config << "        },";
    service_config << "        \"cacheSizeBytes\":" << cache_size_bytes << ",";
    service_config << "        \"defaultTarget\":\"" << default_target << "\",";
    service_config << "        \"requestProcessingStrategy\":"
                   << request_processing_strategy;
    service_config << "      },";
    service_config << "      \"childPolicy\":[{";
    service_config << "        \"grpclb\":{";
    service_config << "        }";
    service_config << "      }],";
    service_config
        << "      \"childPolicyConfigTargetFieldName\":\"targetName\"";
    service_config << "    }";
    service_config << "  }]";
    service_config << "}";

    return service_config.str();
  }

  // TODO: remove
  /*
  grpc::lookup::v1::RouteLookupResponse BuildLookupResponse(int port,
  grpc::string header_data = {}) { grpc::lookup::v1::RouteLookupResponse
  response;

    response.set_target(absl::StrCat(kServerHost, port));
    response.set_header_data(header_data);

    return response;
  }*/

  void SetNextResolution(const char* service_config_json = nullptr) {
    resolver_response_generator_->SetNextResolution(balancer_->port_,
                                                    service_config_json);
  }

  void SetNextRlsResponse(
      grpc_status_code status, const char* header_data = nullptr,
      grpc_millis response_delay = 0,
      absl::optional<RlsServiceImpl::Request> request_match = {},
      const std::string& target = kTarget) {
    RlsServiceImpl::Response response;
    response.status = status;
    response.response_delay = response_delay;
    response.response.set_target(target);
    if (header_data != nullptr) response.response.set_header_data(header_data);
    if (request_match.has_value()) {
      response.request_match = std::move(request_match);
    }
    rls_server_->service_.AddResponse(std::move(response));
  }

  void SetNextLbResponse(std::vector<std::pair<std::string, int>> responses) {
    std::unordered_map<std::string, grpc::lb::v1::LoadBalanceResponse>
        response_map;
    for (auto& item : responses) {
      grpc::lb::v1::LoadBalanceResponse res;
      auto server_list = res.mutable_server_list();
      auto server = server_list->add_servers();
      char localhost[] = {0x7F, 0x00, 0x00, 0x01};
      server->set_ip_address(localhost, 4);
      server->set_port(backends_[item.second]->port_);

      response_map.insert(std::make_pair(item.first, res));
    }

    balancer_->service_.add_response(response_map, 0);
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
      grpc::internal::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc::internal::MutexLock lock(&mu);
      grpc::internal::CondVar cond;
      thread_.reset(new std::thread(
          std::bind(&ServerThread::Serve, this, server_host, &mu, &cond)));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Serve(const grpc::string& server_host, grpc::internal::Mutex* mu,
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
    grpc::string type_;
    T service_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
  };

  std::shared_ptr<ChannelCredentials> creds_;
  std::vector<std::unique_ptr<ServerThread<MyTestServiceImpl>>> backends_;
  std::unique_ptr<ServerThread<RlsServiceImpl>> rls_server_;
  std::unique_ptr<ServerThread<BalancerServiceImpl>> balancer_;
  std::unique_ptr<FakeResolverResponseGeneratorWrapper>
      resolver_response_generator_;
  std::shared_ptr<Channel> channel_;
};

TEST_F(RlsPolicyEnd2endTest, RlsGrpcLb) {
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK, "TestHeaderData");
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  auto rls_data = backends_[0]->service_.rls_data();
  EXPECT_EQ(rls_data.size(), 1);
  EXPECT_NE(rls_data.find("TestHeaderData"), rls_data.end());
}

TEST_F(RlsPolicyEnd2endTest, FailedRlsRequestFallback) {
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_INTERNAL);
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, RlsGrpcLbWithoutKeyMapMatch) {
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(
      GRPC_STATUS_OK, "", 0,
      RlsServiceImpl::Request{
          kTestUrl, kTestRequestPath, {{"testKey", "testValue"}}});
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, RlsGrpcLbWithKeyMapMatch) {
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(
      GRPC_STATUS_OK, "", 0,
      RlsServiceImpl::Request{
          kTestUrl, kTestRequestPath, {{kTestKey, "testValue"}}});
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION, false, 2000, {{"key2", "testValue"}});
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
}

TEST_F(RlsPolicyEnd2endTest, UpdateRlsConfig) {
  const std::string kAlternativeDefaultTarget = "test_default_target_2";
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_INTERNAL);
  SetNextLbResponse({{kDefaultTarget, 0}, {kAlternativeDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);

  service_config = BuildServiceConfig(10, 5, kAlternativeDefaultTarget);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_INTERNAL);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, FailedRlsRequestError) {
  StartBackends(2);
  auto service_config = BuildServiceConfig(10, 5, kDefaultTarget, 1);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_INTERNAL);
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendFailure(stub);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
}

TEST_F(RlsPolicyEnd2endTest, RlsRequestTimeout) {
  StartBackends(2);
  auto service_config = BuildServiceConfig(10, 5, kDefaultTarget, 0, 2);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK, nullptr, 4000);
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION, false, 4000);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, AsyncRlsRequest) {
  StartBackends(2);
  auto service_config = BuildServiceConfig(10, 5, kDefaultTarget, 2);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK, nullptr, 1000);
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);

  // wait for RLS response to reach RLS lb policy
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, CachedRlsResponse) {
  StartBackends(2);
  auto service_config = BuildServiceConfig();
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK);
  SetNextLbResponse({{kTarget, 0}, {kDefaultTarget, 1}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
}

TEST_F(RlsPolicyEnd2endTest, StaleRlsResponse) {
  const std::string kAlternativeTarget = "test_target_2";
  StartBackends(3);
  auto service_config = BuildServiceConfig(10, 1);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK);
  SetNextLbResponse(
      {{kTarget, 0}, {kAlternativeTarget, 1}, {kDefaultTarget, 2}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  SetNextRlsResponse(GRPC_STATUS_OK, nullptr, 0, {}, kAlternativeTarget);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  EXPECT_EQ(backends_[2]->service_.request_count(), 0);
  // wait for rls server to receive the second request
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
}

TEST_F(RlsPolicyEnd2endTest, ExpiredRlsResponse) {
  const std::string kAlternativeTarget = "test_target_2";
  StartBackends(3);
  auto service_config = BuildServiceConfig(1, 1);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK);
  SetNextLbResponse(
      {{kTarget, 0}, {kAlternativeTarget, 1}, {kDefaultTarget, 2}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  SetNextRlsResponse(GRPC_STATUS_OK, nullptr, 0, {}, kAlternativeTarget);
  CheckRpcSendOk(stub, DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
  EXPECT_EQ(backends_[2]->service_.request_count(), 0);
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
}

TEST_F(RlsPolicyEnd2endTest, CacheEviction) {
  const std::string kAlternativeTarget = "test_target_2";
  StartBackends(3);
  // set cache bytes to 1 byte
  auto service_config = BuildServiceConfig(10, 5, kDefaultTarget, 0, 10, 1);
  SetNextResolution(service_config.c_str());
  SetNextRlsResponse(GRPC_STATUS_OK);
  SetNextLbResponse(
      {{kTarget, 0}, {kAlternativeTarget, 1}, {kDefaultTarget, 2}});

  auto stub = BuildStub();
  CheckRpcSendOk(stub, DEBUG_LOCATION, false, 2000, {{"key3", "testValue1"}});

  SetNextRlsResponse(GRPC_STATUS_OK, nullptr, 0, {}, kAlternativeTarget);
  CheckRpcSendOk(stub, DEBUG_LOCATION, false, 2000, {{"key3", "testValue2"}});

  // expect the cache entry for the first call is already evicted
  SetNextRlsResponse(GRPC_STATUS_OK);
  CheckRpcSendOk(stub, DEBUG_LOCATION, false, 2000, {{"key3", "testValue1"}});

  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
  EXPECT_EQ(backends_[2]->service_.request_count(), 0);
  EXPECT_EQ(rls_server_->service_.request_count(), 3);
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
