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

#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpcpp/security/tls_certificate_provider.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/surface/server.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/util/tls_test_utils.h"

namespace grpc {
namespace testing {

using ::envoy::config::core::v3::HealthStatus;
using ::envoy::service::discovery::v3::DiscoveryRequest;
using ::envoy::service::load_stats::v3::LoadStatsRequest;

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::grpc::experimental::ServerMetricRecorder;
using ::grpc::experimental::StaticDataCertificateProvider;

//
// XdsEnd2endTest::ServerThread::XdsServingStatusNotifier
//

void XdsEnd2endTest::ServerThread::XdsServingStatusNotifier::
    OnServingStatusUpdate(std::string uri, ServingStatusUpdate update) {
  grpc_core::MutexLock lock(&mu_);
  status_map[uri] = update.status;
  cond_.Signal();
}

void XdsEnd2endTest::ServerThread::XdsServingStatusNotifier::
    WaitOnServingStatusChange(std::string uri,
                              grpc::StatusCode expected_status) {
  grpc_core::MutexLock lock(&mu_);
  std::map<std::string, grpc::Status>::iterator it;
  while ((it = status_map.find(uri)) == status_map.end() ||
         it->second.error_code() != expected_status) {
    cond_.Wait(&mu_);
  }
}

//
// XdsEnd2endTest::ServerThread::XdsChannelArgsServerBuilderOption
//

namespace {

// Channel arg pointer vtable for storing xDS channel args in the parent
// channel's channel args.
void* ChannelArgsArgCopy(void* p) {
  auto* args = static_cast<grpc_channel_args*>(p);
  return grpc_channel_args_copy(args);
}
void ChannelArgsArgDestroy(void* p) {
  auto* args = static_cast<grpc_channel_args*>(p);
  grpc_channel_args_destroy(args);
}
int ChannelArgsArgCmp(void* a, void* b) {
  auto* args_a = static_cast<grpc_channel_args*>(a);
  auto* args_b = static_cast<grpc_channel_args*>(b);
  return grpc_channel_args_compare(args_a, args_b);
}
const grpc_arg_pointer_vtable kChannelArgsArgVtable = {
    ChannelArgsArgCopy, ChannelArgsArgDestroy, ChannelArgsArgCmp};

}  // namespace

class XdsEnd2endTest::ServerThread::XdsChannelArgsServerBuilderOption
    : public grpc::ServerBuilderOption {
 public:
  explicit XdsChannelArgsServerBuilderOption(XdsEnd2endTest* test_obj)
      : test_obj_(test_obj) {}

  void UpdateArguments(grpc::ChannelArguments* args) override {
    args->SetString(GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG,
                    test_obj_->bootstrap_);
    args->SetPointerWithVtable(
        GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS,
        &test_obj_->xds_channel_args_, &kChannelArgsArgVtable);
  }

  void UpdatePlugins(
      std::vector<std::unique_ptr<grpc::ServerBuilderPlugin>>* /*plugins*/)
      override {}

 private:
  XdsEnd2endTest* test_obj_;
};

//
// XdsEnd2endTest::ServerThread
//

void XdsEnd2endTest::ServerThread::Start() {
  gpr_log(GPR_INFO, "starting %s server on port %d", Type(), port_);
  GPR_ASSERT(!running_);
  running_ = true;
  StartAllServices();
  grpc_core::Mutex mu;
  // We need to acquire the lock here in order to prevent the notify_one
  // by ServerThread::Serve from firing before the wait below is hit.
  grpc_core::MutexLock lock(&mu);
  grpc_core::CondVar cond;
  thread_ = std::make_unique<std::thread>(
      std::bind(&ServerThread::Serve, this, &mu, &cond));
  cond.Wait(&mu);
  gpr_log(GPR_INFO, "%s server startup complete", Type());
}

void XdsEnd2endTest::ServerThread::Shutdown() {
  if (!running_) return;
  gpr_log(GPR_INFO, "%s about to shutdown", Type());
  ShutdownAllServices();
  server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  thread_->join();
  gpr_log(GPR_INFO, "%s shutdown completed", Type());
  running_ = false;
}

void XdsEnd2endTest::ServerThread::StopListeningAndSendGoaways() {
  gpr_log(GPR_INFO, "%s sending GOAWAYs", Type());
  {
    grpc_core::ExecCtx exec_ctx;
    auto* server = grpc_core::Server::FromC(server_->c_server());
    server->StopListening();
    server->SendGoaways();
  }
  gpr_log(GPR_INFO, "%s done sending GOAWAYs", Type());
}

void XdsEnd2endTest::ServerThread::Serve(grpc_core::Mutex* mu,
                                         grpc_core::CondVar* cond) {
  // We need to acquire the lock here in order to prevent the notify_one
  // below from firing before its corresponding wait is executed.
  grpc_core::MutexLock lock(mu);
  std::string server_address = absl::StrCat("localhost:", port_);
  if (use_xds_enabled_server_) {
    XdsServerBuilder builder;
    if (GetParam().bootstrap_source() ==
        XdsTestType::kBootstrapFromChannelArg) {
      builder.SetOption(
          std::make_unique<XdsChannelArgsServerBuilderOption>(test_obj_));
    }
    builder.set_status_notifier(&notifier_);
    builder.experimental().set_drain_grace_time(
        test_obj_->xds_drain_grace_time_ms_);
    builder.AddListeningPort(server_address, Credentials());
    // Allow gRPC Core's HTTP server to accept PUT requests for testing
    // purposes.
    if (allow_put_requests_) {
      builder.AddChannelArgument(
          GRPC_ARG_DO_NOT_USE_UNLESS_YOU_HAVE_PERMISSION_FROM_GRPC_TEAM_ALLOW_BROKEN_PUT_REQUESTS,
          true);
    }
    RegisterAllServices(&builder);
    server_ = builder.BuildAndStart();
  } else {
    ServerBuilder builder;
    builder.AddListeningPort(server_address, Credentials());
    RegisterAllServices(&builder);
    server_ = builder.BuildAndStart();
  }
  cond->Signal();
}

//
// XdsEnd2endTest::BackendServerThread
//

XdsEnd2endTest::BackendServerThread::BackendServerThread(
    XdsEnd2endTest* test_obj, bool use_xds_enabled_server)
    : ServerThread(test_obj, use_xds_enabled_server) {
  if (use_xds_enabled_server) {
    test_obj->SetServerListenerNameAndRouteConfiguration(
        test_obj->balancer_.get(), test_obj->default_server_listener_, port(),
        test_obj->default_server_route_config_);
  }
}

std::shared_ptr<ServerCredentials>
XdsEnd2endTest::BackendServerThread::Credentials() {
  if (GetParam().use_xds_credentials()) {
    if (use_xds_enabled_server()) {
      // We are testing server's use of XdsServerCredentials
      return XdsServerCredentials(InsecureServerCredentials());
    } else {
      // We are testing client's use of XdsCredentials
      std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
      std::string identity_cert =
          grpc_core::testing::GetFileContents(kServerCertPath);
      std::string private_key =
          grpc_core::testing::GetFileContents(kServerKeyPath);
      std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs = {
          {private_key, identity_cert}};
      auto certificate_provider =
          std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
              root_cert, identity_key_cert_pairs);
      grpc::experimental::TlsServerCredentialsOptions options(
          certificate_provider);
      options.watch_root_certs();
      options.watch_identity_key_cert_pairs();
      options.set_cert_request_type(
          GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
      return grpc::experimental::TlsServerCredentials(options);
    }
  }
  return ServerThread::Credentials();
}

void XdsEnd2endTest::BackendServerThread::RegisterAllServices(
    ServerBuilder* builder) {
  server_metric_recorder_ = ServerMetricRecorder::Create();
  ServerBuilder::experimental_type(builder).EnableCallMetricRecording(
      server_metric_recorder_.get());
  builder->RegisterService(&backend_service_);
  builder->RegisterService(&backend_service1_);
  builder->RegisterService(&backend_service2_);
}

void XdsEnd2endTest::BackendServerThread::StartAllServices() {
  backend_service_.Start();
  backend_service1_.Start();
  backend_service2_.Start();
}

void XdsEnd2endTest::BackendServerThread::ShutdownAllServices() {
  backend_service_.Shutdown();
  backend_service1_.Shutdown();
  backend_service2_.Shutdown();
}

//
// XdsEnd2endTest::BalancerServerThread
//

XdsEnd2endTest::BalancerServerThread::BalancerServerThread(
    XdsEnd2endTest* test_obj)
    : ServerThread(test_obj, /*use_xds_enabled_server=*/false),
      ads_service_(new AdsServiceImpl(
          // First request must have node set with the right client features.
          [&](const DiscoveryRequest& request) {
            EXPECT_TRUE(request.has_node());
            EXPECT_THAT(request.node().client_features(),
                        ::testing::UnorderedElementsAre(
                            "envoy.lb.does_not_support_overprovisioning",
                            "xds.config.resource-in-sotw"));
          },
          // NACKs must use the right status code.
          [&](absl::StatusCode code) {
            EXPECT_EQ(code, absl::StatusCode::kInvalidArgument);
          })),
      lrs_service_(new LrsServiceImpl(
          (GetParam().enable_load_reporting() ? 20 : 0), {kDefaultClusterName},
          // Fail if load reporting is used when not enabled.
          [&]() { EXPECT_TRUE(GetParam().enable_load_reporting()); },
          // Make sure we send the client feature saying that we support
          // send_all_clusters.
          [&](const LoadStatsRequest& request) {
            EXPECT_THAT(
                request.node().client_features(),
                ::testing::Contains("envoy.lrs.supports_send_all_clusters"));
          })) {}

void XdsEnd2endTest::BalancerServerThread::RegisterAllServices(
    ServerBuilder* builder) {
  builder->RegisterService(ads_service_.get());
  builder->RegisterService(lrs_service_.get());
}

void XdsEnd2endTest::BalancerServerThread::StartAllServices() {
  ads_service_->Start();
  lrs_service_->Start();
}

void XdsEnd2endTest::BalancerServerThread::ShutdownAllServices() {
  ads_service_->Shutdown();
  lrs_service_->Shutdown();
}

//
// XdsEnd2endTest::RpcOptions
//

void XdsEnd2endTest::RpcOptions::SetupRpc(ClientContext* context,
                                          EchoRequest* request) const {
  for (const auto& item : metadata) {
    context->AddMetadata(item.first, item.second);
  }
  if (timeout_ms != 0) {
    context->set_deadline(grpc_timeout_milliseconds_to_deadline(timeout_ms));
  }
  if (wait_for_ready) context->set_wait_for_ready(true);
  request->set_message(kRequestMessage);
  if (server_fail) {
    request->mutable_param()->mutable_expected_error()->set_code(
        GRPC_STATUS_FAILED_PRECONDITION);
  }
  if (server_sleep_us != 0) {
    request->mutable_param()->set_server_sleep_us(server_sleep_us);
  }
  if (client_cancel_after_us != 0) {
    request->mutable_param()->set_client_cancel_after_us(
        client_cancel_after_us);
  }
  if (skip_cancelled_check) {
    request->mutable_param()->set_skip_cancelled_check(true);
  }
  if (backend_metrics.has_value()) {
    *request->mutable_param()->mutable_backend_metrics() = *backend_metrics;
  }
}

//
// XdsEnd2endTest
//

const char XdsEnd2endTest::kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
const char XdsEnd2endTest::kServerCertPath[] =
    "src/core/tsi/test_creds/server1.pem";
const char XdsEnd2endTest::kServerKeyPath[] =
    "src/core/tsi/test_creds/server1.key";

const char XdsEnd2endTest::kRequestMessage[] = "Live long and prosper.";

XdsEnd2endTest::XdsEnd2endTest() : balancer_(CreateAndStartBalancer()) {
  // Initialize default client-side xDS resources.
  default_listener_ = XdsResourceUtils::DefaultListener();
  default_route_config_ = XdsResourceUtils::DefaultRouteConfig();
  default_cluster_ = XdsResourceUtils::DefaultCluster();
  if (GetParam().enable_load_reporting()) {
    default_cluster_.mutable_lrs_server()->mutable_self();
  }
  // Initialize client-side resources on balancer.
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   default_route_config_);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  // Initialize default server-side xDS resources.
  default_server_route_config_ = XdsResourceUtils::DefaultServerRouteConfig();
  default_server_listener_ = XdsResourceUtils::DefaultServerListener();
}

void XdsEnd2endTest::TearDown() {
  ShutdownAllBackends();
  balancer_->Shutdown();
  // Clear global xDS channel args, since they will go out of scope
  // when this test object is destroyed.
  grpc_core::internal::SetXdsChannelArgsForTest(nullptr);
  grpc_core::UnsetEnv("GRPC_XDS_BOOTSTRAP");
  grpc_core::UnsetEnv("GRPC_XDS_BOOTSTRAP_CONFIG");
  if (bootstrap_file_ != nullptr) {
    remove(bootstrap_file_);
    gpr_free(bootstrap_file_);
  }
}

std::unique_ptr<XdsEnd2endTest::BalancerServerThread>
XdsEnd2endTest::CreateAndStartBalancer() {
  std::unique_ptr<BalancerServerThread> balancer =
      std::make_unique<BalancerServerThread>(this);
  balancer->Start();
  return balancer;
}

std::vector<XdsEnd2endTest::EdsResourceArgs::Endpoint>
XdsEnd2endTest::CreateEndpointsForBackends(size_t start_index,
                                           size_t stop_index,
                                           HealthStatus health_status,
                                           int lb_weight) {
  if (stop_index == 0) stop_index = backends_.size();
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  for (size_t i = start_index; i < stop_index; ++i) {
    endpoints.emplace_back(CreateEndpoint(i, health_status, lb_weight));
  }
  return endpoints;
}

void XdsEnd2endTest::ResetBackendCounters(size_t start_index,
                                          size_t stop_index) {
  if (stop_index == 0) stop_index = backends_.size();
  for (size_t i = start_index; i < stop_index; ++i) {
    backends_[i]->backend_service()->ResetCounters();
    backends_[i]->backend_service1()->ResetCounters();
    backends_[i]->backend_service2()->ResetCounters();
  }
}

bool XdsEnd2endTest::SeenBackend(size_t backend_idx,
                                 const RpcService rpc_service) {
  switch (rpc_service) {
    case SERVICE_ECHO:
      if (backends_[backend_idx]->backend_service()->request_count() == 0) {
        return false;
      }
      break;
    case SERVICE_ECHO1:
      if (backends_[backend_idx]->backend_service1()->request_count() == 0) {
        return false;
      }
      break;
    case SERVICE_ECHO2:
      if (backends_[backend_idx]->backend_service2()->request_count() == 0) {
        return false;
      }
      break;
  }
  return true;
}

bool XdsEnd2endTest::SeenAllBackends(size_t start_index, size_t stop_index,
                                     const RpcService rpc_service) {
  if (stop_index == 0) stop_index = backends_.size();
  for (size_t i = start_index; i < stop_index; ++i) {
    if (!SeenBackend(i, rpc_service)) {
      return false;
    }
  }
  return true;
}

std::vector<int> XdsEnd2endTest::GetBackendPorts(size_t start_index,
                                                 size_t stop_index) const {
  if (stop_index == 0) stop_index = backends_.size();
  std::vector<int> backend_ports;
  for (size_t i = start_index; i < stop_index; ++i) {
    backend_ports.push_back(backends_[i]->port());
  }
  return backend_ports;
}

void XdsEnd2endTest::InitClient(XdsBootstrapBuilder builder,
                                std::string lb_expected_authority,
                                int xds_resource_does_not_exist_timeout_ms) {
  if (xds_resource_does_not_exist_timeout_ms > 0) {
    xds_channel_args_to_add_.emplace_back(grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS),
        xds_resource_does_not_exist_timeout_ms));
  }
  if (!lb_expected_authority.empty()) {
    constexpr char authority_const[] = "localhost:%d";
    if (lb_expected_authority == authority_const) {
      lb_expected_authority =
          absl::StrFormat(authority_const, balancer_->port());
    }
    xds_channel_args_to_add_.emplace_back(grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS),
        const_cast<char*>(lb_expected_authority.c_str())));
  }
  xds_channel_args_.num_args = xds_channel_args_to_add_.size();
  xds_channel_args_.args = xds_channel_args_to_add_.data();
  // Initialize XdsClient state.
  builder.SetDefaultServer(absl::StrCat("localhost:", balancer_->port()),
                           /*ignore_if_set=*/true);
  bootstrap_ = builder.Build();
  if (GetParam().bootstrap_source() == XdsTestType::kBootstrapFromEnvVar) {
    grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP_CONFIG", bootstrap_.c_str());
  } else if (GetParam().bootstrap_source() == XdsTestType::kBootstrapFromFile) {
    FILE* out = gpr_tmpfile("xds_bootstrap_v3", &bootstrap_file_);
    fputs(bootstrap_.c_str(), out);
    fclose(out);
    grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP", bootstrap_file_);
  }
  if (GetParam().bootstrap_source() != XdsTestType::kBootstrapFromChannelArg) {
    // If getting bootstrap from channel arg, we'll pass these args in
    // via the parent channel args in CreateChannel() instead.
    grpc_core::internal::SetXdsChannelArgsForTest(&xds_channel_args_);
    // Make sure each test creates a new XdsClient instance rather than
    // reusing the one from the previous test.  This avoids spurious failures
    // caused when a load reporting test runs after a non-load reporting test
    // and the XdsClient is still talking to the old LRS server, which fails
    // because it's not expecting the client to connect.  It also
    // ensures that each test can independently set the global channel
    // args for the xDS channel.
    grpc_core::internal::UnsetGlobalXdsClientForTest();
  }
  // Create channel and stub.
  ResetStub();
}

void XdsEnd2endTest::ResetStub(int failover_timeout_ms,
                               ChannelArguments* args) {
  channel_ = CreateChannel(failover_timeout_ms, kServerName, "", args);
  stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  stub1_ = grpc::testing::EchoTest1Service::NewStub(channel_);
  stub2_ = grpc::testing::EchoTest2Service::NewStub(channel_);
}

std::shared_ptr<Channel> XdsEnd2endTest::CreateChannel(
    int failover_timeout_ms, const char* server_name, const char* xds_authority,
    ChannelArguments* args) {
  ChannelArguments local_args;
  if (args == nullptr) args = &local_args;
  // TODO(roth): Remove this once we enable retries by default internally.
  args->SetInt(GRPC_ARG_ENABLE_RETRIES, 1);
  if (failover_timeout_ms > 0) {
    args->SetInt(GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS,
                 failover_timeout_ms * grpc_test_slowdown_factor());
  }
  if (GetParam().bootstrap_source() == XdsTestType::kBootstrapFromChannelArg) {
    // We're getting the bootstrap from a channel arg, so we do the
    // same thing for the response generator to use for the xDS
    // channel and the xDS resource-does-not-exist timeout value.
    args->SetString(GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG,
                    bootstrap_);
    args->SetPointerWithVtable(
        GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS,
        &xds_channel_args_, &kChannelArgsArgVtable);
  }
  std::string uri = absl::StrCat("xds://", xds_authority, "/", server_name);
  std::shared_ptr<ChannelCredentials> channel_creds =
      GetParam().use_xds_credentials()
          ? XdsCredentials(CreateTlsFallbackCredentials())
          : std::make_shared<SecureChannelCredentials>(
                grpc_fake_transport_security_credentials_create());
  return grpc::CreateCustomChannel(uri, channel_creds, *args);
}

Status XdsEnd2endTest::SendRpc(
    const RpcOptions& rpc_options, EchoResponse* response,
    std::multimap<std::string, std::string>* server_initial_metadata) {
  EchoResponse local_response;
  if (response == nullptr) response = &local_response;
  ClientContext context;
  EchoRequest request;
  if (rpc_options.server_expected_error != StatusCode::OK) {
    auto* error = request.mutable_param()->mutable_expected_error();
    error->set_code(rpc_options.server_expected_error);
  }
  rpc_options.SetupRpc(&context, &request);
  Status status;
  switch (rpc_options.service) {
    case SERVICE_ECHO:
      status =
          SendRpcMethod(stub_.get(), rpc_options, &context, request, response);
      break;
    case SERVICE_ECHO1:
      status =
          SendRpcMethod(stub1_.get(), rpc_options, &context, request, response);
      break;
    case SERVICE_ECHO2:
      status =
          SendRpcMethod(stub2_.get(), rpc_options, &context, request, response);
      break;
  }
  if (server_initial_metadata != nullptr) {
    for (const auto& it : context.GetServerInitialMetadata()) {
      std::string header(it.first.data(), it.first.size());
      // Guard against implementation-specific header case - RFC 2616
      absl::AsciiStrToLower(&header);
      server_initial_metadata->emplace(
          header, std::string(it.second.data(), it.second.size()));
    }
  }
  return status;
}

void XdsEnd2endTest::SendRpcsUntil(
    const grpc_core::DebugLocation& debug_location,
    std::function<bool(const RpcResult&)> continue_predicate, int timeout_ms,
    const RpcOptions& rpc_options) {
  absl::Time deadline = absl::InfiniteFuture();
  if (timeout_ms != 0) {
    deadline = absl::Now() +
               (absl::Milliseconds(timeout_ms) * grpc_test_slowdown_factor());
  }
  while (true) {
    RpcResult result;
    result.status = SendRpc(rpc_options, &result.response);
    if (!continue_predicate(result)) return;
    EXPECT_LE(absl::Now(), deadline)
        << debug_location.file() << ":" << debug_location.line();
    if (absl::Now() >= deadline) break;
  }
}

void XdsEnd2endTest::CheckRpcSendOk(
    const grpc_core::DebugLocation& debug_location, const size_t times,
    const RpcOptions& rpc_options) {
  SendRpcsUntil(
      debug_location,
      [debug_location, times, n = size_t{0}](const RpcResult& result) mutable {
        EXPECT_TRUE(result.status.ok())
            << "code=" << result.status.error_code()
            << " message=" << result.status.error_message() << " at "
            << debug_location.file() << ":" << debug_location.line();
        EXPECT_EQ(result.response.message(), kRequestMessage);
        return ++n < times;
      },
      /*timeout_ms=*/0, rpc_options);
}

void XdsEnd2endTest::CheckRpcSendFailure(
    const grpc_core::DebugLocation& debug_location, StatusCode expected_status,
    absl::string_view expected_message_regex, const RpcOptions& rpc_options) {
  const Status status = SendRpc(rpc_options);
  EXPECT_FALSE(status.ok())
      << debug_location.file() << ":" << debug_location.line();
  EXPECT_EQ(expected_status, status.error_code())
      << debug_location.file() << ":" << debug_location.line();
  EXPECT_THAT(status.error_message(),
              ::testing::MatchesRegex(expected_message_regex))
      << debug_location.file() << ":" << debug_location.line();
}

size_t XdsEnd2endTest::SendRpcsAndCountFailuresWithMessage(
    const grpc_core::DebugLocation& debug_location, size_t num_rpcs,
    StatusCode expected_status, absl::string_view expected_message_prefix,
    const RpcOptions& rpc_options) {
  size_t num_failed = 0;
  SendRpcsUntil(
      debug_location,
      [&, n = size_t{0}](const RpcResult& result) mutable {
        if (!result.status.ok()) {
          EXPECT_EQ(result.status.error_code(), expected_status)
              << debug_location.file() << ":" << debug_location.line();
          EXPECT_THAT(result.status.error_message(),
                      ::testing::StartsWith(expected_message_prefix))
              << debug_location.file() << ":" << debug_location.line();
          ++num_failed;
        }
        return ++n < num_rpcs;
      },
      /*timeout_ms=*/0, rpc_options);
  return num_failed;
}

void XdsEnd2endTest::LongRunningRpc::StartRpc(
    grpc::testing::EchoTestService::Stub* stub, const RpcOptions& rpc_options) {
  sender_thread_ = std::thread([this, stub, rpc_options]() {
    EchoRequest request;
    EchoResponse response;
    rpc_options.SetupRpc(&context_, &request);
    status_ = stub->Echo(&context_, request, &response);
  });
}

void XdsEnd2endTest::LongRunningRpc::CancelRpc() {
  context_.TryCancel();
  if (sender_thread_.joinable()) sender_thread_.join();
}

Status XdsEnd2endTest::LongRunningRpc::GetStatus() {
  if (sender_thread_.joinable()) sender_thread_.join();
  return status_;
}

std::vector<XdsEnd2endTest::ConcurrentRpc> XdsEnd2endTest::SendConcurrentRpcs(
    const grpc_core::DebugLocation& debug_location,
    grpc::testing::EchoTestService::Stub* stub, size_t num_rpcs,
    const RpcOptions& rpc_options) {
  // Variables for RPCs.
  std::vector<ConcurrentRpc> rpcs(num_rpcs);
  EchoRequest request;
  // Variables for synchronization
  grpc_core::Mutex mu;
  grpc_core::CondVar cv;
  size_t completed = 0;
  // Set-off callback RPCs
  for (size_t i = 0; i < num_rpcs; i++) {
    ConcurrentRpc* rpc = &rpcs[i];
    rpc_options.SetupRpc(&rpc->context, &request);
    grpc_core::Timestamp t0 = NowFromCycleCounter();
    stub->async()->Echo(&rpc->context, &request, &rpc->response,
                        [rpc, &mu, &completed, &cv, num_rpcs, t0](Status s) {
                          rpc->status = s;
                          rpc->elapsed_time = NowFromCycleCounter() - t0;
                          bool done;
                          {
                            grpc_core::MutexLock lock(&mu);
                            done = (++completed) == num_rpcs;
                          }
                          if (done) cv.Signal();
                        });
  }
  {
    grpc_core::MutexLock lock(&mu);
    cv.Wait(&mu);
  }
  EXPECT_EQ(completed, num_rpcs)
      << " at " << debug_location.file() << ":" << debug_location.line();
  return rpcs;
}

size_t XdsEnd2endTest::WaitForAllBackends(
    const grpc_core::DebugLocation& debug_location, size_t start_index,
    size_t stop_index, std::function<void(const RpcResult&)> check_status,
    const WaitForBackendOptions& wait_options, const RpcOptions& rpc_options) {
  if (check_status == nullptr) {
    check_status = [&](const RpcResult& result) {
      EXPECT_TRUE(result.status.ok())
          << "code=" << result.status.error_code()
          << " message=" << result.status.error_message() << " at "
          << debug_location.file() << ":" << debug_location.line();
    };
  }
  gpr_log(GPR_INFO,
          "========= WAITING FOR BACKENDS [%" PRIuPTR ", %" PRIuPTR
          ") ==========",
          start_index, stop_index);
  size_t num_rpcs = 0;
  SendRpcsUntil(
      debug_location,
      [&](const RpcResult& result) {
        ++num_rpcs;
        check_status(result);
        return !SeenAllBackends(start_index, stop_index, rpc_options.service);
      },
      wait_options.timeout_ms, rpc_options);
  if (wait_options.reset_counters) ResetBackendCounters();
  gpr_log(GPR_INFO, "Backends up; sent %" PRIuPTR " warm up requests",
          num_rpcs);
  return num_rpcs;
}

absl::optional<AdsServiceImpl::ResponseState> XdsEnd2endTest::WaitForNack(
    const grpc_core::DebugLocation& debug_location,
    std::function<absl::optional<AdsServiceImpl::ResponseState>()> get_state,
    const RpcOptions& rpc_options, StatusCode expected_status) {
  absl::optional<AdsServiceImpl::ResponseState> response_state;
  auto deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  auto continue_predicate = [&]() {
    if (absl::Now() >= deadline) {
      return false;
    }
    response_state = get_state();
    return !response_state.has_value() ||
           response_state->state != AdsServiceImpl::ResponseState::NACKED;
  };
  do {
    const Status status = SendRpc(rpc_options);
    EXPECT_EQ(expected_status, status.error_code())
        << "code=" << status.error_code()
        << " message=" << status.error_message() << " at "
        << debug_location.file() << ":" << debug_location.line();
  } while (continue_predicate());
  return response_state;
}

void XdsEnd2endTest::SetProtoDuration(
    grpc_core::Duration duration, google::protobuf::Duration* duration_proto) {
  duration *= grpc_test_slowdown_factor();
  gpr_timespec ts = duration.as_timespec();
  duration_proto->set_seconds(ts.tv_sec);
  duration_proto->set_nanos(ts.tv_nsec);
}

std::string XdsEnd2endTest::MakeConnectionFailureRegex(
    absl::string_view prefix) {
  return absl::StrCat(
      prefix,
      "(UNKNOWN|UNAVAILABLE): (ipv6:%5B::1%5D|ipv4:127.0.0.1):[0-9]+: "
      "(Failed to connect to remote host: )?"
      "(Connection refused|Connection reset by peer|"
      "recvmsg:Connection reset by peer|"
      "getsockopt\\(SO\\_ERROR\\): Connection reset by peer|"
      "Socket closed|FD shutdown)");
}

grpc_core::PemKeyCertPairList XdsEnd2endTest::ReadTlsIdentityPair(
    const char* key_path, const char* cert_path) {
  return grpc_core::PemKeyCertPairList{grpc_core::PemKeyCertPair(
      grpc_core::testing::GetFileContents(key_path),
      grpc_core::testing::GetFileContents(cert_path))};
}

std::shared_ptr<ChannelCredentials>
XdsEnd2endTest::CreateTlsFallbackCredentials() {
  IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key =
      grpc_core::testing::GetFileContents(kServerKeyPath);
  key_cert_pair.certificate_chain =
      grpc_core::testing::GetFileContents(kServerCertPath);
  std::vector<IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      grpc_core::testing::GetFileContents(kCaCertPath),
      identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(std::move(certificate_provider));
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  options.set_certificate_verifier(std::move(verifier));
  options.set_verify_server_certs(true);
  options.set_check_call_host(false);
  auto channel_creds = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_creds.get() != nullptr);
  return channel_creds;
}

}  // namespace testing
}  // namespace grpc
