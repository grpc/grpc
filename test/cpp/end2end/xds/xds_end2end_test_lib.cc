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

#include <grpcpp/security/tls_certificate_provider.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/server/server.h"
#include "src/core/util/env.h"
#include "src/core/util/tmpfile.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/xds_client/xds_channel_args.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/util/credentials.h"
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

bool XdsEnd2endTest::ServerThread::XdsServingStatusNotifier::
    WaitOnServingStatusChange(const std::string& uri,
                              grpc::StatusCode expected_status,
                              absl::Duration timeout) {
  grpc_core::MutexLock lock(&mu_);
  absl::Time deadline = absl::Now() + timeout * grpc_test_slowdown_factor();
  std::map<std::string, grpc::Status>::iterator it;
  while ((it = status_map.find(uri)) == status_map.end() ||
         it->second.error_code() != expected_status) {
    if (cond_.WaitWithDeadline(&mu_, deadline)) {
      LOG(ERROR) << "\nTimeout Elapsed waiting on serving status "
                    "change\nExpected status: "
                 << expected_status << "\nActual:"
                 << (it == status_map.end()
                         ? "Entry not found in map"
                         : absl::StrCat(it->second.error_code()));
      return false;
    }
  }
  return true;
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

XdsEnd2endTest::ServerThread::ServerThread(
    XdsEnd2endTest* test_obj, bool use_xds_enabled_server,
    std::shared_ptr<ServerCredentials> credentials)
    : test_obj_(test_obj),
      use_xds_enabled_server_(use_xds_enabled_server),
      credentials_(credentials == nullptr ? CreateFakeServerCredentials()
                                          : std::move(credentials)),
      port_(grpc_pick_unused_port_or_die()) {}

void XdsEnd2endTest::ServerThread::Start() {
  LOG(INFO) << "starting " << Type() << " server on port " << port_;
  CHECK(!running_);
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
  LOG(INFO) << Type() << " server startup complete";
}

void XdsEnd2endTest::ServerThread::Shutdown() {
  if (!running_) return;
  LOG(INFO) << Type() << " about to shutdown";
  ShutdownAllServices();
  server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  thread_->join();
  LOG(INFO) << Type() << " shutdown completed";
  running_ = false;
}

void XdsEnd2endTest::ServerThread::StopListeningAndSendGoaways() {
  LOG(INFO) << Type() << " sending GOAWAYs";
  {
    grpc_core::ExecCtx exec_ctx;
    auto* server = grpc_core::Server::FromC(server_->c_server());
    server->StopListening();
    server->SendGoaways();
  }
  LOG(INFO) << Type() << " done sending GOAWAYs";
}

void XdsEnd2endTest::ServerThread::StopListening() {
  LOG(INFO) << Type() << " about to stop listening";
  {
    grpc_core::ExecCtx exec_ctx;
    auto* server = grpc_core::Server::FromC(server_->c_server());
    server->StopListening();
  }
  LOG(INFO) << Type() << " stopped listening";
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
    builder.AddListeningPort(server_address, credentials_);
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
    builder.AddListeningPort(server_address, credentials_);
    RegisterAllServices(&builder);
    server_ = builder.BuildAndStart();
  }
  cond->Signal();
}

//
// XdsEnd2endTest::BackendServerThread
//

XdsEnd2endTest::BackendServerThread::BackendServerThread(
    XdsEnd2endTest* test_obj, bool use_xds_enabled_server,
    std::shared_ptr<ServerCredentials> credentials)
    : ServerThread(test_obj, use_xds_enabled_server, std::move(credentials)) {
  if (use_xds_enabled_server) {
    test_obj->SetServerListenerNameAndRouteConfiguration(
        test_obj->balancer_.get(), test_obj->default_server_listener_, port(),
        test_obj->default_server_route_config_);
  }
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
    XdsEnd2endTest* test_obj, absl::string_view debug_label,
    std::shared_ptr<ServerCredentials> credentials)
    : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                   std::move(credentials)),
      ads_service_(new AdsServiceImpl(
          // First request must have node set with the right client
          // features.
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
          },
          debug_label)),
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
          },
          debug_label)) {}

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
  for (const auto& [key, value] : metadata) {
    context->AddMetadata(key, value);
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
  if (server_notify_client_when_started) {
    request->mutable_param()->set_server_notify_client_when_started(true);
  }
  if (echo_host_from_authority_header) {
    request->mutable_param()->set_echo_host_from_authority_header(true);
  }
  if (echo_metadata_initially) {
    request->mutable_param()->set_echo_metadata_initially(true);
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

XdsEnd2endTest::XdsEnd2endTest(
    std::shared_ptr<ServerCredentials> balancer_credentials)
    : scoped_event_engine_(
          grpc_event_engine::experimental::CreateEventEngine()),
      balancer_(CreateAndStartBalancer("Default Balancer",
                                       std::move(balancer_credentials))) {
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
XdsEnd2endTest::CreateAndStartBalancer(
    absl::string_view debug_label,
    std::shared_ptr<ServerCredentials> credentials) {
  std::unique_ptr<BalancerServerThread> balancer =
      std::make_unique<BalancerServerThread>(this, debug_label,
                                             std::move(credentials));
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

void XdsEnd2endTest::InitClient(
    std::optional<XdsBootstrapBuilder> builder,
    std::string lb_expected_authority,
    int xds_resource_does_not_exist_timeout_ms,
    std::string balancer_authority_override, ChannelArguments* args,
    std::shared_ptr<ChannelCredentials> credentials) {
  if (!builder.has_value()) {
    builder = MakeBootstrapBuilder();
  }
  if (xds_resource_does_not_exist_timeout_ms > 0) {
    xds_channel_args_to_add_.emplace_back(grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS),
        xds_resource_does_not_exist_timeout_ms * grpc_test_slowdown_factor()));
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
  if (!balancer_authority_override.empty()) {
    xds_channel_args_to_add_.emplace_back(grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
        const_cast<char*>(balancer_authority_override.c_str())));
  }
  xds_channel_args_.num_args = xds_channel_args_to_add_.size();
  xds_channel_args_.args = xds_channel_args_to_add_.data();
  bootstrap_ = builder->Build();
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
    grpc_core::internal::UnsetGlobalXdsClientsForTest();
  }
  // Create channel and stub.
  ResetStub(/*failover_timeout_ms=*/0, args, std::move(credentials));
}

void XdsEnd2endTest::ResetStub(
    int failover_timeout_ms, ChannelArguments* args,
    std::shared_ptr<ChannelCredentials> credentials) {
  channel_ = CreateChannel(failover_timeout_ms, kServerName, "", args,
                           std::move(credentials));
  stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  stub1_ = grpc::testing::EchoTest1Service::NewStub(channel_);
  stub2_ = grpc::testing::EchoTest2Service::NewStub(channel_);
}

std::shared_ptr<Channel> XdsEnd2endTest::CreateChannel(
    int failover_timeout_ms, const char* server_name, const char* xds_authority,
    ChannelArguments* args, std::shared_ptr<ChannelCredentials> credentials) {
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
  // Construct target URI.
  std::vector<absl::string_view> parts = {"xds:"};
  if (xds_authority != nullptr && xds_authority[0] != '\0') {
    parts.emplace_back("//");
    parts.emplace_back(xds_authority);
    parts.emplace_back("/");
  }
  parts.emplace_back(server_name);
  std::string uri = absl::StrJoin(parts, "");
  // Credentials defaults to fake credentials.
  if (credentials == nullptr) {
    credentials = std::make_shared<FakeTransportSecurityChannelCredentials>();
  }
  return grpc::CreateCustomChannel(uri, credentials, *args);
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
    for (const auto& [key, value] : context.GetServerInitialMetadata()) {
      std::string header(key.data(), key.size());
      // Guard against implementation-specific header case - RFC 2616
      absl::AsciiStrToLower(&header);
      server_initial_metadata->emplace(header,
                                       std::string(value.data(), value.size()));
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

void XdsEnd2endTest::SendRpcsUntilFailure(
    const grpc_core::DebugLocation& debug_location, StatusCode expected_status,
    absl::string_view expected_message_regex, int timeout_ms,
    const RpcOptions& rpc_options) {
  SendRpcsUntil(
      debug_location,
      [&](const RpcResult& result) {
        // Might still succeed if channel hasn't yet seen the server go down.
        if (result.status.ok()) return true;
        // RPC failed.  Make sure the failure status is as expected and stop.
        EXPECT_EQ(result.status.error_code(), expected_status)
            << debug_location.file() << ":" << debug_location.line();
        EXPECT_THAT(result.status.error_message(),
                    ::testing::MatchesRegex(expected_message_regex))
            << debug_location.file() << ":" << debug_location.line();
        return false;
      },
      timeout_ms, rpc_options);
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

std::vector<std::unique_ptr<XdsEnd2endTest::ConcurrentRpc>>
XdsEnd2endTest::SendConcurrentRpcs(
    const grpc_core::DebugLocation& debug_location,
    grpc::testing::EchoTestService::Stub* stub, size_t num_rpcs,
    const RpcOptions& rpc_options) {
  // Variables for RPCs.
  std::vector<std::unique_ptr<ConcurrentRpc>> rpcs;
  rpcs.reserve(num_rpcs);
  EchoRequest request;
  // Variables for synchronization
  grpc_core::Mutex mu;
  grpc_core::CondVar cv;
  size_t completed = 0;
  // Set-off callback RPCs
  for (size_t i = 0; i < num_rpcs; ++i) {
    auto rpc = std::make_unique<ConcurrentRpc>();
    rpc_options.SetupRpc(&rpc->context, &request);
    grpc_core::Timestamp t0 = NowFromCycleCounter();
    stub->async()->Echo(
        &rpc->context, &request, &rpc->response,
        [rpc = rpc.get(), &mu, &completed, &cv, num_rpcs, t0](Status s) {
          rpc->status = s;
          rpc->elapsed_time = NowFromCycleCounter() - t0;
          bool done;
          {
            grpc_core::MutexLock lock(&mu);
            done = (++completed) == num_rpcs;
          }
          if (done) cv.Signal();
        });
    rpcs.push_back(std::move(rpc));
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
  LOG(INFO) << "========= WAITING FOR BACKENDS [" << start_index << ", "
            << stop_index << ") ==========";
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
  LOG(INFO) << "Backends up; sent " << num_rpcs << " warm up requests";
  return num_rpcs;
}

std::optional<AdsServiceImpl::ResponseState> XdsEnd2endTest::WaitForNack(
    const grpc_core::DebugLocation& debug_location,
    std::function<std::optional<AdsServiceImpl::ResponseState>()> get_state,
    const RpcOptions& rpc_options, StatusCode expected_status) {
  std::optional<AdsServiceImpl::ResponseState> response_state;
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
    absl::string_view prefix, bool has_resolution_note) {
  return absl::StrCat(
      prefix,
      "(UNKNOWN|UNAVAILABLE): "
      // IP address
      "(ipv6:%5B::1%5D|ipv4:127.0.0.1):[0-9]+: "
      // Prefixes added for context
      "(Failed to connect to remote host: )?"
      "(Timeout occurred: )?"
      // Parenthetical wrappers
      "((Secure read failed|"
      "Handshake read failed|"
      "Delayed close due to in-progress write) \\()?"
      // Syscall
      "((connect|sendmsg|recvmsg|getsockopt\\(SO\\_ERROR\\)): ?)?"
      // strerror() output or other message
      "(Connection refused"
      "|Connection reset by peer"
      "|Socket closed"
      "|Broken pipe"
      "|FD shutdown"
      "|Endpoint closing)"
      // errno value
      "( \\([0-9]+\\))?"
      // close paren from wrappers above
      "\\)?",
      // xDS node ID
      has_resolution_note ? " \\(xDS node ID:xds_end2end_test\\)" : "");
}

std::string XdsEnd2endTest::MakeTlsHandshakeFailureRegex(
    absl::string_view prefix) {
  return absl::StrCat(
      prefix,
      "(UNKNOWN|UNAVAILABLE): "
      // IP address
      "(ipv6:%5B::1%5D|ipv4:127.0.0.1):[0-9]+: "
      // Prefixes added for context
      "(Failed to connect to remote host: )?"
      // Tls handshake failure
      "Tls handshake failed \\(TSI_PROTOCOL_FAILURE\\): SSL_ERROR_SSL: "
      "error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED"
      // Detailed reason for certificate verify failure
      "(: .*)?");
}

grpc_core::PemKeyCertPairList XdsEnd2endTest::ReadTlsIdentityPair(
    const char* key_path, const char* cert_path) {
  return grpc_core::PemKeyCertPairList{grpc_core::PemKeyCertPair(
      grpc_core::testing::GetFileContents(key_path),
      grpc_core::testing::GetFileContents(cert_path))};
}

std::vector<experimental::IdentityKeyCertPair>
XdsEnd2endTest::MakeIdentityKeyCertPairForTlsCreds() {
  std::string identity_cert =
      grpc_core::testing::GetFileContents(kServerCertPath);
  std::string private_key = grpc_core::testing::GetFileContents(kServerKeyPath);
  return {{std::move(private_key), std::move(identity_cert)}};
}

std::shared_ptr<ChannelCredentials>
XdsEnd2endTest::CreateXdsChannelCredentials() {
  return XdsCredentials(CreateTlsChannelCredentials());
}

std::shared_ptr<ChannelCredentials>
XdsEnd2endTest::CreateTlsChannelCredentials() {
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      grpc_core::testing::GetFileContents(kCaCertPath),
      MakeIdentityKeyCertPairForTlsCreds());
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(std::move(certificate_provider));
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  options.set_certificate_verifier(std::move(verifier));
  options.set_verify_server_certs(true);
  options.set_check_call_host(false);
  return grpc::experimental::TlsCredentials(options);
}

std::shared_ptr<ServerCredentials>
XdsEnd2endTest::CreateFakeServerCredentials() {
  return std::make_shared<SecureServerCredentials>(
      grpc_fake_transport_security_server_credentials_create());
}

std::shared_ptr<ServerCredentials>
XdsEnd2endTest::CreateMtlsServerCredentials() {
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          std::move(root_cert), MakeIdentityKeyCertPairForTlsCreds());
  grpc::experimental::TlsServerCredentialsOptions options(
      std::move(certificate_provider));
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  options.set_cert_request_type(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<ServerCredentials>
XdsEnd2endTest::CreateTlsServerCredentials() {
  auto certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          MakeIdentityKeyCertPairForTlsCreds());
  grpc::experimental::TlsServerCredentialsOptions options(
      std::move(certificate_provider));
  options.watch_identity_key_cert_pairs();
  return grpc::experimental::TlsServerCredentials(options);
}

}  // namespace testing
}  // namespace grpc
