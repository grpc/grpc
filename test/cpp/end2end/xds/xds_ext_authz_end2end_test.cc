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

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/tls/v3/tls_credentials.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.grpc.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "test/core/test_util/fake_stats_plugin.h"
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
// GrpcService.initial_metadata, sent on the Check call.
constexpr absl::string_view kInitialMetadataKey = "x-ext-authz-metadata";
constexpr absl::string_view kInitialMetadataValue = "metadata-val";

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

// A fake ext_authz service that records every CheckRequest it receives, along
// with the peer and the client metadata of the Check call.  By default, it
// allows every request; tests can install their own handler via
// SetCheckHandler().
class FakeExtAuthzService final
    : public envoy::service::auth::v3::Authorization::Service {
 public:
  using CheckHandler = std::function<grpc::Status(CheckResponse*)>;

  // What the service saw of one Check call.
  struct CheckCall {
    std::string peer;
    std::multimap<std::string, std::string> client_metadata;
  };

  grpc::Status Check(grpc::ServerContext* context, const CheckRequest* request,
                     CheckResponse* response) override {
    CheckHandler handler;
    {
      grpc_core::MutexLock lock(mu_);
      requests_.push_back(*request);
      CheckCall& call = calls_.emplace_back();
      call.peer = context->peer();
      for (const auto& [key, value] : context->client_metadata()) {
        call.client_metadata.emplace(std::string(key.data(), key.size()),
                                     std::string(value.data(), value.size()));
      }
      handler = check_handler_;
    }
    // The handler runs without holding the lock, since it may block.
    if (handler != nullptr) return handler(response);
    response->mutable_status()->set_code(GRPC_STATUS_OK);
    response->mutable_ok_response();
    return grpc::Status::OK;
  }

  void SetCheckHandler(CheckHandler handler) {
    grpc_core::MutexLock lock(mu_);
    check_handler_ = std::move(handler);
  }

  std::vector<CheckRequest> requests() {
    grpc_core::MutexLock lock(mu_);
    return requests_;
  }

  std::vector<CheckCall> calls() {
    grpc_core::MutexLock lock(mu_);
    return calls_;
  }

 private:
  grpc_core::Mutex mu_;
  std::vector<CheckRequest> requests_ ABSL_GUARDED_BY(mu_);
  std::vector<CheckCall> calls_ ABSL_GUARDED_BY(mu_);
  CheckHandler check_handler_ ABSL_GUARDED_BY(mu_);
};

MATCHER_P2(HasCounterDeltas, before, expected_delta, "") {
  return ::testing::ExplainMatchResult(::testing::Eq(expected_delta.allowed),
                                       arg.allowed - before.allowed,
                                       result_listener) &&
         ::testing::ExplainMatchResult(::testing::Eq(expected_delta.denied),
                                       arg.denied - before.denied,
                                       result_listener) &&
         ::testing::ExplainMatchResult(::testing::Eq(expected_delta.failed),
                                       arg.failed - before.failed,
                                       result_listener) &&
         ::testing::ExplainMatchResult(
             ::testing::Eq(expected_delta.filter_disabled),
             arg.filter_disabled - before.filter_disabled, result_listener);
}

class XdsExtAuthzTest : public XdsEnd2endTest {
 protected:
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

  // Decodes %XX escapes.  This is deliberately independent of the production
  // URL-encoding code, so that a bug there can't cancel itself out.
  static std::string PercentDecode(absl::string_view input) {
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

  // Returns the default client or server RouteConfiguration, depending on
  // where the filter is.
  const RouteConfiguration& DefaultRouteConfig() const {
    return GetParam().filter_on_server() ? default_server_route_config_
                                         : default_route_config_;
  }

  // Sets the ext_authz filter's entry in \a typed_per_filter_config.
  static void SetExtAuthzOverride(
      google::protobuf::Map<std::string, google::protobuf::Any>*
          typed_per_filter_config,
      const grpc::protobuf::Message& config) {
    (void)(*typed_per_filter_config)[std::string(kFilterInstanceName)].PackFrom(
        config);
  }

  // Publishes a Listener with the ext_authz config, and \a route_config.
  void SetExtAuthzListener(const ExtAuthz& ext_authz_config,
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
  }

  // Publishes the ext_authz config, initializes the client, and starts the
  // backend.  The ordering matters on the server path: the backend is an
  // xDS-enabled server, so it can only pick up the bootstrap config that
  // InitClient() publishes if it is started afterwards, and it reports a
  // serving status once it has received its Listener.
  void SetExtAuthzListenerAndStart(const ExtAuthz& ext_authz_config,
                                   const RouteConfiguration& route_config,
                                   bool use_tls = false) {
    SetExtAuthzListener(ext_authz_config, route_config, use_tls);
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
    const absl::string_view local_host =
        absl::StripSuffix(absl::StripPrefix(grpc_core::LocalIp(), "["), "]");
    // The destination is the backend's own listening address.
    EXPECT_TRUE(attr.destination().address().has_socket_address());
    const auto& destination = attr.destination().address().socket_address();
    EXPECT_EQ(destination.protocol(), SocketAddress::TCP);
    EXPECT_EQ(destination.address(), local_host);
    EXPECT_EQ(destination.port_value(), backends_[0]->port());
    // The source is the client end of the same connection, which the
    // backend saw as its peer (as a URI, e.g. "ipv4:127.0.0.1:<port>").
    EXPECT_TRUE(attr.source().address().has_socket_address());
    const auto& source = attr.source().address().socket_address();
    EXPECT_EQ(source.protocol(), SocketAddress::TCP);
    EXPECT_EQ(source.address(), local_host);
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

  // Whether the ext_authz filter is disabled in the HttpConnectionManager.
  bool hcm_filter_disabled_ = false;
  // Whether the xDS server is trusted (gRFC A102).  If not, the ext_authz
  // service is added to the bootstrap's allowed_grpc_services.
  bool trusted_xds_server_ = true;

 private:
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
    filter0->set_disabled(hcm_filter_disabled_);
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

  XdsBootstrapBuilder MakeExtAuthzBootstrapBuilder() {
    XdsBootstrapBuilder builder = MakeBootstrapBuilder();
    if (trusted_xds_server_) {
      builder.SetTrustedXdsServer();
    } else {
      builder.AddAllowedGrpcService(
          grpc_core::LocalIpUri(ext_authz_server_->port()));
    }
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

  grpc_core::testing::ScopedExperimentalEnvVar env_var_;
  std::unique_ptr<ExtAuthzServerThread> ext_authz_server_;
};

// Run with the filter on the client and on a server-side filter chain.
// There are no per-route override variants: the contents of ExtAuthzPerRoute
// are ignored by design (gRFC A92), so they would re-run every test without
// changing behavior.  Per-route disabling is covered by
// FilterDisabledPerRoute.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtAuthzTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_filter_on_server()),
    &XdsTestType::Name);

//
// Tests
//

// Verifies basic allow behavior, CheckRequest HTTP fields & timestamp,
// client/server source & destination attributes, and allowed_rpcs metric.
// Note: nothing here delays the filter by a known amount after the RPC starts,
// so explicit start-time serialization is covered by the ExplicitStartTime
// unit test in ext_authz_message_test.cc.
TEST_P(XdsExtAuthzTest, SuccessBasic) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  const gpr_timespec time_before = gpr_now(GPR_CLOCK_REALTIME);
  CheckRpcSendOk(DEBUG_LOCATION);
  const gpr_timespec time_after = gpr_now(GPR_CLOCK_REALTIME);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  const auto& attr = requests[0].attributes();
  EXPECT_EQ(attr.request().http().method(), "POST");
  EXPECT_EQ(attr.request().http().path(), kEchoPath);
  EXPECT_TRUE(attr.request().has_time());
  // Allow some slack, since the start time is recorded with millisecond
  // granularity on a monotonic clock and then converted to wall-clock time.
  EXPECT_GE(attr.request().time().seconds(), time_before.tv_sec - 1);
  EXPECT_LE(attr.request().time().seconds(), time_after.tv_sec + 1);
  if (GetParam().filter_on_server()) {
    ExpectServerSideAddresses(attr);
    EXPECT_THAT(attr.source().principal(), IsEmpty());
    EXPECT_THAT(attr.destination().principal(), IsEmpty());
    EXPECT_THAT(attr.source().certificate(), IsEmpty());
  } else {
    EXPECT_FALSE(attr.has_source());
    EXPECT_FALSE(attr.has_destination());
  }
  EXPECT_THAT(
      GetExtAuthzCounters(*stats_plugin),
      HasCounterDeltas(before, ExtAuthzCounters{/*allowed=*/1,
                                                /*denied=*/0,
                                                /*failed=*/0,
                                                /*filter_disabled=*/0}));
}

// With mTLS on the server filter chain, AttributeContext.source carries the
// client's principal and (with include_peer_certificate) its URL-encoded
// certificate, and .destination carries the server's principal but never a
// certificate (gRFC A92).
TEST_P(XdsExtAuthzTest, MtlsSourceAndDestination) {
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

TEST_P(XdsExtAuthzTest, SuccessWithHeaderMutations) {
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

TEST_P(XdsExtAuthzTest, SuccessFailureModeAllow) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ext_authz_service().SetCheckHandler([](CheckResponse* /*response*/) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        std::string(kUnavailableMessage));
  });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true));
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
  EXPECT_THAT(server_initial_metadata,
              Contains(Pair(kFailureModeAllowedHeader, kTrue)));
  EXPECT_THAT(
      GetExtAuthzCounters(*stats_plugin),
      HasCounterDeltas(before, ExtAuthzCounters{/*allowed=*/1,
                                                /*denied=*/0,
                                                /*failed=*/1,
                                                /*filter_disabled=*/0}));
}

// An error status from the ext_authz service fails the RPC with
// status_on_error and counts as both failed and denied.
TEST_P(XdsExtAuthzTest, ErrorStatusUsesStatusOnError) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ext_authz_service().SetCheckHandler([](CheckResponse* /*response*/) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        std::string(kUnavailableMessage));
  });
  auto ext_authz_config = BuildExtAuthzConfig();
  // HTTP 401 maps to UNAUTHENTICATED.
  ext_authz_config.mutable_status_on_error()->set_code(
      envoy::type::v3::Unauthorized);
  SetExtAuthzListenerAndStart(ext_authz_config);
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAUTHENTICATED,
                      kUnavailableMessage);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_THAT(ext_authz_service().requests(), SizeIs(1));
  EXPECT_THAT(
      GetExtAuthzCounters(*stats_plugin),
      HasCounterDeltas(before, ExtAuthzCounters{/*allowed=*/0,
                                                /*denied=*/1,
                                                /*failed=*/1,
                                                /*filter_disabled=*/0}));
}

// The Check call is bounded by GrpcService.timeout (gRFC A102).  When the
// ext_authz service doesn't respond in time, the RPC fails with
// status_on_error.
TEST_P(XdsExtAuthzTest, CheckTimeoutUsesStatusOnError) {
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

// A denied response fails the RPC with the status that it maps to, and its
// headers are added to the trailing metadata that the client sees.
TEST_P(XdsExtAuthzTest, DeniedWithStatusAndHeaders) {
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
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
  const ExtAuthzCounters before = GetExtAuthzCounters(*stats_plugin);
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
  EXPECT_THAT(
      GetExtAuthzCounters(*stats_plugin),
      HasCounterDeltas(before, ExtAuthzCounters{/*allowed=*/0,
                                                /*denied=*/1,
                                                /*failed=*/0,
                                                /*filter_disabled=*/0}));
}

// A route can disable the filter via the generic FilterConfig wrapper in
// typed_per_filter_config, whereas the contents of ExtAuthzPerRoute,
// including its own disabled field, are ignored (gRFC A92).
TEST_P(XdsExtAuthzTest, FilterDisabledPerRoute) {
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

// A filter that is disabled in the HttpConnectionManager is enabled for a
// route that has a non-disabled override, since the most specific config
// wins (gRFC A92).
TEST_P(XdsExtAuthzTest, HcmDisabledFilterEnabledByRouteOverride) {
  hcm_filter_disabled_ = true;
  RouteConfiguration route_config = DefaultRouteConfig();
  auto* vhost = route_config.mutable_virtual_hosts(0);
  const envoy::config::route::v3::Route default_route = vhost->routes(0);
  vhost->clear_routes();
  // Echo1: the filter is enabled by the route's override.
  auto* echo1_route = vhost->add_routes();
  *echo1_route = default_route;
  echo1_route->mutable_match()->set_path(kEcho1Path);
  SetExtAuthzOverride(echo1_route->mutable_typed_per_filter_config(),
                      ExtAuthzPerRoute());
  // Echo: no override, so the filter stays disabled.
  *vhost->add_routes() = default_route;
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(), route_config);
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_THAT(ext_authz_service().requests(), IsEmpty());
  CheckRpcSendOk(DEBUG_LOCATION, /*times=*/1,
                 RpcOptions().set_rpc_method(METHOD_ECHO1));
  auto requests = ext_authz_service().requests();
  ASSERT_THAT(requests, SizeIs(1));
  EXPECT_EQ(requests[0].attributes().request().http().path(), kEcho1Path);
}

// With a trusted xDS server, the initial_metadata in the GrpcService is sent
// on the Check call (gRFC A102).
TEST_P(XdsExtAuthzTest, TrustedXdsServerSendsInitialMetadata) {
  auto ext_authz_config = BuildExtAuthzConfig();
  auto* header =
      ext_authz_config.mutable_grpc_service()->add_initial_metadata();
  header->set_key(kInitialMetadataKey);
  header->set_value(kInitialMetadataValue);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendOk(DEBUG_LOCATION);
  auto calls = ext_authz_service().calls();
  ASSERT_THAT(calls, SizeIs(1));
  EXPECT_THAT(calls[0].client_metadata,
              Contains(Pair(kInitialMetadataKey, kInitialMetadataValue)));
}

// With an untrusted xDS server, the ext_authz service must be in the
// bootstrap's allowed_grpc_services, whose credentials are used, and the
// initial_metadata in the GrpcService is ignored (gRFC A102).
TEST_P(XdsExtAuthzTest, UntrustedXdsServerUsesAllowedGrpcServices) {
  trusted_xds_server_ = false;
  auto ext_authz_config = BuildExtAuthzConfig();
  // TLS credentials, which would fail against the plaintext ext_authz
  // server, so the RPC would be denied if they were used instead of the
  // insecure credentials from allowed_grpc_services.
  auto* google_grpc =
      ext_authz_config.mutable_grpc_service()->mutable_google_grpc();
  google_grpc->clear_channel_credentials_plugin();
  envoy::extensions::grpc_service::channel_credentials::tls::v3::TlsCredentials
      tls_credentials;
  tls_credentials.mutable_root_certificate_provider()->set_instance_name(
      "file_plugin");
  (void)google_grpc->add_channel_credentials_plugin()->PackFrom(
      tls_credentials);
  auto* header =
      ext_authz_config.mutable_grpc_service()->add_initial_metadata();
  header->set_key(kInitialMetadataKey);
  header->set_value(kInitialMetadataValue);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendOk(DEBUG_LOCATION);
  auto calls = ext_authz_service().calls();
  ASSERT_THAT(calls, SizeIs(1));
  EXPECT_THAT(calls[0].client_metadata,
              Not(Contains(Key(kInitialMetadataKey))));
}

// An LDS update that changes only the timeout and initial_metadata of the
// GrpcService does not recreate the channel to the ext_authz service (gRFC
// A92), so the Check calls keep using the same connection.
TEST_P(XdsExtAuthzTest, LdsUpdateKeepsSideChannel) {
  auto ext_authz_config = BuildExtAuthzConfig();
  auto* header =
      ext_authz_config.mutable_grpc_service()->add_initial_metadata();
  header->set_key(kInitialMetadataKey);
  header->set_value("v1");
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendOk(DEBUG_LOCATION);
  header->set_value("v2");
  SetProtoDuration(grpc_core::Duration::Seconds(20),
                   ext_authz_config.mutable_grpc_service()->mutable_timeout());
  SetExtAuthzListener(ext_authz_config, DefaultRouteConfig());
  // Send RPCs until one is checked with the new config.  The status of the
  // RPCs is ignored, since on the server path the update may drain the
  // data plane connection.
  const absl::Time deadline =
      absl::Now() + absl::Seconds(30 * grpc_test_slowdown_factor());
  while (!::testing::Value(ext_authz_service().calls().back().client_metadata,
                           Contains(Pair(kInitialMetadataKey, "v2")))) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for the update";
    (void)SendRpc();
  }
  auto calls = ext_authz_service().calls();
  for (const auto& call : calls) {
    EXPECT_EQ(call.peer, calls[0].peer);
  }
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
