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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/xds_server_builder.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/config_vars.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/tls/certificate_provider_registry.h"
#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/util/env.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/audit_logging_utils.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::rbac::v3::Policy;
using ::envoy::config::rbac::v3::RBAC_Action_ALLOW;
using ::envoy::config::rbac::v3::RBAC_Action_DENY;
using ::envoy::config::rbac::v3::RBAC_Action_LOG;
using ::envoy::config::rbac::v3::
    RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW;
using ::envoy::config::rbac::v3::
    RBAC_AuditLoggingOptions_AuditCondition_ON_DENY;
using ::envoy::config::rbac::v3::
    RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::rbac::v3::RBACPerRoute;
using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using ::envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using ::envoy::type::matcher::v3::StringMatcher;
using ::xds::type::v3::TypedStruct;

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::grpc::experimental::RegisterAuditLoggerFactory;
using ::grpc::experimental::StaticDataCertificateProvider;
using ::grpc_core::experimental::AuditLoggerRegistry;
using ::grpc_core::testing::ScopedExperimentalEnvVar;
using ::grpc_core::testing::TestAuditLoggerFactory;

constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kBadClientCertPath[] = "src/core/tsi/test_creds/badclient.pem";
constexpr char kBadClientKeyPath[] = "src/core/tsi/test_creds/badclient.key";

// Based on StaticDataCertificateProvider, but provides alternate certificates
// if the certificate name is not empty.
class FakeCertificateProvider final : public grpc_tls_certificate_provider {
 public:
  struct CertData {
    std::string root_certificate;
    grpc_core::PemKeyCertPairList identity_key_cert_pairs;
  };

  using CertDataMap = std::map<std::string /*cert_name */, CertData>;
  class CertDataMapWrapper {
   public:
    CertDataMap Get() {
      grpc_core::MutexLock lock(&mu_);
      return cert_data_map_;
    }

    void Set(CertDataMap data) {
      grpc_core::MutexLock lock(&mu_);
      cert_data_map_ = std::move(data);
    }

   private:
    grpc_core::Mutex mu_;
    CertDataMap cert_data_map_ ABSL_GUARDED_BY(mu_);
  };

  explicit FakeCertificateProvider(CertDataMap cert_data_map)
      : distributor_(
            grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>()),
        cert_data_map_(std::move(cert_data_map)) {
    distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                                bool root_being_watched,
                                                bool identity_being_watched) {
      if (!root_being_watched && !identity_being_watched) return;
      auto it = cert_data_map_.find(cert_name);
      if (it == cert_data_map_.end()) {
        grpc_error_handle error = GRPC_ERROR_CREATE(absl::StrCat(
            "No certificates available for cert_name \"", cert_name, "\""));
        distributor_->SetErrorForCert(cert_name, error, error);
      } else {
        std::optional<std::string> root_certificate;
        std::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
        if (root_being_watched) {
          root_certificate = it->second.root_certificate;
        }
        if (identity_being_watched) {
          pem_key_cert_pairs = it->second.identity_key_cert_pairs;
        }
        distributor_->SetKeyMaterials(cert_name, std::move(root_certificate),
                                      std::move(pem_key_cert_pairs));
      }
    });
  }

  ~FakeCertificateProvider() override {
    distributor_->SetWatchStatusCallback(nullptr);
  }

  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

  grpc_core::UniqueTypeName type() const override {
    static grpc_core::UniqueTypeName::Factory kFactory("fake");
    return kFactory.Create();
  }

 private:
  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    // TODO(yashykt): Maybe do something better here.
    return grpc_core::QsortCompare(
        static_cast<const grpc_tls_certificate_provider*>(this), other);
  }

  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  CertDataMap cert_data_map_;
};

class FakeCertificateProviderFactory
    : public grpc_core::CertificateProviderFactory {
 public:
  class Config : public grpc_core::CertificateProviderFactory::Config {
   public:
    explicit Config(absl::string_view name) : name_(name) {}

    absl::string_view name() const override { return name_; }

    std::string ToString() const override { return "{}"; }

   private:
    absl::string_view name_;
  };

  FakeCertificateProviderFactory(
      absl::string_view name,
      FakeCertificateProvider::CertDataMapWrapper* cert_data_map)
      : name_(name), cert_data_map_(cert_data_map) {
    CHECK_NE(cert_data_map, nullptr);
  }

  absl::string_view name() const override { return name_; }

  grpc_core::RefCountedPtr<grpc_core::CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(
      const grpc_core::Json& /*config_json*/,
      const grpc_core::JsonArgs& /*args*/,
      grpc_core::ValidationErrors* /*errors*/) override {
    return grpc_core::MakeRefCounted<Config>(name_);
  }

  grpc_core::RefCountedPtr<grpc_tls_certificate_provider>
  CreateCertificateProvider(
      grpc_core::RefCountedPtr<grpc_core::CertificateProviderFactory::Config>
      /*config*/) override {
    CHECK_NE(cert_data_map_, nullptr);
    return grpc_core::MakeRefCounted<FakeCertificateProvider>(
        cert_data_map_->Get());
  }

 private:
  absl::string_view name_;
  FakeCertificateProvider::CertDataMapWrapper* cert_data_map_;
};

// Global variables for each provider.
FakeCertificateProvider::CertDataMapWrapper* g_fake1_cert_data_map = nullptr;
FakeCertificateProvider::CertDataMapWrapper* g_fake2_cert_data_map = nullptr;

//
// Client-side mTLS tests
//

class XdsSecurityTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    XdsBootstrapBuilder builder = MakeBootstrapBuilder();
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
    InitClient(builder, /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               CreateXdsChannelCredentials());
    CreateAndStartBackends(2, /*xds_enabled=*/false,
                           CreateMtlsServerCredentials());
    root_cert_ = grpc_core::testing::GetFileContents(kCaCertPath);
    bad_root_cert_ = grpc_core::testing::GetFileContents(kBadClientCertPath);
    identity_pair_ = ReadTlsIdentityPair(kClientKeyPath, kClientCertPath);
    // TODO(yashykt): Use different client certs here instead of reusing
    // server certs after https://github.com/grpc/grpc/pull/24876 is merged
    fallback_identity_pair_ =
        ReadTlsIdentityPair(kServerKeyPath, kServerCertPath);
    bad_identity_pair_ =
        ReadTlsIdentityPair(kBadClientKeyPath, kBadClientCertPath);
    server_san_exact_.set_exact("*.test.google.fr");
    server_san_prefix_.set_prefix("waterzooi.test.google");
    server_san_suffix_.set_suffix("google.fr");
    server_san_contains_.set_contains("google");
    server_san_regex_.mutable_safe_regex()->mutable_google_re2();
    server_san_regex_.mutable_safe_regex()->set_regex(
        "(foo|waterzooi).test.google.(fr|be)");
    bad_san_1_.set_exact("192.168.1.4");
    bad_san_2_.set_exact("foo.test.google.in");
    authenticated_identity_ = {"testclient"};
    fallback_authenticated_identity_ = {"*.test.google.fr",
                                        "waterzooi.test.google.be",
                                        "*.test.youtube.com", "192.168.1.3"};
    EdsResourceArgs args({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    });
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  }

  void MaybeSetUpstreamTlsContextOnCluster(
      absl::string_view root_instance_name,
      absl::string_view root_certificate_name,
      absl::string_view identity_instance_name,
      absl::string_view identity_certificate_name,
      const std::vector<StringMatcher>& san_matchers, Cluster* cluster) {
    if (!identity_instance_name.empty() || !root_instance_name.empty()) {
      auto* transport_socket = cluster->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");
      UpstreamTlsContext upstream_tls_context;
      if (!identity_instance_name.empty()) {
        upstream_tls_context.mutable_common_tls_context()
            ->mutable_tls_certificate_provider_instance()
            ->set_instance_name(std::string(identity_instance_name));
        upstream_tls_context.mutable_common_tls_context()
            ->mutable_tls_certificate_provider_instance()
            ->set_certificate_name(std::string(identity_certificate_name));
      }
      if (!root_instance_name.empty()) {
        upstream_tls_context.mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_ca_certificate_provider_instance()
            ->set_instance_name(std::string(root_instance_name));
        upstream_tls_context.mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_ca_certificate_provider_instance()
            ->set_certificate_name(std::string(root_certificate_name));
      }
      if (!san_matchers.empty()) {
        auto* validation_context =
            upstream_tls_context.mutable_common_tls_context()
                ->mutable_validation_context();
        for (const auto& san_matcher : san_matchers) {
          *validation_context->add_match_subject_alt_names() = san_matcher;
        }
      }
      transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
    }
  }

  // Sends CDS updates with the new security configuration and verifies that
  // after propagation, this new configuration is used for connections. If \a
  // identity_instance_name and \a root_instance_name are both empty,
  // connections are expected to use fallback credentials.
  // TODO(yashykt): The core of this logic should be inlined into the
  // individual tests instead of being in this helper function.
  void UpdateAndVerifyXdsSecurityConfiguration(
      absl::string_view root_instance_name,
      absl::string_view root_certificate_name,
      absl::string_view identity_instance_name,
      absl::string_view identity_certificate_name,
      const std::vector<StringMatcher>& san_matchers,
      const std::vector<std::string>& expected_authenticated_identity,
      bool test_expects_failure = false) {
    // Change the backend and use a unique service name to use so that we know
    // that the CDS update was applied.
    std::string service_name = absl::StrCat(
        "eds_service_name",
        absl::FormatTime("%H%M%E3S", absl::Now(), absl::LocalTimeZone()));
    backend_index_ = (backend_index_ + 1) % 2;
    EdsResourceArgs args({
        {"locality0",
         CreateEndpointsForBackends(backend_index_, backend_index_ + 1)},
    });
    balancer_->ads_service()->SetEdsResource(
        BuildEdsResource(args, service_name.c_str()));
    auto cluster = default_cluster_;
    cluster.mutable_eds_cluster_config()->set_service_name(service_name);
    MaybeSetUpstreamTlsContextOnCluster(
        root_instance_name, root_certificate_name, identity_instance_name,
        identity_certificate_name, san_matchers, &cluster);
    balancer_->ads_service()->SetCdsResource(cluster);
    // The updates might take time to have an effect, so use a retry loop.
    if (test_expects_failure) {
      SendRpcsUntilFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                           // TODO(yashkt): Change individual test cases to
                           // expect the exact error message here.
                           ".*", /*timeout_ms=*/20 * 1000,
                           RpcOptions().set_timeout_ms(5000));
    } else {
      backends_[backend_index_]->backend_service()->ResetCounters();
      SendRpcsUntil(
          DEBUG_LOCATION,
          [&](const RpcResult& result) {
            // Make sure that we are hitting the correct backend.
            // TODO(yashykt): Even if we haven't moved to the correct backend
            // and are still using the previous update, we should still check
            // for the status and make sure that it fits our expectations.
            if (backends_[backend_index_]->backend_service()->request_count() ==
                0) {
              return true;
            }
            EXPECT_TRUE(result.status.ok())
                << "code=" << result.status.error_code()
                << " message=" << result.status.error_message();
            // Check that the identity is as expected.
            EXPECT_EQ(backends_[backend_index_]
                          ->backend_service()
                          ->last_peer_identity(),
                      expected_authenticated_identity);
            return false;
          },
          /* timeout_ms= */ 20 * 1000, RpcOptions().set_timeout_ms(5000));
    }
  }

  std::string root_cert_;
  std::string bad_root_cert_;
  grpc_core::PemKeyCertPairList identity_pair_;
  grpc_core::PemKeyCertPairList fallback_identity_pair_;
  grpc_core::PemKeyCertPairList bad_identity_pair_;
  StringMatcher server_san_exact_;
  StringMatcher server_san_prefix_;
  StringMatcher server_san_suffix_;
  StringMatcher server_san_contains_;
  StringMatcher server_san_regex_;
  StringMatcher bad_san_1_;
  StringMatcher bad_san_2_;
  std::vector<std::string> authenticated_identity_;
  std::vector<std::string> fallback_authenticated_identity_;
  int backend_index_ = 0;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsSecurityTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsSecurityTest, TestTlsConfigurationInCombinedValidationContext) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_combined_validation_context()
      ->mutable_default_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancer_->ads_service()->SetCdsResource(cluster);
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(5000));
}

// TODO(yashykt): Remove this test once we stop supporting old fields
TEST_P(XdsSecurityTest,
       TestTlsConfigurationInValidationContextCertificateProviderInstance) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_combined_validation_context()
      ->mutable_validation_context_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancer_->ads_service()->SetCdsResource(cluster);
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(5000));
}

TEST_P(XdsSecurityTest, UseSystemRootCerts) {
  grpc_core::testing::ScopedExperimentalEnvVar env1(
      "GRPC_EXPERIMENTAL_XDS_SYSTEM_ROOT_CERTS");
  grpc_core::testing::ScopedEnvVar env2("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH",
                                        kCaCertPath);
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_system_root_certs();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancer_->ads_service()->SetCdsResource(cluster);
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(5000));
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithNoSanMatchers) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {}, authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithExactSanMatcher) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithPrefixSanMatcher) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithSuffixSanMatcher) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_suffix_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithContainsSanMatcher) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_contains_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRegexSanMatcher) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_regex_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithSanMatchersUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "fake_plugin1", "",
      {server_san_exact_, server_san_prefix_}, authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {bad_san_1_, bad_san_2_}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "fake_plugin1", "",
      {server_san_prefix_, server_san_regex_}, authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRootPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin2" /* bad root */, "",
                                          "fake_plugin1", "", {}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithIdentityPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, fallback_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin2",
                                          "", {server_san_exact_},
                                          fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithBothPluginsUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {bad_root_cert_, bad_identity_pair_}},
                              {"good", {root_cert_, fallback_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin2", "", "fake_plugin2",
                                          "", {}, {}, true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin2", "good", "fake_plugin2", "good", {server_san_prefix_},
      fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRootCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_regex_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "fake_plugin1",
                                          "", {server_san_regex_}, {},
                                          true /* failure */);
}

TEST_P(XdsSecurityTest,
       TestMtlsConfigurationWithIdentityCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "bad", {server_san_exact_}, {},
                                          true /* failure */);
}

TEST_P(XdsSecurityTest,
       TestMtlsConfigurationWithIdentityCertificateNameUpdateGoodCerts) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, fallback_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "good", {server_san_exact_},
                                          fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithBothCertificateNamesUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "fake_plugin1",
                                          "bad", {server_san_prefix_}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithNoSanMatchers) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "", {},
                                          {} /* unauthenticated */);
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithSanMatchers) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "",
      {server_san_exact_, server_san_prefix_, server_san_regex_},
      {} /* unauthenticated */);
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithSanMatchersUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {server_san_exact_, server_san_prefix_},
      {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {bad_san_1_, bad_san_2_},
      {} /* unauthenticated */, true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {server_san_prefix_, server_san_regex_},
      {} /* unauthenticated */);
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithRootCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "", "",
                                          {server_san_exact_}, {},
                                          true /* failure */);
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithRootPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {bad_root_cert_, bad_identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin2", "", "", "", {server_san_exact_}, {}, true /* failure */);
}

TEST_P(XdsSecurityTest, TestFallbackConfiguration) {
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestMtlsToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
}

TEST_P(XdsSecurityTest, TestMtlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestTlsToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestTlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestFallbackToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, TestFallbackToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
}

TEST_P(XdsSecurityTest, TestFileWatcherCertificateProvider) {
  UpdateAndVerifyXdsSecurityConfiguration("file_plugin", "", "file_plugin", "",
                                          {server_san_exact_},
                                          authenticated_identity_);
}

TEST_P(XdsSecurityTest, MtlsWithAggregateCluster) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, fallback_identity_pair_}}});
  // Set up aggregate cluster.
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  MaybeSetUpstreamTlsContextOnCluster("fake_plugin1", "", "fake_plugin1", "",
                                      {}, &new_cluster1);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  MaybeSetUpstreamTlsContextOnCluster("fake_plugin1", "", "fake_plugin2", "",
                                      {}, &new_cluster2);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  auto* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  envoy::extensions::clusters::aggregate::v3::ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // RPC should go to backend 0.
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  // Make sure the backend saw the right client identity.
  EXPECT_EQ(backends_[0]->backend_service()->last_peer_identity(),
            authenticated_identity_);
  // Now stop backend 0 and wait for backend 1.
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  // Make sure the backend saw the right client identity.
  EXPECT_EQ(backends_[1]->backend_service()->last_peer_identity(),
            fallback_authenticated_identity_);
}

//
// Server-side mTLS tests
//

class XdsServerSecurityTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    XdsBootstrapBuilder builder = MakeBootstrapBuilder();
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
    InitClient(builder, /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/
               500,  // using a low timeout to quickly end negative tests.
                     // Prefer using WaitOnServingStatusChange() or a similar
                     // loop on the client side to wait on status changes
                     // instead of increasing this timeout.
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               CreateXdsChannelCredentials());
    CreateBackends(1, /*xds_enabled=*/true,
                   XdsServerCredentials(InsecureServerCredentials()));
    root_cert_ = grpc_core::testing::GetFileContents(kCaCertPath);
    bad_root_cert_ = grpc_core::testing::GetFileContents(kBadClientCertPath);
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

  // TODO(yashykt): These methods to create channels should be
  // integrated into the framework, probably by just constructing the
  // credentials and then passing them to XdsEnd2endTest::CreateChannel().
  // It may also be helpful to add methods to the framework to construct
  // these creds types, similar to
  // XdsEnd2endTest::CreateTlsChannelCredentials().

  std::shared_ptr<grpc::Channel> CreateMtlsChannel() {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
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
    options.set_verify_server_certs(true);
    options.set_certificate_verifier(std::move(verifier));
    auto channel_creds = grpc::experimental::TlsCredentials(options);
    CHECK_NE(channel_creds.get(), nullptr);
    return CreateCustomChannel(uri, channel_creds, args);
  }

  std::shared_ptr<grpc::Channel> CreateTlsChannel() {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   std::string(grpc_core::LocalIp()));
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = grpc_core::LocalIpUri(backends_[0]->port());
    auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
        grpc_core::testing::GetFileContents(kCaCertPath));
    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(std::move(certificate_provider));
    options.watch_root_certs();
    auto verifier =
        ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
    options.set_verify_server_certs(true);
    options.set_certificate_verifier(std::move(verifier));
    auto channel_creds = grpc::experimental::TlsCredentials(options);
    CHECK_NE(channel_creds.get(), nullptr);
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

  // TODO(yashykt): The core of this logic should be inlined into the
  // individual tests instead of being in this helper function.  This
  // can probably be replaced with something like
  // XdsEnd2endTest::SendRpcsUntil().
  void SendRpc(
      absl::FunctionRef<std::shared_ptr<grpc::Channel>()> channel_creator,
      const RpcOptions& rpc_options,
      const std::vector<std::string>& expected_server_identity,
      const std::vector<std::string>& expected_client_identity,
      bool test_expects_failure = false,
      std::optional<grpc::StatusCode> expected_status = std::nullopt,
      absl::string_view expected_error_message_regex = "") {
    LOG(INFO) << "Sending RPC";
    int num_tries = 0;
    constexpr int kRetryCount = 100;
    auto overall_deadline =
        absl::Now() + absl::Seconds(20) * grpc_test_slowdown_factor();
    auto channel = channel_creator();
    auto stub = grpc::testing::EchoTestService::NewStub(channel);
    for (; num_tries < kRetryCount || absl::Now() < overall_deadline;
         num_tries++) {
      ClientContext context;
      EchoRequest request;
      rpc_options.SetupRpc(&context, &request);
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
          LOG(ERROR) << "RPC succeeded. Failure expected. Trying again.";
          continue;
        }
        if (expected_status.has_value() &&
            *expected_status != status.error_code()) {
          LOG(ERROR) << "Expected status does not match Actual("
                     << status.error_code() << ") vs Expected("
                     << *expected_status << ")";
          continue;
        }
        EXPECT_THAT(status.error_message(),
                    ::testing::MatchesRegex(expected_error_message_regex));
      } else {
        if (!status.ok()) {
          LOG(ERROR) << "RPC failed. code=" << status.error_code()
                     << " message=" << status.error_message()
                     << " Trying again.";
          continue;
        }
        EXPECT_EQ(response.message(), kRequestMessage);
        std::vector<std::string> peer_identity;
        for (const auto& entry : context.auth_context()->GetPeerIdentity()) {
          peer_identity.emplace_back(
              std::string(entry.data(), entry.size()).c_str());
        }
        if (peer_identity != expected_server_identity) {
          LOG(ERROR) << "Expected server identity does not match. (actual) "
                     << absl::StrJoin(peer_identity, ",") << " vs (expected) "
                     << absl::StrJoin(expected_server_identity, ",")
                     << " Trying again.";
          continue;
        }
        if (backends_[0]->backend_service()->last_peer_identity() !=
            expected_client_identity) {
          LOG(ERROR)
              << "Expected client identity does not match. (actual) "
              << absl::StrJoin(
                     backends_[0]->backend_service()->last_peer_identity(), ",")
              << " vs (expected) "
              << absl::StrJoin(expected_client_identity, ",")
              << " Trying again.";
          continue;
        }
      }
      break;
    }
    EXPECT_TRUE(absl::Now() <= overall_deadline || num_tries < kRetryCount);
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

// We are only testing the server here.
// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerSecurityTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

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
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
}

TEST_P(XdsServerSecurityTest, CertificatesNotAvailable) {
  g_fake1_cert_data_map->Set({});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeConnectionFailureRegex(
              "failed to connect to all addresses; last error: ",
              /*has_resolution_note=*/false));
}

TEST_P(XdsServerSecurityTest, TestMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithRootPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin2", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeConnectionFailureRegex(
              "failed to connect to all addresses; last error: ",
              /*has_resolution_note=*/false));
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin2", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothPluginsUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"good", {root_cert_, identity_pair_2_}},
                              {"", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin2", "", "fake_plugin2", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeTlsHandshakeFailureRegex(
              "failed to connect to all addresses; last error: "));
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin2", "good", "fake_plugin2", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithRootCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"bad", {bad_root_cert_, bad_identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "bad", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeConnectionFailureRegex(
              "failed to connect to all addresses; last error: ",
              /*has_resolution_note=*/false));
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothCertificateNamesUpdated) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "good", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringButProvidingClientCerts) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringAndNotProvidingClientCerts) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
}

TEST_P(XdsServerSecurityTest, TestTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityPluginUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  g_fake2_cert_data_map->Set({{"", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
  SetLdsUpdate("", "", "fake_plugin2", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityCertificateNameUpdate) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}},
                              {"good", {root_cert_, identity_pair_2_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
  SetLdsUpdate("", "", "fake_plugin1", "good", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true),
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

TEST_P(XdsServerSecurityTest, TestMtlsToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeConnectionFailureRegex(
              "failed to connect to all addresses; last error: ",
              /*has_resolution_note=*/false));
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
}

TEST_P(XdsServerSecurityTest, TestTlsToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateTlsChannel(); }, RpcOptions(), {}, {},
          true /* test_expects_failure */, grpc::StatusCode::UNAVAILABLE,
          MakeConnectionFailureRegex(
              "failed to connect to all addresses; last error: ",
              /*has_resolution_note=*/false));
}

TEST_P(XdsServerSecurityTest, TestMtlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToMtls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestTlsToFallback) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToTls) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  SetLdsUpdate("", "", "", "", false);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          {});
}

//
// Basic RBAC tests
//

class XdsRbacTest : public XdsServerSecurityTest {
 protected:
  XdsRbacTest() {
    RegisterAuditLoggerFactory(
        std::make_unique<TestAuditLoggerFactory>(&audit_logs_));
  }

  ~XdsRbacTest() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }

  void SetServerRbacPolicies(Listener listener,
                             const std::vector<RBAC>& rbac_policies) {
    HttpConnectionManager http_connection_manager =
        ServerHcmAccessor().Unpack(listener);
    http_connection_manager.clear_http_filters();
    RouteConfiguration route_config = default_server_route_config_;
    int count = 0;
    for (auto& rbac : rbac_policies) {
      auto* filter = http_connection_manager.add_http_filters();
      std::string filter_name = absl::StrFormat("rbac%d", ++count);
      filter->set_name(filter_name);
      switch (GetParam().filter_config_setup()) {
        case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInListener:
          filter->mutable_typed_config()->PackFrom(rbac);
          break;
        case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute:
          filter->mutable_typed_config()->PackFrom(RBAC());
          google::protobuf::Any filter_config;
          RBACPerRoute rbac_per_route;
          *rbac_per_route.mutable_rbac() = rbac;
          filter_config.PackFrom(rbac_per_route);
          auto* config_map = route_config.mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_typed_per_filter_config();
          (*config_map)[filter_name] = std::move(filter_config);
      }
    }
    auto* filter = http_connection_manager.add_http_filters();
    filter->set_name("router");
    filter->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());
    ServerHcmAccessor().Pack(http_connection_manager, &listener);
    SetServerListenerNameAndRouteConfiguration(
        balancer_.get(), listener, backends_[0]->port(), route_config);
  }

  void SetServerRbacPolicy(Listener listener, const RBAC& rbac) {
    SetServerRbacPolicies(std::move(listener), {rbac});
  }

  void SetServerRbacPolicy(const RBAC& rbac) {
    SetServerRbacPolicy(default_server_listener_, rbac);
  }

  std::vector<std::string> audit_logs_;
};

// We test with and without RDS, and with the filter config both at the
// top level and in the route.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType().set_enable_rds_testing().set_bootstrap_source(
            XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

TEST_P(XdsRbacTest, AbsentRbacPolicy) {
  SetServerRbacPolicy(RBAC());
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // An absent RBAC policy leads to all RPCs being accepted.
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

TEST_P(XdsRbacTest, LogAction) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(RBAC_Action_LOG);
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A Log action is identical to no rbac policy being configured.
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

//
// RBAC tests with route config override always present
//

using XdsRbacTestWithRouteOverrideAlwaysPresent = XdsRbacTest;

// Run both with and without RDS.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithRouteOverrideAlwaysPresent,
    ::testing::Values(
        XdsTestType()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

TEST_P(XdsRbacTestWithRouteOverrideAlwaysPresent, EmptyRBACPerRouteOverride) {
  HttpConnectionManager http_connection_manager;
  Listener listener = default_server_listener_;
  RouteConfiguration route_config = default_server_route_config_;
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("rbac");
  // Create a top-level RBAC policy with a DENY action for all RPCs
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(RBAC_Action_DENY);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  filter->mutable_typed_config()->PackFrom(rbac);
  // Override with an Empty RBACPerRoute policy which should result in RBAC
  // being disabled and RPCs being allowed.
  google::protobuf::Any filter_config;
  filter_config.PackFrom(RBACPerRoute());
  auto* config_map = route_config.mutable_virtual_hosts(0)
                         ->mutable_routes(0)
                         ->mutable_typed_per_filter_config();
  (*config_map)["rbac"] = std::move(filter_config);
  filter = http_connection_manager.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), listener, backends_[0]->port(), route_config);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

// Test a non-empty top level RBAC with a non-empty RBACPerRouteOverride
TEST_P(XdsRbacTestWithRouteOverrideAlwaysPresent,
       NonEmptyTopLevelRBACNonEmptyPerRouteOverride) {
  HttpConnectionManager http_connection_manager;
  Listener listener = default_server_listener_;
  RouteConfiguration route_config = default_server_route_config_;
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("rbac");
  // Create a top-level RBAC policy with a DENY action for all RPCs
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(RBAC_Action_DENY);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  filter->mutable_typed_config()->PackFrom(rbac);
  // Override with a non-empty RBACPerRoute policy which allows all RPCs.
  google::protobuf::Any filter_config;
  RBACPerRoute rbac_per_route;
  rules = rbac_per_route.mutable_rbac()->mutable_rules();
  rules->set_action(RBAC_Action_ALLOW);
  (*rules->mutable_policies())["policy"] = policy;
  filter_config.PackFrom(RBACPerRoute());
  auto* config_map = route_config.mutable_virtual_hosts(0)
                         ->mutable_routes(0)
                         ->mutable_typed_per_filter_config();
  (*config_map)["rbac"] = std::move(filter_config);
  filter = http_connection_manager.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), listener, backends_[0]->port(), route_config);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {});
}

//
// RBAC tests with action permutations
//

using XdsRbacTestWithActionPermutations = XdsRbacTest;

// Run with and without RDS, with the filter config both at the top
// level and in the route, and without various actions.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithActionPermutations,
    ::testing::Values(
        XdsTestType()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

TEST_P(XdsRbacTestWithActionPermutations, EmptyRbacPolicy) {
  RBAC rbac;
  rbac.mutable_rules()->set_action(GetParam().rbac_action());
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // An empty RBAC policy leads to all RPCs being rejected.
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, MultipleRbacPolicies) {
  RBAC always_allow;
  auto* rules = always_allow.mutable_rules();
  rules->set_action(RBAC_Action_ALLOW);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, MethodPostPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("POST");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->set_allow_put_requests(true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test that an RPC with PUT method is handled properly.
  SendRpc([this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations,
       MethodPostPermissionWithStringMatcherAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":method");
  auto* string_match = header->mutable_string_match();
  string_match->set_exact("post");
  string_match->set_ignore_case(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->set_allow_put_requests(true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test that an RPC with PUT method is handled properly.
  SendRpc([this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, MethodGetPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("GET");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // TODO(yashykt): When we start supporting GET requests in the future, this
  // should be modified to test that they are accepted with this rule.
}

TEST_P(XdsRbacTestWithActionPermutations, MethodPutPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("PUT");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->set_allow_put_requests(true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test that an RPC with a PUT method gets accepted
  SendRpc(
      [this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, UrlPathPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test an RPC with a different URL path
  auto stub = grpc::testing::EchoTestService::NewStub(CreateInsecureChannel());
  ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(grpc_timeout_milliseconds_to_deadline(2000));
  EchoRequest request;
  request.set_message(kRequestMessage);
  EchoResponse response;
  Status status = stub->Echo1(&context, request, &response);
  EXPECT_TRUE(GetParam().rbac_action() == RBAC_Action_DENY ? status.ok()
                                                           : !status.ok())
      << status.error_code() << ", " << status.error_message() << ", "
      << status.error_details() << ", " << context.debug_error_string();
}

TEST_P(XdsRbacTestWithActionPermutations, DestinationIpPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* range = policy.add_permissions()->mutable_destination_ip();
  range->set_address_prefix(grpc_core::LocalIp());
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  range = policy.add_permissions()->mutable_destination_ip();
  range->set_address_prefix(grpc_core::RunningWithIPv6Only() ? "::2"
                                                             : "127.0.0.2");
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations,
       DestinationPortPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->set_destination_port(backends_[0]->port());
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  policy.add_permissions()->set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, MetadataPermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->mutable_metadata();
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test metadata with inverted match
  policy.clear_permissions();
  policy.add_permissions()->mutable_metadata()->set_invert(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, ReqServerNamePermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_principals()->set_any(true);
  policy.add_permissions()->mutable_requested_server_name()->set_exact(
      "server_name");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  policy.clear_permissions();
  policy.add_permissions()->mutable_requested_server_name()->set_exact("");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, NotRulePermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()
      ->mutable_not_rule()
      ->mutable_requested_server_name()
      ->set_exact("server_name");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  policy.add_permissions()->mutable_not_rule()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AndRulePermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* and_rules = policy.add_permissions()->mutable_and_rules();
  and_rules->add_rules()->set_any(true);
  and_rules->add_rules()->set_destination_port(backends_[0]->port());
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  and_rules = (*policy.mutable_permissions())[0].mutable_and_rules();
  (*and_rules->mutable_rules())[1].set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, OrRulePermissionAnyPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* or_rules = policy.add_permissions()->mutable_or_rules();
  or_rules->add_rules()->mutable_not_rule()->set_any(true);
  or_rules->add_rules()->set_destination_port(backends_[0]->port());
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  or_rules = (*policy.mutable_permissions())[0].mutable_or_rules();
  (*or_rules->mutable_rules())[1].set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionMethodPostPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_principals()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("POST");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->set_allow_put_requests(true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test that an RPC with PUT method is handled properly.
  SendRpc([this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionMethodGetPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_principals()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("GET");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // TODO(yashykt): When we start supporting GET requests in the future, this
  // should be modified to test that they are accepted with this rule.
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionMethodPutPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* header = policy.add_principals()->mutable_header();
  header->set_name(":method");
  header->set_exact_match("PUT");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->set_allow_put_requests(true);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // Test that an RPC with a PUT method gets accepted
  SendRpc(
      [this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionUrlPathPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_principals()->mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test an RPC with a different URL path
  auto stub = grpc::testing::EchoTestService::NewStub(CreateInsecureChannel());
  ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(grpc_timeout_milliseconds_to_deadline(2000));
  EchoRequest request;
  request.set_message(kRequestMessage);
  EchoResponse response;
  Status status = stub->Echo1(&context, request, &response);
  EXPECT_TRUE(GetParam().rbac_action() == RBAC_Action_DENY ? status.ok()
                                                           : !status.ok())
      << status.error_code() << ", " << status.error_message() << ", "
      << status.error_details() << ", " << context.debug_error_string();
}

TEST_P(XdsRbacTestWithActionPermutations,
       AnyPermissionDirectRemoteIpPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* range = policy.add_principals()->mutable_direct_remote_ip();
  range->set_address_prefix(grpc_core::LocalIp());
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  range = policy.add_principals()->mutable_direct_remote_ip();
  range->set_address_prefix(grpc_core::RunningWithIPv6Only() ? "::2"
                                                             : "127.0.0.2");
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionRemoteIpPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* range = policy.add_principals()->mutable_remote_ip();
  range->set_address_prefix(grpc_core::LocalIp());
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  range = policy.add_principals()->mutable_remote_ip();
  range->set_address_prefix(grpc_core::RunningWithIPv6Only() ? "::2"
                                                             : "127.0.0.2");
  range->mutable_prefix_len()->set_value(grpc_core::RunningWithIPv6Only() ? 128
                                                                          : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionAuthenticatedPrincipal) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  downstream_tls_context.mutable_require_client_certificate()->set_value(true);
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_principals()
      ->mutable_authenticated()
      ->mutable_principal_name()
      ->set_exact("*.test.google.fr");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(listener, rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateMtlsChannel(); },
          RpcOptions().set_wait_for_ready(true), server_authenticated_identity_,
          client_authenticated_identity_,
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionMetadataPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_principals()->mutable_metadata();
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Test metadata with inverted match
  policy.clear_principals();
  policy.add_principals()->mutable_metadata()->set_invert(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionNotIdPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_principals()
      ->mutable_not_id()
      ->mutable_url_path()
      ->mutable_path()
      ->set_exact("/grpc.testing.EchoTestService/Echo1");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  policy.add_principals()->mutable_not_id()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionAndIdPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* and_ids = policy.add_principals()->mutable_and_ids();
  and_ids->add_ids()->set_any(true);
  and_ids->add_ids()->mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  and_ids = (*policy.mutable_principals())[0].mutable_and_ids();
  (*and_ids->mutable_ids())[1].mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo1");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionOrIdPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* or_ids = policy.add_principals()->mutable_or_ids();
  or_ids->add_ids()->mutable_not_id()->set_any(true);
  or_ids->add_ids()->mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Change the policy itself for a negative test where there is no match.
  or_ids = (*policy.mutable_principals())[0].mutable_or_ids();
  (*or_ids->mutable_ids())[1].mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo1");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
}

TEST_P(XdsRbacTestWithActionPermutations,
       AuditLoggerNotInvokedOnAuditConditionNone) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  rbac.mutable_rules()->set_action(GetParam().rbac_action());
  auto* logging_options = rbac.mutable_rules()->mutable_audit_logging_options();
  auto* audit_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // An empty RBAC policy leads to all RPCs being rejected.
  SendRpc(
      [this]() { return CreateInsecureChannel(); },
      RpcOptions().set_wait_for_ready(true), {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  EXPECT_THAT(audit_logs_, ::testing::ElementsAre());
}

TEST_P(XdsRbacTestWithActionPermutations,
       MultipleRbacPoliciesWithAuditOnAllow) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC always_allow;
  auto* rules = always_allow.mutable_rules();
  rules->set_action(RBAC_Action_ALLOW);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW);
  auto* audit_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW);
  audit_logger = logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // If the second rbac denies the rpc, only one log from the first rbac.
  // Otherwise, all three rbacs log.
  std::vector<absl::string_view> expected(
      GetParam().rbac_action() != RBAC_Action_DENY ? 3 : 1,
      "{\"authorized\":true,\"matched_rule\":\"policy\","
      "\"policy_name\":\"\",\"principal\":\"\",\"rpc_"
      "method\":\"/grpc.testing.EchoTestService/Echo\"}");
  EXPECT_THAT(audit_logs_, ::testing::ElementsAreArray(expected));
}

TEST_P(XdsRbacTestWithActionPermutations, MultipleRbacPoliciesWithAuditOnDeny) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC always_allow;
  auto* rules = always_allow.mutable_rules();
  rules->set_action(RBAC_Action_ALLOW);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  auto* audit_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  audit_logger = logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // Only the second rbac logs if it denies the rpc.
  std::vector<absl::string_view> expected;
  if (GetParam().rbac_action() == RBAC_Action_DENY) {
    expected.push_back(
        "{\"authorized\":false,\"matched_rule\":\"policy\",\"policy_name\":"
        "\"\",\"principal\":\"\",\"rpc_method\":\"/"
        "grpc.testing.EchoTestService/Echo\"}");
  }
  EXPECT_THAT(audit_logs_, ::testing::ElementsAreArray(expected));
}

TEST_P(XdsRbacTestWithActionPermutations,
       MultipleRbacPoliciesWithAuditOnDenyAndAllow) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC always_allow;
  auto* rules = always_allow.mutable_rules();
  rules->set_action(RBAC_Action_ALLOW);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW);
  auto* audit_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW);
  audit_logger = logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  // If the second rbac denies the request, the last rbac won't log. Otherwise
  // all rbacs log.
  std::vector<absl::string_view> expected = {
      "{\"authorized\":true,\"matched_rule\":\"policy\",\"policy_name\":"
      "\"\",\"principal\":\"\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}"};
  if (GetParam().rbac_action() == RBAC_Action_DENY) {
    expected.push_back(
        "{\"authorized\":false,\"matched_rule\":\"policy\",\"policy_name\":"
        "\"\",\"principal\":\"\",\"rpc_method\":\"/"
        "grpc.testing.EchoTestService/Echo\"}");
  } else {
    expected = std::vector<absl::string_view>(
        3,
        "{\"authorized\":true,\"matched_rule\":\"policy\",\"policy_name\":"
        "\"\",\"principal\":\"\",\"rpc_method\":\"/"
        "grpc.testing.EchoTestService/Echo\"}");
  }
  EXPECT_THAT(audit_logs_, ::testing::ElementsAreArray(expected));
}

//
// RBAC tests with audit conditions
//

using XdsRbacTestWithActionAndAuditConditionPermutations = XdsRbacTest;

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithActionAndAuditConditionPermutations,
    ::testing::Values(
        XdsTestType()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

TEST_P(XdsRbacTestWithActionAndAuditConditionPermutations,
       AuditLoggingDisabled) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(GetParam().rbac_audit_condition());
  auto* audit_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  EXPECT_THAT(audit_logs_, ::testing::ElementsAre());
}

TEST_P(XdsRbacTestWithActionAndAuditConditionPermutations, MultipleLoggers) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(GetParam().rbac_audit_condition());
  auto* stdout_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  stdout_logger->mutable_typed_config()->set_type_url(
      "/envoy.extensions.rbac.audit_loggers.stream.v3.StdoutAuditLog");
  auto* test_logger =
      logging_options->add_logger_configs()->mutable_audit_logger();
  test_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  test_logger->mutable_typed_config()->PackFrom(typed_struct);
  SetServerRbacPolicy(rbac);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  auto action = GetParam().rbac_action();
  SendRpc([this]() { return CreateInsecureChannel(); },
          RpcOptions().set_wait_for_ready(true), {}, {},
          /*test_expects_failure=*/action == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED, "Unauthorized RPC rejected");
  auto audit_condition = GetParam().rbac_audit_condition();
  bool should_log =
      (audit_condition ==
       RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW) ||
      (action != RBAC_Action_DENY &&
       audit_condition == RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW) ||
      (action == RBAC_Action_DENY &&
       audit_condition == RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  if (should_log) {
    EXPECT_THAT(audit_logs_,
                ::testing::ElementsAre(absl::StrFormat(
                    "{\"authorized\":%s,\"matched_rule\":\"policy\","
                    "\"policy_name\":\"\",\"principal\":\"\","
                    "\"rpc_"
                    "method\":\"/grpc.testing.EchoTestService/Echo\"}",
                    action == RBAC_Action_DENY ? "false" : "true")));
  } else {
    EXPECT_THAT(audit_logs_, ::testing::ElementsAre());
  }
}

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
  overrides.trace =
      "call,channel,client_channel,client_channel_call,client_channel_lb_call,"
      "handshaker";
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
