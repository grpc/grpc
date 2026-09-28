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

#include <grpcpp/client_context.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.grpc.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "envoy/type/v3/percent.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/tls_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Not;
using ::testing::Pair;

constexpr absl::string_view kFilterInstanceName = "ext_authz";
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
constexpr absl::string_view kEchoMethod = "/grpc.testing.EchoTestService/Echo";
constexpr absl::string_view kDeniedMessage = "not authorized";

class XdsExtAuthzTest : public XdsEnd2endTest {
 protected:
  // Fake implementation of the ext_authz service.  By default, it allows
  // every request; tests can install their own handler via SetCheckHandler().
  class ExtAuthzServiceImpl
      : public envoy::service::auth::v3::Authorization::Service {
   public:
    using CheckHandler =
        std::function<grpc::Status(const CheckRequest*, CheckResponse*)>;

    grpc::Status Check(grpc::ServerContext* /*context*/,
                       const CheckRequest* request,
                       CheckResponse* response) override {
      grpc_core::MutexLock lock(&mu_);
      requests_.push_back(*request);
      if (check_handler_ != nullptr) {
        return check_handler_(request, response);
      }
      response->mutable_status()->set_code(0);
      response->mutable_ok_response();
      return grpc::Status::OK;
    }

    void SetCheckHandler(CheckHandler handler) {
      grpc_core::MutexLock lock(&mu_);
      check_handler_ = std::move(handler);
    }

    size_t request_count() {
      grpc_core::MutexLock lock(&mu_);
      return requests_.size();
    }

    std::vector<CheckRequest> requests() {
      grpc_core::MutexLock lock(&mu_);
      return requests_;
    }

    void Reset() {
      grpc_core::MutexLock lock(&mu_);
      requests_.clear();
      check_handler_ = nullptr;
    }

   private:
    grpc_core::Mutex mu_;
    std::vector<CheckRequest> requests_ ABSL_GUARDED_BY(&mu_);
    CheckHandler check_handler_ ABSL_GUARDED_BY(&mu_);
  };

  class ExtAuthzServerThread : public ServerThread {
   public:
    explicit ExtAuthzServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       InsecureServerCredentials()) {}

    ExtAuthzServiceImpl* service() { return &service_; }

   private:
    const char* Type() override { return "ExtAuthzServer"; }
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&service_);
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    ExtAuthzServiceImpl service_;
  };

  XdsExtAuthzTest()
      : env_var_(GetParam().filter_on_server()
                     ? "GRPC_EXPERIMENTAL_XDS_EXT_AUTHZ_ON_SERVER"
                     : "GRPC_EXPERIMENTAL_XDS_EXT_AUTHZ_ON_CLIENT") {}

  void SetUp() override {
    if (GetParam().filter_on_server() &&
        !grpc_core::IsXdsServerFilterChainPerRouteEnabled()) {
      GTEST_SKIP()
          << "test requires xds_server_filter_chain_per_route experiment";
    }
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
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz BuildExtAuthzConfig(
      bool failure_mode_allow = false,
      bool failure_mode_allow_header_add = false) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_authz;
    ext_authz.set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    ext_authz.set_failure_mode_allow(failure_mode_allow);
    ext_authz.set_failure_mode_allow_header_add(failure_mode_allow_header_add);
    auto* grpc_service = ext_authz.mutable_grpc_service();
    grpc_service->mutable_timeout()->set_seconds(10);
    auto* google_grpc = grpc_service->mutable_google_grpc();
    google_grpc->set_target_uri(
        absl::StrCat("ipv4:127.0.0.1:", ext_authz_server_->port()));
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::insecure::v3::
            InsecureCredentials());
    return ext_authz;
  }

  Listener BuildListenerWithExtAuthzFilter(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz&
          ext_authz_config,
      bool use_tls = false) {
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
    filter0->set_name(std::string(kFilterInstanceName));
    filter0->mutable_typed_config()->PackFrom(ext_authz_config);
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
      transport_socket->mutable_typed_config()->PackFrom(
          downstream_tls_context);
    }
    return listener;
  }

  // Adds an ExtAuthzPerRoute override to the first route.  Unlike most
  // filters, ExtAuthzPerRoute cannot carry a full ExtAuthz config (it has no
  // grpc_service field), so the top-level config always stays in the listener
  // and the override only exercises the route-override code path.
  RouteConfiguration BuildRouteConfigurationWithExtAuthzOverride(
      RouteConfiguration route_config) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route;
    per_route.mutable_check_settings();
    auto* config_map = route_config.mutable_virtual_hosts(0)
                           ->mutable_routes(0)
                           ->mutable_typed_per_filter_config();
    (*config_map)[std::string(kFilterInstanceName)].PackFrom(per_route);
    return route_config;
  }

  void SetExtAuthzListener(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz&
          ext_authz_config,
      bool use_tls = false) {
    RouteConfiguration route_config = GetParam().filter_on_server()
                                          ? default_server_route_config_
                                          : default_route_config_;
    if (GetParam().filter_config_setup() ==
        XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute) {
      route_config =
          BuildRouteConfigurationWithExtAuthzOverride(std::move(route_config));
    }
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
  void SetExtAuthzListenerAndStart(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz&
          ext_authz_config,
      bool use_tls = false) {
    SetExtAuthzListener(ext_authz_config, use_tls);
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

  // Sends an Echo RPC, returning the server's initial and trailing metadata
  // in addition to the status.  XdsEnd2endTest::SendRpc() does not expose
  // trailing metadata, which the ext_authz filter mutates for denied
  // responses.
  Status SendRpcWithMetadata(
      const RpcOptions& rpc_options,
      std::multimap<std::string, std::string>* server_initial_metadata,
      std::multimap<std::string, std::string>* server_trailing_metadata) {
    ClientContext context;
    EchoRequest request;
    EchoResponse response;
    rpc_options.SetupRpc(&context, &request);
    Status status = stub_->Echo(&context, request, &response);
    if (server_initial_metadata != nullptr) {
      *server_initial_metadata =
          ConvertMetadata(context.GetServerInitialMetadata());
    }
    if (server_trailing_metadata != nullptr) {
      *server_trailing_metadata =
          ConvertMetadata(context.GetServerTrailingMetadata());
    }
    return status;
  }

  // Converts metadata keys and values to strings, lower-casing the keys.
  static std::multimap<std::string, std::string> ConvertMetadata(
      const std::multimap<grpc::string_ref, grpc::string_ref>& input) {
    std::multimap<std::string, std::string> output;
    for (const auto& [key, value] : input) {
      std::string header(key.data(), key.size());
      absl::AsciiStrToLower(&header);
      output.emplace(header, std::string(value.data(), value.size()));
    }
    return output;
  }

  // Returns the headers that the filter reported to the ext_authz service in
  // AttributeContext.HttpRequest.header_map.
  static std::multimap<std::string, std::string> HeaderMap(
      const CheckRequest& request) {
    std::multimap<std::string, std::string> output;
    for (const auto& header :
         request.attributes().request().http().header_map().headers()) {
      output.emplace(header.key(), header.value().empty() ? header.raw_value()
                                                          : header.value());
    }
    return output;
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
        ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
    options.set_certificate_verifier(std::move(verifier));
    options.set_verify_server_certs(true);
    options.set_check_call_host(false);
    return CreateCustomChannel(uri, grpc::experimental::TlsCredentials(options),
                               args);
  }

  grpc_core::testing::ScopedExperimentalEnvVar env_var_;
  std::unique_ptr<ExtAuthzServerThread> ext_authz_server_;
};

// Run with the HTTP filter config in the Listener only, with an additional
// per-route override, and on a server-side filter chain (both with and without
// per-route override).
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtAuthzTest,
    ::testing::Values(
        XdsTestType(),
        XdsTestType().set_filter_config_setup(
            XdsTestType::kHttpFilterConfigInRoute),
        XdsTestType().set_filter_on_server(),
        XdsTestType().set_filter_on_server().set_filter_config_setup(
            XdsTestType::kHttpFilterConfigInRoute)),
    &XdsTestType::Name);

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessBasic) {
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
  auto requests = ext_authz_server_->service()->requests();
  ASSERT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0].attributes().request().http().method(), "POST");
  EXPECT_EQ(requests[0].attributes().request().http().path(), kEchoMethod);
}

// AttributeContext.source and .destination are reported on the server side
// only (gRFC A92).  The fixture's insecure credentials produce no X.509 auth
// context properties, so both principals are empty; the addresses come from
// the connection's endpoints and are asserted here.
TEST_P(XdsExtAuthzTest, ExtAuthzSourceAndDestination) {
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendOk(DEBUG_LOCATION);
  auto requests = ext_authz_server_->service()->requests();
  ASSERT_EQ(requests.size(), 1);
  const auto& attr = requests[0].attributes();
  if (!GetParam().filter_on_server()) {
    EXPECT_FALSE(attr.has_source());
    EXPECT_FALSE(attr.has_destination());
    return;
  }
  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.has_destination());
  // The destination is the backend's own listening address.
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            backends_[0]->port());
  // The source is the client end of the same connection.  Its port is
  // ephemeral, so only its presence is checked.
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_FALSE(attr.source().address().socket_address().address().empty());
  EXPECT_GT(attr.source().address().socket_address().port_value(), 0);
  // Insecure credentials mean no peer or local certificate, hence no
  // principals.
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
  EXPECT_THAT(attr.source().certificate(), ::testing::IsEmpty());
}

// When mTLS is enabled on the server filter chain, AttributeContext.source and
// .destination carry the peer and local principals and certificates.
TEST_P(XdsExtAuthzTest, ExtAuthzMtlsSourceAndDestination) {
  if (!GetParam().filter_on_server()) return;
  auto config = BuildExtAuthzConfig();
  config.set_include_peer_certificate(true);
  SetExtAuthzListenerAndStart(config, /*use_tls=*/true);
  constexpr char kClientCertPath[] =
      "test/core/tsi/test_creds/spiffe_end2end/client_spiffe.pem";
  constexpr char kClientKeyPath[] =
      "test/core/tsi/test_creds/spiffe_end2end/client.key";
  auto channel = CreateMtlsClientChannel(kSpiffeCaCertPath, kClientCertPath,
                                         kClientKeyPath);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext context;
  EchoRequest request;
  request.set_message(kRequestMessage);
  EchoResponse response;
  Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  auto requests = ext_authz_server_->service()->requests();
  ASSERT_EQ(requests.size(), 1);
  const auto& attr = requests[0].attributes();
  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.source().principal(),
              ::testing::StrEq(
                  "spiffe://foo.bar.com/9eebccd2-12bf-40a6-b262-65fe0487d453"));
  EXPECT_THAT(attr.destination().principal(),
              ::testing::StrEq("spiffe://example.com/workload/9eebccd2"));
  std::string client_pem = grpc_core::testing::GetFileContents(kClientCertPath);
  EXPECT_THAT(attr.source().certificate(),
              ::testing::StrEq(grpc_core::UrlEncode(client_pem)));
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            backends_[0]->port());
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_GT(attr.source().address().socket_address().port_value(), 0);
}

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessWithHeaderMutations) {
  ext_authz_server_->service()->SetCheckHandler([](const CheckRequest*,
                                                   CheckResponse* response) {
    response->mutable_status()->set_code(0);
    auto* ok = response->mutable_ok_response();
    // Mutations to the client's initial metadata.
    auto* add_hdr = ok->add_headers();
    add_hdr->mutable_header()->set_key(std::string(kCustomHeaderKey));
    add_hdr->mutable_header()->set_value(std::string(kCustomHeaderValue));
    ok->add_headers_to_remove(std::string(kHeaderToRemove));
    // Mutations to the server's initial metadata.
    auto* resp_hdr = ok->add_response_headers_to_add();
    resp_hdr->mutable_header()->set_key(std::string(kResponseHeaderKey));
    resp_hdr->mutable_header()->set_value(std::string(kResponseHeaderValue));
    return grpc::Status::OK;
  });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  // The backend echoes the client's initial metadata back to us in its own
  // initial metadata, so the mutations to the client's initial metadata and
  // the mutations to the server's initial metadata both show up there.
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status = SendRpcWithMetadata(
      RpcOptions()
          .set_metadata({{std::string(kHeaderToRemove),
                          std::string(kHeaderToRemoveValue)}})
          .set_echo_metadata_initially(true),
      &server_initial_metadata, &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
  EXPECT_THAT(server_initial_metadata,
              AllOf(
                  // Client initial metadata mutations, as seen by the backend.
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)),
                  Not(Contains(Pair(kHeaderToRemove, kHeaderToRemoveValue))),
                  // Server initial metadata mutation, added by the filter.
                  Contains(Pair(kResponseHeaderKey, kResponseHeaderValue))));
  // Server trailing metadata is not mutated for an OK response.
  EXPECT_THAT(server_trailing_metadata,
              Not(Contains(Pair(kResponseHeaderKey, kResponseHeaderValue))));
  // The header that the ext_authz service asked us to remove was still
  // visible to the ext_authz service itself.
  auto requests = ext_authz_server_->service()->requests();
  ASSERT_EQ(requests.size(), 1);
  EXPECT_THAT(HeaderMap(requests[0]),
              Contains(Pair(kHeaderToRemove, kHeaderToRemoveValue)));
}

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessFailureModeAllow) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse*) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "ext_authz service unavailable");
      });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true));
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpcWithMetadata(
      RpcOptions().set_echo_metadata_initially(true), &server_initial_metadata,
      /*server_trailing_metadata=*/nullptr);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
  EXPECT_THAT(server_initial_metadata,
              Contains(Pair(kFailureModeAllowedHeader, kTrue)));
}

TEST_P(XdsExtAuthzTest, ExtAuthzCommunicationFailureUsesStatusOnError) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse*) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "ext_authz service unavailable");
      });
  auto ext_authz_config = BuildExtAuthzConfig();
  ext_authz_config.mutable_status_on_error()->set_code(
      envoy::type::v3::Unauthorized);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAUTHENTICATED,
                      "ext_authz service unavailable");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

TEST_P(XdsExtAuthzTest, ExtAuthzSuccessFilterDisabled) {
  auto ext_authz_config = BuildExtAuthzConfig();
  auto* filter_enabled = ext_authz_config.mutable_filter_enabled();
  auto* default_value = filter_enabled->mutable_default_value();
  default_value->set_numerator(0);
  default_value->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 0);
}

TEST_P(XdsExtAuthzTest, ExtAuthzDenied) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        response->mutable_status()->set_message(std::string(kDeniedMessage));
        response->mutable_denied_response();
        return grpc::Status::OK;
      });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
      absl::StrCat("ExtAuthz request is denied, error message: ",
                   kDeniedMessage));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

TEST_P(XdsExtAuthzTest, ExtAuthzDeniedWithHttpStatusAndHeaders) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        auto* denied = response->mutable_denied_response();
        // HTTP 401 maps to UNAUTHENTICATED.
        denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kDeniedTrailerKey));
        header->mutable_header()->set_value(std::string(kDeniedTrailerValue));
        return grpc::Status::OK;
      });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAUTHENTICATED,
                      "ExtAuthz request is denied");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

TEST_P(XdsExtAuthzTest, ExtAuthzDeniedWithMultipleHeaders) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        auto* denied = response->mutable_denied_response();
        auto* header1 = denied->add_headers();
        header1->mutable_header()->set_key(std::string(kDeniedTrailerKey));
        header1->mutable_header()->set_value(std::string(kDeniedTrailerValue));
        auto* header2 = denied->add_headers();
        header2->mutable_header()->set_key(std::string(kDeniedTrailerKey2));
        header2->mutable_header()->set_value(std::string(kDeniedTrailerValue2));
        return grpc::Status::OK;
      });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "ExtAuthz request is denied");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

// A denied response whose headers are rejected by decoder_header_mutation_rules
// fails the RPC with status_on_error rather than with the denied status.
TEST_P(XdsExtAuthzTest, ExtAuthzDeniedDisallowedHeaderMutation) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        auto* denied = response->mutable_denied_response();
        denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kDeniedTrailerKey));
        header->mutable_header()->set_value(std::string(kDeniedTrailerValue));
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "ExtAuthz header mutation is not allowed");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

// failure_mode_allow applies only to communication failures with the ext_authz
// service, so a denied response with a disallowed header mutation still fails.
TEST_P(XdsExtAuthzTest,
       ExtAuthzDeniedDisallowedHeaderMutationFailureModeAllow) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        auto* denied = response->mutable_denied_response();
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kDeniedTrailerKey));
        header->mutable_header()->set_value(std::string(kDeniedTrailerValue));
        return grpc::Status::OK;
      });
  auto ext_authz_config = BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true);
  auto* rules = ext_authz_config.mutable_decoder_header_mutation_rules();
  rules->mutable_disallow_all()->set_value(true);
  rules->mutable_disallow_is_error()->set_value(true);
  SetExtAuthzListenerAndStart(ext_authz_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "ExtAuthz header mutation is not allowed");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

TEST_P(XdsExtAuthzTest, ExtAuthzDeniedIgnoresFailureModeAllow) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        response->mutable_denied_response();
        return grpc::Status::OK;
      });
  // failure_mode_allow applies only to communication failures with the
  // ext_authz service, not to requests that the service explicitly denies.
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig(
      /*failure_mode_allow=*/true, /*failure_mode_allow_header_add=*/true));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "ExtAuthz request is denied");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
}

TEST_P(XdsExtAuthzTest, ExtAuthzDeniedWithoutDeniedResponse) {
  ext_authz_server_->service()->SetCheckHandler(
      [](const CheckRequest*, CheckResponse* response) {
        // Non-OK status with neither ok_response nor denied_response set.
        response->mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
        return grpc::Status::OK;
      });
  SetExtAuthzListenerAndStart(BuildExtAuthzConfig());
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED,
                      "denied_response not present in CheckResponse");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(ext_authz_server_->service()->request_count(), 1);
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
