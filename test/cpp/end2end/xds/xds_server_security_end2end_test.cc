// Copyright 2023 gRPC authors.
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

// TODO(roth): Split this file up into a common test framework and a set
// of test files that use that framework.  Need to figure out the best
// way to split up the tests.  One option would be to split it up by xDS
// resource type; another approach would be to have all of the "core"
// xDS functionality in one file and then move specific features to
// their own files (e.g., mTLS security, fault injection, circuit
// breaking, etc).

#include <deque>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/functional/bind_front.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/xds_server_builder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/audit_logging_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::grpc::experimental::RegisterAuditLoggerFactory;
using ::grpc::experimental::StaticDataCertificateProvider;
using ::grpc_core::experimental::AuditLoggerRegistry;
using ::grpc_core::testing::ScopedExperimentalEnvVar;
using ::grpc_core::testing::TestAuditLoggerFactory;

// Global variables for each provider.
FakeCertificateProvider::CertDataMapWrapper* g_fake1_cert_data_map = nullptr;
FakeCertificateProvider::CertDataMapWrapper* g_fake2_cert_data_map = nullptr;

class XdsServerSecurityTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    XdsBootstrapBuilder builder;
    builder.AddCertificateProviderPlugin("fake_plugin1", "fake1");
    builder.AddCertificateProviderPlugin("fake_plugin2", "fake2");
    std::vector<std::string> fields;
    fields.push_back(absl::StrFormat("        \"certificate_file\": \"%s\"",
                                     kClientCertPath));
    fields.push_back(absl::StrFormat("        \"private_key_file\": \"%s\"",
                                     kClientKeyPath));
    fields.push_back(absl::StrFormat("        \"ca_certificate_file\": \"%s\"",
                                     kCaCertPath));
    builder.AddCertificateProviderPlugin("file_plugin", "file_watcher",
                                         absl::StrJoin(fields, ",\n"));
    InitClient(builder);
    CreateBackends(1, /*xds_enabled=*/true);
    root_cert_ = ReadFile(kCaCertPath);
    bad_root_cert_ = ReadFile(kBadClientCertPath);
    identity_pair_ = ReadTlsIdentityPair(kServerKeyPath, kServerCertPath);
    bad_identity_pair_ =
        ReadTlsIdentityPair(kBadClientKeyPath, kBadClientCertPath);
    identity_pair_2_ = ReadTlsIdentityPair(kClientKeyPath, kClientCertPath);
    server_authenticated_identity_ = {"*.test.google.fr",
                                      "waterzooi.test.google.be",
                                      "*.test.youtube.com", "192.168.1.3"};
    server_authenticated_identity_2_ = {"testclient"};
    client_authenticated_identity_ = {"*.test.google.fr",
                                      "waterzooi.test.google.be",
                                      "*.test.youtube.com", "192.168.1.3"};
    EdsResourceArgs args({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    });
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  }

  void SetLdsUpdate(absl::string_view root_instance_name,
                    absl::string_view root_certificate_name,
                    absl::string_view identity_instance_name,
                    absl::string_view identity_certificate_name,
                    bool require_client_certificates) {
    Listener listener = default_server_listener_;
    auto* filter_chain = listener.mutable_default_filter_chain();
    if (!identity_instance_name.empty()) {
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");
      DownstreamTlsContext downstream_tls_context;
      downstream_tls_context.mutable_common_tls_context()
          ->mutable_tls_certificate_provider_instance()
          ->set_instance_name(std::string(identity_instance_name));
      downstream_tls_context.mutable_common_tls_context()
          ->mutable_tls_certificate_provider_instance()
          ->set_certificate_name(std::string(identity_certificate_name));
      if (!root_instance_name.empty()) {
        downstream_tls_context.mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_ca_certificate_provider_instance()
            ->set_instance_name(std::string(root_instance_name));
        downstream_tls_context.mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_ca_certificate_provider_instance()
            ->set_certificate_name(std::string(root_certificate_name));
        downstream_tls_context.mutable_require_client_certificate()->set_value(
            require_client_certificates);
      }
      transport_socket->mutable_typed_config()->PackFrom(
          downstream_tls_context);
    }
    SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                               backends_[0]->port(),
                                               default_server_route_config_);
  }

  std::shared_ptr<grpc::Channel> CreateMtlsChannel() {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
    IdentityKeyCertPair key_cert_pair;
    key_cert_pair.private_key = ReadFile(kServerKeyPath);
    key_cert_pair.certificate_chain = ReadFile(kServerCertPath);
    std::vector<IdentityKeyCertPair> identity_key_cert_pairs;
    identity_key_cert_pairs.emplace_back(key_cert_pair);
    auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
        ReadFile(kCaCertPath), identity_key_cert_pairs);
    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(std::move(certificate_provider));
    options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    auto verifier =
        ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
    options.set_verify_server_certs(true);
    options.set_certificate_verifier(std::move(verifier));
    auto channel_creds = grpc::experimental::TlsCredentials(options);
    GPR_ASSERT(channel_creds.get() != nullptr);
    return CreateCustomChannel(uri, channel_creds, args);
  }

  std::shared_ptr<grpc::Channel> CreateTlsChannel() {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
    auto certificate_provider =
        std::make_shared<StaticDataCertificateProvider>(ReadFile(kCaCertPath));
    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(std::move(certificate_provider));
    options.watch_root_certs();
    auto verifier =
        ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
    options.set_verify_server_certs(true);
    options.set_certificate_verifier(std::move(verifier));
    auto channel_creds = grpc::experimental::TlsCredentials(options);
    GPR_ASSERT(channel_creds.get() != nullptr);
    return CreateCustomChannel(uri, channel_creds, args);
  }

  std::shared_ptr<grpc::Channel> CreateInsecureChannel(
      bool use_put_requests = false) {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    if (use_put_requests) {
      args.SetInt(GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS, 1);
    }
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
    return CreateCustomChannel(uri, InsecureChannelCredentials(), args);
  }

  void SendRpc(
      std::function<std::shared_ptr<grpc::Channel>()> channel_creator,
      std::vector<std::string> expected_server_identity,
      std::vector<std::string> expected_client_identity,
      bool test_expects_failure = false,
      absl::optional<grpc::StatusCode> expected_status = absl::nullopt) {
    gpr_log(GPR_INFO, "Sending RPC");
    int num_tries = 0;
    constexpr int kRetryCount = 100;
    auto overall_deadline = absl::Now() + absl::Seconds(5);
    for (; num_tries < kRetryCount || absl::Now() < overall_deadline;
         num_tries++) {
      auto channel = channel_creator();
      auto stub = grpc::testing::EchoTestService::NewStub(channel);
      ClientContext context;
      context.set_wait_for_ready(true);
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(2000));
      EchoRequest request;
      // TODO(yashykt): Skipping the cancelled check on the server since the
      // server's graceful shutdown isn't as per spec and the check isn't
      // necessary for what we want to test here anyway.
      // https://github.com/grpc/grpc/issues/24237
      request.mutable_param()->set_skip_cancelled_check(true);
      request.set_message(kRequestMessage);
      EchoResponse response;
      Status status = stub->Echo(&context, request, &response);
      if (test_expects_failure) {
        if (status.ok()) {
          gpr_log(GPR_ERROR, "RPC succeeded. Failure expected. Trying again.");
          continue;
        }
        if (expected_status.has_value() &&
            *expected_status != status.error_code()) {
          gpr_log(GPR_ERROR,
                  "Expected status does not match Actual(%d) vs Expected(%d)",
                  status.error_code(), *expected_status);
          continue;
        }
      } else {
        if (!status.ok()) {
          gpr_log(GPR_ERROR, "RPC failed. code=%d message=%s Trying again.",
                  status.error_code(), status.error_message().c_str());
          continue;
        }
        EXPECT_EQ(response.message(), kRequestMessage);
        std::vector<std::string> peer_identity;
        for (const auto& entry : context.auth_context()->GetPeerIdentity()) {
          peer_identity.emplace_back(
              std::string(entry.data(), entry.size()).c_str());
        }
        if (peer_identity != expected_server_identity) {
          gpr_log(GPR_ERROR,
                  "Expected server identity does not match. (actual) %s vs "
                  "(expected) %s Trying again.",
                  absl::StrJoin(peer_identity, ",").c_str(),
                  absl::StrJoin(expected_server_identity, ",").c_str());
          continue;
        }
        if (backends_[0]->backend_service()->last_peer_identity() !=
            expected_client_identity) {
          gpr_log(
              GPR_ERROR,
              "Expected client identity does not match. (actual) %s vs "
              "(expected) %s Trying again.",
              absl::StrJoin(
                  backends_[0]->backend_service()->last_peer_identity(), ",")
                  .c_str(),
              absl::StrJoin(expected_client_identity, ",").c_str());
          continue;
        }
      }
      break;
    }
    EXPECT_LT(num_tries, kRetryCount);
  }

  std::string root_cert_;
  std::string bad_root_cert_;
  grpc_core::PemKeyCertPairList identity_pair_;
  grpc_core::PemKeyCertPairList bad_identity_pair_;
  grpc_core::PemKeyCertPairList identity_pair_2_;
  std::vector<std::string> server_authenticated_identity_;
  std::vector<std::string> server_authenticated_identity_2_;
  std::vector<std::string> client_authenticated_identity_;
};

TEST_P(XdsServerSecurityTest,
       TestDeprecateTlsCertificateCertificateProviderInstanceField) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->mutable_filters()->at(0).mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, CertificatesNotAvailable) {
  g_fake1_cert_data_map->Set({});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithRootPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin2", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin2", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothPluginsUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"good", {root_cert_, identity_pair_2_}},
                              {"", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin2", "", "fake_plugin2", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin2", "good", "fake_plugin2", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithRootCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "bad", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothCertificateNamesUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "good", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringButProvidingClientCerts) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringAndNotProvidingClientCerts) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "fake_plugin2", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "fake_plugin1", "good", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestMtlsToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestTlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

// We are only testing the server here.
// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsServerSecurityTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_xds_credentials()),
    &XdsTestType::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_1;
  grpc::testing::g_fake1_cert_data_map = &cert_data_map_1;
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_2;
  grpc::testing::g_fake2_cert_data_map = &cert_data_map_2;
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->certificate_provider_registry()
            ->RegisterCertificateProviderFactory(
                std::make_unique<grpc::testing::FakeCertificateProviderFactory>(
                    "fake1", grpc::testing::g_fake1_cert_data_map));
        builder->certificate_provider_registry()
            ->RegisterCertificateProviderFactory(
                std::make_unique<grpc::testing::FakeCertificateProviderFactory>(
                    "fake2", grpc::testing::g_fake2_cert_data_map));
      });
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
