//
// Copyright 2020 gRPC authors.
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

// FIXME: add tests:
// - cache eviction via cleanup timer (based on age)
// - RLS channel is down; wait_for_ready request is sent and RLS request fails
//   and goes into backoff; RLS channel comes back up before backoff timer
//   fires; request is processed at that point

#include <deque>
#include <map>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/lookup/v1/rls.grpc.pb.h"
#include "src/proto/grpc/lookup/v1/rls.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_lb_policies.h"
#include "test/cpp/end2end/counted_service.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_config.h"

using ::grpc::lookup::v1::RouteLookupRequest;
using ::grpc::lookup::v1::RouteLookupResponse;

namespace grpc {
namespace testing {
namespace {

const char* kServerName = "test.google.fr";
const char* kRequestMessage = "Live long and prosper.";

const char* kCallCredsMdKey = "call_cred_name";
const char* kCallCredsMdValue = "call_cred_value";

const char* kTestKey = "test_key";
const char* kTestValue = "test_value";
const char* kHostKey = "host_key";
const char* kServiceKey = "service_key";
const char* kServiceValue = "grpc.testing.EchoTestService";
const char* kMethodKey = "method_key";
const char* kMethodValue = "Echo";
const char* kConstantKey = "constant_key";
const char* kConstantValue = "constant_value";

using BackendService = CountedService<TestServiceImpl>;
using RlsService =
    CountedService<grpc::lookup::v1::RouteLookupService::Service>;

class RlsServiceImpl : public RlsService {
 public:
  ::grpc::Status RouteLookup(::grpc::ServerContext* context,
                             const RouteLookupRequest* request,
                             RouteLookupResponse* response) override {
    gpr_log(GPR_INFO, "RLS: Received request: %s",
            request->DebugString().c_str());
    // RLS server should see call creds.
    EXPECT_THAT(context->client_metadata(),
                ::testing::Contains(
                    ::testing::Pair(kCallCredsMdKey, kCallCredsMdValue)));
    IncreaseRequestCount();
    EXPECT_EQ(request->target_type(), "grpc");
    // See if we have a configured response for this request.
    ResponseData res;
    {
      grpc::internal::MutexLock lock(&mu_);
      auto it = responses_.find(*request);
      if (it == responses_.end()) {
        gpr_log(GPR_INFO, "RLS: no matching request, returning INTERNAL");
        unmatched_requests_.push_back(*request);
        return Status(StatusCode::INTERNAL, "no response entry");
      }
      res = it->second;
    }
    // Configured response found, so use it.
    if (res.response_delay > 0) {
      gpr_sleep_until(
          grpc_timeout_milliseconds_to_deadline(res.response_delay));
    }
    IncreaseResponseCount();
    *response = res.response;
    gpr_log(GPR_INFO, "RLS: returning configured response: %s",
            response->DebugString().c_str());
    return Status::OK;
  }

  void Start() {}

  void Shutdown() {}

  void SetResponse(RouteLookupRequest request, RouteLookupResponse response,
                   grpc_millis response_delay = 0) {
    grpc::internal::MutexLock lock(&mu_);
    responses_[std::move(request)] = {std::move(response), response_delay};
  }

  void RemoveResponse(const RouteLookupRequest& request) {
    grpc::internal::MutexLock lock(&mu_);
    responses_.erase(request);
  }

  std::vector<RouteLookupRequest> GetUnmatchedRequests() {
    grpc::internal::MutexLock lock(&mu_);
    return std::move(unmatched_requests_);
  }

 private:
  // Sorting thunk for RouteLookupRequest.
  struct RlsRequestLessThan {
    bool operator()(const RouteLookupRequest& req1,
                    const RouteLookupRequest& req2) const {
      std::map<absl::string_view, absl::string_view> key_map1(
          req1.key_map().begin(), req1.key_map().end());
      std::map<absl::string_view, absl::string_view> key_map2(
          req2.key_map().begin(), req2.key_map().end());
      if (key_map1 < key_map2) return true;
      if (req1.reason() < req2.reason()) return true;
      if (req1.stale_header_data() < req2.stale_header_data()) return true;
      return false;
    }
  };

  struct ResponseData {
    RouteLookupResponse response;
    grpc_millis response_delay;
  };

  grpc::internal::Mutex mu_;
  std::map<RouteLookupRequest, ResponseData, RlsRequestLessThan> responses_
      ABSL_GUARDED_BY(&mu_);
  std::vector<RouteLookupRequest> unmatched_requests_ ABSL_GUARDED_BY(&mu_);
};

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo Rpc.
class MyTestServiceImpl : public BackendService {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    // Backend should see call creds.
    EXPECT_THAT(context->client_metadata(),
                ::testing::Contains(
                    ::testing::Pair(kCallCredsMdKey, kCallCredsMdValue)));
    IncreaseRequestCount();
    auto client_metadata = context->client_metadata();
    auto range = client_metadata.equal_range("X-Google-RLS-Data");
    {
      grpc::internal::MutexLock lock(&mu_);
      for (auto it = range.first; it != range.second; ++it) {
        rls_header_data_.insert(
            std::string(it->second.begin(), it->second.length()));
      }
    }
    IncreaseResponseCount();
    return TestServiceImpl::Echo(context, request, response);
  }

  std::set<std::string> rls_data() {
    grpc::internal::MutexLock lock(&mu_);
    return std::move(rls_header_data_);
  }

  void Start() {}

  void Shutdown() {}

 private:
  grpc::internal::Mutex mu_;
  std::set<std::string> rls_header_data_ ABSL_GUARDED_BY(&mu_);
};

class FakeResolverResponseGeneratorWrapper {
 public:
  FakeResolverResponseGeneratorWrapper()
      : response_generator_(grpc_core::MakeRefCounted<
                            grpc_core::FakeResolverResponseGenerator>()) {}

  void SetNextResolution(absl::string_view service_config_json) {
    grpc_core::ExecCtx exec_ctx;
    response_generator_->SetResponse(BuildFakeResults(service_config_json));
  }

  grpc_core::FakeResolverResponseGenerator* Get() const {
    return response_generator_.get();
  }

 private:
  static grpc_core::Resolver::Result BuildFakeResults(
      absl::string_view service_config_json) {
    grpc_core::Resolver::Result result;
    result.service_config_error = GRPC_ERROR_NONE;
    result.service_config = grpc_core::ServiceConfig::Create(
        result.args, service_config_json, &result.service_config_error);
    EXPECT_EQ(result.service_config_error, GRPC_ERROR_NONE)
        << "JSON: " << service_config_json
        << "Error: " << grpc_error_std_string(result.service_config_error);
    EXPECT_NE(result.service_config, nullptr);
    return result;
  }

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
};

class RlsEnd2endTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    gpr_setenv("GRPC_EXPERIMENTAL_ENABLE_RLS_LB_POLICY", "true");
    GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
    grpc_init();
    grpc_core::RegisterFixedAddressLoadBalancingPolicy();
  }

  static void TearDownTestSuite() {
    grpc_shutdown_blocking();
    gpr_unsetenv("GRPC_EXPERIMENTAL_ENABLE_RLS_LB_POLICY");
  }

  void SetUp() override {
    bool localhost_resolves_to_ipv4 = false;
    bool localhost_resolves_to_ipv6 = false;
    grpc_core::LocalhostResolves(&localhost_resolves_to_ipv4,
                                 &localhost_resolves_to_ipv6);
    ipv6_only_ = !localhost_resolves_to_ipv4 && localhost_resolves_to_ipv6;
    rls_server_ = absl::make_unique<ServerThread<RlsServiceImpl>>("rls");
    rls_server_->Start();
    resolver_response_generator_ =
        absl::make_unique<FakeResolverResponseGeneratorWrapper>();
    ResetStub();
  }

  void TearDown() override {
    ShutdownBackends();
    rls_server_->Shutdown();
  }

  void ResetStub(const char* expected_authority = kServerName) {
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    resolver_response_generator_->Get());
    args.SetString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS, expected_authority);
    grpc_channel_credentials* channel_creds =
        grpc_fake_transport_security_credentials_create();
    grpc_call_credentials* call_creds = grpc_md_only_test_credentials_create(
        kCallCredsMdKey, kCallCredsMdValue, false);
    auto creds = std::make_shared<SecureChannelCredentials>(
        grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                  nullptr));
    call_creds->Unref();
    channel_creds->Unref();
    channel_ = ::grpc::CreateCustomChannel(
        absl::StrCat("fake:///", kServerName).c_str(), std::move(creds), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void ShutdownBackends() {
    for (auto& server : backends_) {
      server->Shutdown();
    }
  }

  void StartBackends(size_t num_servers) {
    backends_.clear();
    for (size_t i = 0; i < num_servers; ++i) {
      backends_.push_back(
          absl::make_unique<ServerThread<MyTestServiceImpl>>("backend"));
      backends_.back()->Start();
    }
  }

  std::string TargetStringForPort(int port) {
    if (ipv6_only_) return absl::StrCat("ipv6:[::1]:", port);
    return absl::StrCat("ipv4:127.0.0.1:", port);
  }

  static RouteLookupRequest BuildRlsRequest(
      std::map<std::string, std::string> key,
      RouteLookupRequest::Reason reason = RouteLookupRequest::REASON_MISS,
      const char* stale_header_data = "") {
    RouteLookupRequest request;
    request.set_target_type("grpc");
    request.mutable_key_map()->insert(key.begin(), key.end());
    request.set_reason(reason);
    request.set_stale_header_data(stale_header_data);
    return request;
  }

  static RouteLookupResponse BuildRlsResponse(std::vector<std::string> targets,
                                              const char* header_data = "") {
    RouteLookupResponse response;
    response.mutable_targets()->Add(targets.begin(), targets.end());
    response.set_header_data(header_data);
    return response;
  }

  struct RpcOptions {
    int timeout_ms = 1000;
    bool wait_for_ready = false;
    std::vector<std::pair<std::string, std::string>> metadata;

    RpcOptions() {}

    RpcOptions& set_timeout_ms(int rpc_timeout_ms) {
      timeout_ms = rpc_timeout_ms;
      return *this;
    }

    RpcOptions& set_wait_for_ready(bool rpc_wait_for_ready) {
      wait_for_ready = rpc_wait_for_ready;
      return *this;
    }

    RpcOptions& set_metadata(
        std::vector<std::pair<std::string, std::string>> rpc_metadata) {
      metadata = std::move(rpc_metadata);
      return *this;
    }

    // Populates context.
    void SetupRpc(ClientContext* context) const {
      for (const auto& item : metadata) {
        context->AddMetadata(item.first, item.second);
      }
      if (timeout_ms != 0) {
        context->set_deadline(
            grpc_timeout_milliseconds_to_deadline(timeout_ms));
      }
      if (wait_for_ready) context->set_wait_for_ready(true);
    }
  };

  Status SendRpc(const RpcOptions& rpc_options = RpcOptions(),
                 EchoResponse* response = nullptr) {
    EchoResponse local_response;
    if (response == nullptr) response = &local_response;
    ClientContext context;
    rpc_options.SetupRpc(&context);
    EchoRequest request;
    request.set_message(kRequestMessage);
    return stub_->Echo(&context, request, response);
  }

  void CheckRpcSendOk(const grpc_core::DebugLocation& location,
                      const RpcOptions& rpc_options = RpcOptions()) {
    EchoResponse response;
    Status status = SendRpc(rpc_options, &response);
    ASSERT_TRUE(status.ok()) << location.file() << ":" << location.line()
                             << ": RPC failed: " << status.error_code() << ": "
                             << status.error_message();
    EXPECT_EQ(response.message(), kRequestMessage)
        << location.file() << ":" << location.line();
  }

  void CheckRpcSendFailure(const grpc_core::DebugLocation& location,
                           const RpcOptions& rpc_options = RpcOptions()) {
    Status status = SendRpc(rpc_options);
    ASSERT_FALSE(status.ok()) << location.file() << ":" << location.line();
  }

  class ServiceConfigBuilder {
   public:
    explicit ServiceConfigBuilder(int rls_server_port)
        : rls_server_port_(rls_server_port) {}

    ServiceConfigBuilder& set_lookup_service_timeout(grpc_millis timeout) {
      lookup_service_timeout_ = timeout * grpc_test_slowdown_factor();
      return *this;
    }

    ServiceConfigBuilder& set_default_target(std::string default_target) {
      default_target_ = std::move(default_target);
      return *this;
    }

    ServiceConfigBuilder& set_max_age(grpc_millis max_age) {
      max_age_ = max_age * grpc_test_slowdown_factor();
      return *this;
    }

    ServiceConfigBuilder& set_stale_age(grpc_millis stale_age) {
      stale_age_ = stale_age * grpc_test_slowdown_factor();
      return *this;
    }

    ServiceConfigBuilder& set_cache_size_bytes(int64_t size) {
      cache_size_bytes_ = size;
      return *this;
    }

    ServiceConfigBuilder& AddKeyBuilder(absl::string_view key_builder) {
      key_builders_.push_back(absl::StrCat("{", key_builder, "}"));
      return *this;
    }

    std::string Build() {
      // First build parts of routeLookupConfig.
      std::vector<std::string> route_lookup_config_parts;
      route_lookup_config_parts.push_back(absl::StrFormat(
          "        \"lookupService\":\"localhost:%d\"", rls_server_port_));
      if (lookup_service_timeout_ > 0) {
        route_lookup_config_parts.push_back(absl::StrFormat(
            "        \"lookupServiceTimeout\":\"%d.%09ds\"",
            lookup_service_timeout_ / 1000, lookup_service_timeout_ % 1000));
      }
      if (!default_target_.empty()) {
        route_lookup_config_parts.push_back(absl::StrFormat(
            "        \"defaultTarget\":\"%s\"", default_target_));
      }
      route_lookup_config_parts.push_back(absl::StrFormat(
          "        \"cacheSizeBytes\":%" PRId64, cache_size_bytes_));
      if (max_age_ > 0) {
        route_lookup_config_parts.push_back(
            absl::StrFormat("        \"maxAge\":\"%d.%09ds\"", max_age_ / 1000,
                            max_age_ % 1000));
      }
      if (stale_age_ > 0) {
        route_lookup_config_parts.push_back(
            absl::StrFormat("        \"staleAge\":\"%d.%09ds\"",
                            stale_age_ / 1000, stale_age_ % 1000));
      }
      if (!key_builders_.empty()) {
        route_lookup_config_parts.push_back(
            absl::StrFormat("        \"grpcKeybuilders\":[%s]",
                            absl::StrJoin(key_builders_, ",")));
      }
      // Now build parts of RLS LB policy config.
      std::vector<std::string> rls_config_parts;
      if (!route_lookup_config_parts.empty()) {
        rls_config_parts.push_back(absl::StrCat(
            "      \"routeLookupConfig\":{",
            absl::StrJoin(route_lookup_config_parts, ","), "      }"));
      }
      rls_config_parts.push_back(
          "      \"childPolicy\":[{"
          "        \"fixed_address_lb\":{}\n"
          "      }],\n"
          "      \"childPolicyConfigTargetFieldName\":\"address\"\n");
      // Put it all together.
      return absl::StrCat(
          "{"
          "  \"loadBalancingConfig\":[{"
          "    \"rls\":{",
          absl::StrJoin(rls_config_parts, ","),
          "    }"
          "  }]"
          "}");
    }

   private:
    int rls_server_port_;
    grpc_millis lookup_service_timeout_ = 0;
    std::string default_target_;
    grpc_millis max_age_ = 0;
    grpc_millis stale_age_ = 0;
    int64_t cache_size_bytes_ = 10485760;
    std::vector<std::string> key_builders_;
  };

  ServiceConfigBuilder MakeServiceConfigBuilder() {
    return ServiceConfigBuilder(rls_server_->port_);
  }

  void SetNextResolution(absl::string_view service_config_json) {
    resolver_response_generator_->SetNextResolution(service_config_json);
  }

  template <typename T>
  struct ServerThread {
    template <typename... Args>
    explicit ServerThread(const grpc::string& type, Args&&... args)
        : port_(grpc_pick_unused_port_or_die()),
          type_(type),
          service_(std::forward<Args>(args)...) {}

    void Start() {
      gpr_log(GPR_INFO, "starting %s server on port %d", type_.c_str(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      service_.Start();
      grpc::internal::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc::internal::MutexLock lock(&mu);
      grpc::internal::CondVar cond;
      thread_ = absl::make_unique<std::thread>(
          std::bind(&ServerThread::Serve, this, &mu, &cond));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", type_.c_str());
    }

    void Serve(grpc::internal::Mutex* mu, grpc::internal::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc::internal::MutexLock lock(mu);
      ServerBuilder builder;
      auto creds = std::make_shared<SecureServerCredentials>(
          grpc_fake_transport_security_server_credentials_create());
      builder.AddListeningPort(absl::StrCat("localhost:", port_),
                               std::move(creds));
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

  bool ipv6_only_;
  std::vector<std::unique_ptr<ServerThread<MyTestServiceImpl>>> backends_;
  std::unique_ptr<ServerThread<RlsServiceImpl>> rls_server_;
  std::unique_ptr<FakeResolverResponseGeneratorWrapper>
      resolver_response_generator_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
};

TEST_F(RlsEnd2endTest, Basic) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  // No RLS header seen by the backend, since the RLS response didn't set any.
  EXPECT_THAT(backends_[0]->service_.rls_data(), ::testing::ElementsAre());
}

TEST_F(RlsEnd2endTest, DuplicateHeadersAreMerged) {
  const char* kTestValue2 = "test_value_2";
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, absl::StrCat(kTestValue, ",", kTestValue2)}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Same header present twice in the request.  Values should be merged.
  CheckRpcSendOk(
      DEBUG_LOCATION,
      RpcOptions().set_metadata({{"key1", kTestValue}, {"key1", kTestValue2}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, SecondHeaderUsed) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\", \"key2\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key2", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, MultipleHeaderKeys) {
  const char* kTestKey2 = "test_key_2";
  const char* kTestValue2 = "test_value_2";
  StartBackends(1);
  SetNextResolution(MakeServiceConfigBuilder()
                        .AddKeyBuilder(absl::StrFormat(
                            "\"names\":[{"
                            "  \"service\":\"%s\","
                            "  \"method\":\"%s\""
                            "}],"
                            "\"headers\":["
                            "  {"
                            "    \"key\":\"%s\","
                            "    \"names\":["
                            "      \"key1\""
                            "    ]"
                            "  },"
                            "  {"
                            "    \"key\":\"%s\","
                            "    \"names\":["
                            "      \"key2\""
                            "    ]"
                            "  }"
                            "]",
                            kServiceValue, kMethodValue, kTestKey, kTestKey2))
                        .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({
          {kTestKey, kTestValue},
          {kTestKey2, kTestValue2},
      }),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(
      DEBUG_LOCATION,
      RpcOptions().set_metadata({{"key1", kTestValue}, {"key2", kTestValue2}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  // No RLS header seen by the backend, since the RLS response didn't set any.
  EXPECT_THAT(backends_[0]->service_.rls_data(), ::testing::ElementsAre());
}

TEST_F(RlsEnd2endTest, NoHeaderMatch) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Request does not have header "key1", so kTestKey will not be added.
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, WildcardMethod) {
  StartBackends(1);
  SetNextResolution(MakeServiceConfigBuilder()
                        .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                                       "  \"service\":\"%s\""
                                                       "}],"
                                                       "\"headers\":["
                                                       "  {"
                                                       "    \"key\":\"%s\","
                                                       "    \"names\":["
                                                       "      \"key1\""
                                                       "    ]"
                                                       "  }"
                                                       "]",
                                                       kServiceValue, kTestKey))
                        .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, NoKeyBuilderForMethod) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"some_other_method\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, HeaderData) {
  const char* kHeaderData = "header_data";
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)},
                       kHeaderData));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_THAT(backends_[0]->service_.rls_data(),
              ::testing::ElementsAre(kHeaderData));
}

TEST_F(RlsEnd2endTest, ExtraKeysAndConstantKeys) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\",\"key2\",\"key3\""
                                         "    ]"
                                         "  }"
                                         "],"
                                         "\"extraKeys\":{"
                                         "  \"host\":\"%s\","
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "},"
                                         "\"constantKeys\":{"
                                         "  \"%s\":\"%s\""
                                         "}",
                                         kServiceValue, kMethodValue, kTestKey,
                                         kHostKey, kServiceKey, kMethodKey,
                                         kConstantKey, kConstantValue))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({
          {kTestKey, kTestValue},
          {kHostKey, kServerName},
          {kServiceKey, kServiceValue},
          {kMethodKey, kMethodValue},
          {kConstantKey, kConstantValue},
      }),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, TwoCacheEntriesWithSameTarget) {
  const char* kTestValue2 = "test_value2";
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue2}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue2}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 2);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
}

TEST_F(RlsEnd2endTest, FailedRlsRequestWithoutDefaultTarget) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  // Send an RPC before we give the RLS server a response.
  // The RLS request will fail, and thus so will the data plane RPC.
  CheckRpcSendFailure(DEBUG_LOCATION,
                      RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_THAT(
      rls_server_->service_.GetUnmatchedRequests(),
      ::testing::ElementsAre(
          // TODO(roth): Change this to use ::testing::ProtoEquals()
          // once that becomes available in OSS.
          ::testing::Property(
              &RouteLookupRequest::DebugString,
              BuildRlsRequest({{kTestKey, kTestValue}}).DebugString())));
  // Now give the RLS server the right response.
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Sleep long enough for backoff to elapse, then try another RPC.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(3));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, FailedRlsRequestWithDefaultTarget) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_default_target(TargetStringForPort(backends_[0]->port_))
          .Build());
  // Don't give the RLS server a response, so the RLS request will fail.
  // The data plane RPC should be sent to the default target.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_THAT(
      rls_server_->service_.GetUnmatchedRequests(),
      ::testing::ElementsAre(
          // TODO(roth): Change this to use ::testing::ProtoEquals()
          // once that becomes available in OSS.
          ::testing::Property(
              &RouteLookupRequest::DebugString,
              BuildRlsRequest({{kTestKey, kTestValue}}).DebugString())));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 0);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, RlsRequestTimeout) {
  StartBackends(2);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_default_target(TargetStringForPort(backends_[1]->port_))
          .set_lookup_service_timeout(2000)
          .Build());
  // RLS server will send a response, but it's longer than the timeout.
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}),
      /*response_delay=*/3000);
  // The data plane RPC should be sent to the default target.
  CheckRpcSendOk(DEBUG_LOCATION, RpcOptions().set_timeout_ms(4000).set_metadata(
                                     {{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 0);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, UpdateConfig) {
  StartBackends(2);
  auto service_config_builder =
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_default_target(TargetStringForPort(backends_[0]->port_));
  SetNextResolution(service_config_builder.Build());
  // Don't give the RLS server a response, so the RLS request will fail.
  // The data plane RPC should be sent to the default target.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_THAT(
      rls_server_->service_.GetUnmatchedRequests(),
      ::testing::ElementsAre(
          // TODO(roth): Change this to use ::testing::ProtoEquals()
          // once that becomes available in OSS.
          ::testing::Property(
              &RouteLookupRequest::DebugString,
              BuildRlsRequest({{kTestKey, kTestValue}}).DebugString())));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 0);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  // Now update the config to point to a new default target.
  service_config_builder.set_default_target(
      TargetStringForPort(backends_[1]->port_));
  SetNextResolution(service_config_builder.Build());
  // Send another RPC, which should go to the new default target.
  // The RLS server will *not* see another request, because the cache
  // entry is still in backoff.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 0);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, CachedResponse) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Send two RPCs.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  // The RLS server should have seen only one request.
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
}

TEST_F(RlsEnd2endTest, StaleCacheEntry) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_max_age(5000)
          .set_stale_age(1000)
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Send one RPC.  RLS server gets a request, and RPC goes to backend.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  // Update RLS server to expect stale request.
  rls_server_->service_.RemoveResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}));
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}},
                      RouteLookupRequest::REASON_STALE),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Wait longer than stale age.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  // Send another RPC.  This should use the stale value but should
  // dispatch a second RLS request.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  // Wait for RLS server to receive the second request.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 2);
}

TEST_F(RlsEnd2endTest, StaleCacheEntryWithHeaderData) {
  const char* kHeaderData = "header_data";
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_max_age(5000)
          .set_stale_age(1000)
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)},
                       kHeaderData));
  // Send one RPC.  RLS server gets a request, and RPC goes to backend.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  // Update RLS server to expect stale request.
  rls_server_->service_.RemoveResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}));
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}},
                      RouteLookupRequest::REASON_STALE, kHeaderData),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)},
                       kHeaderData));
  // Wait longer than stale age.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  // Send another RPC.  This should use the stale value but should
  // dispatch a second RLS request.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  // Wait for RLS server to receive the second request.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 2);
}

TEST_F(RlsEnd2endTest, ExpiredCacheEntry) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .set_max_age(1000)
          .set_lookup_service_timeout(1000)
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  // Send one RPC.  RLS server gets a request, and RPC goes to backend.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  // Remove response from RLS server so that the next RLS request fails.
  rls_server_->service_.RemoveResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}));
  // Wait for cache to be expired.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  // Send another RPC.  This should trigger a second RLS request, but
  // that fails, so the RPC fails.
  CheckRpcSendFailure(DEBUG_LOCATION,
                      RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, CacheSizeLimit) {
  const char* kTestValue2 = "test_value_2";
  StartBackends(2);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue,
                                         kTestKey))
          .set_cache_size_bytes(1)  // Not even big enough for one entry.
          .Build());
  // Set RLS responses for both kTestValue and kTestValue2.
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse({TargetStringForPort(backends_[0]->port_)}));
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue2}}),
      BuildRlsResponse({TargetStringForPort(backends_[1]->port_)}));
  // Send an RPC for kTestValue.
  // RLS server gets a request, and RPC goes to backend.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  // A second RPC for kTestValue should not generate another RLS
  // request, because the cache entry is held by min_eviction_time.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  EXPECT_EQ(backends_[1]->service_.request_count(), 0);
  // Wait for min_eviction_time to elapse.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(6));
  // Send a request for kTestValue2.
  // RLS server gets a request, and RPC goes to backend.
  // This causes the entry for kTestValue to be evicted.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue2}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 2);
  EXPECT_EQ(rls_server_->service_.response_count(), 2);
  EXPECT_EQ(backends_[0]->service_.request_count(), 2);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
  // Send another RPC for kTestValue.
  // This should now trigger a new RLS request.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 3);
  EXPECT_EQ(rls_server_->service_.response_count(), 3);
  EXPECT_EQ(backends_[0]->service_.request_count(), 3);
  EXPECT_EQ(backends_[1]->service_.request_count(), 1);
  // Another RPC for kTestValue2 should still work due to min_eviction_time.
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue2}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 3);
  EXPECT_EQ(rls_server_->service_.response_count(), 3);
  EXPECT_EQ(backends_[0]->service_.request_count(), 3);
  EXPECT_EQ(backends_[1]->service_.request_count(), 2);
}

TEST_F(RlsEnd2endTest, MultipleTargets) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse(
          // First target will report TRANSIENT_FAILURE..
          {"invalid_target", TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
}

TEST_F(RlsEnd2endTest, ConnectivityStateReady) {
  StartBackends(1);
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(/*try_to_connect=*/false));
  rls_server_->service_.SetResponse(
      BuildRlsRequest({{kTestKey, kTestValue}}),
      BuildRlsResponse(
          // One target in TRANSIENT_FAILURE, the other in READY.
          {"invalid_target", TargetStringForPort(backends_[0]->port_)}));
  CheckRpcSendOk(DEBUG_LOCATION,
                 RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(backends_[0]->service_.request_count(), 1);
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(/*try_to_connect=*/false));
}

TEST_F(RlsEnd2endTest, ConnectivityStateIdle) {
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(/*try_to_connect=*/false));
  // RLS server not given any responses, so the request will fail.
  CheckRpcSendFailure(DEBUG_LOCATION);
  // No child policies, so should be IDLE.
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(/*try_to_connect=*/false));
}

TEST_F(RlsEnd2endTest, ConnectivityStateTransientFailure) {
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(/*try_to_connect=*/false));
  rls_server_->service_.SetResponse(BuildRlsRequest({{kTestKey, kTestValue}}),
                                    BuildRlsResponse({"invalid_target"}));
  CheckRpcSendFailure(DEBUG_LOCATION,
                      RpcOptions().set_metadata({{"key1", kTestValue}}));
  EXPECT_EQ(rls_server_->service_.request_count(), 1);
  EXPECT_EQ(rls_server_->service_.response_count(), 1);
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE,
            channel_->GetState(/*try_to_connect=*/false));
}

TEST_F(RlsEnd2endTest, RlsAuthorityDeathTest) {
  GRPC_GTEST_FLAG_SET_DEATH_TEST_STYLE("threadsafe");
  ResetStub("incorrect_authority");
  SetNextResolution(
      MakeServiceConfigBuilder()
          .AddKeyBuilder(absl::StrFormat("\"names\":[{"
                                         "  \"service\":\"%s\","
                                         "  \"method\":\"%s\""
                                         "}],"
                                         "\"headers\":["
                                         "  {"
                                         "    \"key\":\"%s\","
                                         "    \"names\":["
                                         "      \"key1\""
                                         "    ]"
                                         "  }"
                                         "]",
                                         kServiceValue, kMethodValue, kTestKey))
          .Build());
  // Make sure that we blow up (via abort() from the security connector) when
  // the authority for the RLS channel doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED(
      {
        CheckRpcSendOk(DEBUG_LOCATION,
                       RpcOptions().set_metadata({{"key1", kTestValue}}));
      },
      "");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
