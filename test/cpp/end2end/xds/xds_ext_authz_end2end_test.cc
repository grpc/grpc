// Copyright 2026 gRPC authors.
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

#include <grpc/support/time.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.grpc.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "envoy/type/v3/percent.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/tls_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::core::v3::SocketAddress;
using ::envoy::extensions::filters::http::ext_authz::v3::ExtAuthz;
using ::envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using ::envoy::service::auth::v3::AttributeContext;
using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::StrCaseEq;

// Name of the ext_authz filter instance in the xDS HttpConnectionManager.
constexpr absl::string_view kFilterInstanceName = "ext_authz";

// Headers.
constexpr absl::string_view kCustomHeaderKey = "x-custom-header";
constexpr absl::string_view kCustomHeaderValue = "custom-value";
constexpr absl::string_view kHeaderToRemove = "x-header-to-remove";
constexpr absl::string_view kHeaderToRemoveValue = "remove-me";
constexpr absl::string_view kResponseHeaderKey = "x-response-header";
constexpr absl::string_view kResponseHeaderValue = "response-val";
constexpr absl::string_view kDeniedTrailerKey = "x-denied-trailer";
constexpr absl::string_view kDeniedTrailerValue = "denied-val";
constexpr absl::string_view kDeniedTrailerKey2 = "x-denied-trailer-2";
constexpr absl::string_view kDeniedTrailerValue2 = "denied-val-2";
constexpr absl::string_view kFailureModeAllowedHeader =
    "x-envoy-auth-failure-mode-allowed";
constexpr absl::string_view kTrue = "true";

// Paths of the EchoTestService methods.
constexpr absl::string_view kEchoPath = "/grpc.testing.EchoTestService/Echo";
constexpr absl::string_view kEcho1Path = "/grpc.testing.EchoTestService/Echo1";
constexpr absl::string_view kEcho2Path = "/grpc.testing.EchoTestService/Echo2";

// Status messages.
constexpr absl::string_view kDeniedMessage = "not authorized";
constexpr absl::string_view kUnavailableMessage =
    "ext_authz service unavailable";

// Paths of the client's certificate and key for mTLS.
constexpr char kClientCertPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffe.pem";
constexpr char kClientKeyPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/client.key";

// A fake ext_authz service that records every CheckRequest it receives.  By
// default, it allows every request; tests can install their own handler via
// SetCheckHandler().
class FakeExtAuthzService final
    : public envoy::service::auth::v3::Authorization::Service {
 public:
  using CheckHandler = std::function<grpc::Status(CheckResponse*)>;

  grpc::Status Check(grpc::ServerContext* /*context*/,
                     const CheckRequest* request,
                     CheckResponse* response) override {
    CheckHandler handler;
    {
      grpc_core::MutexLock lock(&mu_);
      requests_.push_back(*request);
      handler = check_handler_;
    }
    // The handler runs without holding the lock, since it may block.
    if (handler != nullptr) return handler(response);
    response->mutable_status()->set_code(GRPC_STATUS_OK);
    response->mutable_ok_response();
    return grpc::Status::OK;
  }

  void SetCheckHandler(CheckHandler handler) {
    grpc_core::MutexLock lock(&mu_);
    check_handler_ = std::move(handler);
  }

  std::vector<CheckRequest> requests() {
    grpc_core::MutexLock lock(&mu_);
    return requests_;
  }

 private:
  grpc_core::Mutex mu_;
  std::vector<CheckRequest> requests_ ABSL_GUARDED_BY(&mu_);
  CheckHandler check_handler_ ABSL_GUARDED_BY(&mu_);
};

// A CheckHandler that fails the Check call itself, as opposed to returning a
// CheckResponse that denies the request.
grpc::Status FailCheckCall(CheckResponse* /*response*/) {
  return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                      std::string(kUnavailableMessage));
}

// Decodes %XX escapes.  This is deliberately independent of the production
// URL-encoding code, so that a bug there can't cancel itself out.
std::string PercentDecode(absl::string_view input) {
  std::string output;
  for (size_t i = 0; i < input.size(); ++i) {
    int value;
    if (input[i] == '%' && i + 2 < input.size() &&
        absl::SimpleHexAtoi(input.substr(i + 1, 2), &value)) {
      output.push_back(static_cast<char>(value));
      i += 2;
    } else {
      output.push_back(input[i]);
    }
  }
  return output;
}

// Returns grpc_core::LocalIp() without the brackets that it has for IPv6,
// i.e., the form used in SocketAddress.address.
std::string LocalHost() {
  return std::string(
      absl::StripSuffix(absl::StripPrefix(grpc_core::LocalIp(), "["), "]"));
}

class XdsExtAuthzTest : public XdsEnd2endTest {
 protected:
  class ExtAuthzServerThread : public ServerThread {
   public:
    explicit ExtAuthzServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       InsecureServerCredentials()) {}

    FakeExtAuthzService* ext_authz_service() { return &service_; }

   private:
    const char* Type() override { return "ExtAuthz"; }
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&service_);
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    FakeExtAuthzService service_;
  };

  // Snapshot of the four ext_authz counters (gRFC A92).
  struct ExtAuthzCounters {
    uint64_t allowed = 0;
    uint64_t denied = 0;
    uint64_t failed = 0;
    uint64_t filter_disabled = 0;
  };

  XdsExtAuthzTest()
      : env_var_(GetParam().filter_on_server()
                     ? "GRPC_EXPERIMENTAL_XDS_EXT_AUTHZ_ON_SERVER"
                     : "GRPC_EXPERIMENTAL_XDS_EXT_AUTHZ_ON_CLIENT") {}

  void SetUp() override {
    ext_authz_server_ = std::make_unique<ExtAuthzServerThread>(this);
    ext_authz_server_->Start();
    // The backend is not started here: on the server path it is an
    // xDS-enabled server, which must not start until InitClient() has
    // published the bootstrap config and the Listener resource is available.
    // See SetExtAuthzListenerAndStart().
    CreateBackends(1, /*xds_enabled=*/GetParam().filter_on_server(),
                   GetParam().filter_on_server()
                       ? XdsServerCredentials(InsecureServerCredentials())
                       : nullptr);
    EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  }

  void TearDown() override {
    channel_.reset();
    stub_.reset();
    stub1_.reset();
    stub2_.reset();
    if (ext_authz_server_ != nullptr) ext_authz_server_->Shutdown();
    XdsEnd2endTest::TearDown();
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
  }

  ExtAuthz BuildExtAuthzConfig(bool failure_mode_allow = false,
                               bool failure_mode_allow_header_add = false) {
    ExtAuthz ext_authz;
    ext_authz.set_failure_mode_allow(failure_mode_allow);
    ext_authz.set_failure_mode_allow_header_add(failure_mode_allow_header_add);
    auto* grpc_service = ext_authz.mutable_grpc_service();
    SetProtoDuration(grpc_core::Duration::Seconds(10),
                     grpc_service->mutable_timeout());
    auto* google_grpc = grpc_service->mutable_google_grpc();
    google_grpc->set_target_uri(
        grpc_core::LocalIpUri(ext_authz_server_->port()));
    (void)google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::insecure::v3::
            InsecureCredentials());
    return ext_authz;
  }

  Listener BuildListenerWithExtAuthzFilter(const ExtAuthz& ext_authz_config,
                                           bool use_tls) const {
    Listener listener;
    std::unique_ptr<HcmAccessor> hcm_accessor;
    if (GetParam().filter_on_server()) {
      listener = default_server_listener_;
      hcm_accessor = std::make_unique<ServerHcmAccessor>();
    } else {
      listener = default_listener_;
      hcm_accessor = std::make_unique<ClientHcmAccessor>();
    }
    HttpConnectionManager hcm = hcm_accessor->Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    (void)filter0->mutable_typed_config()->PackFrom(ext_authz_config);
    hcm_accessor->Pack(hcm, &listener);
    if (use_tls && GetParam().filter_on_server()) {
      auto* filter_chain = listener.mutable_default_filter_chain();
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");
      DownstreamTlsContext downstream_tls_context;
      downstream_tls_context.mutable_common_tls_context()
          ->mutable_tls_certificate_provider_instance()
          ->set_instance_name("file_plugin");
      downstream_tls_context.mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_ca_certificate_provider_instance()
          ->set_instance_name("file_plugin");
      downstream_tls_context.mutable_require_client_certificate()->set_value(
          true);
      (void)transport_socket->mutable_typed_config()->PackFrom(
          downstream_tls_context);
    }
    return listener;
  }

  // Returns the default client or server RouteConfiguration, depending on
  // where the filter is.
  const RouteConfiguration& DefaultRouteConfig() const {
    return GetParam().filter_on_server() ? default_server_route_config_
                                         : default_route_config_;
  }

  XdsBootstrapBuilder MakeExtAuthzBootstrapBuilder() {
    XdsBootstrapBuilder builder = MakeBootstrapBuilder();
    builder.SetTrustedXdsServer();
    std::vector<std::string> fields;
    fields.push_back(absl::StrFormat("        \"certificate_file\": \"%s\"",
                                     kSpiffeServerCertPath));
    fields.push_back(absl::StrFormat("        \"private_key_file\": \"%s\"",
                                     kSpiffeServerKeyPath));
    fields.push_back(absl::StrFormat("        \"ca_certificate_file\": \"%s\"",
                                     kSpiffeCaCertPath));
    builder.AddCertificateProviderPlugin("file_plugin", "file_watcher",
                                         absl::StrJoin(fields, ",\n"));
    return builder;
  }

  // Publishes the ext_authz config, initializes the client, and starts the
  // backend.  The ordering matters on the server path: the backend is an
  // xDS-enabled server, so it can only pick up the bootstrap config that
  // InitClient() publishes if it is started afterwards, and it reports a
  // serving status once it has received its Listener.
  void SetExtAuthzListenerAndStart(const ExtAuthz& ext_authz_config,
                                   const RouteConfiguration& route_config,
                                   bool use_tls = false) {
    Listener listener =
        BuildListenerWithExtAuthzFilter(ext_authz_config, use_tls);
    if (GetParam().filter_on_server()) {
      SetServerListenerNameAndRouteConfiguration(
          balancer_.get(), std::move(listener), backends_[0]->port(),
          route_config);
    } else {
      SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                       route_config);
    }
    InitClient(
        MakeExtAuthzBootstrapBuilder(), /*lb_expected_authority=*/"",
        /*xds_resource_does_not_exist_timeout_ms=*/0,
        /*balancer_authority_override=*/"", /*args=*/nullptr,
        GetParam().filter_on_server() ? InsecureChannelCredentials() : nullptr);
    StartBackend(0);
    if (GetParam().filter_on_server()) {
      EXPECT_THAT(backends_[0]->GetNextStatus(),
                  ::testing::Optional(absl::OkStatus()));
    }
  }

  void SetExtAuthzListenerAndStart(const ExtAuthz& ext_authz_config,
                                   bool use_tls = false) {
    SetExtAuthzListenerAndStart(ext_authz_config, DefaultRouteConfig(),
                                use_tls);
  }

  FakeExtAuthzService& ext_authz_service() {
    return *ext_authz_server_->ext_authz_service();
  }

  // Returns the value of the ext_authz counter \a name (e.g.,
  // "allowed_rpcs") from the client or server side, depending on where the
  // filter is.  Unset values are returned as 0.
  uint64_t GetExtAuthzCounter(grpc_core::FakeStatsPlugin& stats_plugin,
                              absl::string_view name) {
    const bool on_server = GetParam().filter_on_server();
    const std::string metric_name = absl::StrCat(
        on_server ? "grpc.server_ext_authz." : "grpc.client_ext_authz.", name);
    const std::string target = absl::StrCat("xds:", kServerName);
    std::vector<absl::string_view> labels;
    if (!on_server) labels.push_back(target);
    return stats_plugin.GetUInt64MetricValueByName(metric_name, labels)
        .value_or(0);
  }

  ExtAuthzCounters GetExtAuthzCounters(
      grpc_core::FakeStatsPlugin& stats_plugin) {
    ExtAuthzCounters counters;
    counters.allowed = GetExtAuthzCounter(stats_plugin, "allowed_rpcs");
    counters.denied = GetExtAuthzCounter(stats_plugin, "denied_rpcs");
    counters.failed = GetExtAuthzCounter(stats_plugin, "failed_rpcs");
    counters.filter_disabled =
        GetExtAuthzCounter(stats_plugin, "filter_disabled_rpcs");
    return counters;
  }

  // Expects each counter to have grown by exactly \a expected_delta since
  // \a before was taken.
  void ExpectExtAuthzCounterDeltas(
      const grpc_core::DebugLocation& debug_location,
      grpc_core::FakeStatsPlugin& stats_plugin, const ExtAuthzCounters& before,
      const ExtAuthzCounters& expected_delta) {
    const ExtAuthzCounters after = GetExtAuthzCounters(stats_plugin);
    const std::string location =
        absl::StrCat(debug_location.file(), ":", debug_location.line());
    EXPECT_EQ(after.allowed - before.allowed, expected_delta.allowed)
        << location;
    EXPECT_EQ(after.denied - before.denied, expected_delta.denied) << location;
    EXPECT_EQ(after.failed - before.failed, expected_delta.failed) << location;
    EXPECT_EQ(after.filter_disabled - before.filter_disabled,
              expected_delta.filter_disabled)
        << location;
  }

  // Returns the headers that the filter reported to the ext_authz service in
  // AttributeContext.HttpRequest.header_map. Per gRFC A102, the filter always
  // sets raw_value and never value.
  static std::multimap<std::string, std::string> HeaderMap(
      const CheckRequest& request) {
    std::multimap<std::string, std::string> output;
    for (const auto& header :
         request.attributes().request().http().header_map().headers()) {
      EXPECT_TRUE(header.value().empty()) << header.key();
      output.emplace(header.key(), header.raw_value());
    }
    return output;
  }

  // Checks the source and destination addresses that the server-side filter
  // reported for an RPC that went over a single loopback connection to
  // backend 0.
  void ExpectServerSideAddresses(const AttributeContext& attr) {
    // The destination is the backend's own listening address.
    ASSERT_TRUE(attr.destination().address().has_socket_address());
    const auto& destination = attr.destination().address().socket_address();
    EXPECT_EQ(destination.protocol(), SocketAddress::TCP);
    EXPECT_EQ(destination.address(), LocalHost());
    EXPECT_EQ(destination.port_value(), backends_[0]->port());
    // The source is the client end of the same connection, which the
    // backend saw as its peer (as a URI, e.g. "ipv4:127.0.0.1:<port>").
    ASSERT_TRUE(attr.source().address().has_socket_address());
    const auto& source = attr.source().address().socket_address();
    EXPECT_EQ(source.protocol(), SocketAddress::TCP);
    EXPECT_EQ(source.address(), LocalHost());
    EXPECT_THAT(
        backends_[0]->backend_service()->clients(),
        ElementsAre(StrCaseEq(grpc_core::LocalIpUri(source.port_value()))));
  }

  std::shared_ptr<grpc::Channel> CreateMtlsClientChannel(const char* ca_path,
                                                         const char* cert_path,
                                                         const char* key_path) {
    ChannelArguments args;
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
    IdentityKeyCertPair key_cert_pair;
    key_cert_pair.private_key = grpc_core::testing::GetFileContents(key_path);
    key_cert_pair.certificate_chain =
        grpc_core::testing::GetFileContents(cert_path);
    std::vector<IdentityKeyCertPair> identity_key_cert_pairs;
    identity_key_cert_pairs.emplace_back(std::move(key_cert_pair));
    auto certificate_provider =
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            grpc_core::testing::GetFileContents(ca_path),
            std::move(identity_key_cert_pairs));
    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(std::move(certificate_provider));
    options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    auto verifier =
        ExternalCertificateVerifier::Create<SyncCertificateVerifier>(
            /*success=*/true);
    options.set_certificate_verifier(std::move(verifier));
    options.set_verify_server_certs(true);
    options.set_check_call_host(false);
    return CreateCustomChannel(uri, grpc::experimental::TlsCredentials(options),
                               args);
  }

  grpc_core::testing::ScopedExperimentalEnvVar env_var_;
  std::unique_ptr<ExtAuthzServerThread> ext_authz_server_;
};

// Run with the filter on the client and on a server-side filter chain.
// There are no per-route override variants: the contents of ExtAuthzPerRoute
// are ignored by design (gRFC A92), so they would re-run every test without
// changing behavior.  Per-route disabling is covered by
// ExtAuthzFilterDisabledPerRoute.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtAuthzTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_filter_on_server()),
    &XdsTestType::Name);

//
// Tests
//

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessBasic) {
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  EXPECT_EQ(requests[0].attributes().request().http().method(), "POST");
  EXPECT_EQ(requests[0].attributes().request().http().path(), kEchoPath);
}

// AttributeContext.Request.time is set to a plausible wall-clock time.
// Nothing here delays the filter by a known amount after the RPC starts, so
// this can't tell the RPC's start time (gRFC A92) apart from the time at
// which the filter runs; the serialization of an explicit start time is
// covered by the ExplicitStartTime unit test in ext_authz_message_test.cc.
TEST_P(XdsExtAuthzTest, ExtAuthzRequestTimeIsSet) {
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  const gpr_timespec before = gpr_now(GPR_CLOCK_REALTIME);
  CheckRpcSendOk(DEBUG_LOCATION);
  const gpr_timespec after = gpr_now(GPR_CLOCK_REALTIME);
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  ASSERT_TRUE(requests[0].attributes().request().has_time());
  const auto& time = requests[0].attributes().request().time();
  // Allow some slack, since the start time is recorded with millisecond
  // granularity on a monotonic clock and then converted to wall-clock time.
  EXPECT_GE(time.seconds(), before.tv_sec - 1);
  EXPECT_LE(time.seconds(), after.tv_sec + 1);
}

// AttributeContext.source and .destination are reported on the server side
// only (gRFC A92).
TEST_P(XdsExtAuthzTest, ExtAuthzClientOmitsSourceAndDestination) {
  if (GetParam().filter_on_server()) {
    GTEST_SKIP() << "source and destination are only omitted on the client";
  }
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendOk(DEBUG_LOCATION);
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  EXPECT_FALSE(requests[0].attributes().has_source());
  EXPECT_FALSE(requests[0].attributes().has_destination());
}

// On the server side, AttributeContext.source and .destination carry the
// addresses of the connection.  The fixture's insecure credentials produce
// no X.509 auth context properties, so there are no principals.
TEST_P(XdsExtAuthzTest, ExtAuthzServerSourceAndDestination) {
  if (!GetParam().filter_on_server()) {
    GTEST_SKIP() << "source and destination are only set on the server";
  }
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendOk(DEBUG_LOCATION);
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  const auto& attr = requests[0].attributes();
  ExpectServerSideAddresses(attr);
  EXPECT_THAT(attr.source().principal(), IsEmpty());
  EXPECT_THAT(attr.destination().principal(), IsEmpty());
  EXPECT_THAT(attr.source().certificate(), IsEmpty());
}

// With mTLS on the server filter chain, AttributeContext.source carries the
// client's principal and (with include_peer_certificate) its URL-encoded
// certificate, and .destination carries the server's principal but never a
// certificate (gRFC A92).
TEST_P(XdsExtAuthzTest, ExtAuthzMtlsSourceAndDestination) {
  if (!GetParam().filter_on_server()) {
    GTEST_SKIP() << "source and destination are only set on the server";
  }
  auto config = BuildExtAuthzConfig();
  config.set_include_peer_certificate(true);
  SetExtAuthzListenerAndStart(config, /*use_tls=*/true);
  auto channel = CreateMtlsClientChannel(kSpiffeCaCertPath, kClientCertPath,
                                         kClientKeyPath);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  const auto& attr = requests[0].attributes();
  ExpectServerSideAddresses(attr);
  EXPECT_EQ(attr.source().principal(),
            "spiffe://foo.bar.com/9eebccd2-12bf-40a6-b262-65fe0487d453");
  EXPECT_EQ(attr.destination().principal(),
            "spiffe://example.com/workload/9eebccd2");
  const std::string& certificate = attr.source().certificate();
  EXPECT_THAT(certificate, StartsWith("-----BEGIN%20CERTIFICATE-----%0A"));
  EXPECT_THAT(certificate, Not(HasSubstr("\n")));
  EXPECT_EQ(PercentDecode(certificate),
            grpc_core::testing::GetFileContents(kClientCertPath));
  EXPECT_THAT(attr.destination().certificate(), IsEmpty());
}

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessWithHeaderMutations) {
  ext_authz_service().SetCheckHandler([](CheckResponse* response) {
    response->mutable_status()->set_code(GRPC_STATUS_OK);
    auto* ok = response->mutable_ok_response();
    // Mutations to the client's initial metadata.
    auto* add_hdr = ok->add_headers();
    add_hdr->mutable_header()->set_key(kCustomHeaderKey);
    add_hdr->mutable_header()->set_value(kCustomHeaderValue);
    ok->add_headers_to_remove(kHeaderToRemove);
    // Mutations to the server's initial metadata.
    auto* resp_hdr = ok->add_response_headers_to_add();
    resp_hdr->mutable_header()->set_key(kResponseHeaderKey);
    resp_hdr->mutable_header()->set_value(kResponseHeaderValue);
    return grpc::Status::OK;
  });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  // The backend echoes the client's initial metadata back to us in its own
  // initial metadata, so the mutations to the client's initial metadata and
  // the mutations to the server's initial metadata both show up there.
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(),
               RpcOptions()
                   .set_metadata({{std::string(kHeaderToRemove),
                                   std::string(kHeaderToRemoveValue)}})
                   .set_echo_metadata_initially(true));
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(rpc.GetServerInitialMetadata(),
              AllOf(
                  // Client initial metadata mutations, as seen by the backend.
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)),
                  Not(Contains(Pair(kHeaderToRemove, kHeaderToRemoveValue))),
                  // Server initial metadata mutation, added by the filter.
                  Contains(Pair(kResponseHeaderKey, kResponseHeaderValue))));
  // Server trailing metadata is not mutated for an OK response.
  EXPECT_THAT(rpc.GetServerTrailingMetadata(),
              Not(Contains(Pair(kResponseHeaderKey, kResponseHeaderValue))));
  // The header that the ext_authz service asked us to remove was still
  // visible to the ext_authz service itself.
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  EXPECT_THAT(HeaderMap(requests[0]),
              Contains(Pair(kHeaderToRemove, kHeaderToRemoveValue)));
}

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessFailureModeAllow) {
  ext_authz_service().SetCheckHandler(FailCheckCall);
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true));
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
  EXPECT_THAT(server_initial_metadata,
              Contains(Pair(kFailureModeAllowedHeader, kTrue)));
}

// An error status from the ext_authz service fails the RPC with
// status_on_error.
TEST_P(XdsExtAuthzTest, ExtAuthzErrorStatusUsesStatusOnError) {
  ext_authz_service().SetCheckHandler(FailCheckCall);
  auto ext_authz_config = BuildExtAuthzConfig();
  // HTTP 401 maps to UNAUTHENTICATED.
  ext_authz_config.mutable_status_on_error()->set_code(
      envoy::type::v3::Unauthorized);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAUTHENTICATED,
                      kUnavailableMessage);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
}

// The Check call is bounded by GrpcService.timeout (gRFC A102).  When the
// ext_authz service doesn't respond in time, the RPC fails with
// status_on_error, or is allowed with failure_mode_allow.
TEST_P(XdsExtAuthzTest, ExtAuthzCheckTimeoutUsesStatusOnError) {
  auto unblock = std::make_shared<grpc_core::Notification>();
  ext_authz_service().SetCheckHandler([unblock](CheckResponse* /*response*/) {
    // Blocks until the test is done, but not forever.
    unblock->WaitForNotificationWithTimeout(
        absl::Seconds(10 * grpc_test_slowdown_factor()));
    return grpc::Status::OK;
  });
  auto ext_authz_config = BuildExtAuthzConfig();
  SetProtoDuration(grpc_core::Duration::Milliseconds(500),
                   ext_authz_config.mutable_grpc_service()->mutable_timeout());
  SetExtAuthzListenerAndStart(ext_authz_config);
  // The default status_on_error is HTTP 403, i.e., PERMISSION_DENIED.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "Deadline Exceeded");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  unblock->Notify();
}

TEST_P(XdsExtAuthzTest, ExtAuthzCheckTimeoutFailureModeAllow) {
  auto unblock = std::make_shared<grpc_core::Notification>();
  ext_authz_service().SetCheckHandler([unblock](CheckResponse* /*response*/) {
    // Blocks until the test is done, but not forever.
    unblock->WaitForNotificationWithTimeout(
        absl::Seconds(10 * grpc_test_slowdown_factor()));
    return grpc::Status::OK;
  });
  auto ext_authz_config = BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true);
  SetProtoDuration(grpc_core::Duration::Milliseconds(500),
                   ext_authz_config.mutable_grpc_service()->mutable_timeout());
  SetExtAuthzListenerAndStart(ext_authz_config);
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(server_initial_metadata,
              Contains(Pair(kFailureModeAllowedHeader, kTrue)));
  unblock->Notify();
}

// When the ext_authz service can't be reached at all, the RPC fails with
// status_on_error, or is allowed with failure_mode_allow.
TEST_P(XdsExtAuthzTest, ExtAuthzServerUnreachableUsesStatusOnError) {
  auto ext_authz_config = BuildExtAuthzConfig();
  ext_authz_config.mutable_grpc_service()
      ->mutable_google_grpc()
      ->set_target_uri(grpc_core::LocalIpUri(grpc_pick_unused_port_or_die()));
  SetExtAuthzListenerAndStart(ext_authz_config);
  // The default status_on_error is HTTP 403, i.e., PERMISSION_DENIED.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "failed to connect to all addresses.*");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_THAT(ext_authz_service().requests(), IsEmpty());
}

TEST_P(XdsExtAuthzTest, ExtAuthzServerUnreachableFailureModeAllow) {
  auto ext_authz_config = BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true);
  ext_authz_config.mutable_grpc_service()
      ->mutable_google_grpc()
      ->set_target_uri(grpc_core::LocalIpUri(grpc_pick_unused_port_or_die()));
  SetExtAuthzListenerAndStart(ext_authz_config);
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(server_initial_metadata,
              Contains(Pair(kFailureModeAllowedHeader, kTrue)));
}

// A denied response fails the RPC with the status that it maps to, and its
// headers are added to the trailing metadata that the client sees.
TEST_P(XdsExtAuthzTest, ExtAuthzDeniedWithStatusAndHeaders) {
  ext_authz_service().SetCheckHandler([](CheckResponse* response) {
    response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
    response->mutable_status()->set_message(kDeniedMessage);
    auto* denied = response->mutable_denied_response();
    // HTTP 401 maps to UNAUTHENTICATED.
    denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
    auto* header1 = denied->add_headers();
    header1->mutable_header()->set_key(kDeniedTrailerKey);
    header1->mutable_header()->set_value(kDeniedTrailerValue);
    auto* header2 = denied->add_headers();
    header2->mutable_header()->set_key(kDeniedTrailerKey2);
    header2->mutable_header()->set_value(kDeniedTrailerValue2);
    return grpc::Status::OK;
  });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  Status status = rpc.GetStatus();
  EXPECT_EQ(status.error_code(), StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(status.error_message(),
            absl::StrCat("ExtAuthz request is denied, error message: ",
                         kDeniedMessage));
  if (GetParam().filter_on_server()) {
    EXPECT_THAT(
        rpc.GetServerTrailingMetadata(),
        AllOf(Contains(Pair(kDeniedTrailerKey, kDeniedTrailerValue)),
              Contains(Pair(kDeniedTrailerKey2, kDeniedTrailerValue2))));
  } else {
    // When the filter runs on the client and denies the RPC, the call is
    // aborted locally before sending on the wire, so no server trailing
    // metadata is received.
    EXPECT_THAT(rpc.GetServerTrailingMetadata(), IsEmpty());
  }
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
}

// A denied response whose headers are rejected by decoder_header_mutation_rules
// fails the RPC with status_on_error rather than with the denied status.
TEST_P(XdsExtAuthzTest, ExtAuthzDeniedDisallowedHeaderMutation) {
  ext_authz_service().SetCheckHandler([](CheckResponse* response) {
    response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
    auto* denied = response->mutable_denied_response();
    denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
    auto* header = denied->add_headers();
    header->mutable_header()->set_key(kDeniedTrailerKey);
    header->mutable_header()->set_value(kDeniedTrailerValue);
    return grpc::Status::OK;
  });
  auto ext_authz_config = BuildExtAuthzConfig();
  // HTTP 429 maps to UNAVAILABLE.
  ext_authz_config.mutable_status_on_error()->set_code(
      envoy::type::v3::TooManyRequests);
  auto* rules = ext_authz_config.mutable_decoder_header_mutation_rules();
  rules->mutable_disallow_all()->set_value(true);
  rules->mutable_disallow_is_error()->set_value(true);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("Forbidden header mutation: ", kDeniedTrailerKey));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
}

// A route can disable the filter via the generic FilterConfig wrapper in
// typed_per_filter_config, whereas the contents of ExtAuthzPerRoute,
// including its own disabled field, are ignored (gRFC A92).
TEST_P(XdsExtAuthzTest, ExtAuthzFilterDisabledPerRoute) {
  RouteConfiguration route_config = DefaultRouteConfig();
  auto* vhost = route_config.mutable_virtual_hosts(0);
  const envoy::config::route::v3::Route default_route = vhost->routes(0);
  vhost->clear_routes();
  ExtAuthzPerRoute per_route;
  per_route.set_disabled(true);
  // Echo1: filter disabled by the FilterConfig wrapper.
  auto* echo1_route = vhost->add_routes();
  *echo1_route = default_route;
  echo1_route->mutable_match()->set_path(kEcho1Path);
  envoy::config::route::v3::FilterConfig filter_config;
  (void)filter_config.mutable_config()->PackFrom(per_route);
  filter_config.set_disabled(true);
  auto& echo1_overrides = *echo1_route->mutable_typed_per_filter_config();
  (void)echo1_overrides[std::string(kFilterInstanceName)].PackFrom(
      filter_config);
  // Echo2: ExtAuthzPerRoute's disabled field, which is ignored.
  auto* echo2_route = vhost->add_routes();
  *echo2_route = default_route;
  echo2_route->mutable_match()->set_path(kEcho2Path);
  auto& echo2_overrides = *echo2_route->mutable_typed_per_filter_config();
  (void)echo2_overrides[std::string(kFilterInstanceName)].PackFrom(per_route);
  // Echo: no override.
  *vhost->add_routes() = default_route;
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(), route_config);
  CheckRpcSendOk(DEBUG_LOCATION, /*times=*/1,
                 RpcOptions().set_rpc_method(METHOD_ECHO1));
  EXPECT_THAT(ext_authz_service().requests(), IsEmpty());
  CheckRpcSendOk(DEBUG_LOCATION, /*times=*/1,
                 RpcOptions().set_rpc_method(METHOD_ECHO2));
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 3);
  std::vector<std::string> checked_paths;
  for (const auto& request : ext_authz_service().requests()) {
    checked_paths.push_back(request.attributes().request().http().path());
  }
  EXPECT_THAT(checked_paths, ElementsAre(kEcho2Path, kEchoPath));
}

//
// Metrics
//

// The counters are compared against their values before the RPC, since their
// storage may be shared with earlier tests that used the same target.

TEST_P(XdsExtAuthzTest, ExtAuthzAllowedRpcsMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  CheckRpcSendOk(DEBUG_LOCATION);
  ExpectExtAuthzCounterDeltas(DEBUG_LOCATION, *stats_plugin, before,
                              {/*allowed=*/1, /*denied=*/0, /*failed=*/0,
                               /*filter_disabled=*/0});
}

TEST_P(XdsExtAuthzTest, ExtAuthzDeniedRpcsMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ext_authz_service().SetCheckHandler([](CheckResponse* response) {
    response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
    response->mutable_denied_response();
    return grpc::Status::OK;
  });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  // Without a denied_response status, the RPC fails with PERMISSION_DENIED.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "ExtAuthz request is denied");
  ExpectExtAuthzCounterDeltas(DEBUG_LOCATION, *stats_plugin, before,
                              {/*allowed=*/0, /*denied=*/1, /*failed=*/0,
                               /*filter_disabled=*/0});
}

// With filter_enabled at 0%, RPCs bypass the ext_authz service and count as
// neither allowed nor denied.
TEST_P(XdsExtAuthzTest, ExtAuthzFilterDisabledRpcsMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  auto ext_authz_config = BuildExtAuthzConfig();
  auto* default_value =
      ext_authz_config.mutable_filter_enabled()->mutable_default_value();
  default_value->set_numerator(0);
  default_value->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  SetExtAuthzListenerAndStart(ext_authz_config);
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  CheckRpcSendOk(DEBUG_LOCATION);
  ExpectExtAuthzCounterDeltas(DEBUG_LOCATION, *stats_plugin, before,
                              {/*allowed=*/0, /*denied=*/0, /*failed=*/0,
                               /*filter_disabled=*/1});
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(ext_authz_service().requests(), IsEmpty());
}

TEST_P(XdsExtAuthzTest, ExtAuthzFailedRpcsMetric) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ext_authz_service().SetCheckHandler(FailCheckCall);
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  // The default status_on_error is HTTP 403, i.e., PERMISSION_DENIED.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      kUnavailableMessage);
  ExpectExtAuthzCounterDeltas(DEBUG_LOCATION, *stats_plugin, before,
                              {/*allowed=*/0, /*denied=*/0, /*failed=*/1,
                               /*filter_disabled=*/0});
}

// With failure_mode_allow, an RPC whose ext_authz call fails counts as both
// failed and allowed.  Without failure_mode_allow_header_add, the RPC does
// not get the x-envoy-auth-failure-mode-allowed header.
TEST_P(XdsExtAuthzTest, ExtAuthzFailedRpcsMetricWithFailureModeAllow) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ext_authz_service().SetCheckHandler(FailCheckCall);
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/false));
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              Not(Contains(Key(kFailureModeAllowedHeader))));
  ExpectExtAuthzCounterDeltas(DEBUG_LOCATION, *stats_plugin, before,
                              {/*allowed=*/1, /*denied=*/0, /*failed=*/1,
                               /*filter_disabled=*/0});
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
