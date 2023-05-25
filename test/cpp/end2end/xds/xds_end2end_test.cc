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
#include "src/core/lib/resolver/server_address.h"
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
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::listener::v3::FilterChainMatch;
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
        absl::optional<std::string> root_certificate;
        absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
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
    explicit Config(const char* name) : name_(name) {}

    const char* name() const override { return name_; }

    std::string ToString() const override { return "{}"; }

   private:
    const char* name_;
  };

  FakeCertificateProviderFactory(
      const char* name,
      FakeCertificateProvider::CertDataMapWrapper* cert_data_map)
      : name_(name), cert_data_map_(cert_data_map) {
    GPR_ASSERT(cert_data_map != nullptr);
  }

  const char* name() const override { return name_; }

  grpc_core::RefCountedPtr<grpc_core::CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const grpc_core::Json& /*config_json*/,
                                  grpc_error_handle* /*error*/) override {
    return grpc_core::MakeRefCounted<Config>(name_);
  }

  grpc_core::RefCountedPtr<grpc_tls_certificate_provider>
  CreateCertificateProvider(
      grpc_core::RefCountedPtr<grpc_core::CertificateProviderFactory::Config>
      /*config*/) override {
    GPR_ASSERT(cert_data_map_ != nullptr);
    return grpc_core::MakeRefCounted<FakeCertificateProvider>(
        cert_data_map_->Get());
  }

 private:
  const char* name_;
  FakeCertificateProvider::CertDataMapWrapper* cert_data_map_;
};

// Global variables for each provider.
FakeCertificateProvider::CertDataMapWrapper* g_fake1_cert_data_map = nullptr;
FakeCertificateProvider::CertDataMapWrapper* g_fake2_cert_data_map = nullptr;

class XdsSecurityTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    BootstrapBuilder builder = BootstrapBuilder();
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
    CreateAndStartBackends(2);
    root_cert_ = ReadFile(kCaCertPath);
    bad_root_cert_ = ReadFile(kBadClientCertPath);
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

  // Sends CDS updates with the new security configuration and verifies that
  // after propagation, this new configuration is used for connections. If \a
  // identity_instance_name and \a root_instance_name are both empty,
  // connections are expected to use fallback credentials.
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
    if (!identity_instance_name.empty() || !root_instance_name.empty()) {
      auto* transport_socket = cluster.mutable_transport_socket();
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
    balancer_->ads_service()->SetCdsResource(cluster);
    // The updates might take time to have an effect, so use a retry loop.
    if (test_expects_failure) {
      SendRpcsUntil(
          DEBUG_LOCATION,
          [&](const RpcResult& result) {
            if (result.status.ok()) {
              gpr_log(GPR_ERROR,
                      "RPC succeeded. Failure expected. Trying again.");
              return true;
            }
            EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
            // TODO(yashkt): Change individual test cases to expect the exact
            // error message here.
            return false;
          },
          /* timeout_ms= */ 20 * 1000, RpcOptions().set_timeout_ms(5000));
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

class XdsEnabledServerTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {}  // No-op -- individual tests do this themselves.

  void DoSetUp(BootstrapBuilder builder = BootstrapBuilder()) {
    InitClient(builder);
    CreateBackends(1, /*xds_enabled=*/true);
    EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  }
};

TEST_P(XdsEnabledServerTest, Basic) {
  DoSetUp();
  backends_[0]->Start();
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(XdsEnabledServerTest, ListenerDeletionIgnored) {
  DoSetUp(BootstrapBuilder().SetIgnoreResourceDeletion());
  backends_[0]->Start();
  WaitForBackend(DEBUG_LOCATION, 0);
  // Check that we ACKed.
  // TODO(roth): There may be multiple entries in the resource state response
  // queue, because the client doesn't necessarily subscribe to all resources
  // in a single message, and the server currently (I suspect incorrectly?)
  // thinks that each subscription message is an ACK.  So for now, we
  // drain the entire LDS resource state response queue, ensuring that
  // all responses are ACKs.  Need to look more closely at the protocol
  // semantics here and make sure the server is doing the right thing,
  // in which case we may be able to avoid this.
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) break;
    ASSERT_TRUE(response_state.has_value());
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  }
  // Now unset the resource.
  balancer_->ads_service()->UnsetResource(
      kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  // Wait for update to be ACKed.
  absl::Time deadline =
      absl::Now() + (absl::Seconds(10) * grpc_test_slowdown_factor());
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) {
      gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
      continue;
    }
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
    ASSERT_LT(absl::Now(), deadline);
    break;
  }
  // Make sure server is still serving.
  CheckRpcSendOk(DEBUG_LOCATION);
}

// Testing just one example of an invalid resource here.
// Unit tests for XdsListenerResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(XdsEnabledServerTest, BadLdsUpdateNoApiListenerNorAddress) {
  DoSetUp();
  Listener listener = default_server_listener_;
  listener.clear_address();
  listener.set_name(GetServerListenerName(backends_[0]->port()));
  balancer_->ads_service()->SetLdsResource(listener);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::EndsWith(absl::StrCat(
                  GetServerListenerName(backends_[0]->port()),
                  ": INVALID_ARGUMENT: Listener has neither address nor "
                  "ApiListener]")));
}

// Verify that a non-TCP listener results in "not serving" status.
TEST_P(XdsEnabledServerTest, NonTcpListener) {
  DoSetUp();
  Listener listener = default_listener_;  // Client-side listener.
  listener = PopulateServerListenerNameAndPort(listener, backends_[0]->port());
  auto hcm = ClientHcmAccessor().Unpack(listener);
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  ClientHcmAccessor().Pack(hcm, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::FAILED_PRECONDITION);
}

// Verify that a mismatch of listening address results in "not serving"
// status.
TEST_P(XdsEnabledServerTest, ListenerAddressMismatch) {
  DoSetUp();
  Listener listener = default_server_listener_;
  // Set a different listening address in the LDS update
  listener.mutable_address()->mutable_socket_address()->set_address(
      "192.168.1.1");
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::FAILED_PRECONDITION);
}

class XdsServerSecurityTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    BootstrapBuilder builder = BootstrapBuilder();
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
                   ipv6_only_ ? "::1" : "127.0.0.1");
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = absl::StrCat(
        ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", backends_[0]->port());
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
                   ipv6_only_ ? "::1" : "127.0.0.1");
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = absl::StrCat(
        ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", backends_[0]->port());
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
                   ipv6_only_ ? "::1" : "127.0.0.1");
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    if (use_put_requests) {
      args.SetInt(GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS, 1);
    }
    std::string uri = absl::StrCat(
        ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", backends_[0]->port());
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

class XdsEnabledServerStatusNotificationTest : public XdsServerSecurityTest {
 protected:
  void SetValidLdsUpdate() { SetLdsUpdate("", "", "", "", false); }

  void SetInvalidLdsUpdate() {
    Listener listener = default_server_listener_;
    listener.clear_address();
    listener.set_name(absl::StrCat(
        "grpc/server?xds.resource.listening_address=",
        ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()));
    balancer_->ads_service()->SetLdsResource(listener);
  }

  void UnsetLdsUpdate() {
    balancer_->ads_service()->UnsetResource(
        kLdsTypeUrl, absl::StrCat("grpc/server?xds.resource.listening_address=",
                                  ipv6_only_ ? "[::1]:" : "127.0.0.1:",
                                  backends_[0]->port()));
  }
};

TEST_P(XdsEnabledServerStatusNotificationTest, ServingStatus) {
  SetValidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsEnabledServerStatusNotificationTest, NotServingStatus) {
  SetInvalidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::UNAVAILABLE);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsEnabledServerStatusNotificationTest, ErrorUpdateWhenAlreadyServing) {
  SetValidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  // Invalid update does not lead to a change in the serving status.
  SetInvalidLdsUpdate();
  do {
    SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  } while (!balancer_->ads_service()->lds_response_state().has_value());
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsEnabledServerStatusNotificationTest,
       NotServingStatusToServingStatusTransition) {
  SetInvalidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::UNAVAILABLE);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
  // Send a valid LDS update to change to serving status
  SetValidLdsUpdate();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

// This test verifies that the resource getting deleted when already serving
// results in future connections being dropped.
TEST_P(XdsEnabledServerStatusNotificationTest,
       ServingStatusToNonServingStatusTransition) {
  SetValidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  // Deleting the resource should result in a non-serving status.
  UnsetLdsUpdate();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::NOT_FOUND);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsEnabledServerStatusNotificationTest, RepeatedServingStatusChanges) {
  backends_[0]->Start();
  for (int i = 0; i < 5; i++) {
    // Send a valid LDS update to get the server to start listening
    SetValidLdsUpdate();
    backends_[0]->notifier()->WaitOnServingStatusChange(
        absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:",
                     backends_[0]->port()),
        grpc::StatusCode::OK);
    SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
    // Deleting the resource will make the server start rejecting connections
    UnsetLdsUpdate();
    backends_[0]->notifier()->WaitOnServingStatusChange(
        absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:",
                     backends_[0]->port()),
        grpc::StatusCode::NOT_FOUND);
    SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
            true /* test_expects_failure */);
  }
}

TEST_P(XdsEnabledServerStatusNotificationTest, ExistingRpcsOnResourceDeletion) {
  // Send a valid LDS update to get the server to start listening
  SetValidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  constexpr int kNumChannels = 10;
  struct StreamingRpc {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<grpc::testing::EchoTestService::Stub> stub;
    ClientContext context;
    std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream;
  } streaming_rpcs[kNumChannels];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  for (int i = 0; i < kNumChannels; i++) {
    streaming_rpcs[i].channel = CreateInsecureChannel();
    streaming_rpcs[i].stub =
        grpc::testing::EchoTestService::NewStub(streaming_rpcs[i].channel);
    streaming_rpcs[i].context.set_wait_for_ready(true);
    streaming_rpcs[i].stream =
        streaming_rpcs[i].stub->BidiStream(&streaming_rpcs[i].context);
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
  }
  // Deleting the resource will make the server start rejecting connections
  UnsetLdsUpdate();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::NOT_FOUND);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
  for (int i = 0; i < kNumChannels; i++) {
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
    EXPECT_TRUE(streaming_rpcs[i].stream->WritesDone());
    auto status = streaming_rpcs[i].stream->Finish();
    EXPECT_TRUE(status.ok())
        << status.error_message() << ", " << status.error_details() << ", "
        << streaming_rpcs[i].context.debug_error_string();
    // New RPCs on the existing channels should fail.
    ClientContext new_context;
    new_context.set_deadline(grpc_timeout_milliseconds_to_deadline(1000));
    EXPECT_FALSE(
        streaming_rpcs[i].stub->Echo(&new_context, request, &response).ok());
  }
}

TEST_P(XdsEnabledServerStatusNotificationTest,
       ExistingRpcsFailOnResourceUpdateAfterDrainGraceTimeExpires) {
  constexpr int kDrainGraceTimeMs = 100;
  xds_drain_grace_time_ms_ = kDrainGraceTimeMs;
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
  // Send a valid LDS update to get the server to start listening
  SetValidLdsUpdate();
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  constexpr int kNumChannels = 10;
  struct StreamingRpc {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<grpc::testing::EchoTestService::Stub> stub;
    ClientContext context;
    std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream;
  } streaming_rpcs[kNumChannels];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  for (int i = 0; i < kNumChannels; i++) {
    streaming_rpcs[i].channel = CreateInsecureChannel();
    streaming_rpcs[i].stub =
        grpc::testing::EchoTestService::NewStub(streaming_rpcs[i].channel);
    streaming_rpcs[i].context.set_wait_for_ready(true);
    streaming_rpcs[i].stream =
        streaming_rpcs[i].stub->BidiStream(&streaming_rpcs[i].context);
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
  }
  grpc_core::Timestamp update_time = NowFromCycleCounter();
  // Update the resource.
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  // Wait for the updated resource to take effect.
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  // After the drain grace time expires, the existing RPCs should all fail.
  for (int i = 0; i < kNumChannels; i++) {
    // Wait for the drain grace time to expire
    EXPECT_FALSE(streaming_rpcs[i].stream->Read(&response));
    // Make sure that the drain grace interval is honored.
    EXPECT_GE(NowFromCycleCounter() - update_time,
              grpc_core::Duration::Milliseconds(kDrainGraceTimeMs));
    auto status = streaming_rpcs[i].stream->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE)
        << status.error_code() << ", " << status.error_message() << ", "
        << status.error_details() << ", "
        << streaming_rpcs[i].context.debug_error_string();
  }
}

class XdsServerFilterChainMatchTest : public XdsServerSecurityTest {
 public:
  HttpConnectionManager GetHttpConnectionManager(const Listener& listener) {
    HttpConnectionManager http_connection_manager =
        ServerHcmAccessor().Unpack(listener);
    *http_connection_manager.mutable_route_config() =
        default_server_route_config_;
    return http_connection_manager;
  }
};

TEST_P(XdsServerFilterChainMatchTest,
       DefaultFilterChainUsedWhenNoFilterChainMentioned) {
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       DefaultFilterChainUsedWhenOtherFilterChainsDontMatch) {
  Listener listener = default_server_listener_;
  // Add a filter chain that will never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithDestinationPortDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with destination port that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerFilterChainMatchTest, FilterChainsWithServerNamesDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with server name that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithTransportProtocolsOtherThanRawBufferDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with transport protocol "tls" that should never match
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol("tls");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithApplicationProtocolsDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with application protocol that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithTransportProtocolRawBufferIsPreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with "raw_buffer" transport protocol
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  // Add another filter chain with no transport protocol set but application
  // protocol set (fails match)
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that filter chains that mention "raw_buffer" as
  // the transport protocol are chosen as the best match in the round.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificDestinationPrefixRangesArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with prefix range (length 4 and 16) but with server name
  // mentioned. (Prefix range is matched first.)
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  auto* prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(4);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(16);
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  // Add filter chain with two prefix ranges (length 8 and 24). Since 24 is
  // the highest match, it should be chosen.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(8);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(24);
  // Add another filter chain with a non-matching prefix range (with length
  // 30)
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix("192.168.1.1");
  prefix_range->mutable_prefix_len()->set_value(30);
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  // Add another filter chain with no prefix range mentioned
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that the filter chain with the longest matching
  // prefix range was the best match.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsThatMentionSourceTypeArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with the local source type (best match)
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add filter chain with the external source type but bad source port.
  // Note that backends_[0]->port() will never be a match for the source port
  // because it is already being used by a backend.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add filter chain with the default source type (ANY) but bad source port.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that the filter chain with the longest matching
  // prefix range was the best match.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificSourcePrefixRangesArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with source prefix range (length 16) but with a bad
  // source port mentioned. (Prefix range is matched first.) Note that
  // backends_[0]->port() will never be a match for the source port because it
  // is already being used by a backend.
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  auto* source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  source_prefix_range->mutable_prefix_len()->set_value(4);
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  source_prefix_range->mutable_prefix_len()->set_value(16);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add filter chain with two source prefix ranges (length 8 and 24). Since
  // 24 is the highest match, it should be chosen.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  source_prefix_range->mutable_prefix_len()->set_value(8);
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  source_prefix_range->mutable_prefix_len()->set_value(24);
  // Add another filter chain with a non-matching source prefix range (with
  // length 30) and bad source port
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix("192.168.1.1");
  source_prefix_range->mutable_prefix_len()->set_value(30);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add another filter chain with no source prefix range mentioned and bad
  // source port
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that the filter chain with the longest matching
  // source prefix range was the best match.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificSourcePortArePreferred) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  // Since we don't know which port will be used by the channel, just add all
  // ports except for 0.
  for (int i = 1; i < 65536; i++) {
    filter_chain->mutable_filter_chain_match()->add_source_ports(i);
  }
  // Add another filter chain with no source port mentioned with a bad
  // DownstreamTlsContext configuration.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that the filter chain with matching source port
  // was chosen.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

using XdsServerRdsTest = XdsEnabledServerStatusNotificationTest;

TEST_P(XdsServerRdsTest, Basic) {
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerRdsTest, FailsRouteMatchesOtherThanNonForwardingAction) {
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      default_route_config_ /* inappropriate route config for servers */);
  backends_[0]->Start();
  // The server should be ready to serve but RPCs should fail.
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

// Test that non-inline route configuration also works for non-default filter
// chains
TEST_P(XdsServerRdsTest, NonInlineRouteConfigurationNonDefaultFilterChain) {
  if (!GetParam().enable_rds_testing()) {
    return;
  }
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.add_filter_chains();
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultServerRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerRdsTest, NonInlineRouteConfigurationNotAvailable) {
  if (!GetParam().enable_rds_testing()) {
    return;
  }
  Listener listener = default_server_listener_;
  PopulateServerListenerNameAndPort(listener, backends_[0]->port());
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name("unknown_server_route_config");
  rds->mutable_config_source()->mutable_self();
  listener.add_filter_chains()->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          true /* test_expects_failure */);
}

// TODO(yashykt): Once https://github.com/grpc/grpc/issues/24035 is fixed, we
// should add tests that make sure that different route configs are used for
// incoming connections with a different match.
TEST_P(XdsServerRdsTest, MultipleRouteConfigurations) {
  Listener listener = default_server_listener_;
  // Set a filter chain with a new route config name
  auto new_route_config = default_server_route_config_;
  new_route_config.set_name("new_server_route_config");
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(new_route_config.name());
  rds->mutable_config_source()->mutable_self();
  listener.add_filter_chains()->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  // Set another filter chain with another route config name
  auto another_route_config = default_server_route_config_;
  another_route_config.set_name("another_server_route_config");
  http_connection_manager.mutable_rds()->set_route_config_name(
      another_route_config.name());
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add another filter chain with the same route config name
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  // Add another filter chain with an inline route config
  filter_chain = listener.add_filter_chains();
  filter_chain->mutable_filter_chain_match()->add_source_ports(1234);
  http_connection_manager = ServerHcmAccessor().Unpack(listener);
  *http_connection_manager.mutable_route_config() =
      default_server_route_config_;
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  // Set resources on the ADS service
  balancer_->ads_service()->SetRdsResource(new_route_config);
  balancer_->ads_service()->SetRdsResource(another_route_config);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

// Tests RBAC configurations on the server with RDS testing and route config
// override permutations.
class XdsRbacTest : public XdsServerRdsTest {
 protected:
  XdsRbacTest() {
    RegisterAuditLoggerFactory(
        std::make_unique<TestAuditLoggerFactory>(&audit_logs_));
  }

  ~XdsRbacTest() { AuditLoggerRegistry::TestOnlyResetRegistry(); }

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

TEST_P(XdsRbacTest, AbsentRbacPolicy) {
  SetServerRbacPolicy(RBAC());
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // An absent RBAC policy leads to all RPCs being accepted.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsRbacTest, LogAction) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(RBAC_Action_LOG);
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // A Log action is identical to no rbac policy being configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

// Tests RBAC policies where a route override is always present. Action
// permutations are not added.
using XdsRbacTestWithRouteOverrideAlwaysPresent = XdsRbacTest;

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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

// Adds Action Permutations to XdsRbacTest
using XdsRbacTestWithActionPermutations = XdsRbacTest;

TEST_P(XdsRbacTestWithActionPermutations, EmptyRbacPolicy) {
  RBAC rbac;
  rbac.mutable_rules()->set_action(GetParam().rbac_action());
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // An empty RBAC policy leads to all RPCs being rejected.
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
}

TEST_P(XdsRbacTestWithActionPermutations,
       AuditLoggerNotInvokedOnAuditConditionNone) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  rbac.mutable_rules()->set_action(GetParam().rbac_action());
  auto* logging_options = rbac.mutable_rules()->mutable_audit_logging_options();
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger = test_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // An empty RBAC policy leads to all RPCs being rejected.
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_TRUE(audit_logs_.empty());
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger = test_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // If the second rbac denies the rpc, only one log from the first rbac.
  // Otherwise, all three rbacs log.
  EXPECT_EQ(audit_logs_.size(),
            GetParam().rbac_action() == RBAC_Action_DENY ? 1 : 3);
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
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger = test_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Only the second rbac logs if it denies the rpc.
  EXPECT_EQ(audit_logs_.size(),
            GetParam().rbac_action() == RBAC_Action_DENY ? 1 : 0);
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
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger = test_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  RBAC rbac;
  rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  (*rules->mutable_policies())["policy"] = policy;
  logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicies(default_server_listener_,
                        {always_allow, rbac, always_allow});
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // If the second rbac denies the request, the last rbac won't log. Otherwise
  // all rbacs log.
  EXPECT_EQ(audit_logs_.size(),
            GetParam().rbac_action() == RBAC_Action_DENY ? 2 : 3);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Test that an RPC with PUT method is handled properly.
  SendRpc([this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
          {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // Test that an RPC with a PUT method gets accepted
  SendRpc(
      [this]() { return CreateInsecureChannel(/*use_put_requests=*/true); }, {},
      {},
      /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  range = policy.add_permissions()->mutable_destination_ip();
  range->set_address_prefix(ipv6_only_ ? "::2" : "127.0.0.2");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  policy.add_permissions()->set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // Test metadata with inverted match
  policy.clear_permissions();
  policy.add_permissions()->mutable_metadata()->set_invert(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  policy.clear_permissions();
  policy.add_permissions()->mutable_requested_server_name()->set_exact("");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_permissions();
  policy.add_permissions()->mutable_not_rule()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  and_rules = (*policy.mutable_permissions())[0].mutable_and_rules();
  (*and_rules->mutable_rules())[1].set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  or_rules = (*policy.mutable_permissions())[0].mutable_or_rules();
  (*or_rules->mutable_rules())[1].set_destination_port(1);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Test that an RPC with PUT method is handled properly.
  SendRpc([this]() { return CreateInsecureChannel(/*use_put_requests=*/true); },
          {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a PUT method gets accepted
  SendRpc(
      [this]() { return CreateInsecureChannel(/*use_put_requests=*/true); }, {},
      {},
      /*test_expects_failure=*/GetParam().rbac_action() != RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  range = policy.add_principals()->mutable_direct_remote_ip();
  range->set_address_prefix(ipv6_only_ ? "::2" : "127.0.0.2");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
}

TEST_P(XdsRbacTestWithActionPermutations, AnyPermissionRemoteIpPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(GetParam().rbac_action());
  Policy policy;
  auto* range = policy.add_principals()->mutable_remote_ip();
  range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  range = policy.add_principals()->mutable_remote_ip();
  range->set_address_prefix(ipv6_only_ ? "::2" : "127.0.0.2");
  range->mutable_prefix_len()->set_value(ipv6_only_ ? 128 : 32);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_,
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // Test metadata with inverted match
  policy.clear_principals();
  policy.add_principals()->mutable_metadata()->set_invert(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  policy.clear_principals();
  policy.add_principals()->mutable_not_id()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  and_ids = (*policy.mutable_principals())[0].mutable_and_ids();
  (*and_ids->mutable_ids())[1].mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo1");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // Change the policy itself for a negative test where there is no match.
  or_ids = (*policy.mutable_principals())[0].mutable_or_ids();
  (*or_ids->mutable_ids())[1].mutable_url_path()->mutable_path()->set_exact(
      "/grpc.testing.EchoTestService/Echo1");
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
}

// Adds Audit Condition Permutations to XdsRbacTest
using XdsRbacTestWithActionAndAuditConditionPermutations = XdsRbacTest;

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
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger = test_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_TRUE(audit_logs_.empty());
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
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      stdout_logger;
  auto* audit_logger = stdout_logger.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url(
      "/envoy.extensions.rbac.audit_loggers.stream.v3.StdoutAuditLog");
  *logging_options->add_logger_configs() = stdout_logger;
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      test_logger;
  auto* audit_logger2 = test_logger.mutable_audit_logger();
  audit_logger2->mutable_typed_config()->set_type_url("/test_logger");
  TypedStruct typed_struct;
  typed_struct.set_type_url("/test_logger");
  typed_struct.mutable_value()->mutable_fields();
  audit_logger2->mutable_typed_config()->PackFrom(typed_struct);
  *logging_options->add_logger_configs() = test_logger;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  auto action = GetParam().rbac_action();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/action == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
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
                    "\"policy_name\":\"rbac1\",\"principal\":\"\",\"rpc_"
                    "method\":\"/grpc.testing.EchoTestService/Echo\"}",
                    action == RBAC_Action_DENY ? "false" : "true")));
  } else {
    EXPECT_TRUE(audit_logs_.empty());
  }
}

// CDS depends on XdsResolver.
// Security depends on v3.
// Not enabling load reporting or RDS, since those are irrelevant to these
// tests.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsSecurityTest,
    ::testing::Values(XdsTestType().set_use_xds_credentials()),
    &XdsTestType::Name);

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsEnabledServerTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

// We are only testing the server here.
// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsServerSecurityTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_xds_credentials()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsEnabledServerStatusNotificationTest,
    ::testing::Values(XdsTestType().set_use_xds_credentials()),
    &XdsTestType::Name);

// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsServerFilterChainMatchTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_xds_credentials()),
    &XdsTestType::Name);

// Test xDS-enabled server with and without RDS.
// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsServerRdsTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_xds_credentials(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_xds_credentials()
            .set_enable_rds_testing()),
    &XdsTestType::Name);

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTest,
    ::testing::Values(
        XdsTestType().set_use_xds_credentials().set_bootstrap_source(
            XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithRouteOverrideAlwaysPresent,
    ::testing::Values(
        XdsTestType()
            .set_use_xds_credentials()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithActionPermutations,
    ::testing::Values(
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_filter_config_setup(
                XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)
            .set_rbac_action(RBAC_Action_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacTestWithActionAndAuditConditionPermutations,
    ::testing::Values(
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_ALLOW)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_use_xds_credentials()
            .set_enable_rds_testing()
            .set_rbac_action(RBAC_Action_DENY)
            .set_rbac_audit_condition(
                RBAC_AuditLoggingOptions_AuditCondition_ON_DENY_AND_ALLOW)
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)),
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
