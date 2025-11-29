//
// Copyright 2025 gRPC authors.
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

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using ::envoy::type::matcher::v3::StringMatcher;

constexpr char kClientKeyPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/client.key";
constexpr char kClientCertPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffe.pem";
constexpr char kClientSpiffeBundleMapPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffebundle.json";
constexpr char kServerSpiffeBundleMapPath[] =
    "test/core/tsi/test_creds/spiffe_end2end/server_spiffebundle.json";
constexpr char kBadClientCertPath[] = "src/core/tsi/test_creds/badclient.pem";
constexpr char kBadClientKeyPath[] = "src/core/tsi/test_creds/badclient.key";

// Based on StaticDataCertificateProvider, but provides alternate certificates
// if the certificate name is not empty.
class FakeCertificateProvider final : public grpc_tls_certificate_provider {
 public:
  struct CertData {
    std::string root_certificate;
    grpc_core::PemKeyCertPairList identity_key_cert_pairs;
    grpc_core::SpiffeBundleMap spiffe_bundle_map;
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
        std::shared_ptr<RootCertInfo> root_cert_info;
        std::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
        if (root_being_watched) {
          if (it->second.spiffe_bundle_map.size() != 0) {
            root_cert_info =
                std::make_shared<RootCertInfo>(it->second.spiffe_bundle_map);
          } else {
            root_cert_info =
                std::make_shared<RootCertInfo>(it->second.root_certificate);
          }
        }
        if (identity_being_watched) {
          pem_key_cert_pairs = it->second.identity_key_cert_pairs;
        }
        distributor_->SetKeyMaterials(cert_name, std::move(root_cert_info),
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
    fields.push_back(
        absl::StrFormat("        \"spiffe_bundle_map_file\": \"%s\"",
                        kClientSpiffeBundleMapPath));
    builder.AddCertificateProviderPlugin("file_plugin", "file_watcher",
                                         absl::StrJoin(fields, ",\n"));

    InitClient(builder, /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               CreateSpiffeXdsChannelCredentials());
    CreateAndStartBackends(2, /*xds_enabled=*/false,
                           CreateMtlsSpiffeServerCredentials());
    root_cert_ = grpc_core::testing::GetFileContents(kSpiffeCaCertPath);
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
    authenticated_identity_ = {
        "spiffe://foo.bar.com/9eebccd2-12bf-40a6-b262-65fe0487d453"};
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
  grpc_core::SpiffeBundleMap spiffe_bundle_map_;
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

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRootPluginUpdateSpiffe) {
  auto map = grpc_core::SpiffeBundleMap::FromFile(kClientSpiffeBundleMapPath);
  ASSERT_TRUE(map.ok());
  auto bad_map =
      grpc_core::SpiffeBundleMap::FromFile(kServerSpiffeBundleMapPath);
  ASSERT_TRUE(bad_map.ok());
  g_fake1_cert_data_map->Set({{"", {"", identity_pair_, *map}}});
  g_fake2_cert_data_map->Set({{"", {"", bad_identity_pair_, *bad_map}}});
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
  // grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_1;
  grpc::testing::g_fake1_cert_data_map = &cert_data_map_1;
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_2;
  grpc::testing::g_fake2_cert_data_map = &cert_data_map_2;
  grpc_core::CoreConfiguration::RegisterEphemeralBuilder(
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
