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
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/xds_server_builder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/xds/ads_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/cds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/eds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/lds_rds_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/lrs_for_test.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/ads.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/counted_service.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/end2end/xds/xds_server.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

#ifndef DISABLED_XDS_PROTO_IN_CC
#include "src/cpp/server/csds/csds.h"
#include "src/proto/grpc/testing/xds/v3/csds.grpc.pb.h"
#endif  // DISABLED_XDS_PROTO_IN_CC

namespace grpc {
namespace testing {
namespace {

using std::chrono::system_clock;

#ifndef DISABLED_XDS_PROTO_IN_CC
using ::envoy::admin::v3::ClientResourceStatus;
#endif  // DISABLED_XDS_PROTO_IN_CC
using ::envoy::config::cluster::v3::CircuitBreakers;
using ::envoy::config::cluster::v3::Cluster;
using ::envoy::config::cluster::v3::CustomClusterType;
using ::envoy::config::cluster::v3::RoutingPriority;
using ::envoy::config::endpoint::v3::ClusterLoadAssignment;
using ::envoy::config::endpoint::v3::HealthStatus;
using ::envoy::config::listener::v3::FilterChainMatch;
using ::envoy::config::listener::v3::Listener;
using ::envoy::config::route::v3::RouteConfiguration;
using ::envoy::extensions::clusters::aggregate::v3::ClusterConfig;
using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpConnectionManager;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using ::envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using ::envoy::type::matcher::v3::StringMatcher;
using ::envoy::type::v3::FractionalPercent;

using ClientStats = LrsServiceImpl::ClientStats;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::grpc::experimental::StaticDataCertificateProvider;

constexpr char kDefaultLocalityRegion[] = "xds_default_locality_region";
constexpr char kDefaultLocalityZone[] = "xds_default_locality_zone";
constexpr char kLbDropType[] = "lb";
constexpr char kThrottleDropType[] = "throttle";
constexpr char kServerName[] = "server.example.com";
constexpr char kDefaultRouteConfigurationName[] = "route_config_name";
constexpr char kDefaultServerRouteConfigurationName[] =
    "default_server_route_config_name";
constexpr char kDefaultClusterName[] = "cluster_name";
constexpr char kDefaultEdsServiceName[] = "eds_service_name";
constexpr int kDefaultLocalityWeight = 3;
constexpr int kDefaultLocalityPriority = 0;

constexpr char kRequestMessage[] = "Live long and prosper.";
constexpr char kDefaultServiceConfig[] =
    "{\n"
    "  \"loadBalancingConfig\":[\n"
    "    { \"does_not_exist\":{} },\n"
    "    { \"xds_cluster_resolver_experimental\":{\n"
    "      \"discoveryMechanisms\": [\n"
    "      { \"clusterName\": \"server.example.com\",\n"
    "        \"type\": \"EDS\",\n"
    "        \"lrsLoadReportingServerName\": \"\"\n"
    "      } ]\n"
    "    } }\n"
    "  ]\n"
    "}";
constexpr char kDefaultServiceConfigWithoutLoadReporting[] =
    "{\n"
    "  \"loadBalancingConfig\":[\n"
    "    { \"does_not_exist\":{} },\n"
    "    { \"xds_cluster_resolver_experimental\":{\n"
    "      \"discoveryMechanisms\": [\n"
    "      { \"clusterName\": \"server.example.com\",\n"
    "        \"type\": \"EDS\"\n"
    "      } ]\n"
    "    } }\n"
    "  ]\n"
    "}";

constexpr char kBootstrapFileV3[] =
    "{\n"
    "  \"xds_servers\": [\n"
    "    {\n"
    "      \"server_uri\": \"fake:///xds_server\",\n"
    "      \"channel_creds\": [\n"
    "        {\n"
    "          \"type\": \"fake\"\n"
    "        }\n"
    "      ],\n"
    "      \"server_features\": [\"xds_v3\"]\n"
    "    }\n"
    "  ],\n"
    "  \"node\": {\n"
    "    \"id\": \"xds_end2end_test\",\n"
    "    \"cluster\": \"test\",\n"
    "    \"metadata\": {\n"
    "      \"foo\": \"bar\"\n"
    "    },\n"
    "    \"locality\": {\n"
    "      \"region\": \"corp\",\n"
    "      \"zone\": \"svl\",\n"
    "      \"sub_zone\": \"mp3\"\n"
    "    }\n"
    "  },\n"
    "  \"server_listener_resource_name_template\": "
    "\"grpc/server?xds.resource.listening_address=%s\",\n"
    "  \"certificate_providers\": {\n"
    "    \"fake_plugin1\": {\n"
    "      \"plugin_name\": \"fake1\"\n"
    "    },\n"
    "    \"fake_plugin2\": {\n"
    "      \"plugin_name\": \"fake2\"\n"
    "    },\n"
    "    \"file_plugin\": {\n"
    "      \"plugin_name\": \"file_watcher\",\n"
    "      \"config\": {\n"
    "        \"certificate_file\": \"src/core/tsi/test_creds/client.pem\",\n"
    "        \"private_key_file\": \"src/core/tsi/test_creds/client.key\",\n"
    "        \"ca_certificate_file\": \"src/core/tsi/test_creds/ca.pem\"\n"
    "      }"
    "    }\n"
    "  }\n"
    "}\n";

constexpr char kBootstrapFileV2[] =
    "{\n"
    "  \"xds_servers\": [\n"
    "    {\n"
    "      \"server_uri\": \"fake:///xds_server\",\n"
    "      \"channel_creds\": [\n"
    "        {\n"
    "          \"type\": \"fake\"\n"
    "        }\n"
    "      ]\n"
    "    }\n"
    "  ],\n"
    "  \"node\": {\n"
    "    \"id\": \"xds_end2end_test\",\n"
    "    \"cluster\": \"test\",\n"
    "    \"metadata\": {\n"
    "      \"foo\": \"bar\"\n"
    "    },\n"
    "    \"locality\": {\n"
    "      \"region\": \"corp\",\n"
    "      \"zone\": \"svl\",\n"
    "      \"sub_zone\": \"mp3\"\n"
    "    }\n"
    "  }\n"
    "}\n";
constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kBadClientCertPath[] = "src/core/tsi/test_creds/badclient.pem";
constexpr char kBadClientKeyPath[] = "src/core/tsi/test_creds/badclient.key";

char* g_bootstrap_file_v3;
char* g_bootstrap_file_v2;

void WriteBootstrapFiles() {
  char* bootstrap_file;
  FILE* out = gpr_tmpfile("xds_bootstrap_v3", &bootstrap_file);
  fputs(kBootstrapFileV3, out);
  fclose(out);
  g_bootstrap_file_v3 = bootstrap_file;
  out = gpr_tmpfile("xds_bootstrap_v2", &bootstrap_file);
  fputs(kBootstrapFileV2, out);
  fclose(out);
  g_bootstrap_file_v2 = bootstrap_file;
}

template <typename RpcService>
class BackendServiceImpl
    : public CountedService<TestMultipleServiceImpl<RpcService>> {
 public:
  BackendServiceImpl() {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    auto peer_identity = context->auth_context()->GetPeerIdentity();
    CountedService<TestMultipleServiceImpl<RpcService>>::IncreaseRequestCount();
    const auto status =
        TestMultipleServiceImpl<RpcService>::Echo(context, request, response);
    CountedService<
        TestMultipleServiceImpl<RpcService>>::IncreaseResponseCount();
    {
      grpc_core::MutexLock lock(&mu_);
      clients_.insert(context->peer());
      last_peer_identity_.clear();
      for (const auto& entry : peer_identity) {
        last_peer_identity_.emplace_back(entry.data(), entry.size());
      }
    }
    return status;
  }

  Status Echo1(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) override {
    return Echo(context, request, response);
  }

  Status Echo2(ServerContext* context, const EchoRequest* request,
               EchoResponse* response) override {
    return Echo(context, request, response);
  }

  void Start() {}
  void Shutdown() {}

  std::set<std::string> clients() {
    grpc_core::MutexLock lock(&mu_);
    return clients_;
  }

  const std::vector<std::string>& last_peer_identity() {
    grpc_core::MutexLock lock(&mu_);
    return last_peer_identity_;
  }

 private:
  grpc_core::Mutex mu_;
  std::set<std::string> clients_ ABSL_GUARDED_BY(mu_);
  std::vector<std::string> last_peer_identity_ ABSL_GUARDED_BY(mu_);
};

class TestType {
 public:
  enum FilterConfigSetup {
    // Set the fault injection filter directly from LDS
    kHTTPConnectionManagerOriginal,
    // Enable the fault injection filter in LDS, but override the filter config
    // in route.
    kRouteOverride,
  };

  enum BootstrapSource {
    kBootstrapFromChannelArg,
    kBootstrapFromFile,
    kBootstrapFromEnvVar,
  };

  TestType& set_use_fake_resolver() {
    use_fake_resolver_ = true;
    return *this;
  }

  TestType& set_enable_load_reporting() {
    enable_load_reporting_ = true;
    return *this;
  }

  TestType& set_enable_rds_testing() {
    enable_rds_testing_ = true;
    return *this;
  }

  TestType& set_use_v2() {
    use_v2_ = true;
    return *this;
  }

  TestType& set_use_xds_credentials() {
    use_xds_credentials_ = true;
    return *this;
  }

  TestType& set_use_csds_streaming() {
    use_csds_streaming_ = true;
    return *this;
  }

  TestType& set_filter_config_setup(FilterConfigSetup setup) {
    filter_config_setup_ = setup;
    return *this;
  }

  TestType& set_bootstrap_source(BootstrapSource bootstrap_source) {
    bootstrap_source_ = bootstrap_source;
    return *this;
  }

  bool use_fake_resolver() const { return use_fake_resolver_; }
  bool enable_load_reporting() const { return enable_load_reporting_; }
  bool enable_rds_testing() const { return enable_rds_testing_; }
  bool use_v2() const { return use_v2_; }
  bool use_xds_credentials() const { return use_xds_credentials_; }
  bool use_csds_streaming() const { return use_csds_streaming_; }
  FilterConfigSetup filter_config_setup() const { return filter_config_setup_; }
  BootstrapSource bootstrap_source() const { return bootstrap_source_; }

  std::string AsString() const {
    std::string retval = (use_fake_resolver_ ? "FakeResolver" : "XdsResolver");
    retval += (use_v2_ ? "V2" : "V3");
    if (enable_load_reporting_) retval += "WithLoadReporting";
    if (enable_rds_testing_) retval += "Rds";
    if (use_xds_credentials_) retval += "XdsCreds";
    if (use_csds_streaming_) retval += "CsdsStreaming";
    if (filter_config_setup_ == kRouteOverride) {
      retval += "FilterPerRouteOverride";
    }
    if (bootstrap_source_ == kBootstrapFromFile) {
      retval += "BootstrapFromFile";
    } else if (bootstrap_source_ == kBootstrapFromEnvVar) {
      retval += "BootstrapFromEnvVar";
    }
    return retval;
  }

 private:
  bool use_fake_resolver_ = false;
  bool enable_load_reporting_ = false;
  bool enable_rds_testing_ = false;
  bool use_v2_ = false;
  bool use_xds_credentials_ = false;
  bool use_csds_streaming_ = false;
  FilterConfigSetup filter_config_setup_ = kHTTPConnectionManagerOriginal;
  BootstrapSource bootstrap_source_ = kBootstrapFromChannelArg;
};

std::string ReadFile(const char* file_path) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(file_path, 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

grpc_core::PemKeyCertPairList ReadTlsIdentityPair(const char* key_path,
                                                  const char* cert_path) {
  return grpc_core::PemKeyCertPairList{
      grpc_core::PemKeyCertPair(ReadFile(key_path), ReadFile(cert_path))};
}

// Based on StaticDataCertificateProvider, but provides alternate certificates
// if the certificate name is not empty.
class FakeCertificateProvider final : public grpc_tls_certificate_provider {
 public:
  struct CertData {
    std::string root_certificate;
    grpc_core::PemKeyCertPairList identity_key_cert_pairs;
  };

  using CertDataMap = std::map<std::string /*cert_name */, CertData>;

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
        grpc_error_handle error =
            GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
                "No certificates available for cert_name \"", cert_name, "\""));
        distributor_->SetErrorForCert(cert_name, GRPC_ERROR_REF(error),
                                      GRPC_ERROR_REF(error));
        GRPC_ERROR_UNREF(error);
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

 private:
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
      const char* name, FakeCertificateProvider::CertDataMap** cert_data_map)
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
    if (*cert_data_map_ == nullptr) return nullptr;
    return grpc_core::MakeRefCounted<FakeCertificateProvider>(**cert_data_map_);
  }

 private:
  const char* name_;
  FakeCertificateProvider::CertDataMap** cert_data_map_;
};

// Global variables for each provider.
FakeCertificateProvider::CertDataMap* g_fake1_cert_data_map = nullptr;
FakeCertificateProvider::CertDataMap* g_fake2_cert_data_map = nullptr;

std::shared_ptr<ChannelCredentials> CreateTlsFallbackCredentials() {
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
  options.set_certificate_verifier(std::move(verifier));
  options.set_verify_server_certs(true);
  options.set_check_call_host(false);
  auto channel_creds = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_creds.get() != nullptr);
  return channel_creds;
}

// A No-op HTTP filter used for verifying parsing logic.
class NoOpHttpFilter : public grpc_core::XdsHttpFilterImpl {
 public:
  NoOpHttpFilter(std::string name, bool supported_on_clients,
                 bool supported_on_servers, bool is_terminal_filter)
      : name_(std::move(name)),
        supported_on_clients_(supported_on_clients),
        supported_on_servers_(supported_on_servers),
        is_terminal_filter_(is_terminal_filter) {}

  void PopulateSymtab(upb_symtab* /* symtab */) const override {}

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfig(upb_strview /* serialized_filter_config */,
                       upb_arena* /* arena */) const override {
    return grpc_core::XdsHttpFilterImpl::FilterConfig{name_, grpc_core::Json()};
  }

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfigOverride(upb_strview /*serialized_filter_config*/,
                               upb_arena* /*arena*/) const override {
    return grpc_core::XdsHttpFilterImpl::FilterConfig{name_, grpc_core::Json()};
  }

  const grpc_channel_filter* channel_filter() const override { return nullptr; }

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::ServiceConfigJsonEntry>
  GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return grpc_core::XdsHttpFilterImpl::ServiceConfigJsonEntry{name_, ""};
  }

  bool IsSupportedOnClients() const override { return supported_on_clients_; }

  bool IsSupportedOnServers() const override { return supported_on_servers_; }

  bool IsTerminalFilter() const override { return is_terminal_filter_; }

 private:
  const std::string name_;
  const bool supported_on_clients_;
  const bool supported_on_servers_;
  const bool is_terminal_filter_;
};

// There is slight difference between time fetched by GPR and by C++ system
// clock API. It's unclear if they are using the same syscall, but we do know
// GPR round the number at millisecond-level. This creates a 1ms difference,
// which could cause flake.
grpc_millis NowFromCycleCounter() {
  return grpc_timespec_to_millis_round_down(gpr_now(GPR_CLOCK_MONOTONIC));
}

// Returns the number of RPCs needed to pass error_tolerance at 99.99994%
// chance. Rolling dices in drop/fault-injection generates a binomial
// distribution (if our code is not horribly wrong). Let's make "n" the number
// of samples, "p" the probability. If we have np>5 & n(1-p)>5, we can
// approximately treat the binomial distribution as a normal distribution.
//
// For normal distribution, we can easily look up how many standard deviation we
// need to reach 99.995%. Based on Wiki's table
// https://en.wikipedia.org/wiki/68%E2%80%9395%E2%80%9399.7_rule, we need 5.00
// sigma (standard deviation) to cover the probability area of 99.99994%. In
// another word, for a sample with size "n" probability "p" error-tolerance "k",
// we want the error always land within 5.00 sigma. The sigma of binominal
// distribution and be computed as sqrt(np(1-p)). Hence, we have the equation:
//
//   kn <= 5.00 * sqrt(np(1-p))
size_t ComputeIdealNumRpcs(double p, double error_tolerance) {
  GPR_ASSERT(p >= 0 && p <= 1);
  size_t num_rpcs =
      ceil(p * (1 - p) * 5.00 * 5.00 / error_tolerance / error_tolerance);
  gpr_log(GPR_INFO,
          "Sending %" PRIuPTR " RPCs for percentage=%.3f error_tolerance=%.3f",
          num_rpcs, p, error_tolerance);
  return num_rpcs;
}

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

class XdsEnd2endTest : public ::testing::TestWithParam<TestType> {
 protected:
  // TODO(roth): We currently set the number of backends and number of
  // balancers on a per-test-suite basis, not a per-test-case basis.
  // However, not every individual test case in a given test suite uses
  // the same number of backends or balancers, so we wind up having to
  // set the numbers for the test suite to the max number needed by any
  // one test case in that test suite.  This results in starting more
  // servers (and using more ports) than we actually need.  When we have
  // time, change each test to directly start the number of backends and
  // balancers that it needs, so that we aren't wasting resources.
  XdsEnd2endTest(size_t num_backends, size_t num_balancers,
                 int client_load_reporting_interval_seconds = 100,
                 int xds_resource_does_not_exist_timeout_ms = 0,
                 bool use_xds_enabled_server = false)
      : num_backends_(num_backends),
        num_balancers_(num_balancers),
        client_load_reporting_interval_seconds_(
            client_load_reporting_interval_seconds),
        xds_resource_does_not_exist_timeout_ms_(
            xds_resource_does_not_exist_timeout_ms),
        use_xds_enabled_server_(use_xds_enabled_server) {
    bool localhost_resolves_to_ipv4 = false;
    bool localhost_resolves_to_ipv6 = false;
    grpc_core::LocalhostResolves(&localhost_resolves_to_ipv4,
                                 &localhost_resolves_to_ipv6);
    ipv6_only_ = !localhost_resolves_to_ipv4 && localhost_resolves_to_ipv6;
    // Initialize default xDS resources.
    // Construct LDS resource.
    default_listener_.set_name(kServerName);
    HttpConnectionManager http_connection_manager;
    if (!GetParam().use_v2()) {
      auto* filter = http_connection_manager.add_http_filters();
      filter->set_name("router");
      filter->mutable_typed_config()->PackFrom(
          envoy::extensions::filters::http::router::v3::Router());
    }
    default_listener_.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    // Construct RDS resource.
    default_route_config_.set_name(kDefaultRouteConfigurationName);
    auto* virtual_host = default_route_config_.add_virtual_hosts();
    virtual_host->add_domains("*");
    auto* route = virtual_host->add_routes();
    route->mutable_match()->set_prefix("");
    route->mutable_route()->set_cluster(kDefaultClusterName);
    // Construct CDS resource.
    default_cluster_.set_name(kDefaultClusterName);
    default_cluster_.set_type(Cluster::EDS);
    auto* eds_config = default_cluster_.mutable_eds_cluster_config();
    eds_config->mutable_eds_config()->mutable_ads();
    eds_config->set_service_name(kDefaultEdsServiceName);
    default_cluster_.set_lb_policy(Cluster::ROUND_ROBIN);
    if (GetParam().enable_load_reporting()) {
      default_cluster_.mutable_lrs_server()->mutable_self();
    }
    // Construct a default server-side RDS resource for tests to use.
    default_server_route_config_.set_name(kDefaultServerRouteConfigurationName);
    virtual_host = default_server_route_config_.add_virtual_hosts();
    virtual_host->add_domains("*");
    route = virtual_host->add_routes();
    route->mutable_match()->set_prefix("");
    route->mutable_non_forwarding_action();
    // Construct a default server-side Listener resource
    default_server_listener_.mutable_address()
        ->mutable_socket_address()
        ->set_address(ipv6_only_ ? "::1" : "127.0.0.1");
    default_server_listener_.mutable_default_filter_chain()
        ->add_filters()
        ->mutable_typed_config()
        ->PackFrom(http_connection_manager);
    // Create the backends but don't start them yet. We need to create the
    // backends to allocate the ports, so that we know what resource names to
    // populate in the xDS servers when we start them. However, we can't start
    // the backends until after we've started the xDS servers, because in the
    // tests that use xDS-enabled servers, the backends will try to contact the
    // xDS servers as soon as they start up.
    for (size_t i = 0; i < num_backends_; ++i) {
      backends_.emplace_back(
          new BackendServerThread(this, use_xds_enabled_server_));
    }
    // Start the load balancers.
    for (size_t i = 0; i < num_balancers_; ++i) {
      balancers_.emplace_back(new BalancerServerThread(
          this, GetParam().enable_load_reporting()
                    ? client_load_reporting_interval_seconds_
                    : 0));
      balancers_.back()->Start();
      // Initialize resources.
      SetListenerAndRouteConfiguration(i, default_listener_,
                                       default_route_config_);
      if (use_xds_enabled_server_) {
        for (const auto& backend : backends_) {
          SetServerListenerNameAndRouteConfiguration(
              i, default_server_listener_, backend->port(),
              default_server_route_config_);
        }
      }
      balancers_.back()->ads_service()->SetCdsResource(default_cluster_);
    }
    // Create fake resolver response generators used by client.
    if (GetParam().use_fake_resolver()) {
      response_generator_ =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    }
    logical_dns_cluster_resolver_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    lb_channel_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    // Construct channel args for XdsClient.
    xds_channel_args_to_add_.emplace_back(
        grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
            lb_channel_response_generator_.get()));
    if (xds_resource_does_not_exist_timeout_ms_ > 0) {
      xds_channel_args_to_add_.emplace_back(grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS),
          xds_resource_does_not_exist_timeout_ms_));
    }
    xds_channel_args_.num_args = xds_channel_args_to_add_.size();
    xds_channel_args_.args = xds_channel_args_to_add_.data();
    // Initialize XdsClient state.
    // TODO(roth): Consider changing this to dynamically generate the
    // bootstrap config in each individual test instead of hard-coding
    // the contents here.  That would allow us to use an ipv4: or ipv6:
    // URI for the xDS server instead of using the fake resolver.
    if (GetParam().bootstrap_source() == TestType::kBootstrapFromEnvVar) {
      gpr_setenv("GRPC_XDS_BOOTSTRAP_CONFIG",
                 GetParam().use_v2() ? kBootstrapFileV2 : kBootstrapFileV3);
    } else if (GetParam().bootstrap_source() == TestType::kBootstrapFromFile) {
      gpr_setenv("GRPC_XDS_BOOTSTRAP", GetParam().use_v2()
                                           ? g_bootstrap_file_v2
                                           : g_bootstrap_file_v3);
    }
    if (GetParam().bootstrap_source() != TestType::kBootstrapFromChannelArg) {
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

  ~XdsEnd2endTest() override {
    ShutdownAllBackends();
    for (auto& balancer : balancers_) balancer->Shutdown();
    // Clear global xDS channel args, since they will go out of scope
    // when this test object is destroyed.
    grpc_core::internal::SetXdsChannelArgsForTest(nullptr);
    gpr_unsetenv("GRPC_XDS_BOOTSTRAP");
    gpr_unsetenv("GRPC_XDS_BOOTSTRAP_CONFIG");
  }

  const char* DefaultEdsServiceName() const {
    return GetParam().use_fake_resolver() ? kServerName
                                          : kDefaultEdsServiceName;
  }

  void StartAllBackends() {
    for (auto& backend : backends_) backend->Start();
  }

  void StartBackend(size_t index) { backends_[index]->Start(); }

  void ShutdownAllBackends() {
    for (auto& backend : backends_) backend->Shutdown();
  }

  void ShutdownBackend(size_t index) { backends_[index]->Shutdown(); }

  void ResetStub(int failover_timeout = 0) {
    channel_ = CreateChannel(failover_timeout);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
    stub1_ = grpc::testing::EchoTest1Service::NewStub(channel_);
    stub2_ = grpc::testing::EchoTest2Service::NewStub(channel_);
  }

  std::shared_ptr<Channel> CreateChannel(
      int failover_timeout = 0, const char* server_name = kServerName,
      grpc_core::FakeResolverResponseGenerator* response_generator = nullptr,
      grpc_channel_args* xds_channel_args = nullptr) {
    ChannelArguments args;
    if (failover_timeout > 0) {
      args.SetInt(GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS, failover_timeout);
    }
    // If the parent channel is using the fake resolver, we inject the
    // response generator here.
    if (GetParam().use_fake_resolver()) {
      if (response_generator == nullptr) {
        response_generator = response_generator_.get();
      }
      args.SetPointerWithVtable(
          GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR, response_generator,
          &grpc_core::FakeResolverResponseGenerator::kChannelArgPointerVtable);
    }
    if (GetParam().bootstrap_source() == TestType::kBootstrapFromChannelArg) {
      // We're getting the bootstrap from a channel arg, so we do the
      // same thing for the response generator to use for the xDS
      // channel and the xDS resource-does-not-exist timeout value.
      args.SetString(GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG,
                     GetParam().use_v2() ? kBootstrapFileV2 : kBootstrapFileV3);
      if (xds_channel_args == nullptr) xds_channel_args = &xds_channel_args_;
      args.SetPointerWithVtable(
          GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS,
          xds_channel_args, &kChannelArgsArgVtable);
    }
    args.SetPointerWithVtable(
        GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR,
        logical_dns_cluster_resolver_response_generator_.get(),
        &grpc_core::FakeResolverResponseGenerator::kChannelArgPointerVtable);
    std::string uri = absl::StrCat(
        GetParam().use_fake_resolver() ? "fake" : "xds", ":///", server_name);
    std::shared_ptr<ChannelCredentials> channel_creds =
        GetParam().use_xds_credentials()
            ? XdsCredentials(CreateTlsFallbackCredentials())
            : std::make_shared<SecureChannelCredentials>(
                  grpc_fake_transport_security_credentials_create());
    return ::grpc::CreateCustomChannel(uri, channel_creds, args);
  }

  enum RpcService {
    SERVICE_ECHO,
    SERVICE_ECHO1,
    SERVICE_ECHO2,
  };

  enum RpcMethod {
    METHOD_ECHO,
    METHOD_ECHO1,
    METHOD_ECHO2,
  };

  struct RpcOptions {
    RpcService service = SERVICE_ECHO;
    RpcMethod method = METHOD_ECHO;
    int timeout_ms = 1000;
    bool wait_for_ready = false;
    bool server_fail = false;
    std::vector<std::pair<std::string, std::string>> metadata;
    int server_sleep_us = 0;
    int client_cancel_after_us = 0;
    bool skip_cancelled_check = false;
    StatusCode server_expected_error = StatusCode::OK;

    RpcOptions() {}

    RpcOptions& set_rpc_service(RpcService rpc_service) {
      service = rpc_service;
      return *this;
    }

    RpcOptions& set_rpc_method(RpcMethod rpc_method) {
      method = rpc_method;
      return *this;
    }

    RpcOptions& set_timeout_ms(int rpc_timeout_ms) {
      timeout_ms = rpc_timeout_ms;
      return *this;
    }

    RpcOptions& set_wait_for_ready(bool rpc_wait_for_ready) {
      wait_for_ready = rpc_wait_for_ready;
      return *this;
    }

    RpcOptions& set_server_fail(bool rpc_server_fail) {
      server_fail = rpc_server_fail;
      return *this;
    }

    RpcOptions& set_skip_cancelled_check(bool rpc_skip_cancelled_check) {
      skip_cancelled_check = rpc_skip_cancelled_check;
      return *this;
    }

    RpcOptions& set_metadata(
        std::vector<std::pair<std::string, std::string>> rpc_metadata) {
      metadata = std::move(rpc_metadata);
      return *this;
    }

    RpcOptions& set_server_sleep_us(int rpc_server_sleep_us) {
      server_sleep_us = rpc_server_sleep_us;
      return *this;
    }

    RpcOptions& set_client_cancel_after_us(int rpc_client_cancel_after_us) {
      client_cancel_after_us = rpc_client_cancel_after_us;
      return *this;
    }

    RpcOptions& set_server_expected_error(StatusCode code) {
      server_expected_error = code;
      return *this;
    }

    // Populates context and request.
    void SetupRpc(ClientContext* context, EchoRequest* request) const {
      for (const auto& item : metadata) {
        context->AddMetadata(item.first, item.second);
      }
      if (timeout_ms != 0) {
        context->set_deadline(
            grpc_timeout_milliseconds_to_deadline(timeout_ms));
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
    }
  };

  template <typename Stub>
  Status SendRpcMethod(Stub* stub, const RpcOptions& rpc_options,
                       ClientContext* context, EchoRequest& request,
                       EchoResponse* response) {
    switch (rpc_options.method) {
      case METHOD_ECHO:
        return stub->Echo(context, request, response);
      case METHOD_ECHO1:
        return stub->Echo1(context, request, response);
      case METHOD_ECHO2:
        return stub->Echo2(context, request, response);
    }
    GPR_UNREACHABLE_CODE();
  }

  void ResetBackendCounters(size_t start_index = 0, size_t stop_index = 0) {
    if (stop_index == 0) stop_index = backends_.size();
    for (size_t i = start_index; i < stop_index; ++i) {
      backends_[i]->backend_service()->ResetCounters();
      backends_[i]->backend_service1()->ResetCounters();
      backends_[i]->backend_service2()->ResetCounters();
    }
  }

  bool SeenBackend(size_t backend_idx,
                   const RpcService rpc_service = SERVICE_ECHO) {
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

  bool SeenAllBackends(size_t start_index = 0, size_t stop_index = 0,
                       const RpcService rpc_service = SERVICE_ECHO) {
    if (stop_index == 0) stop_index = backends_.size();
    for (size_t i = start_index; i < stop_index; ++i) {
      if (!SeenBackend(i, rpc_service)) {
        return false;
      }
    }
    return true;
  }

  void SendRpcAndCount(
      int* num_total, int* num_ok, int* num_failure, int* num_drops,
      const RpcOptions& rpc_options = RpcOptions(),
      const char* drop_error_message_prefix = "EDS-configured drop: ") {
    const Status status = SendRpc(rpc_options);
    if (status.ok()) {
      ++*num_ok;
    } else {
      if (absl::StartsWith(status.error_message(), drop_error_message_prefix)) {
        ++*num_drops;
      } else {
        ++*num_failure;
      }
    }
    ++*num_total;
  }

  struct WaitForBackendOptions {
    bool reset_counters = true;
    bool allow_failures = false;

    WaitForBackendOptions() {}

    WaitForBackendOptions& set_reset_counters(bool enable) {
      reset_counters = enable;
      return *this;
    }

    WaitForBackendOptions& set_allow_failures(bool enable) {
      allow_failures = enable;
      return *this;
    }
  };

  std::tuple<int, int, int> WaitForAllBackends(
      size_t start_index = 0, size_t stop_index = 0,
      const WaitForBackendOptions& wait_options = WaitForBackendOptions(),
      const RpcOptions& rpc_options = RpcOptions()) {
    int num_ok = 0;
    int num_failure = 0;
    int num_drops = 0;
    int num_total = 0;
    gpr_log(GPR_INFO, "========= WAITING FOR All BACKEND %lu TO %lu ==========",
            static_cast<unsigned long>(start_index),
            static_cast<unsigned long>(stop_index));
    while (!SeenAllBackends(start_index, stop_index, rpc_options.service)) {
      SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_drops,
                      rpc_options);
    }
    if (wait_options.reset_counters) ResetBackendCounters();
    gpr_log(GPR_INFO,
            "Performed %d warm up requests against the backends. "
            "%d succeeded, %d failed, %d dropped.",
            num_total, num_ok, num_failure, num_drops);
    if (!wait_options.allow_failures) EXPECT_EQ(num_failure, 0);
    return std::make_tuple(num_ok, num_failure, num_drops);
  }

  void WaitForBackend(
      size_t backend_idx,
      const WaitForBackendOptions& wait_options = WaitForBackendOptions(),
      const RpcOptions& rpc_options = RpcOptions()) {
    gpr_log(GPR_INFO, "========= WAITING FOR BACKEND %lu ==========",
            static_cast<unsigned long>(backend_idx));
    do {
      Status status = SendRpc(rpc_options);
      if (!wait_options.allow_failures) {
        EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                                 << " message=" << status.error_message();
      }
    } while (!SeenBackend(backend_idx, rpc_options.service));
    if (wait_options.reset_counters) ResetBackendCounters();
    gpr_log(GPR_INFO, "========= BACKEND %lu READY ==========",
            static_cast<unsigned long>(backend_idx));
  }

  grpc_core::ServerAddressList CreateAddressListFromPortList(
      const std::vector<int>& ports) {
    grpc_core::ServerAddressList addresses;
    for (int port : ports) {
      absl::StatusOr<grpc_core::URI> lb_uri = grpc_core::URI::Parse(
          absl::StrCat(ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", port));
      GPR_ASSERT(lb_uri.ok());
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(*lb_uri, &address));
      addresses.emplace_back(address.addr, address.len, nullptr);
    }
    return addresses;
  }

  std::string CreateMetadataValueThatHashesToBackendPort(int port) {
    return absl::StrCat(ipv6_only_ ? "[::1]" : "127.0.0.1", ":", port, "_0");
  }

  std::string CreateMetadataValueThatHashesToBackend(int index) {
    return CreateMetadataValueThatHashesToBackendPort(backends_[index]->port());
  }

  void SetNextResolution(
      const std::vector<int>& ports,
      grpc_core::FakeResolverResponseGenerator* response_generator = nullptr) {
    if (!GetParam().use_fake_resolver()) return;  // Not used with xds resolver.
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    grpc_error_handle error = GRPC_ERROR_NONE;
    const char* service_config_json =
        GetParam().enable_load_reporting()
            ? kDefaultServiceConfig
            : kDefaultServiceConfigWithoutLoadReporting;
    result.service_config =
        grpc_core::ServiceConfig::Create(nullptr, service_config_json, &error);
    ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
    ASSERT_NE(result.service_config.get(), nullptr);
    if (response_generator == nullptr) {
      response_generator = response_generator_.get();
    }
    response_generator->SetResponse(std::move(result));
  }

  void SetNextResolutionForLbChannelAllBalancers(
      const char* service_config_json = nullptr,
      const char* expected_targets = nullptr,
      grpc_core::FakeResolverResponseGenerator* response_generator = nullptr) {
    std::vector<int> ports;
    for (size_t i = 0; i < balancers_.size(); ++i) {
      ports.emplace_back(balancers_[i]->port());
    }
    SetNextResolutionForLbChannel(ports, service_config_json, expected_targets,
                                  response_generator);
  }

  void SetNextResolutionForLbChannel(
      const std::vector<int>& ports, const char* service_config_json = nullptr,
      const char* expected_targets = nullptr,
      grpc_core::FakeResolverResponseGenerator* response_generator = nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    if (service_config_json != nullptr) {
      grpc_error_handle error = GRPC_ERROR_NONE;
      result.service_config = grpc_core::ServiceConfig::Create(
          nullptr, service_config_json, &error);
      ASSERT_NE(result.service_config.get(), nullptr);
      ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
    }
    if (expected_targets != nullptr) {
      grpc_arg expected_targets_arg = grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS),
          const_cast<char*>(expected_targets));
      result.args =
          grpc_channel_args_copy_and_add(nullptr, &expected_targets_arg, 1);
    }
    if (response_generator == nullptr) {
      response_generator = lb_channel_response_generator_.get();
    }
    response_generator->SetResponse(std::move(result));
  }

  void SetNextReresolutionResponse(const std::vector<int>& ports) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(ports);
    response_generator_->SetReresolutionResponse(std::move(result));
  }

  std::vector<int> GetBackendPorts(size_t start_index = 0,
                                   size_t stop_index = 0) const {
    if (stop_index == 0) stop_index = backends_.size();
    std::vector<int> backend_ports;
    for (size_t i = start_index; i < stop_index; ++i) {
      backend_ports.push_back(backends_[i]->port());
    }
    return backend_ports;
  }

  Status SendRpc(const RpcOptions& rpc_options = RpcOptions(),
                 EchoResponse* response = nullptr) {
    const bool local_response = (response == nullptr);
    if (local_response) response = new EchoResponse;
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
        status = SendRpcMethod(stub_.get(), rpc_options, &context, request,
                               response);
        break;
      case SERVICE_ECHO1:
        status = SendRpcMethod(stub1_.get(), rpc_options, &context, request,
                               response);
        break;
      case SERVICE_ECHO2:
        status = SendRpcMethod(stub2_.get(), rpc_options, &context, request,
                               response);
        break;
    }
    if (local_response) delete response;
    return status;
  }

  void CheckRpcSendOk(const size_t times = 1,
                      const RpcOptions& rpc_options = RpcOptions()) {
    for (size_t i = 0; i < times; ++i) {
      EchoResponse response;
      const Status status = SendRpc(rpc_options, &response);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }

  struct CheckRpcSendFailureOptions {
    std::function<bool(size_t)> continue_predicate = [](size_t i) {
      return i < 1;
    };
    RpcOptions rpc_options;
    StatusCode expected_error_code = StatusCode::OK;

    CheckRpcSendFailureOptions() {}

    CheckRpcSendFailureOptions& set_times(size_t times) {
      continue_predicate = [times](size_t i) { return i < times; };
      return *this;
    }

    CheckRpcSendFailureOptions& set_continue_predicate(
        std::function<bool(size_t)> pred) {
      continue_predicate = std::move(pred);
      return *this;
    }

    CheckRpcSendFailureOptions& set_rpc_options(const RpcOptions& options) {
      rpc_options = options;
      return *this;
    }

    CheckRpcSendFailureOptions& set_expected_error_code(StatusCode code) {
      expected_error_code = code;
      return *this;
    }
  };

  void CheckRpcSendFailure(const CheckRpcSendFailureOptions& options =
                               CheckRpcSendFailureOptions()) {
    for (size_t i = 0; options.continue_predicate(i); ++i) {
      const Status status = SendRpc(options.rpc_options);
      EXPECT_FALSE(status.ok());
      if (options.expected_error_code != StatusCode::OK) {
        EXPECT_EQ(options.expected_error_code, status.error_code());
      }
    }
  }

  bool WaitForNack(
      std::function<AdsServiceImpl::ResponseState::State()> get_state,
      StatusCode expected_status = StatusCode::UNAVAILABLE) {
    auto deadline = absl::Now() + absl::Seconds(30);
    bool success = true;
    CheckRpcSendFailure(CheckRpcSendFailureOptions()
                            .set_continue_predicate([&](size_t) {
                              if (absl::Now() >= deadline) {
                                success = false;
                                return false;
                              }
                              return get_state() !=
                                     AdsServiceImpl::ResponseState::NACKED;
                            })
                            .set_expected_error_code(expected_status));
    return success;
  }

  bool WaitForLdsNack(StatusCode expected_status = StatusCode::UNAVAILABLE) {
    return WaitForNack(
        [&]() {
          return balancers_[0]->ads_service()->lds_response_state().state;
        },
        expected_status);
  }

  bool WaitForRdsNack(StatusCode expected_status = StatusCode::UNAVAILABLE) {
    return WaitForNack(
        [&]() { return RouteConfigurationResponseState(0).state; },
        expected_status);
  }

  bool WaitForCdsNack() {
    return WaitForNack([&]() {
      return balancers_[0]->ads_service()->cds_response_state().state;
    });
  }

  bool WaitForEdsNack() {
    return WaitForNack([&]() {
      return balancers_[0]->ads_service()->eds_response_state().state;
    });
  }

  bool WaitForRouteConfigNack(
      StatusCode expected_status = StatusCode::UNAVAILABLE) {
    if (GetParam().enable_rds_testing()) {
      return WaitForRdsNack(expected_status);
    }
    return WaitForLdsNack(expected_status);
  }

  AdsServiceImpl::ResponseState RouteConfigurationResponseState(int idx) const {
    AdsServiceImpl* ads_service = balancers_[idx]->ads_service();
    if (GetParam().enable_rds_testing()) {
      return ads_service->rds_response_state();
    }
    return ads_service->lds_response_state();
  }

  Listener PopulateServerListenerNameAndPort(const Listener& listener_template,
                                             int port) {
    Listener listener = listener_template;
    listener.set_name(
        absl::StrCat("grpc/server?xds.resource.listening_address=",
                     ipv6_only_ ? "[::1]:" : "127.0.0.1:", port));
    listener.mutable_address()->mutable_socket_address()->set_port_value(port);
    return listener;
  }

  // Interface for accessing HttpConnectionManager config in Listener.
  class HcmAccessor {
   public:
    virtual ~HcmAccessor() = default;
    virtual HttpConnectionManager Unpack(const Listener& listener) const = 0;
    virtual void Pack(const HttpConnectionManager& hcm,
                      Listener* listener) const = 0;
  };

  // Client-side impl.
  class ClientHcmAccessor : public HcmAccessor {
   public:
    HttpConnectionManager Unpack(const Listener& listener) const override {
      HttpConnectionManager http_connection_manager;
      listener.api_listener().api_listener().UnpackTo(&http_connection_manager);
      return http_connection_manager;
    }
    void Pack(const HttpConnectionManager& hcm,
              Listener* listener) const override {
      auto* api_listener =
          listener->mutable_api_listener()->mutable_api_listener();
      api_listener->PackFrom(hcm);
    }
  };

  // Server-side impl.
  class ServerHcmAccessor : public HcmAccessor {
   public:
    HttpConnectionManager Unpack(const Listener& listener) const override {
      HttpConnectionManager http_connection_manager;
      listener.default_filter_chain().filters().at(0).typed_config().UnpackTo(
          &http_connection_manager);
      return http_connection_manager;
    }
    void Pack(const HttpConnectionManager& hcm,
              Listener* listener) const override {
      listener->mutable_default_filter_chain()
          ->mutable_filters()
          ->at(0)
          .mutable_typed_config()
          ->PackFrom(hcm);
    }
  };

  void SetListenerAndRouteConfiguration(
      int idx, Listener listener, const RouteConfiguration& route_config,
      const HcmAccessor& hcm_accessor = ClientHcmAccessor()) {
    HttpConnectionManager http_connection_manager =
        hcm_accessor.Unpack(listener);
    if (GetParam().enable_rds_testing()) {
      auto* rds = http_connection_manager.mutable_rds();
      rds->set_route_config_name(route_config.name());
      rds->mutable_config_source()->mutable_ads();
      balancers_[idx]->ads_service()->SetRdsResource(route_config);
    } else {
      *http_connection_manager.mutable_route_config() = route_config;
    }
    hcm_accessor.Pack(http_connection_manager, &listener);
    balancers_[idx]->ads_service()->SetLdsResource(listener);
  }

  void SetServerListenerNameAndRouteConfiguration(
      int idx, Listener listener, int port,
      const RouteConfiguration& route_config) {
    SetListenerAndRouteConfiguration(
        idx, PopulateServerListenerNameAndPort(listener, port), route_config,
        ServerHcmAccessor());
  }

  void SetRouteConfiguration(int idx, const RouteConfiguration& route_config,
                             const Listener* listener_to_copy = nullptr) {
    if (GetParam().enable_rds_testing()) {
      balancers_[idx]->ads_service()->SetRdsResource(route_config);
    } else {
      Listener listener(listener_to_copy == nullptr ? default_listener_
                                                    : *listener_to_copy);
      HttpConnectionManager http_connection_manager;
      listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
          &http_connection_manager);
      *(http_connection_manager.mutable_route_config()) = route_config;
      listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
          http_connection_manager);
      balancers_[idx]->ads_service()->SetLdsResource(listener);
    }
  }

  struct EdsResourceArgs {
    struct Endpoint {
      explicit Endpoint(int port,
                        HealthStatus health_status = HealthStatus::UNKNOWN,
                        int lb_weight = 1)
          : port(port), health_status(health_status), lb_weight(lb_weight) {}

      int port;
      HealthStatus health_status;
      int lb_weight;
    };

    struct Locality {
      Locality(std::string sub_zone, std::vector<Endpoint> endpoints,
               int lb_weight = kDefaultLocalityWeight,
               int priority = kDefaultLocalityPriority)
          : sub_zone(std::move(sub_zone)),
            endpoints(std::move(endpoints)),
            lb_weight(lb_weight),
            priority(priority) {}

      const std::string sub_zone;
      std::vector<Endpoint> endpoints;
      int lb_weight;
      int priority;
    };

    EdsResourceArgs() = default;
    explicit EdsResourceArgs(std::vector<Locality> locality_list)
        : locality_list(std::move(locality_list)) {}

    std::vector<Locality> locality_list;
    std::map<std::string, uint32_t> drop_categories;
    FractionalPercent::DenominatorType drop_denominator =
        FractionalPercent::MILLION;
  };

  EdsResourceArgs::Endpoint CreateEndpoint(
      size_t backend_idx, HealthStatus health_status = HealthStatus::UNKNOWN,
      int lb_weight = 1) {
    return EdsResourceArgs::Endpoint(backends_[backend_idx]->port(),
                                     health_status, lb_weight);
  }

  std::vector<EdsResourceArgs::Endpoint> CreateEndpointsForBackends(
      size_t start_index = 0, size_t stop_index = 0,
      HealthStatus health_status = HealthStatus::UNKNOWN, int lb_weight = 1) {
    if (stop_index == 0) stop_index = backends_.size();
    std::vector<EdsResourceArgs::Endpoint> endpoints;
    for (size_t i = start_index; i < stop_index; ++i) {
      endpoints.emplace_back(CreateEndpoint(i, health_status, lb_weight));
    }
    return endpoints;
  }

  EdsResourceArgs::Endpoint MakeNonExistantEndpoint() {
    return EdsResourceArgs::Endpoint(grpc_pick_unused_port_or_die());
  }

  ClusterLoadAssignment BuildEdsResource(
      const EdsResourceArgs& args,
      const char* eds_service_name = kDefaultEdsServiceName) {
    ClusterLoadAssignment assignment;
    assignment.set_cluster_name(eds_service_name);
    for (const auto& locality : args.locality_list) {
      auto* endpoints = assignment.add_endpoints();
      endpoints->mutable_load_balancing_weight()->set_value(locality.lb_weight);
      endpoints->set_priority(locality.priority);
      endpoints->mutable_locality()->set_region(kDefaultLocalityRegion);
      endpoints->mutable_locality()->set_zone(kDefaultLocalityZone);
      endpoints->mutable_locality()->set_sub_zone(locality.sub_zone);
      for (size_t i = 0; i < locality.endpoints.size(); ++i) {
        const int& port = locality.endpoints[i].port;
        auto* lb_endpoints = endpoints->add_lb_endpoints();
        if (locality.endpoints.size() > i &&
            locality.endpoints[i].health_status != HealthStatus::UNKNOWN) {
          lb_endpoints->set_health_status(locality.endpoints[i].health_status);
        }
        if (locality.endpoints.size() > i &&
            locality.endpoints[i].lb_weight >= 1) {
          lb_endpoints->mutable_load_balancing_weight()->set_value(
              locality.endpoints[i].lb_weight);
        }
        auto* endpoint = lb_endpoints->mutable_endpoint();
        auto* address = endpoint->mutable_address();
        auto* socket_address = address->mutable_socket_address();
        socket_address->set_address(ipv6_only_ ? "::1" : "127.0.0.1");
        socket_address->set_port_value(port);
      }
    }
    if (!args.drop_categories.empty()) {
      auto* policy = assignment.mutable_policy();
      for (const auto& p : args.drop_categories) {
        const std::string& name = p.first;
        const uint32_t parts_per_million = p.second;
        auto* drop_overload = policy->add_drop_overloads();
        drop_overload->set_category(name);
        auto* drop_percentage = drop_overload->mutable_drop_percentage();
        drop_percentage->set_numerator(parts_per_million);
        drop_percentage->set_denominator(args.drop_denominator);
      }
    }
    return assignment;
  }

 public:
  // This method could benefit test subclasses; to make it accessible
  // via bind with a qualified name, it needs to be public.
  void SetEdsResourceWithDelay(size_t i,
                               const ClusterLoadAssignment& assignment,
                               int delay_ms) {
    GPR_ASSERT(delay_ms > 0);
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(delay_ms));
    balancers_[i]->ads_service()->SetEdsResource(assignment);
  }

 protected:
  class XdsServingStatusNotifier
      : public grpc::experimental::XdsServerServingStatusNotifierInterface {
   public:
    void OnServingStatusUpdate(std::string uri,
                               ServingStatusUpdate update) override {
      grpc_core::MutexLock lock(&mu_);
      status_map[uri] = update.status;
      cond_.Signal();
    }

    void WaitOnServingStatusChange(std::string uri,
                                   grpc::StatusCode expected_status) {
      grpc_core::MutexLock lock(&mu_);
      std::map<std::string, grpc::Status>::iterator it;
      while ((it = status_map.find(uri)) == status_map.end() ||
             it->second.error_code() != expected_status) {
        cond_.Wait(&mu_);
      }
    }

   private:
    grpc_core::Mutex mu_;
    grpc_core::CondVar cond_;
    std::map<std::string, grpc::Status> status_map ABSL_GUARDED_BY(mu_);
  };

  class ServerThread {
   public:
    explicit ServerThread(XdsEnd2endTest* test_obj,
                          bool use_xds_enabled_server = false)
        : test_obj_(test_obj),
          port_(grpc_pick_unused_port_or_die()),
          use_xds_enabled_server_(use_xds_enabled_server) {}
    virtual ~ServerThread(){};

    void Start() {
      gpr_log(GPR_INFO, "starting %s server on port %d", Type(), port_);
      GPR_ASSERT(!running_);
      running_ = true;
      StartAllServices();
      grpc_core::Mutex mu;
      // We need to acquire the lock here in order to prevent the notify_one
      // by ServerThread::Serve from firing before the wait below is hit.
      grpc_core::MutexLock lock(&mu);
      grpc_core::CondVar cond;
      thread_ = absl::make_unique<std::thread>(
          std::bind(&ServerThread::Serve, this, &mu, &cond));
      cond.Wait(&mu);
      gpr_log(GPR_INFO, "%s server startup complete", Type());
    }

    void Serve(grpc_core::Mutex* mu, grpc_core::CondVar* cond) {
      // We need to acquire the lock here in order to prevent the notify_one
      // below from firing before its corresponding wait is executed.
      grpc_core::MutexLock lock(mu);
      std::ostringstream server_address;
      server_address << "localhost:" << port_;
      if (use_xds_enabled_server_) {
        XdsServerBuilder builder;
        if (GetParam().bootstrap_source() ==
            TestType::kBootstrapFromChannelArg) {
          builder.SetOption(
              absl::make_unique<XdsChannelArgsServerBuilderOption>(test_obj_));
        }
        builder.set_status_notifier(&notifier_);
        builder.AddListeningPort(server_address.str(), Credentials());
        RegisterAllServices(&builder);
        server_ = builder.BuildAndStart();
      } else {
        ServerBuilder builder;
        builder.AddListeningPort(server_address.str(), Credentials());
        RegisterAllServices(&builder);
        server_ = builder.BuildAndStart();
      }
      cond->Signal();
    }

    void Shutdown() {
      if (!running_) return;
      gpr_log(GPR_INFO, "%s about to shutdown", Type());
      ShutdownAllServices();
      server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      thread_->join();
      gpr_log(GPR_INFO, "%s shutdown completed", Type());
      running_ = false;
    }

    virtual std::shared_ptr<ServerCredentials> Credentials() {
      return std::make_shared<SecureServerCredentials>(
          grpc_fake_transport_security_server_credentials_create());
    }

    int port() const { return port_; }

    bool use_xds_enabled_server() const { return use_xds_enabled_server_; }

    XdsServingStatusNotifier* notifier() { return &notifier_; }

   private:
    class XdsChannelArgsServerBuilderOption
        : public ::grpc::ServerBuilderOption {
     public:
      explicit XdsChannelArgsServerBuilderOption(XdsEnd2endTest* test_obj)
          : test_obj_(test_obj) {}

      void UpdateArguments(grpc::ChannelArguments* args) override {
        args->SetString(
            GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG,
            GetParam().use_v2() ? kBootstrapFileV2 : kBootstrapFileV3);
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

    virtual void RegisterAllServices(ServerBuilder* builder) = 0;
    virtual void StartAllServices() = 0;
    virtual void ShutdownAllServices() = 0;

    virtual const char* Type() = 0;

    XdsEnd2endTest* test_obj_;
    const int port_;
    std::unique_ptr<Server> server_;
    XdsServingStatusNotifier notifier_;
    std::unique_ptr<std::thread> thread_;
    bool running_ = false;
    const bool use_xds_enabled_server_;
  };

  class BackendServerThread : public ServerThread {
   public:
    explicit BackendServerThread(XdsEnd2endTest* test_obj,
                                 bool use_xds_enabled_server)
        : ServerThread(test_obj, use_xds_enabled_server) {}

    BackendServiceImpl<::grpc::testing::EchoTestService::Service>*
    backend_service() {
      return &backend_service_;
    }
    BackendServiceImpl<::grpc::testing::EchoTest1Service::Service>*
    backend_service1() {
      return &backend_service1_;
    }
    BackendServiceImpl<::grpc::testing::EchoTest2Service::Service>*
    backend_service2() {
      return &backend_service2_;
    }

    std::shared_ptr<ServerCredentials> Credentials() override {
      if (GetParam().use_xds_credentials()) {
        if (use_xds_enabled_server()) {
          // We are testing server's use of XdsServerCredentials
          return XdsServerCredentials(InsecureServerCredentials());
        } else {
          // We are testing client's use of XdsCredentials
          std::string root_cert = ReadFile(kCaCertPath);
          std::string identity_cert = ReadFile(kServerCertPath);
          std::string private_key = ReadFile(kServerKeyPath);
          std::vector<experimental::IdentityKeyCertPair>
              identity_key_cert_pairs = {{private_key, identity_cert}};
          auto certificate_provider = std::make_shared<
              grpc::experimental::StaticDataCertificateProvider>(
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

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&backend_service_);
      builder->RegisterService(&backend_service1_);
      builder->RegisterService(&backend_service2_);
    }

    void StartAllServices() override {
      backend_service_.Start();
      backend_service1_.Start();
      backend_service2_.Start();
    }

    void ShutdownAllServices() override {
      backend_service_.Shutdown();
      backend_service1_.Shutdown();
      backend_service2_.Shutdown();
    }

    const char* Type() override { return "Backend"; }

    BackendServiceImpl<::grpc::testing::EchoTestService::Service>
        backend_service_;
    BackendServiceImpl<::grpc::testing::EchoTest1Service::Service>
        backend_service1_;
    BackendServiceImpl<::grpc::testing::EchoTest2Service::Service>
        backend_service2_;
  };

  class BalancerServerThread : public ServerThread {
   public:
    explicit BalancerServerThread(XdsEnd2endTest* test_obj,
                                  int client_load_reporting_interval = 0)
        : ServerThread(test_obj),
          ads_service_(new AdsServiceImpl()),
          lrs_service_(new LrsServiceImpl(client_load_reporting_interval,
                                          {kDefaultClusterName})) {}

    AdsServiceImpl* ads_service() { return ads_service_.get(); }
    LrsServiceImpl* lrs_service() { return lrs_service_.get(); }

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(ads_service_->v2_rpc_service());
      builder->RegisterService(ads_service_->v3_rpc_service());
      builder->RegisterService(lrs_service_->v2_rpc_service());
      builder->RegisterService(lrs_service_->v3_rpc_service());
    }

    void StartAllServices() override {
      ads_service_->Start();
      lrs_service_->Start();
    }

    void ShutdownAllServices() override {
      ads_service_->Shutdown();
      lrs_service_->Shutdown();
    }

    const char* Type() override { return "Balancer"; }

    std::shared_ptr<AdsServiceImpl> ads_service_;
    std::shared_ptr<LrsServiceImpl> lrs_service_;
  };

#ifndef DISABLED_XDS_PROTO_IN_CC
  class AdminServerThread : public ServerThread {
   public:
    explicit AdminServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj) {}

   private:
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&csds_service_);
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    const char* Type() override { return "Admin"; }

    grpc::xds::experimental::ClientStatusDiscoveryService csds_service_;
  };
#endif  // DISABLED_XDS_PROTO_IN_CC

  class LongRunningRpc {
   public:
    void StartRpc(grpc::testing::EchoTestService::Stub* stub,
                  const RpcOptions& rpc_options =
                      RpcOptions().set_timeout_ms(0).set_client_cancel_after_us(
                          1 * 1000 * 1000)) {
      sender_thread_ = std::thread([this, stub, rpc_options]() {
        EchoRequest request;
        EchoResponse response;
        rpc_options.SetupRpc(&context_, &request);
        status_ = stub->Echo(&context_, request, &response);
      });
    }

    void CancelRpc() {
      context_.TryCancel();
      if (sender_thread_.joinable()) sender_thread_.join();
    }

    Status GetStatus() {
      if (sender_thread_.joinable()) sender_thread_.join();
      return status_;
    }

   private:
    std::thread sender_thread_;
    ClientContext context_;
    Status status_;
  };

  struct ConcurrentRpc {
    ClientContext context;
    Status status;
    grpc_millis elapsed_time;
    EchoResponse response;
  };

  std::vector<ConcurrentRpc> SendConcurrentRpcs(
      grpc::testing::EchoTestService::Stub* stub, size_t num_rpcs,
      const RpcOptions& rpc_options) {
    // Variables for RPCs.
    std::vector<ConcurrentRpc> rpcs(num_rpcs);
    EchoRequest request;
    // Variables for synchronization
    absl::Mutex mu;
    absl::CondVar cv;
    size_t completed = 0;
    // Set-off callback RPCs
    for (size_t i = 0; i < num_rpcs; i++) {
      ConcurrentRpc* rpc = &rpcs[i];
      rpc_options.SetupRpc(&rpc->context, &request);
      grpc_millis t0 = NowFromCycleCounter();
      stub->async()->Echo(&rpc->context, &request, &rpc->response,
                          [rpc, &mu, &completed, &cv, num_rpcs, t0](Status s) {
                            rpc->status = s;
                            rpc->elapsed_time = NowFromCycleCounter() - t0;
                            bool done;
                            {
                              absl::MutexLock lock(&mu);
                              done = (++completed) == num_rpcs;
                            }
                            if (done) cv.Signal();
                          });
    }
    {
      absl::MutexLock lock(&mu);
      cv.Wait(&mu);
    }
    EXPECT_EQ(completed, num_rpcs);
    return rpcs;
  }

  const size_t num_backends_;
  const size_t num_balancers_;
  const int client_load_reporting_interval_seconds_;
  bool ipv6_only_ = false;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::testing::EchoTest1Service::Stub> stub1_;
  std::unique_ptr<grpc::testing::EchoTest2Service::Stub> stub2_;
  std::vector<std::unique_ptr<BackendServerThread>> backends_;
  std::vector<std::unique_ptr<BalancerServerThread>> balancers_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      lb_channel_response_generator_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      logical_dns_cluster_resolver_response_generator_;
  int xds_resource_does_not_exist_timeout_ms_ = 0;
  absl::InlinedVector<grpc_arg, 2> xds_channel_args_to_add_;
  grpc_channel_args xds_channel_args_;

  Listener default_listener_;
  RouteConfiguration default_route_config_;
  Listener default_server_listener_;
  RouteConfiguration default_server_route_config_;
  Cluster default_cluster_;
  bool use_xds_enabled_server_;
  bool bootstrap_contents_from_env_var_;
};

class BasicTest : public XdsEnd2endTest {
 public:
  BasicTest() : XdsEnd2endTest(4, 1) { StartAllBackends(); }
};

// Tests that the balancer sends the correct response to the client, and the
// client sends RPCs to the backends using the default child policy.
TEST_P(BasicTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ(
      (GetParam().use_fake_resolver() ? "xds_cluster_resolver_experimental"
                                      : "xds_cluster_manager_experimental"),
      channel_->GetLoadBalancingPolicyName());
}

TEST_P(BasicTest, IgnoresUnhealthyEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcsPerAddress = 100;
  auto endpoints = CreateEndpointsForBackends();
  endpoints[0].health_status = HealthStatus::DRAINING;
  EdsResourceArgs args({
      {"locality0", std::move(endpoints), kDefaultLocalityWeight,
       kDefaultLocalityPriority},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends(/*start_index=*/1);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * (num_backends_ - 1));
  // Each backend should have gotten 100 requests.
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
}

// Tests that subchannel sharing works when the same backend is listed
// multiple times.
TEST_P(BasicTest, SameBackendListedMultipleTimes) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Same backend listed twice.
  auto endpoints = CreateEndpointsForBackends(0, 1);
  endpoints.push_back(endpoints.front());
  EdsResourceArgs args({
      {"locality0", endpoints},
  });
  const size_t kNumRpcsPerAddress = 10;
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // We need to wait for the backend to come online.
  WaitForBackend(0);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * endpoints.size());
  // Backend should have gotten 20 requests.
  EXPECT_EQ(kNumRpcsPerAddress * endpoints.size(),
            backends_[0]->backend_service()->request_count());
  // And they should have come from a single client port, because of
  // subchannel sharing.
  EXPECT_EQ(1UL, backends_[0]->backend_service()->clients().size());
}

// Tests that RPCs will be blocked until a non-empty serverlist is received.
TEST_P(BasicTest, InitiallyEmptyServerlist) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const int kServerlistDelayMs = 500 * grpc_test_slowdown_factor();
  const int kCallDeadlineMs = kServerlistDelayMs * 2;
  // First response is an empty serverlist, sent right away.
  EdsResourceArgs::Locality empty_locality("locality0", {});
  EdsResourceArgs args({
      empty_locality,
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Send non-empty serverlist only after kServerlistDelayMs.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends()},
  });
  std::thread delayed_resource_setter(std::bind(
      &BasicTest::SetEdsResourceWithDelay, this, 0,
      BuildEdsResource(args, DefaultEdsServiceName()), kServerlistDelayMs));
  const auto t0 = system_clock::now();
  // Client will block: LB will initially send empty serverlist.
  CheckRpcSendOk(
      1, RpcOptions().set_timeout_ms(kCallDeadlineMs).set_wait_for_ready(true));
  const auto ellapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_clock::now() - t0);
  // but eventually, the LB sends a serverlist update that allows the call to
  // proceed. The call delay must be larger than the delay in sending the
  // populated serverlist but under the call's deadline (which is enforced by
  // the call's deadline).
  EXPECT_GT(ellapsed_ms.count(), kServerlistDelayMs);
  delayed_resource_setter.join();
}

// Tests that RPCs will fail with UNAVAILABLE instead of DEADLINE_EXCEEDED if
// all the servers are unreachable.
TEST_P(BasicTest, AllServersUnreachableFailFast) {
  // Set Rpc timeout to 5 seconds to ensure there is enough time
  // for communication with the xDS server to take place upon test start up.
  const uint32_t kRpcTimeoutMs = 5000;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumUnreachableServers = 5;
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    endpoints.emplace_back(grpc_pick_unused_port_or_die());
  }
  EdsResourceArgs args({
      {"locality0", endpoints},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  const Status status = SendRpc(RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  // The error shouldn't be DEADLINE_EXCEEDED because timeout is set to 5
  // seconds, and we should disocver in that time that the target backend is
  // down.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
}

// Tests that RPCs fail when the backends are down, and will succeed again
// after the backends are restarted.
TEST_P(BasicTest, BackendsRestart) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  // Sending multiple failed requests instead of just one to ensure that the
  // client notices that all backends are down before we restart them. If we
  // didn't do this, then a single RPC could fail here due to the race
  // condition between the LB pick and the GOAWAY from the chosen backend
  // being shut down, which would not actually prove that the client noticed
  // that all of the backends are down. Then, when we send another request
  // below (which we expect to succeed), if the callbacks happen in the wrong
  // order, the same race condition could happen again due to the client not
  // yet having noticed that the backends were all down.
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(num_backends_));
  // Restart all backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1, RpcOptions().set_timeout_ms(2000).set_wait_for_ready(true));
}

TEST_P(BasicTest, IgnoresDuplicateUpdates) {
  const size_t kNumRpcsPerAddress = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server, but send an EDS update in
  // between.  If the update is not ignored, this will cause the
  // round_robin policy to see an update, which will randomly reset its
  // position in the address list.
  for (size_t i = 0; i < kNumRpcsPerAddress; ++i) {
    CheckRpcSendOk(2);
    balancers_[0]->ads_service()->SetEdsResource(
        BuildEdsResource(args, DefaultEdsServiceName()));
    CheckRpcSendOk(2);
  }
  // Each backend should have gotten the right number of requests.
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
}

using XdsResolverOnlyTest = BasicTest;

TEST_P(XdsResolverOnlyTest, ResourceTypeVersionPersistsAcrossStreamRestarts) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for backends to come online.
  WaitForAllBackends(0, 1);
  // Stop balancer.
  balancers_[0]->Shutdown();
  // Tell balancer to require minimum version 1 for all resource types.
  balancers_[0]->ads_service()->SetResourceMinVersion(kLdsTypeUrl, 1);
  balancers_[0]->ads_service()->SetResourceMinVersion(kRdsTypeUrl, 1);
  balancers_[0]->ads_service()->SetResourceMinVersion(kCdsTypeUrl, 1);
  balancers_[0]->ads_service()->SetResourceMinVersion(kEdsTypeUrl, 1);
  // Update backend, just so we can be sure that the client has
  // reconnected to the balancer.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args2));
  // Restart balancer.
  balancers_[0]->Start();
  // Make sure client has reconnected.
  WaitForAllBackends(1, 2);
}

// Tests switching over from one cluster to another.
TEST_P(XdsResolverOnlyTest, ChangeClusters) {
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 2);
  // Populate new EDS resource.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  // Wait for all new backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends(2, 4);
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

// Tests that we go into TRANSIENT_FAILURE if the Cluster disappears.
TEST_P(XdsResolverOnlyTest, ClusterRemoved) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Unset CDS resource.
  balancers_[0]->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  // Wait for RPCs to start failing.
  do {
  } while (SendRpc(RpcOptions(), nullptr).ok());
  // Make sure RPCs are still failing.
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(1000));
  // Make sure we ACK'ed the update.
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we restart all xDS requests when we reestablish the ADS call.
TEST_P(XdsResolverOnlyTest, RestartsRequestsUponReconnection) {
  // Manually configure use of RDS.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_ads();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  balancers_[0]->ads_service()->SetRdsResource(default_route_config_);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 2);
  // Now shut down and restart the balancer.  When the client
  // reconnects, it should automatically restart the requests for all
  // resource types.
  balancers_[0]->Shutdown();
  balancers_[0]->Start();
  // Make sure things are still working.
  CheckRpcSendOk(100);
  // Populate new EDS resource.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancers_[0]->ads_service()->SetRdsResource(new_route_config);
  // Wait for all new backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends(2, 4);
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

TEST_P(XdsResolverOnlyTest, DefaultRouteSpecifiesSlashPrefix) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/");
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
}

TEST_P(XdsResolverOnlyTest, CircuitBreaking) {
  constexpr size_t kMaxConcurrentRequests = 10;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Send exactly max_concurrent_requests long RPCs.
  LongRunningRpc rpcs[kMaxConcurrentRequests];
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].StartRpc(stub_.get());
  }
  // Wait for all RPCs to be in flight.
  while (backends_[0]->backend_service()->RpcsWaitingForClientCancel() <
         kMaxConcurrentRequests) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1 * 1000, GPR_TIMESPAN)));
  }
  // Sending a RPC now should fail, the error message should tell us
  // we hit the max concurrent requests limit and got dropped.
  Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "circuit breaker drop");
  // Cancel one RPC to allow another one through
  rpcs[0].CancelRpc();
  status = SendRpc();
  EXPECT_TRUE(status.ok());
  for (size_t i = 1; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].CancelRpc();
  }
  // Make sure RPCs go to the correct backend:
  EXPECT_EQ(kMaxConcurrentRequests + 1,
            backends_[0]->backend_service()->request_count());
}

TEST_P(XdsResolverOnlyTest, CircuitBreakingMultipleChannelsShareCallCounter) {
  constexpr size_t kMaxConcurrentRequests = 10;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Create second channel.
  auto response_generator2 =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  auto lb_response_generator2 =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_arg xds_arg = grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
      lb_response_generator2.get());
  grpc_channel_args xds_channel_args2 = {1, &xds_arg};
  auto channel2 = CreateChannel(
      /*failover_timeout=*/0, /*server_name=*/kServerName,
      response_generator2.get(), &xds_channel_args2);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  // Set resolution results for both channels and for the xDS channel.
  SetNextResolution({});
  SetNextResolution({}, response_generator2.get());
  SetNextResolutionForLbChannelAllBalancers();
  SetNextResolutionForLbChannelAllBalancers(nullptr, nullptr,
                                            lb_response_generator2.get());
  // Send exactly max_concurrent_requests long RPCs, alternating between
  // the two channels.
  LongRunningRpc rpcs[kMaxConcurrentRequests];
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].StartRpc(i % 2 == 0 ? stub_.get() : stub2.get());
  }
  // Wait for all RPCs to be in flight.
  while (backends_[0]->backend_service()->RpcsWaitingForClientCancel() <
         kMaxConcurrentRequests) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1 * 1000, GPR_TIMESPAN)));
  }
  // Sending a RPC now should fail, the error message should tell us
  // we hit the max concurrent requests limit and got dropped.
  Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "circuit breaker drop");
  // Cancel one RPC to allow another one through
  rpcs[0].CancelRpc();
  status = SendRpc();
  EXPECT_TRUE(status.ok());
  for (size_t i = 1; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].CancelRpc();
  }
  // Make sure RPCs go to the correct backend:
  EXPECT_EQ(kMaxConcurrentRequests + 1,
            backends_[0]->backend_service()->request_count());
}

TEST_P(XdsResolverOnlyTest, ClusterChangeAfterAdsCallFails) {
  const char* kNewEdsResourceName = "new_eds_resource_name";
  // Populate EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolutionForLbChannelAllBalancers();
  // Check that the channel is working.
  CheckRpcSendOk();
  // Stop and restart the balancer.
  balancers_[0]->Shutdown();
  balancers_[0]->Start();
  // Create new EDS resource.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsResourceName));
  // Change CDS resource to point to new EDS resource.
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Make sure client sees the change.
  // TODO(roth): This should not be allowing errors.  The errors are
  // being caused by a bug that triggers in the following situation:
  //
  // 1. xDS call fails.
  // 2. When xDS call is restarted, the server sends the updated CDS
  //    resource that points to the new EDS resource name.
  // 3. When the client receives the CDS update, it does two things:
  //    - Sends the update to the CDS LB policy, which creates a new
  //      xds_cluster_resolver policy using the new EDS service name.
  //    - Notices that the CDS update no longer refers to the old EDS
  //      service name, so removes that resource, notifying the old
  //      xds_cluster_resolver policy that the resource no longer exists.
  //
  // Need to figure out a way to fix this bug, and then change this to
  // not allow failures.
  WaitForBackend(1, WaitForBackendOptions().set_allow_failures(true));
}

using GlobalXdsClientTest = BasicTest;

TEST_P(GlobalXdsClientTest, MultipleChannelsShareXdsClient) {
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  // Create second channel and tell it to connect to kNewServerName.
  auto channel2 = CreateChannel(/*failover_timeout=*/0, kNewServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  // Make sure there's only one client connected.
  EXPECT_EQ(1UL, balancers_[0]->ads_service()->clients().size());
}

// Tests that the NACK for multiple bad LDS resources includes both errors.
TEST_P(GlobalXdsClientTest, MultipleBadResources) {
  constexpr char kServerName2[] = "server.other.com";
  constexpr char kServerName3[] = "server.another.com";
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancers_[0]->ads_service()->SetLdsResource(listener);
  listener.set_name(kServerName2);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  listener = default_listener_;
  listener.set_name(kServerName3);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  // Need to create a second channel to subscribe to a second LDS resource.
  auto channel2 = CreateChannel(0, kServerName2);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  {
    ClientContext context;
    EchoRequest request;
    request.set_message(kRequestMessage);
    EchoResponse response;
    grpc::Status status = stub2->Echo(&context, request, &response);
    EXPECT_FALSE(status.ok());
    // Wait for second NACK to be reported to xDS server.
    auto deadline = absl::Now() + absl::Seconds(30);
    bool timed_out = false;
    CheckRpcSendFailure(
        CheckRpcSendFailureOptions().set_continue_predicate([&](size_t) {
          if (absl::Now() >= deadline) {
            timed_out = true;
            return false;
          }
          const auto response_state =
              balancers_[0]->ads_service()->lds_response_state();
          return response_state.state !=
                     AdsServiceImpl::ResponseState::NACKED ||
                 ::testing::Matches(::testing::ContainsRegex(absl::StrCat(
                     kServerName,
                     ": validation error.*"
                     "Listener has neither address nor ApiListener.*",
                     kServerName2,
                     ": validation error.*"
                     "Listener has neither address nor ApiListener")))(
                     response_state.error_message);
        }));
    ASSERT_FALSE(timed_out);
  }
  // Now start a new channel with a third server name, this one with a
  // valid resource.
  auto channel3 = CreateChannel(0, kServerName3);
  auto stub3 = grpc::testing::EchoTestService::NewStub(channel3);
  {
    ClientContext context;
    EchoRequest request;
    request.set_message(kRequestMessage);
    EchoResponse response;
    grpc::Status status = stub3->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
  }
}

// Tests that we don't trigger does-not-exist callbacks for a resource
// that was previously valid but is updated to be invalid.
TEST_P(GlobalXdsClientTest, InvalidListenerStillExistsIfPreviouslyCached) {
  // Set up valid resources and check that the channel works.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
  // Now send an update changing the Listener to be invalid.
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancers_[0]->ads_service()->SetLdsResource(listener);
  // Wait for xDS server to see NACK.
  auto deadline = absl::Now() + absl::Seconds(30);
  do {
    CheckRpcSendOk();
    ASSERT_LT(absl::Now(), deadline);
  } while (balancers_[0]->ads_service()->lds_response_state().state !=
           AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(balancers_[0]->ads_service()->lds_response_state().error_message,
              ::testing::ContainsRegex(absl::StrCat(
                  kServerName,
                  ": validation error.*"
                  "Listener has neither address nor ApiListener")));
  // Check one more time, just to make sure it still works after NACK.
  CheckRpcSendOk();
}

class XdsResolverLoadReportingOnlyTest : public XdsEnd2endTest {
 public:
  XdsResolverLoadReportingOnlyTest() : XdsEnd2endTest(4, 1, 3) {
    StartAllBackends();
  }
};

// Tests load reporting when switching over from one cluster to another.
TEST_P(XdsResolverLoadReportingOnlyTest, ChangeClusters) {
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  balancers_[0]->lrs_service()->set_cluster_names(
      {kDefaultClusterName, kNewClusterName});
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // cluster kDefaultClusterName -> locality0 -> backends 0 and 1
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // cluster kNewClusterName -> locality1 -> backends 2 and 3
  EdsResourceArgs args2({
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName));
  // CDS resource for kNewClusterName.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Wait for all backends to come online.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends(0, 2);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&ClientStats::cluster_name, kDefaultClusterName),
          ::testing::Property(
              &ClientStats::locality_stats,
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::AllOf(
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_successful_requests,
                                       num_ok),
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_requests_in_progress,
                                       0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_error_requests,
                          num_failure),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_issued_requests,
                          num_failure + num_ok))))),
          ::testing::Property(&ClientStats::total_dropped_requests,
                              num_drops))));
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  // Wait for all new backends to be used.
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends(2, 4);
  // The load report received at the balancer should be correct.
  load_report = balancers_[0]->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name,
                                  kDefaultClusterName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality0",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Lt(num_ok)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              ::testing::Le(num_failure)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::
                                  total_issued_requests,
                              ::testing::Le(num_failure + num_ok)))))),
              ::testing::Property(&ClientStats::total_dropped_requests,
                                  num_drops)),
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name, kNewClusterName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality1",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Le(num_ok)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              ::testing::Le(num_failure)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::
                                  total_issued_requests,
                              ::testing::Le(num_failure + num_ok)))))),
              ::testing::Property(&ClientStats::total_dropped_requests,
                                  num_drops))));
  int total_ok = 0;
  int total_failure = 0;
  for (const ClientStats& client_stats : load_report) {
    total_ok += client_stats.total_successful_requests();
    total_failure += client_stats.total_error_requests();
  }
  EXPECT_EQ(total_ok, num_ok);
  EXPECT_EQ(total_failure, num_failure);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
}

using SecureNamingTest = BasicTest;

// Tests that secure naming check passes if target name is expected.
TEST_P(SecureNamingTest, TargetNameIsExpected) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()}, nullptr, "xds_server");
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  CheckRpcSendOk();
}

// Tests that secure naming check fails if target name is unexpected.
TEST_P(SecureNamingTest, TargetNameIsUnexpected) {
  GRPC_GTEST_FLAG_SET_DEATH_TEST_STYLE("threadsafe");
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()}, nullptr,
                                "incorrect_server_name");
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED({ CheckRpcSendOk(); }, "");
}

using LdsTest = BasicTest;

// Tests that LDS client should send a NACK if there is no API listener in the
// Listener in the LDS response.
TEST_P(LdsTest, NoApiListener) {
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancers_[0]->ads_service()->SetLdsResource(listener);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Listener has neither address nor ApiListener"));
}

// Tests that LDS client should send a NACK if the route_specifier in the
// http_connection_manager is neither inlined route_config nor RDS.
TEST_P(LdsTest, WrongRouteSpecifier) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.mutable_scoped_routes();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "HttpConnectionManager neither has inlined route_config nor RDS."));
}

// Tests that LDS client should send a NACK if the rds message in the
// http_connection_manager is missing the config_source field.
TEST_P(LdsTest, RdsMissingConfigSource) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.mutable_rds()->set_route_config_name(
      kDefaultRouteConfigurationName);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "HttpConnectionManager missing config_source for RDS."));
}

// Tests that LDS client should send a NACK if the rds message in the
// http_connection_manager has a config_source field that does not specify
// ADS.
TEST_P(LdsTest, RdsConfigSourceDoesNotSpecifyAds) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("HttpConnectionManager ConfigSource for "
                                   "RDS does not specify ADS."));
}

// Tests that we NACK non-terminal filters at the end of the list.
TEST_P(LdsTest, NacksNonTerminalHttpFilterAtEndOfList) {
  SetNextResolutionForLbChannelAllBalancers();
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.client_only_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "non-terminal filter for config type grpc.testing"
                  ".client_only_http_filter is the last filter in the chain"));
}

// Test that we NACK terminal filters that are not at the end of the list.
TEST_P(LdsTest, NacksTerminalFilterBeforeEndOfList) {
  SetNextResolutionForLbChannelAllBalancers();
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // The default_listener_ has a terminal router filter by default. Add an
  // additional filter.
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("grpc.testing.terminal_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.terminal_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "terminal filter for config type envoy.extensions.filters.http"
          ".router.v3.Router must be the last filter in the chain"));
}

// Test that we NACK empty filter names.
TEST_P(LdsTest, RejectsEmptyHttpFilterName) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("empty filter name at index 0"));
}

// Test that we NACK duplicate HTTP filter names.
TEST_P(LdsTest, RejectsDuplicateHttpFilterName) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  http_connection_manager.mutable_http_filters(0)
      ->mutable_typed_config()
      ->PackFrom(HTTPFault());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("duplicate HTTP filter name: router"));
}

// Test that we NACK unknown filter types.
TEST_P(LdsTest, RejectsUnknownHttpFilterType) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types.
TEST_P(LdsTest, IgnoresOptionalUnknownHttpFilterType) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs.
TEST_P(LdsTest, RejectsHttpFilterWithoutConfig) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->set_name("unknown");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs.
TEST_P(LdsTest, IgnoresOptionalHttpFilterWithoutConfig) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->set_name("unknown");
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter configs.
TEST_P(LdsTest, RejectsUnparseableHttpFilterType) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(listener);
  filter->mutable_typed_config()->set_type_url(
      "type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "filter config for type "
          "envoy.extensions.filters.http.fault.v3.HTTPFault failed to parse"));
}

// Test that we NACK HTTP filters unsupported on client-side.
TEST_P(LdsTest, RejectsHttpFiltersNotSupportedOnClients) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("grpc.testing.server_only_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.server_only_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Filter grpc.testing.server_only_http_filter is not "
                           "supported on clients"));
}

// Test that we ignore optional HTTP filters unsupported on client-side.
TEST_P(LdsTest, IgnoresOptionalHttpFiltersNotSupportedOnClients) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("grpc.testing.server_only_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.server_only_http_filter");
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  WaitForBackend(0);
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

using LdsV2Test = LdsTest;

// Tests that we ignore the HTTP filter list in v2.
// TODO(roth): The test framework is not set up to allow us to test
// the server sending v2 resources when the client requests v3, so this
// just tests a pure v2 setup.  When we have time, fix this.
TEST_P(LdsV2Test, IgnoresHttpFilters) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(0, listener, default_route_config_);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
}

using LdsRdsTest = BasicTest;

// Tests that LDS client should send an ACK upon correct LDS response (with
// inlined RDS result).
TEST_P(LdsRdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
  // Make sure we actually used the RPC service for the right version of xDS.
  EXPECT_EQ(balancers_[0]->ads_service()->seen_v2_client(),
            GetParam().use_v2());
  EXPECT_NE(balancers_[0]->ads_service()->seen_v3_client(),
            GetParam().use_v2());
}

// Tests that we go into TRANSIENT_FAILURE if the Listener is removed.
TEST_P(LdsRdsTest, ListenerRemoved) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Unset LDS resource.
  balancers_[0]->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  // Wait for RPCs to start failing.
  do {
  } while (SendRpc(RpcOptions(), nullptr).ok());
  // Make sure RPCs are still failing.
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(1000));
  // Make sure we ACK'ed the update.
  EXPECT_EQ(balancers_[0]->ads_service()->lds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client ACKs but fails if matching domain can't be found in
// the LDS response.
TEST_P(LdsRdsTest, NoMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
  // Do a bit of polling, to allow the ACK to get to the ADS server.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100));
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the virtual host with matching domain
// if multiple virtual hosts exist in the LDS response.
TEST_P(LdsRdsTest, ChooseMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the last route in the virtual host if
// multiple routes exist in the LDS response.
TEST_P(LdsRdsTest, ChooseLastRoute) {
  RouteConfiguration route_config = default_route_config_;
  *(route_config.mutable_virtual_hosts(0)->add_routes()) =
      route_config.virtual_hosts(0).routes(0);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should ignore route which has query_parameters.
TEST_P(LdsRdsTest, RouteMatchHasQueryParameters) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_match()->add_query_parameters();
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should send a ACK if route match has a prefix
// that is either empty or a single slash
TEST_P(LdsRdsTest, RouteMatchHasValidPrefixEmptyOrSingleSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("/");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should ignore route which has a path
// prefix string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string with more than 2 slashes.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixExtraContent) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string "//".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixDoubleSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("//");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// but it's empty.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathEmptyPath) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("grpc.testing.EchoTest1Service/Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has too many slashes; for example, ends with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathTooManySlashes) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has only 1 slash: missing "/" between service and method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathOnlyOneSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service.Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing service.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingService) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("//Echo1");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingMethod) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Test that LDS client should reject route which has invalid path regex.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathRegex) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->mutable_safe_regex()->set_regex("a[z-a]");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "path matcher: Invalid regex string specified in matcher."));
}

// Tests that LDS client should fail RPCs with UNAVAILABLE status code if the
// matching route has an action other than RouteAction.
TEST_P(LdsRdsTest, MatchingRouteHasNoRouteAction) {
  RouteConfiguration route_config = default_route_config_;
  // Set a route with an inappropriate route action
  auto* vhost = route_config.mutable_virtual_hosts(0);
  vhost->mutable_routes(0)->mutable_redirect();
  // Add another route to make sure that the resolver code actually tries to
  // match to a route instead of using a shorthand logic to error out.
  auto* route = vhost->add_routes();
  route->mutable_match()->set_prefix("");
  route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_expected_error_code(
      StatusCode::UNAVAILABLE));
}

TEST_P(LdsRdsTest, RouteActionClusterHasEmptyClusterName) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster("");
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("RouteAction cluster contains empty cluster name."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetHasIncorrectTotalWeightSet) {
  const size_t kWeight75 = 75;
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + 1);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "RouteAction weighted_cluster has incorrect total weight"));
}

TEST_P(LdsRdsTest, RouteActionWeightedClusterHasZeroTotalWeight) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(0);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "RouteAction weighted_cluster has no valid clusters specified."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetClusterHasEmptyClusterName) {
  const size_t kWeight75 = 75;
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name("");
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("RouteAction weighted_cluster cluster "
                                   "contains empty cluster name."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetClusterHasNoWeight) {
  const size_t kWeight75 = 75;
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "RouteAction weighted_cluster cluster missing weight"));
}

TEST_P(LdsRdsTest, RouteHeaderMatchInvalidRegex) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->mutable_safe_regex_match()->set_regex("a[z-a]");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "header matcher: Invalid regex string specified in matcher."));
}

TEST_P(LdsRdsTest, RouteHeaderMatchInvalidRange) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->mutable_range_match()->set_start(1001);
  header_matcher1->mutable_range_match()->set_end(1000);
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(0, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "header matcher: Invalid range specifier specified: end cannot be "
          "smaller than start."));
}

// Tests that LDS client should choose the default route (with no matching
// specified) after unable to find a match with previous routes.
TEST_P(LdsRdsTest, XdsRoutingPathMatching) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* route3 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_path("/grpc.testing.EchoTest3Service/Echo3");
  route3->mutable_route()->set_cluster(kDefaultClusterName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO1)
                                    .set_rpc_method(METHOD_ECHO1)
                                    .set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho2Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO2)
                                    .set_rpc_method(METHOD_ECHO2)
                                    .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPathMatchingCaseInsensitive) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  // First route will not match, since it's case-sensitive.
  // Second route will match with same path.
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/GrPc.TeStInG.EcHoTeSt1SErViCe/EcHo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/GrPc.TeStInG.EcHoTeSt1SErViCe/EcHo1");
  route2->mutable_match()->mutable_case_sensitive()->set_value(false);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO1)
                                    .set_rpc_method(METHOD_ECHO1)
                                    .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPrefixMatching) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/grpc.testing.EchoTest2Service/");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho1Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho2Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO2).set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPrefixMatchingCaseInsensitive) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  // First route will not match, since it's case-sensitive.
  // Second route will match with same path.
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/GrPc.TeStInG.EcHoTeSt1SErViCe");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/GrPc.TeStInG.EcHoTeSt1SErViCe");
  route2->mutable_match()->mutable_case_sensitive()->set_value(false);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO1)
                                    .set_rpc_method(METHOD_ECHO1)
                                    .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPathRegexMatching) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  // Will match "/grpc.testing.EchoTest1Service/"
  route1->mutable_match()->mutable_safe_regex()->set_regex(".*1.*");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  // Will match "/grpc.testing.EchoTest2Service/"
  route2->mutable_match()->mutable_safe_regex()->set_regex(".*2.*");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho1Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_wait_for_ready(true));
  CheckRpcSendOk(
      kNumEcho2Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO2).set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingWeightedCluster) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNotUsedClusterName = "not_used_cluster";
  const size_t kNumEchoRpcs = 10;  // RPCs that will go to a fixed backend.
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const size_t kNumEcho1Rpcs =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  // Cluster with weight 0 will not be used.
  auto* weighted_cluster3 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster3->set_name(kNotUsedClusterName);
  weighted_cluster3->mutable_weight()->set_value(0);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 1);
  WaitForAllBackends(1, 3, WaitForBackendOptions(),
                     RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_25_request_count =
      backends_[2]->backend_service1()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetDefaultRoute) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const size_t kNumEchoRpcs =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(1, 3);
  CheckRpcSendOk(kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service()->request_count();
  const int weight_25_request_count =
      backends_[2]->backend_service()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEchoRpcs,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEchoRpcs,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingWeightedClusterUpdateWeights) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEchoRpcs = 10;
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const size_t kWeight50 = 50;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const double kWeight50Percent = static_cast<double>(kWeight50) / 100;
  const size_t kNumEcho1Rpcs7525 =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  const size_t kNumEcho1Rpcs5050 =
      ComputeIdealNumRpcs(kWeight50Percent, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 1);
  WaitForAllBackends(1, 3, WaitForBackendOptions(),
                     RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_25_request_count =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
  // Change Route Configurations: same clusters different weights.
  weighted_cluster1->mutable_weight()->set_value(kWeight50);
  weighted_cluster2->mutable_weight()->set_value(kWeight50);
  // Change default route to a new cluster to help to identify when new
  // polices are seen by the client.
  default_route->mutable_route()->set_cluster(kNewCluster3Name);
  SetRouteConfiguration(0, new_route_config);
  ResetBackendCounters();
  WaitForAllBackends(3, 4);
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs5050,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_50_request_count_1 =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_50_request_count_2 =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(kNumEchoRpcs, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_1) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_2) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingWeightedClusterUpdateClusters) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEchoRpcs = 10;
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const size_t kWeight50 = 50;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const double kWeight50Percent = static_cast<double>(kWeight50) / 100;
  const size_t kNumEcho1Rpcs7525 =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  const size_t kNumEcho1Rpcs5050 =
      ComputeIdealNumRpcs(kWeight50Percent, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kDefaultClusterName);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForBackend(0);
  WaitForBackend(1, WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  int weight_25_request_count =
      backends_[0]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
  // Change Route Configurations: new set of clusters with different weights.
  weighted_cluster1->mutable_weight()->set_value(kWeight50);
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight50);
  SetRouteConfiguration(0, new_route_config);
  ResetBackendCounters();
  WaitForBackend(2, WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs5050,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_50_request_count_1 =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_50_request_count_2 =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_1) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_2) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  // Change Route Configurations.
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  weighted_cluster2->set_name(kNewCluster3Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  SetRouteConfiguration(0, new_route_config);
  ResetBackendCounters();
  WaitForBackend(3, WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  weight_75_request_count = backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  weight_25_request_count = backends_[3]->backend_service1()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingClusterUpdateClusters) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 5;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Send Route Configuration.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(0, 1);
  CheckRpcSendOk(kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  // Change Route Configurations: new default cluster.
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(0, new_route_config);
  WaitForAllBackends(1, 2);
  CheckRpcSendOk(kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingClusterUpdateClustersWithPickingDelays) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Bring down the current backend: 0, this will delay route picking time,
  // resulting in un-committed RPCs.
  ShutdownBackend(0);
  // Send a RouteConfiguration with a default route that points to
  // backend 0.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(0, new_route_config);
  // Send exactly one RPC with no deadline and with wait_for_ready=true.
  // This RPC will not complete until after backend 0 is started.
  std::thread sending_rpc([this]() {
    CheckRpcSendOk(1, RpcOptions().set_wait_for_ready(true).set_timeout_ms(0));
  });
  // Send a non-wait_for_ready RPC which should fail, this will tell us
  // that the client has received the update and attempted to connect.
  const Status status = SendRpc(RpcOptions().set_timeout_ms(0));
  EXPECT_FALSE(status.ok());
  // Send a update RouteConfiguration to use backend 1.
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(0, new_route_config);
  // Wait for RPCs to go to the new backend: 1, this ensures that the client
  // has processed the update.
  WaitForBackend(
      1, WaitForBackendOptions().set_reset_counters(false).set_allow_failures(
             true));
  // Bring up the previous backend: 0, this will allow the delayed RPC to
  // finally call on_call_committed upon completion.
  StartBackend(0);
  sending_rpc.join();
  // Make sure RPCs go to the correct backend:
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingApplyXdsTimeout) {
  const int64_t kTimeoutMillis = 500;
  const int64_t kTimeoutNano = kTimeoutMillis * 1000000;
  const int64_t kTimeoutGrpcTimeoutHeaderMaxSecond = 1;
  const int64_t kTimeoutMaxStreamDurationSecond = 2;
  const int64_t kTimeoutHttpMaxStreamDurationSecond = 3;
  const int64_t kTimeoutApplicationSecond = 4;
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args3({{"locality0", {MakeNonExistantEndpoint()}}});
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster3);
  // Construct listener.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration of 3.5 seconds
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutHttpMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Construct route config.
  RouteConfiguration new_route_config = default_route_config_;
  // route 1: Set max_stream_duration of 2.5 seconds, Set
  // grpc_timeout_header_max of 1.5
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* max_stream_duration =
      route1->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  duration = max_stream_duration->mutable_grpc_timeout_header_max();
  duration->set_seconds(kTimeoutGrpcTimeoutHeaderMaxSecond);
  duration->set_nanos(kTimeoutNano);
  // route 2: Set max_stream_duration of 2.5 seconds
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  // route 3: No timeout values in route configuration
  auto* route3 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_path("/grpc.testing.EchoTestService/Echo");
  route3->mutable_route()->set_cluster(kNewCluster3Name);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(0, std::move(listener), new_route_config);
  // Test grpc_timeout_header_max of 1.5 seconds applied
  grpc_millis t0 = NowFromCycleCounter();
  grpc_millis t1 =
      t0 + kTimeoutGrpcTimeoutHeaderMaxSecond * 1000 + kTimeoutMillis;
  grpc_millis t2 = t0 + kTimeoutMaxStreamDurationSecond * 1000 + kTimeoutMillis;
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO1)
                  .set_rpc_method(METHOD_ECHO1)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  t0 = NowFromCycleCounter();
  EXPECT_GE(t0, t1);
  EXPECT_LT(t0, t2);
  // Test max_stream_duration of 2.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + kTimeoutMaxStreamDurationSecond * 1000 + kTimeoutMillis;
  t2 = t0 + kTimeoutHttpMaxStreamDurationSecond * 1000 + kTimeoutMillis;
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO2)
                  .set_rpc_method(METHOD_ECHO2)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  t0 = NowFromCycleCounter();
  EXPECT_GE(t0, t1);
  EXPECT_LT(t0, t2);
  // Test http_stream_duration of 3.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + kTimeoutHttpMaxStreamDurationSecond * 1000 + kTimeoutMillis;
  t2 = t0 + kTimeoutApplicationSecond * 1000 + kTimeoutMillis;
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  t0 = NowFromCycleCounter();
  EXPECT_GE(t0, t1);
  EXPECT_LT(t0, t2);
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenXdsTimeoutExplicit0) {
  const int64_t kTimeoutNano = 500000000;
  const int64_t kTimeoutMaxStreamDurationSecond = 2;
  const int64_t kTimeoutHttpMaxStreamDurationSecond = 3;
  const int64_t kTimeoutApplicationSecond = 4;
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Construct listener.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration of 3.5 seconds
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutHttpMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Construct route config.
  RouteConfiguration new_route_config = default_route_config_;
  // route 1: Set max_stream_duration of 2.5 seconds, Set
  // grpc_timeout_header_max of 0
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* max_stream_duration =
      route1->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  duration = max_stream_duration->mutable_grpc_timeout_header_max();
  duration->set_seconds(0);
  duration->set_nanos(0);
  // route 2: Set max_stream_duration to 0
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(0);
  duration->set_nanos(0);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(0, std::move(listener), new_route_config);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO1)
                  .set_rpc_method(METHOD_ECHO1)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
  // Test application timeout is applied for route 2
  t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO2)
                  .set_rpc_method(METHOD_ECHO2)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  ellapsed_nano_seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      system_clock::now() - t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenHttpTimeoutExplicit0) {
  const int64_t kTimeoutApplicationSecond = 4;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration to be explicit 0
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(0);
  duration->set_nanos(0);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(0, std::move(listener),
                                   default_route_config_);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

// Test to ensure application-specified deadline won't be affected when
// the xDS config does not specify a timeout.
TEST_P(LdsRdsTest, XdsRoutingWithOnlyApplicationTimeout) {
  const int64_t kTimeoutApplicationSecond = 4;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

TEST_P(LdsRdsTest, XdsRetryPolicyNumRetries) {
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(0, new_route_config);
  // Ensure we retried the correct number of times on all supported status.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::INTERNAL))
          .set_expected_error_code(StatusCode::INTERNAL));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::RESOURCE_EXHAUSTED))
          .set_expected_error_code(StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::UNAVAILABLE))
          .set_expected_error_code(StatusCode::UNAVAILABLE));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  // Ensure we don't retry on an unsupported status.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::UNAUTHENTICATED))
          .set_expected_error_code(StatusCode::UNAUTHENTICATED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyAtVirtualHostLevel) {
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* retry_policy =
      new_route_config.mutable_virtual_hosts(0)->mutable_retry_policy();
  retry_policy->set_retry_on(
      "cancelled,deadline-exceeded,internal,resource-exhausted,unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(0, new_route_config);
  // Ensure we retried the correct number of times on a supported status.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyLongBackOff) {
  // Set num retries to 3, but due to longer back off, we expect only 1 retry
  // will take place.
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  auto base_interval =
      retry_policy->mutable_retry_back_off()->mutable_base_interval();
  // Set backoff to 1 second, 1/2 of rpc timeout of 2 second.
  base_interval->set_seconds(1 * grpc_test_slowdown_factor());
  base_interval->set_nanos(0);
  SetRouteConfiguration(0, new_route_config);
  // No need to set max interval and just let it be the default of 10x of base.
  // We expect 1 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_timeout_ms(2500).set_server_expected_error(
                  StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1 + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyMaxBackOff) {
  // Set num retries to 3, but due to longer back off, we expect only 2 retry
  // will take place, while the 2nd one will obey the max backoff.
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  auto base_interval =
      retry_policy->mutable_retry_back_off()->mutable_base_interval();
  // Set backoff to 1 second.
  base_interval->set_seconds(1 * grpc_test_slowdown_factor());
  base_interval->set_nanos(0);
  auto max_interval =
      retry_policy->mutable_retry_back_off()->mutable_max_interval();
  // Set max interval to be the same as base, so 2 retries will take 2 seconds
  // and both retries will take place before the 2.5 seconds rpc timeout.
  // Tested to ensure if max is not set, this test will be the same as
  // XdsRetryPolicyLongBackOff and we will only see 1 retry in that case.
  max_interval->set_seconds(1 * grpc_test_slowdown_factor());
  max_interval->set_nanos(0);
  SetRouteConfiguration(0, new_route_config);
  // We expect 2 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_timeout_ms(2500).set_server_expected_error(
                  StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(2 + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyUnsupportedStatusCode) {
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("5xx");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(0, new_route_config);
  // We expect no retry.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest,
       XdsRetryPolicyUnsupportedStatusCodeWithVirtualHostLevelRetry) {
  const size_t kNumRetries = 3;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy with no supported retry_on
  // statuses.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("5xx");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  // Construct a virtual host level retry policy with supported statuses.
  auto* virtual_host_retry_policy =
      new_route_config.mutable_virtual_hosts(0)->mutable_retry_policy();
  virtual_host_retry_policy->set_retry_on(
      "cancelled,deadline-exceeded,internal,resource-exhausted,unavailable");
  virtual_host_retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(0, new_route_config);
  // We expect no retry.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyInvalidNumRetriesZero) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("deadline-exceeded");
  // Setting num_retries to zero is not valid.
  retry_policy->mutable_num_retries()->set_value(0);
  SetRouteConfiguration(0, new_route_config);
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy num_retries set to invalid value 0."));
}

TEST_P(LdsRdsTest, XdsRetryPolicyRetryBackOffMissingBaseInterval) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("deadline-exceeded");
  retry_policy->mutable_num_retries()->set_value(1);
  // RetryBackoff is there but base interval is missing.
  auto max_interval =
      retry_policy->mutable_retry_back_off()->mutable_max_interval();
  max_interval->set_seconds(0);
  max_interval->set_nanos(250000000);
  SetRouteConfiguration(0, new_route_config);
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy RetryBackoff missing base interval."));
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatching) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->set_exact_match("POST,PUT,GET");
  auto* header_matcher2 = route1->mutable_match()->add_headers();
  header_matcher2->set_name("header2");
  header_matcher2->mutable_safe_regex_match()->set_regex("[a-z]*");
  auto* header_matcher3 = route1->mutable_match()->add_headers();
  header_matcher3->set_name("header3");
  header_matcher3->mutable_range_match()->set_start(1);
  header_matcher3->mutable_range_match()->set_end(1000);
  auto* header_matcher4 = route1->mutable_match()->add_headers();
  header_matcher4->set_name("header4");
  header_matcher4->set_present_match(false);
  auto* header_matcher5 = route1->mutable_match()->add_headers();
  header_matcher5->set_name("header5");
  header_matcher5->set_present_match(true);
  auto* header_matcher6 = route1->mutable_match()->add_headers();
  header_matcher6->set_name("header6");
  header_matcher6->set_prefix_match("/grpc");
  auto* header_matcher7 = route1->mutable_match()->add_headers();
  header_matcher7->set_name("header7");
  header_matcher7->set_suffix_match(".cc");
  header_matcher7->set_invert_match(true);
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"header1", "POST"},
      {"header2", "blah"},
      {"header3", "1"},
      {"header5", "anything"},
      {"header6", "/grpc.testing.EchoTest1Service/"},
      {"header1", "PUT"},
      {"header7", "grpc.java"},
      {"header1", "GET"},
  };
  const auto header_match_rpc_options = RpcOptions()
                                            .set_rpc_service(SERVICE_ECHO1)
                                            .set_rpc_method(METHOD_ECHO1)
                                            .set_metadata(std::move(metadata));
  // Make sure all backends are up.
  WaitForBackend(0);
  WaitForBackend(1, WaitForBackendOptions(), header_match_rpc_options);
  // Send RPCs.
  CheckRpcSendOk(kNumEchoRpcs);
  CheckRpcSendOk(kNumEcho1Rpcs, header_match_rpc_options);
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialHeaderContentType) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("content-type");
  header_matcher1->set_exact_match("notapplication/grpc");
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  auto* header_matcher2 = default_route->mutable_match()->add_headers();
  header_matcher2->set_name("content-type");
  header_matcher2->set_exact_match("application/grpc");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  // Make sure the backend is up.
  WaitForAllBackends(0, 1);
  // Send RPCs.
  CheckRpcSendOk(kNumEchoRpcs);
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialCasesToIgnore) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const size_t kNumEchoRpcs = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("grpc-foo-bin");
  header_matcher1->set_present_match(true);
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  // Send headers which will mismatch each route
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"grpc-foo-bin", "grpc-foo-bin"},
  };
  WaitForAllBackends(0, 1);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_metadata(metadata));
  // Verify that only the default backend got RPCs since all previous routes
  // were mismatched.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingRuntimeFractionMatching) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const double kErrorTolerance = 0.05;
  const size_t kRouteMatchNumerator = 25;
  const double kRouteMatchPercent =
      static_cast<double>(kRouteMatchNumerator) / 100;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kRouteMatchPercent, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()
      ->mutable_runtime_fraction()
      ->mutable_default_value()
      ->set_numerator(kRouteMatchNumerator);
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  WaitForAllBackends(0, 2);
  CheckRpcSendOk(kNumRpcs);
  const int default_backend_count =
      backends_[0]->backend_service()->request_count();
  const int matched_backend_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(default_backend_count) / kNumRpcs,
              ::testing::DoubleNear(1 - kRouteMatchPercent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(matched_backend_count) / kNumRpcs,
              ::testing::DoubleNear(kRouteMatchPercent, kErrorTolerance));
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingUnmatchCases) {
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->set_exact_match("POST");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto route2 = route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher2 = route2->mutable_match()->add_headers();
  header_matcher2->set_name("header2");
  header_matcher2->mutable_range_match()->set_start(1);
  header_matcher2->mutable_range_match()->set_end(1000);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto route3 = route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher3 = route3->mutable_match()->add_headers();
  header_matcher3->set_name("header3");
  header_matcher3->mutable_safe_regex_match()->set_regex("[a-z]*");
  route3->mutable_route()->set_cluster(kNewCluster3Name);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  // Send headers which will mismatch each route
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"header1", "POST"},
      {"header2", "1000"},
      {"header3", "123"},
      {"header1", "GET"},
  };
  WaitForAllBackends(0, 1);
  CheckRpcSendOk(kNumEchoRpcs, RpcOptions().set_metadata(metadata));
  CheckRpcSendOk(kNumEcho1Rpcs, RpcOptions()
                                    .set_rpc_service(SERVICE_ECHO1)
                                    .set_rpc_method(METHOD_ECHO1)
                                    .set_metadata(metadata));
  // Verify that only the default backend got RPCs since all previous routes
  // were mismatched.
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_EQ(0, backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingChangeRoutesWithoutChangingClusters) {
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(0, route_config);
  // Make sure all backends are up and that requests for each RPC
  // service go to the right backends.
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(1, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Requests for services Echo and Echo2 should have gone to backend 0.
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(1, backends_[0]->backend_service2()->request_count());
  // Requests for service Echo1 should have gone to backend 1.
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  // Now send an update that changes the first route to match a
  // different RPC service, and wait for the client to make the change.
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest2Service/");
  SetRouteConfiguration(0, route_config);
  WaitForBackend(1, WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Now repeat the earlier test, making sure all traffic goes to the
  // right place.
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(1, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Requests for services Echo and Echo1 should have gone to backend 0.
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  // Requests for service Echo2 should have gone to backend 1.
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service2()->request_count());
}

// Test that we NACK unknown filter types in VirtualHost.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in VirtualHost.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in VirtualHost.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInFilterConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in VirtualHost.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

// Test that we NACK unknown filter types in Route.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in Route.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in Route.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in Route.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInFilterConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in Route.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in Route.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

// Test that we NACK unknown filter types in ClusterWeight.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in ClusterWeight.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in ClusterWeight.
TEST_P(LdsRdsTest,
       RejectsHttpFilterWithoutConfigInFilterConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  EXPECT_EQ(RouteConfigurationResponseState(0).state,
            AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in ClusterWeight.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(0, default_listener_, route_config);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForRdsNack()) << "timed out waiting for NACK";
  const auto response_state = RouteConfigurationResponseState(0);
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

using CdsTest = BasicTest;

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  (void)SendRpc();
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(CdsTest, LogicalDNSClusterType) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(1, 2));
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 1.
  WaitForBackend(1);
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingLoadAssignment) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "load_assignment not present for LOGICAL_DNS cluster"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingLocalities) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 0"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeMultipleLocalities) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* load_assignment = cluster.mutable_load_assignment();
  load_assignment->add_endpoints();
  load_assignment->add_endpoints();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 2"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingEndpoints) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 0"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeMultipleEndpoints) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* locality = cluster.mutable_load_assignment()->add_endpoints();
  locality->add_lb_endpoints();
  locality->add_lb_endpoints();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 2"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeEmptyEndpoint) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("LbEndpoint endpoint field not set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeEndpointMissingAddress) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("Endpoint address field not set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeAddressMissingSocketAddress) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("Address socket_address field not set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressHasResolverName) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_resolver_name("foo");
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("LOGICAL_DNS clusters must NOT have a "
                                   "custom resolver name set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressMissingAddress) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("SocketAddress address field not set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressMissingPort) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address(kServerName);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("SocketAddress port_value field not set"));
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, AggregateClusterType) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Wait for traffic to go to backend 1.
  WaitForBackend(1);
  // Shutdown backend 1 and wait for all traffic to go to backend 2.
  ShutdownBackend(1);
  WaitForBackend(2, WaitForBackendOptions().set_allow_failures(true));
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 1 back and ensure all traffic go back to it.
  StartBackend(1);
  WaitForBackend(1);
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, AggregateClusterEdsToLogicalDns) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster1);
  // Create Logical DNS Cluster
  auto logical_dns_cluster = default_cluster_;
  logical_dns_cluster.set_name(kLogicalDNSClusterName);
  logical_dns_cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = logical_dns_cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancers_[0]->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(2, 3));
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 1.
  WaitForBackend(1);
  // Shutdown backend 1 and wait for all traffic to go to backend 2.
  ShutdownBackend(1);
  WaitForBackend(2, WaitForBackendOptions().set_allow_failures(true));
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 1 back and ensure all traffic go back to it.
  StartBackend(1);
  WaitForBackend(1);
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

TEST_P(CdsTest, AggregateClusterLogicalDnsToEds) {
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER",
             "true");
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancers_[0]->ads_service()->SetCdsResource(new_cluster2);
  // Create Logical DNS Cluster
  auto logical_dns_cluster = default_cluster_;
  logical_dns_cluster.set_name(kLogicalDNSClusterName);
  logical_dns_cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = logical_dns_cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancers_[0]->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kLogicalDNSClusterName);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(1, 2));
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 1.
  WaitForBackend(1);
  // Shutdown backend 1 and wait for all traffic to go to backend 2.
  ShutdownBackend(1);
  WaitForBackend(2, WaitForBackendOptions().set_allow_failures(true));
  EXPECT_EQ(balancers_[0]->ads_service()->cds_response_state().state,
            AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 1 back and ensure all traffic go back to it.
  StartBackend(1);
  WaitForBackend(1);
  gpr_unsetenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
}

// Test that CDS client should send a NACK if cluster type is Logical DNS but
// the feature is not yet supported.
TEST_P(CdsTest, LogicalDNSClusterTypeDisabled) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

// Test that CDS client should send a NACK if cluster type is AGGREGATE but
// the feature is not yet supported.
TEST_P(CdsTest, AggregateClusterTypeDisabled) {
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters("cluster1");
  cluster_config.add_clusters("cluster2");
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

// Tests that CDS client should send a NACK if the cluster type in CDS
// response is unsupported.
TEST_P(CdsTest, UnsupportedClusterType) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

// Tests that the NACK for multiple bad resources includes both errors.
TEST_P(CdsTest, MultipleBadResources) {
  constexpr char kClusterName2[] = "cluster_name_2";
  constexpr char kClusterName3[] = "cluster_name_3";
  // Add cluster with unsupported type.
  auto cluster = default_cluster_;
  cluster.set_name(kClusterName2);
  cluster.set_type(Cluster::STATIC);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Add second cluster with the same error.
  cluster.set_name(kClusterName3);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Change RouteConfig to point to all clusters.
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_routes();
  // First route: default cluster, selected based on header.
  auto* route = route_config.mutable_virtual_hosts(0)->add_routes();
  route->mutable_match()->set_prefix("");
  auto* header_matcher = route->mutable_match()->add_headers();
  header_matcher->set_name("cluster");
  header_matcher->set_exact_match(kDefaultClusterName);
  route->mutable_route()->set_cluster(kDefaultClusterName);
  // Second route: cluster 2, selected based on header.
  route = route_config.mutable_virtual_hosts(0)->add_routes();
  route->mutable_match()->set_prefix("");
  header_matcher = route->mutable_match()->add_headers();
  header_matcher->set_name("cluster");
  header_matcher->set_exact_match(kClusterName2);
  route->mutable_route()->set_cluster(kClusterName2);
  // Third route: cluster 3, used by default.
  route = route_config.mutable_virtual_hosts(0)->add_routes();
  route->mutable_match()->set_prefix("");
  route->mutable_route()->set_cluster(kClusterName3);
  SetRouteConfiguration(0, route_config);
  // Add EDS resource.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send RPC.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::ContainsRegex(absl::StrCat(kClusterName2,
                                            ": validation error.*"
                                            "DiscoveryType is not valid.*",
                                            kClusterName3,
                                            ": validation error.*"
                                            "DiscoveryType is not valid")));
  // RPCs for default cluster should succeed.
  std::vector<std::pair<std::string, std::string>> metadata_default_cluster = {
      {"cluster", kDefaultClusterName},
  };
  CheckRpcSendOk(
      1, RpcOptions().set_metadata(std::move(metadata_default_cluster)));
  // RPCs for cluster 2 should fail.
  std::vector<std::pair<std::string, std::string>> metadata_cluster_2 = {
      {"cluster", kClusterName2},
  };
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_rpc_options(
      RpcOptions().set_metadata(std::move(metadata_cluster_2))));
}

// Tests that we don't trigger does-not-exist callbacks for a resource
// that was previously valid but is updated to be invalid.
TEST_P(CdsTest, InvalidClusterStillExistsIfPreviouslyCached) {
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Check that everything works.
  CheckRpcSendOk();
  // Now send an update changing the Cluster to be invalid.
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Wait for xDS server to see NACK.
  auto deadline = absl::Now() + absl::Seconds(30);
  do {
    CheckRpcSendOk();
    ASSERT_LT(absl::Now(), deadline);
  } while (balancers_[0]->ads_service()->cds_response_state().state !=
           AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(balancers_[0]->ads_service()->cds_response_state().error_message,
              ::testing::ContainsRegex(absl::StrCat(
                  kDefaultClusterName,
                  ": validation error.*DiscoveryType is not valid")));
  // Check one more time, just to make sure it still works after NACK.
  CheckRpcSendOk();
}

// Tests that CDS client should send a NACK if the eds_config in CDS response
// is other than ADS.
TEST_P(CdsTest, WrongEdsConfig) {
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("EDS ConfigSource is not ADS."));
}

// Tests that CDS client should send a NACK if the lb_policy in CDS response
// is other than ROUND_ROBIN.
TEST_P(CdsTest, WrongLbPolicy) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::LEAST_REQUEST);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("LB policy is not supported."));
}

// Tests that CDS client should send a NACK if the lrs_server in CDS response
// is other than SELF.
TEST_P(CdsTest, WrongLrsServer) {
  auto cluster = default_cluster_;
  cluster.mutable_lrs_server()->mutable_ads();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("LRS ConfigSource is not self."));
}

// Tests that ring hash policy that hashes using channel id ensures all RPCs
// to go 1 particular backend.
TEST_P(CdsTest, RingHashChannelIdHashing) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk(100);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Tests that ring hash policy that hashes using a header value can spread
// RPCs across all the backends.
TEST_P(CdsTest, RingHashHeaderHashing) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  const auto rpc_options2 = RpcOptions().set_metadata(std::move(metadata2));
  const auto rpc_options3 = RpcOptions().set_metadata(std::move(metadata3));
  WaitForBackend(0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(1, WaitForBackendOptions(), rpc_options1);
  WaitForBackend(2, WaitForBackendOptions(), rpc_options2);
  WaitForBackend(3, WaitForBackendOptions(), rpc_options3);
  CheckRpcSendOk(100, rpc_options);
  CheckRpcSendOk(100, rpc_options1);
  CheckRpcSendOk(100, rpc_options2);
  CheckRpcSendOk(100, rpc_options3);
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(100, backends_[i]->backend_service()->request_count());
  }
}

// Tests that ring hash policy that hashes using a header value and regex
// rewrite to aggregate RPCs to 1 backend.
TEST_P(CdsTest, RingHashHeaderHashingWithRegexRewrite) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  hash_policy->mutable_header()
      ->mutable_regex_rewrite()
      ->mutable_pattern()
      ->set_regex("[0-9]+");
  hash_policy->mutable_header()->mutable_regex_rewrite()->set_substitution(
      "foo");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  const auto rpc_options2 = RpcOptions().set_metadata(std::move(metadata2));
  const auto rpc_options3 = RpcOptions().set_metadata(std::move(metadata3));
  CheckRpcSendOk(100, rpc_options);
  CheckRpcSendOk(100, rpc_options1);
  CheckRpcSendOk(100, rpc_options2);
  CheckRpcSendOk(100, rpc_options3);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 400)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Tests that ring hash policy that hashes using a random value.
TEST_P(CdsTest, RingHashNoHashPolicy) {
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(0, 2, WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(kNumRpcs);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Test that ring hash policy evaluation will continue past the terminal
// policy if no results are produced yet.
TEST_P(CdsTest, RingHashContinuesPastTerminalPolicyThatDoesNotProduceResult) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  hash_policy->set_terminal(true);
  auto* hash_policy2 = route->mutable_route()->add_hash_policy();
  hash_policy2->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  CheckRpcSendOk(100, rpc_options);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 100);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0);
}

// Test random hash is used when header hashing specified a header field that
// the RPC did not have.
TEST_P(CdsTest, RingHashOnHeaderThatIsNotPresent) {
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"unmatched_header", absl::StrFormat("%" PRIu32, rand())},
  };
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(0, 2, WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(kNumRpcs, rpc_options);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Test random hash is used when only unsupported hash policies are
// configured.
TEST_P(CdsTest, RingHashUnsupportedHashPolicyDefaultToRandomHashing) {
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy_unsupported_1 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_1->mutable_cookie()->set_name("cookie");
  auto* hash_policy_unsupported_2 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_2->mutable_connection_properties()->set_source_ip(
      true);
  auto* hash_policy_unsupported_3 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_3->mutable_query_parameter()->set_name(
      "query_parameter");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(0, 2, WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(kNumRpcs);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a random value can spread
// RPCs across all the backends according to locality weight.
TEST_P(CdsTest, RingHashRandomHashingDistributionAccordingToEndpointWeight) {
  const size_t kWeight1 = 1;
  const size_t kWeight2 = 2;
  const size_t kWeightTotal = kWeight1 + kWeight2;
  const double kWeight33Percent = static_cast<double>(kWeight1) / kWeightTotal;
  const double kWeight66Percent = static_cast<double>(kWeight2) / kWeightTotal;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kWeight33Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0",
                         {CreateEndpoint(0, HealthStatus::UNKNOWN, 1),
                          CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(0, 2, WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(kNumRpcs);
  const int weight_33_request_count =
      backends_[0]->backend_service()->request_count();
  const int weight_66_request_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(weight_33_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight33Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_66_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight66Percent, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a random value can spread
// RPCs across all the backends according to locality weight.
TEST_P(CdsTest,
       RingHashRandomHashingDistributionAccordingToLocalityAndEndpointWeight) {
  const size_t kWeight1 = 1 * 1;
  const size_t kWeight2 = 2 * 2;
  const size_t kWeightTotal = kWeight1 + kWeight2;
  const double kWeight20Percent = static_cast<double>(kWeight1) / kWeightTotal;
  const double kWeight80Percent = static_cast<double>(kWeight2) / kWeightTotal;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kWeight20Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args(
      {{"locality0", {CreateEndpoint(0, HealthStatus::UNKNOWN, 1)}, 1},
       {"locality1", {CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}, 2}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(0, 2, WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(kNumRpcs);
  const int weight_20_request_count =
      backends_[0]->backend_service()->request_count();
  const int weight_80_request_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(weight_20_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight20Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_80_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight80Percent, kErrorTolerance));
}

// Tests round robin is not implacted by the endpoint weight, and that the
// localities in a locality map are picked according to their weights.
TEST_P(CdsTest, RingHashEndpointWeightDoesNotImpactWeightedRoundRobin) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const int kLocalityWeight0 = 2;
  const int kLocalityWeight1 = 8;
  const int kTotalLocalityWeight = kLocalityWeight0 + kLocalityWeight1;
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kLocalityWeightRate0, kErrorTolerance);
  // ADS response contains 2 localities, each of which contains 1 backend.
  EdsResourceArgs args({
      {"locality0",
       {CreateEndpoint(0, HealthStatus::UNKNOWN, 8)},
       kLocalityWeight0},
      {"locality1",
       {CreateEndpoint(1, HealthStatus::UNKNOWN, 2)},
       kLocalityWeight1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for both backends to be ready.
  WaitForAllBackends(0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::DoubleNear(kLocalityWeightRate0, kErrorTolerance));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::DoubleNear(kLocalityWeightRate1, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a fixed string ensures all
// RPCs to go 1 particular backend; and that subsequent hashing policies are
// ignored due to the setting of terminal.
TEST_P(CdsTest, RingHashFixedHashingTerminalPolicy) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("fixed_string");
  hash_policy->set_terminal(true);
  auto* hash_policy_to_be_ignored = route->mutable_route()->add_hash_policy();
  hash_policy_to_be_ignored->mutable_header()->set_header_name("random_string");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"fixed_string", "fixed_value"},
      {"random_string", absl::StrFormat("%" PRIu32, rand())},
  };
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  CheckRpcSendOk(100, rpc_options);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Test that the channel will go from idle to ready via connecting;
// (tho it is not possible to catch the connecting state before moving to
// ready)
TEST_P(CdsTest, RingHashIdleToReady) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  CheckRpcSendOk();
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(false));
}

// Test that when the first pick is down leading to a transient failure, we
// will move on to the next ring hash entry.
TEST_P(CdsTest, RingHashTransientFailureCheckNextOne) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  const int unused_port = grpc_pick_unused_port_or_die();
  endpoints.emplace_back(unused_port);
  endpoints.emplace_back(backends_[1]->port());
  EdsResourceArgs args({
      {"locality0", std::move(endpoints)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash",
       CreateMetadataValueThatHashesToBackendPort(unused_port)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  WaitForBackend(1, WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(100, rpc_options);
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Test that when a backend goes down, we will move on to the next subchannel
// (with a lower priority).  When the backend comes back up, traffic will move
// back.
TEST_P(CdsTest, RingHashSwitchToLowerPrioirtyAndThenBack) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  WaitForBackend(0, WaitForBackendOptions(), rpc_options);
  ShutdownBackend(0);
  WaitForBackend(1, WaitForBackendOptions().set_allow_failures(true),
                 rpc_options);
  StartBackend(0);
  WaitForBackend(0, WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(100, rpc_options);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
}

// Test that when all backends are down, we will keep reattempting.
TEST_P(CdsTest, RingHashAllFailReattempt) {
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  endpoints.emplace_back(grpc_pick_unused_port_or_die());
  endpoints.emplace_back(backends_[1]->port());
  EdsResourceArgs args({
      {"locality0", std::move(endpoints)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  ShutdownBackend(1);
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_rpc_options(
      RpcOptions().set_metadata(std::move(metadata))));
  StartBackend(1);
  // Ensure we are actively connecting without any traffic.
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
}

// Test that when all backends are down and then up, we may pick a TF backend
// and we will then jump to ready backend.
TEST_P(CdsTest, RingHashTransientFailureSkipToAvailableReady) {
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  // Make sure we include some unused ports to fill the ring.
  endpoints.emplace_back(backends_[0]->port());
  endpoints.emplace_back(backends_[1]->port());
  endpoints.emplace_back(grpc_pick_unused_port_or_die());
  endpoints.emplace_back(grpc_pick_unused_port_or_die());
  EdsResourceArgs args({
      {"locality0", std::move(endpoints)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  ShutdownBackend(0);
  ShutdownBackend(1);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions().set_rpc_options(rpc_options));
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel_->GetState(false));
  // Bring up 0, should be picked as the RPC is hashed to it.
  StartBackend(0);
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
  WaitForBackend(0, WaitForBackendOptions(), rpc_options);
  // Bring down 0 and bring up 1.
  // Note the RPC contains a header value that will always be hashed to
  // backend 0. So by purposely bring down backend 0 and bring up another
  // backend, this will ensure Picker's first choice of backend 0 will fail
  // and it will
  // 1. reattempt backend 0 and
  // 2. go through the remaining subchannels to find one in READY.
  // Since the the entries in the ring is pretty distributed and we have
  // unused ports to fill the ring, it is almost guaranteed that the Picker
  // will go through some non-READY entries and skip them as per design.
  ShutdownBackend(0);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions().set_rpc_options(rpc_options));
  StartBackend(1);
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
  WaitForBackend(1, WaitForBackendOptions(), rpc_options);
}

// Test unspported hash policy types are all ignored before a supported
// policy.
TEST_P(CdsTest, RingHashUnsupportedHashPolicyUntilChannelIdHashing) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy_unsupported_1 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_1->mutable_cookie()->set_name("cookie");
  auto* hash_policy_unsupported_2 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_2->mutable_connection_properties()->set_source_ip(
      true);
  auto* hash_policy_unsupported_3 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_3->mutable_query_parameter()->set_name(
      "query_parameter");
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk(100);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Test we nack when ring hash policy has invalid hash function (something
// other than XX_HASH.
TEST_P(CdsTest, RingHashPolicyHasInvalidHashFunction) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->set_hash_function(
      Cluster::RingHashLbConfig::MURMUR_HASH_2);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("ring hash lb config has invalid hash function."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidMinimumRingSize) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      0);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "min_ring_size is not in the range of 1 to 8388608."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidMaxmumRingSize) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_maximum_ring_size()->set_value(
      8388609);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "max_ring_size is not in the range of 1 to 8388608."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidRingSizeMinGreaterThanMax) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_maximum_ring_size()->set_value(
      5000);
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      5001);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(0, default_listener_, new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  SetNextResolutionForLbChannelAllBalancers();
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "min_ring_size cannot be greater than max_ring_size."));
}

class XdsSecurityTest : public BasicTest {
 protected:
  XdsSecurityTest() {
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
    balancers_[0]->ads_service()->SetEdsResource(
        BuildEdsResource(args, DefaultEdsServiceName()));
    SetNextResolutionForLbChannelAllBalancers();
  }

  ~XdsSecurityTest() override {
    g_fake1_cert_data_map = nullptr;
    g_fake2_cert_data_map = nullptr;
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
    auto cluster = default_cluster_;
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
    balancers_[0]->ads_service()->SetCdsResource(cluster);
    // The updates might take time to have an effect, so use a retry loop.
    constexpr int kRetryCount = 100;
    int num_tries = 0;
    for (; num_tries < kRetryCount; num_tries++) {
      // Give some time for the updates to propagate.
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
      if (test_expects_failure) {
        // Restart the servers to force a reconnection so that previously
        // connected subchannels are not used for the RPC.
        ShutdownBackend(0);
        StartBackend(0);
        if (SendRpc().ok()) {
          gpr_log(GPR_ERROR, "RPC succeeded. Failure expected. Trying again.");
          continue;
        }
      } else {
        WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
        Status status = SendRpc();
        if (!status.ok()) {
          gpr_log(GPR_ERROR, "RPC failed. code=%d message=%s Trying again.",
                  status.error_code(), status.error_message().c_str());
          continue;
        }
        if (backends_[0]->backend_service()->last_peer_identity() !=
            expected_authenticated_identity) {
          gpr_log(
              GPR_ERROR,
              "Expected client identity does not match. (actual) %s vs "
              "(expected) %s Trying again.",
              absl::StrJoin(
                  backends_[0]->backend_service()->last_peer_identity(), ",")
                  .c_str(),
              absl::StrJoin(expected_authenticated_identity, ",").c_str());
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
};

TEST_P(XdsSecurityTest, UnknownTransportSocket) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("unknown_transport_socket");
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized transport socket: unknown_transport_socket"));
}

TEST_P(XdsSecurityTest,
       TLSConfigurationWithoutValidationContextCertificateProviderInstance) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("TLS configuration provided but no "
                                   "ca_certificate_provider_instance found."));
}

TEST_P(
    XdsSecurityTest,
    MatchSubjectAltNamesProvidedWithoutValidationContextCertificateProviderInstance) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  auto* validation_context = upstream_tls_context.mutable_common_tls_context()
                                 ->mutable_validation_context();
  *validation_context->add_match_subject_alt_names() = server_san_exact_;
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("TLS configuration provided but no "
                                   "ca_certificate_provider_instance found."));
}

TEST_P(
    XdsSecurityTest,
    TlsCertificateProviderInstanceWithoutValidationContextCertificateProviderInstance) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name(std::string("fake_plugin1"));
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("TLS configuration provided but no "
                                   "ca_certificate_provider_instance found."));
}

TEST_P(XdsSecurityTest, RegexSanMatcherDoesNotAllowIgnoreCase) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name(std::string("fake_plugin1"));
  auto* validation_context = upstream_tls_context.mutable_common_tls_context()
                                 ->mutable_validation_context();
  StringMatcher matcher;
  matcher.mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_safe_regex()->set_regex(
      "(foo|waterzooi).test.google.(fr|be)");
  matcher.set_ignore_case(true);
  *validation_context->add_match_subject_alt_names() = matcher;
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "StringMatcher: ignore_case has no effect for SAFE_REGEX."));
}

TEST_P(XdsSecurityTest, UnknownRootCertificateProvider) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("unknown");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized certificate provider instance name: unknown"));
}

TEST_P(XdsSecurityTest, UnknownIdentityCertificateProvider) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("unknown");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized certificate provider instance name: unknown"));
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest,
       NacksCertificateValidationContextWithVerifyCertificateSpki) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->add_verify_certificate_spki("spki");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "CertificateValidationContext: verify_certificate_spki unsupported"));
}

TEST_P(XdsSecurityTest,
       NacksCertificateValidationContextWithVerifyCertificateHash) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->add_verify_certificate_hash("hash");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "CertificateValidationContext: verify_certificate_hash unsupported"));
}

TEST_P(XdsSecurityTest,
       NacksCertificateValidationContextWithRequireSignedCertificateTimes) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_require_signed_certificate_timestamp()
      ->set_value(true);
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("CertificateValidationContext: "
                           "require_signed_certificate_timestamp unsupported"));
}

TEST_P(XdsSecurityTest, NacksCertificateValidationContextWithCrl) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_crl();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("CertificateValidationContext: crl unsupported"));
}

TEST_P(XdsSecurityTest,
       NacksCertificateValidationContextWithCustomValidatorConfig) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_custom_validator_config();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr(
          "CertificateValidationContext: custom_validator_config unsupported"));
}

TEST_P(XdsSecurityTest, NacksValidationContextSdsSecretConfig) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context_sds_secret_config();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("validation_context_sds_secret_config unsupported"));
}

TEST_P(XdsSecurityTest, NacksTlsParams) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()->mutable_tls_params();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("tls_params unsupported"));
}

TEST_P(XdsSecurityTest, NacksCustomHandshaker) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_custom_handshaker();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("custom_handshaker unsupported"));
}

TEST_P(XdsSecurityTest, NacksTlsCertificates) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()->add_tls_certificates();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("tls_certificates unsupported"));
}

TEST_P(XdsSecurityTest, NacksTlsCertificateSdsSecretConfigs) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_ca_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  upstream_tls_context.mutable_common_tls_context()
      ->add_tls_certificate_sds_secret_configs();
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  ASSERT_TRUE(WaitForCdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->cds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("tls_certificate_sds_secret_configs unsupported"));
}

TEST_P(XdsSecurityTest, TestTlsConfigurationInCombinedValidationContext) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
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
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
  Status status = SendRpc();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
}

// TODO(yashykt): Remove this test once we stop supporting old fields
TEST_P(XdsSecurityTest,
       TestTlsConfigurationInValidationContextCertificateProviderInstance) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  upstream_tls_context.mutable_common_tls_context()
      ->mutable_combined_validation_context()
      ->mutable_validation_context_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
  Status status = SendRpc();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithNoSanMatchers) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {}, authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithExactSanMatcher) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithPrefixSanMatcher) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithSuffixSanMatcher) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_suffix_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithContainsSanMatcher) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_contains_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRegexSanMatcher) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_regex_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithSanMatchersUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "fake_plugin1", "",
      {server_san_exact_, server_san_prefix_}, authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {bad_san_1_, bad_san_2_}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "fake_plugin1", "",
      {server_san_prefix_, server_san_regex_}, authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRootPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {bad_root_cert_, bad_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin2" /* bad root */, "",
                                          "fake_plugin1", "", {}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
  g_fake2_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithIdentityPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {root_cert_, fallback_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin2",
                                          "", {server_san_exact_},
                                          fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
  g_fake2_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithBothPluginsUpdated) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {bad_root_cert_, bad_identity_pair_}},
      {"good", {root_cert_, fallback_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin2", "", "fake_plugin2",
                                          "", {}, {}, true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin2", "good", "fake_plugin2", "good", {server_san_prefix_},
      fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
  g_fake2_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithRootCertificateNameUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"bad", {bad_root_cert_, bad_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_regex_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "fake_plugin1",
                                          "", {server_san_regex_}, {},
                                          true /* failure */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest,
       TestMtlsConfigurationWithIdentityCertificateNameUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"bad", {bad_root_cert_, bad_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "bad", {server_san_exact_}, {},
                                          true /* failure */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest,
       TestMtlsConfigurationWithIdentityCertificateNameUpdateGoodCerts) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"good", {root_cert_, fallback_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "good", {server_san_exact_},
                                          fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsConfigurationWithBothCertificateNamesUpdated) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"bad", {bad_root_cert_, bad_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "fake_plugin1",
                                          "bad", {server_san_prefix_}, {},
                                          true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_prefix_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithNoSanMatchers) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "", {},
                                          {} /* unauthenticated */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithSanMatchers) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "",
      {server_san_exact_, server_san_prefix_, server_san_regex_},
      {} /* unauthenticated */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithSanMatchersUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {server_san_exact_, server_san_prefix_},
      {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {bad_san_1_, bad_san_2_},
      {} /* unauthenticated */, true /* failure */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin1", "", "", "", {server_san_prefix_, server_san_regex_},
      {} /* unauthenticated */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithRootCertificateNameUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"bad", {bad_root_cert_, bad_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "bad", "", "",
                                          {server_san_exact_}, {},
                                          true /* failure */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsConfigurationWithRootPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {bad_root_cert_, bad_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration(
      "fake_plugin2", "", "", "", {server_san_exact_}, {}, true /* failure */);
  g_fake1_cert_data_map = nullptr;
  g_fake2_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestFallbackConfiguration) {
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsToTls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestMtlsToFallback) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsToMtls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestTlsToFallback) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestFallbackToMtls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "fake_plugin1",
                                          "", {server_san_exact_},
                                          authenticated_identity_);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestFallbackToTls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  UpdateAndVerifyXdsSecurityConfiguration("", "", "", "", {},
                                          fallback_authenticated_identity_);
  UpdateAndVerifyXdsSecurityConfiguration("fake_plugin1", "", "", "",
                                          {server_san_exact_},
                                          {} /* unauthenticated */);
  g_fake1_cert_data_map = nullptr;
}

TEST_P(XdsSecurityTest, TestFileWatcherCertificateProvider) {
  UpdateAndVerifyXdsSecurityConfiguration("file_plugin", "", "file_plugin", "",
                                          {server_san_exact_},
                                          authenticated_identity_);
}

class XdsEnabledServerTest : public XdsEnd2endTest {
 protected:
  XdsEnabledServerTest()
      : XdsEnd2endTest(1, 1, 100, 0, true /* use_xds_enabled_server */) {
    EdsResourceArgs args({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    });
    balancers_[0]->ads_service()->SetEdsResource(
        BuildEdsResource(args, DefaultEdsServiceName()));
    SetNextResolution({});
    SetNextResolutionForLbChannelAllBalancers();
  }
};

TEST_P(XdsEnabledServerTest, Basic) {
  backends_[0]->Start();
  WaitForBackend(0);
}

TEST_P(XdsEnabledServerTest, BadLdsUpdateNoApiListenerNorAddress) {
  Listener listener = default_server_listener_;
  listener.clear_address();
  listener.set_name(
      absl::StrCat("grpc/server?xds.resource.listening_address=",
                   ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()));
  balancers_[0]->ads_service()->SetLdsResource(listener);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Listener has neither address nor ApiListener"));
}

TEST_P(XdsEnabledServerTest, BadLdsUpdateBothApiListenerAndAddress) {
  Listener listener = default_server_listener_;
  listener.mutable_api_listener();
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Listener has both address and ApiListener"));
}

TEST_P(XdsEnabledServerTest, UnsupportedL4Filter) {
  Listener listener = default_server_listener_;
  listener.mutable_default_filter_chain()->clear_filters();
  listener.mutable_default_filter_chain()->add_filters()->mutable_typed_config()->PackFrom(default_listener_ /* any proto object other than HttpConnectionManager */);
  balancers_[0]->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("Unsupported filter type"));
}

TEST_P(XdsEnabledServerTest, NacksEmptyHttpFilterList) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.clear_http_filters();
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("Expected at least one HTTP filter"));
}

TEST_P(XdsEnabledServerTest, UnsupportedHttpFilter) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.clear_http_filters();
  auto* http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("grpc.testing.unsupported_http_filter");
  http_filter->mutable_typed_config()->set_type_url(
      "grpc.testing.unsupported_http_filter");
  http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("router");
  http_filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "grpc.testing.unsupported_http_filter"));
}

TEST_P(XdsEnabledServerTest, HttpFilterNotSupportedOnServer) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.clear_http_filters();
  auto* http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("grpc.testing.client_only_http_filter");
  http_filter->mutable_typed_config()->set_type_url(
      "grpc.testing.client_only_http_filter");
  http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("router");
  http_filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Filter grpc.testing.client_only_http_filter is not "
                           "supported on servers"));
}

TEST_P(XdsEnabledServerTest,
       HttpFilterNotSupportedOnServerIgnoredWhenOptional) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.clear_http_filters();
  auto* http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("grpc.testing.client_only_http_filter");
  http_filter->mutable_typed_config()->set_type_url(
      "grpc.testing.client_only_http_filter");
  http_filter->set_is_optional(true);
  http_filter = http_connection_manager.add_http_filters();
  http_filter->set_name("router");
  http_filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  WaitForBackend(0);
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::ACKED);
}

// Verify that a mismatch of listening address results in "not serving"
// status.
TEST_P(XdsEnabledServerTest, ListenerAddressMismatch) {
  Listener listener = default_server_listener_;
  // Set a different listening address in the LDS update
  listener.mutable_address()->mutable_socket_address()->set_address(
      "192.168.1.1");
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_P(XdsEnabledServerTest, UseOriginalDstNotSupported) {
  Listener listener = default_server_listener_;
  listener.mutable_use_original_dst()->set_value(true);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("Field \'use_original_dst\' is not supported."));
}

class XdsServerSecurityTest : public XdsEnd2endTest {
 protected:
  XdsServerSecurityTest()
      : XdsEnd2endTest(1, 1, 100, 0, true /* use_xds_enabled_server */) {
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
    balancers_[0]->ads_service()->SetEdsResource(
        BuildEdsResource(args, DefaultEdsServiceName()));
    SetNextResolution({});
    SetNextResolutionForLbChannelAllBalancers();
  }

  ~XdsServerSecurityTest() override {
    g_fake1_cert_data_map = nullptr;
    g_fake2_cert_data_map = nullptr;
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
    SetServerListenerNameAndRouteConfiguration(
        0, listener, backends_[0]->port(), default_server_route_config_);
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

  std::shared_ptr<grpc::Channel> CreateInsecureChannel() {
    ChannelArguments args;
    // Override target name for host name check
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                   ipv6_only_ ? "::1" : "127.0.0.1");
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    std::string uri = absl::StrCat(
        ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", backends_[0]->port());
    return CreateCustomChannel(uri, InsecureChannelCredentials(), args);
  }

  void SendRpc(std::function<std::shared_ptr<grpc::Channel>()> channel_creator,
               std::vector<std::string> expected_server_identity,
               std::vector<std::string> expected_client_identity,
               bool test_expects_failure = false) {
    gpr_log(GPR_INFO, "Sending RPC");
    int num_tries = 0;
    constexpr int kRetryCount = 100;
    for (; num_tries < kRetryCount; num_tries++) {
      auto channel = channel_creator();
      auto stub = grpc::testing::EchoTestService::NewStub(channel);
      ClientContext context;
      context.set_wait_for_ready(true);
      context.set_deadline(grpc_timeout_milliseconds_to_deadline(2000));
      EchoRequest request;
      request.set_message(kRequestMessage);
      EchoResponse response;
      Status status = stub->Echo(&context, request, &response);
      if (test_expects_failure) {
        if (status.ok()) {
          gpr_log(GPR_ERROR, "RPC succeeded. Failure expected. Trying again.");
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

TEST_P(XdsServerSecurityTest, UnknownTransportSocket) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("unknown_transport_socket");
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized transport socket: unknown_transport_socket"));
}

TEST_P(XdsServerSecurityTest, NacksRequireSNI) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  downstream_tls_context.mutable_require_sni()->set_value(true);
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("require_sni: unsupported"));
}

TEST_P(XdsServerSecurityTest, NacksOcspStaplePolicyOtherThanLenientStapling) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  downstream_tls_context.set_ocsp_staple_policy(
      envoy::extensions::transport_sockets::tls::v3::
          DownstreamTlsContext_OcspStaplePolicy_STRICT_STAPLING);
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "ocsp_staple_policy: Only LENIENT_STAPLING supported"));
}

TEST_P(
    XdsServerSecurityTest,
    NacksRequiringClientCertificateWithoutValidationCertificateProviderInstance) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  downstream_tls_context.mutable_require_client_certificate()->set_value(true);
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "TLS configuration requires client certificates but no "
                  "certificate provider instance specified for validation."));
}

TEST_P(XdsServerSecurityTest,
       NacksTlsConfigurationWithoutIdentityProviderInstance) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("TLS configuration provided but no "
                                   "tls_certificate_provider_instance found."));
}

TEST_P(XdsServerSecurityTest, NacksMatchSubjectAltNames) {
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
      ->add_match_subject_alt_names()
      ->set_exact("*.test.google.fr");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(
      response_state.error_message,
      ::testing::HasSubstr("match_subject_alt_names not supported on servers"));
}

TEST_P(XdsServerSecurityTest, UnknownIdentityCertificateProvider) {
  SetLdsUpdate("", "", "unknown", "", false);
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized certificate provider instance name: unknown"));
}

TEST_P(XdsServerSecurityTest, UnknownRootCertificateProvider) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  SetLdsUpdate("unknown", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->lds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr(
                  "Unrecognized certificate provider instance name: unknown"));
}

TEST_P(XdsServerSecurityTest,
       TestDeprecateTlsCertificateCertificateProviderInstanceField) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
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
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, CertificatesNotAvailable) {
  FakeCertificateProvider::CertDataMap fake1_cert_map;
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithRootPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {bad_root_cert_, bad_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin2", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {root_cert_, identity_pair_2_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin2", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothPluginsUpdated) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"good", {root_cert_, identity_pair_2_}},
      {"", {bad_root_cert_, bad_identity_pair_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
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
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"bad", {bad_root_cert_, bad_identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "bad", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithIdentityCertificateNameUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"good", {root_cert_, identity_pair_2_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsWithBothCertificateNamesUpdated) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"good", {root_cert_, identity_pair_2_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("fake_plugin1", "good", "fake_plugin1", "good", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_2_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringButProvidingClientCerts) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestMtlsNotRequiringAndNotProvidingClientCerts) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityPluginUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  FakeCertificateProvider::CertDataMap fake2_cert_map = {
      {"", {root_cert_, identity_pair_2_}}};
  g_fake2_cert_data_map = &fake2_cert_map;
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "fake_plugin2", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsWithIdentityCertificateNameUpdate) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}},
      {"good", {root_cert_, identity_pair_2_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "fake_plugin1", "good", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_2_, {});
}

TEST_P(XdsServerSecurityTest, TestFallback) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestMtlsToTls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
}

TEST_P(XdsServerSecurityTest, TestTlsToMtls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
}

TEST_P(XdsServerSecurityTest, TestMtlsToFallback) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToMtls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
  SetLdsUpdate("fake_plugin1", "", "fake_plugin1", "", true);
  SendRpc([this]() { return CreateMtlsChannel(); },
          server_authenticated_identity_, client_authenticated_identity_);
}

TEST_P(XdsServerSecurityTest, TestTlsToFallback) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
  SetLdsUpdate("", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  SendRpc([this]() { return CreateTlsChannel(); },
          server_authenticated_identity_, {});
  SetLdsUpdate("", "", "", "", false);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerSecurityTest, TestFallbackToTls) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
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
    balancers_[0]->ads_service()->SetLdsResource(listener);
  }

  void UnsetLdsUpdate() {
    balancers_[0]->ads_service()->UnsetResource(
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
  } while (balancers_[0]->ads_service()->lds_response_state().state ==
           AdsServiceImpl::ResponseState::SENT);
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

using XdsServerFilterChainMatchTest = XdsServerSecurityTest;

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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol("tls");
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  // Add another filter chain with no transport protocol set but application
  // protocol set (fails match)
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix("192.168.1.1");
  prefix_range->mutable_prefix_len()->set_value(30);
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  // Add another filter chain with no prefix range mentioned
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add filter chain with the external source type but bad source port.
  // Note that backends_[0]->port() will never be a match for the source port
  // because it is already being used by a backend.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add filter chain with the default source type (ANY) but bad source port.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
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
      ServerHcmAccessor().Unpack(listener));
  // Since we don't know which port will be used by the channel, just add all
  // ports except for 0.
  for (int i = 1; i < 65536; i++) {
    filter_chain->mutable_filter_chain_match()->add_source_ports(i);
  }
  // Add another filter chain with no source port mentioned with a bad
  // DownstreamTlsContext configuration.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_provider_instance()
      ->set_instance_name("fake_plugin1");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  listener.clear_default_filter_chain();
  balancers_[0]->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  // A successful RPC proves that the filter chain with matching source port
  // was chosen.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerFilterChainMatchTest, DuplicateMatchNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  // Add a duplicate filter chain
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(
      balancers_[0]->ads_service()->lds_response_state().error_message,
      ::testing::HasSubstr(
          "Duplicate matching rules detected when adding filter chain: {}"));
}

TEST_P(XdsServerFilterChainMatchTest, DuplicateMatchOnPrefixRangesNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with prefix range
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  auto* prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(16);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(24);
  // Add a filter chain with a duplicate prefix range entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(16);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(32);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  if (ipv6_only_) {
    EXPECT_THAT(
        balancers_[0]->ads_service()->lds_response_state().error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{prefix_ranges={{address_prefix=[::]:0, prefix_len=16}, "
            "{address_prefix=[::]:0, prefix_len=32}}}"));
  } else {
    EXPECT_THAT(
        balancers_[0]->ads_service()->lds_response_state().error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{prefix_ranges={{address_prefix=127.0.0.0:0, prefix_len=16}, "
            "{address_prefix=127.0.0.1:0, prefix_len=32}}}"));
  }
}

TEST_P(XdsServerFilterChainMatchTest, DuplicateMatchOnTransportProtocolNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with "raw_buffer" transport protocol
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  // Add a duplicate filter chain with the same "raw_buffer" transport
  // protocol entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(
      balancers_[0]->ads_service()->lds_response_state().error_message,
      ::testing::HasSubstr("Duplicate matching rules detected when adding "
                           "filter chain: {transport_protocol=raw_buffer}"));
}

TEST_P(XdsServerFilterChainMatchTest, DuplicateMatchOnLocalSourceTypeNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with the local source type
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add a duplicate filter chain with the same local source type entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(
      balancers_[0]->ads_service()->lds_response_state().error_message,
      ::testing::HasSubstr("Duplicate matching rules detected when adding "
                           "filter chain: {source_type=SAME_IP_OR_LOOPBACK}"));
}

TEST_P(XdsServerFilterChainMatchTest,
       DuplicateMatchOnExternalSourceTypeNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with the external source type
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  // Add a duplicate filter chain with the same external source type entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(
      balancers_[0]->ads_service()->lds_response_state().error_message,
      ::testing::HasSubstr("Duplicate matching rules detected when adding "
                           "filter chain: {source_type=EXTERNAL}"));
}

TEST_P(XdsServerFilterChainMatchTest,
       DuplicateMatchOnSourcePrefixRangesNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with source prefix range
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  auto* prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(16);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(24);
  // Add a filter chain with a duplicate source prefix range entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(16);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  prefix_range->set_address_prefix(ipv6_only_ ? "::1" : "127.0.0.1");
  prefix_range->mutable_prefix_len()->set_value(32);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  if (ipv6_only_) {
    EXPECT_THAT(
        balancers_[0]->ads_service()->lds_response_state().error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{source_prefix_ranges={{address_prefix=[::]:0, prefix_len=16}, "
            "{address_prefix=[::]:0, prefix_len=32}}}"));
  } else {
    EXPECT_THAT(
        balancers_[0]->ads_service()->lds_response_state().error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{source_prefix_ranges={{address_prefix=127.0.0.0:0, "
            "prefix_len=16}, "
            "{address_prefix=127.0.0.1:0, prefix_len=32}}}"));
  }
}

TEST_P(XdsServerFilterChainMatchTest, DuplicateMatchOnSourcePortNacked) {
  Listener listener = default_server_listener_;
  // Add filter chain with the external source type
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(8080);
  // Add a duplicate filter chain with the same source port entry
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      ServerHcmAccessor().Unpack(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(8080);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForLdsNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(
      balancers_[0]->ads_service()->lds_response_state().error_message,
      ::testing::HasSubstr("Duplicate matching rules detected when adding "
                           "filter chain: {source_ports={8080}}"));
}

class XdsServerRdsTest : public XdsEnabledServerStatusNotificationTest {
 protected:
  static void SetUpTestSuite() {
    gpr_setenv("GRPC_XDS_EXPERIMENTAL_RBAC", "true");
  }

  static void TearDownTestSuite() {
    gpr_unsetenv("GRPC_XDS_EXPERIMENTAL_RBAC");
  }
};

TEST_P(XdsServerRdsTest, Basic) {
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

TEST_P(XdsServerRdsTest, NacksInvalidDomainPattern) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()->at(0).add_domains("");
  SetServerListenerNameAndRouteConfiguration(
      0, default_server_listener_, backends_[0]->port(), route_config);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForRouteConfigNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(RouteConfigurationResponseState(0).error_message,
              ::testing::HasSubstr("Invalid domain pattern \"\""));
}

TEST_P(XdsServerRdsTest, NacksEmptyDomainsList) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()->at(0).clear_domains();
  SetServerListenerNameAndRouteConfiguration(
      0, default_server_listener_, backends_[0]->port(), route_config);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForRouteConfigNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(RouteConfigurationResponseState(0).error_message,
              ::testing::HasSubstr("VirtualHost has no domains"));
}

TEST_P(XdsServerRdsTest, NacksEmptyRoutesList) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()->at(0).clear_routes();
  SetServerListenerNameAndRouteConfiguration(
      0, default_server_listener_, backends_[0]->port(), route_config);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForRouteConfigNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(RouteConfigurationResponseState(0).error_message,
              ::testing::HasSubstr("No route found in the virtual host"));
}

TEST_P(XdsServerRdsTest, NacksEmptyMatch) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()
      ->at(0)
      .mutable_routes()
      ->at(0)
      .clear_match();
  SetServerListenerNameAndRouteConfiguration(
      0, default_server_listener_, backends_[0]->port(), route_config);
  backends_[0]->Start();
  ASSERT_TRUE(WaitForRouteConfigNack(StatusCode::DEADLINE_EXCEEDED))
      << "timed out waiting for NACK";
  EXPECT_THAT(RouteConfigurationResponseState(0).error_message,
              ::testing::HasSubstr("Match can't be null"));
}

TEST_P(XdsServerRdsTest, FailsRouteMatchesOtherThanNonForwardingAction) {
  SetServerListenerNameAndRouteConfiguration(
      0, default_server_listener_, backends_[0]->port(),
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
  rds->mutable_config_source()->mutable_ads();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
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
  rds->mutable_config_source()->mutable_ads();
  listener.add_filter_chains()->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
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
  rds->mutable_config_source()->mutable_ads();
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
  balancers_[0]->ads_service()->SetRdsResource(new_route_config);
  balancers_[0]->ads_service()->SetRdsResource(another_route_config);
  SetServerListenerNameAndRouteConfiguration(0, listener, backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

using EdsTest = BasicTest;

// Tests that EDS client should send a NACK if the EDS update contains
// sparse priorities.
TEST_P(EdsTest, NacksSparsePriorityList) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(), kDefaultLocalityWeight, 1},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  ASSERT_TRUE(WaitForEdsNack()) << "timed out waiting for NACK";
  const auto response_state =
      balancers_[0]->ads_service()->eds_response_state();
  EXPECT_EQ(response_state.state, AdsServiceImpl::ResponseState::NACKED);
  EXPECT_THAT(response_state.error_message,
              ::testing::HasSubstr("sparse priority list"));
}

// In most of our tests, we use different names for different resource
// types, to make sure that there are no cut-and-paste errors in the code
// that cause us to look at data for the wrong resource type.  So we add
// this test to make sure that the EDS resource name defaults to the
// cluster name if not specified in the CDS resource.
TEST_P(EdsTest, EdsServiceNameDefaultsToClusterName) {
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, kDefaultClusterName));
  Cluster cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->clear_service_name();
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
}

class TimeoutTest : public XdsEnd2endTest {
 protected:
  TimeoutTest()
      : XdsEnd2endTest(/* num_backends= */ 4, /* num_balancers= */ 1,
                       /*client_load_reporting_interval_seconds= */ 100,
                       /* xds_resource_does_not_exist_timeout_ms */ 500,
                       /* use_xds_enabled_server= */ false) {
    StartAllBackends();
  }
};

TEST_P(TimeoutTest, LdsServerIgnoresRequest) {
  balancers_[0]->ads_service()->IgnoreResourceType(kLdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, LdsResourceNotPresentInRequest) {
  balancers_[0]->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, LdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(), TestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  // Create second channel for a new server name.
  // This should fail because there is no LDS resource for this server name.
  auto channel2 =
      CreateChannel(/*failover_timeout=*/0, "new-server.example.com");
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  RpcOptions rpc_options;
  rpc_options.SetupRpc(&context, &request);
  auto status =
      SendRpcMethod(stub2.get(), rpc_options, &context, request, &response);
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
}

TEST_P(TimeoutTest, RdsServerIgnoresRequest) {
  balancers_[0]->ads_service()->IgnoreResourceType(kRdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, RdsResourceNotPresentInRequest) {
  balancers_[0]->ads_service()->UnsetResource(kRdsTypeUrl,
                                              kDefaultRouteConfigurationName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, RdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(), TestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Add listener for 2nd channel, but no RDS resource.
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name("rds_resource_does_not_exist");
  rds->mutable_config_source()->mutable_ads();
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  // Create second channel for a new server name.
  // This should fail because the LDS resource points to a non-existent RDS
  // resource.
  auto channel2 = CreateChannel(/*failover_timeout=*/0, kNewServerName);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  RpcOptions rpc_options;
  rpc_options.SetupRpc(&context, &request);
  auto status =
      SendRpcMethod(stub2.get(), rpc_options, &context, request, &response);
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
}

TEST_P(TimeoutTest, CdsServerIgnoresRequest) {
  balancers_[0]->ads_service()->IgnoreResourceType(kCdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, CdsResourceNotPresentInRequest) {
  balancers_[0]->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, CdsSecondResourceNotPresentInRequest) {
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  // Change route config to point to non-existing cluster.
  const char* kNewClusterName = "new_cluster_name";
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancers_[0]->ads_service()->SetRdsResource(route_config);
  // New cluster times out.
  // May need to wait a bit for the change to propagate to the client.
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  bool error_seen = false;
  do {
    auto status = SendRpc();
    if (status.error_code() == StatusCode::UNAVAILABLE) {
      error_seen = true;
      break;
    }
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
  EXPECT_TRUE(error_seen);
}

TEST_P(TimeoutTest, EdsServerIgnoresRequest) {
  balancers_[0]->ads_service()->IgnoreResourceType(kEdsTypeUrl);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, EdsResourceNotPresentInRequest) {
  // No need to remove EDS resource, since the test suite does not add it
  // by default.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, EdsSecondResourceNotPresentInRequest) {
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(BuildEdsResource(args));
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  WaitForAllBackends();
  // New cluster that points to a non-existant EDS resource.
  const char* kNewClusterName = "new_cluster_name";
  Cluster cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(
      "eds_service_name_does_not_exist");
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Now add a route pointing to the new cluster.
  RouteConfiguration route_config = default_route_config_;
  auto* route = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  *route_config.mutable_virtual_hosts(0)->add_routes() = *route;
  route->mutable_match()->set_path("/grpc.testing.EchoTestService/Echo1");
  route->mutable_route()->set_cluster(kNewClusterName);
  balancers_[0]->ads_service()->SetRdsResource(route_config);
  // New EDS resource times out.
  // May need to wait a bit for the RDS change to propagate to the client.
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  bool error_seen = false;
  do {
    auto status = SendRpc(RpcOptions().set_rpc_method(METHOD_ECHO1));
    if (status.error_code() == StatusCode::UNAVAILABLE) {
      error_seen = true;
      break;
    }
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
  EXPECT_TRUE(error_seen);
}

using LocalityMapTest = BasicTest;

// Tests that the localities in a locality map are picked according to their
// weights.
TEST_P(LocalityMapTest, WeightedRoundRobin) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const int kLocalityWeight0 = 2;
  const int kLocalityWeight1 = 8;
  const int kTotalLocalityWeight = kLocalityWeight0 + kLocalityWeight1;
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kLocalityWeightRate0, kErrorTolerance);
  // ADS response contains 2 localities, each of which contains 1 backend.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kLocalityWeight0},
      {"locality1", CreateEndpointsForBackends(1, 2), kLocalityWeight1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for both backends to be ready.
  WaitForAllBackends(0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::DoubleNear(kLocalityWeightRate0, kErrorTolerance));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::DoubleNear(kLocalityWeightRate1, kErrorTolerance));
}

// Tests that we correctly handle a locality containing no endpoints.
TEST_P(LocalityMapTest, LocalityContainingNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 5000;
  // EDS response contains 2 localities, one with no endpoints.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
      {"locality1", {}},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for both backends to be ready.
  WaitForAllBackends();
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // All traffic should go to the reachable locality.
  EXPECT_EQ(backends_[0]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[1]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[2]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[3]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
}

// EDS update with no localities.
TEST_P(LocalityMapTest, NoLocalities) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource({}, DefaultEdsServiceName()));
  Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
}

// Tests that the locality map can work properly even when it contains a large
// number of localities.
TEST_P(LocalityMapTest, StressTest) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumLocalities = 100;
  const uint32_t kRpcTimeoutMs = 5000;
  // The first ADS response contains kNumLocalities localities, each of which
  // contains backend 0.
  EdsResourceArgs args;
  for (size_t i = 0; i < kNumLocalities; ++i) {
    std::string name = absl::StrCat("locality", i);
    EdsResourceArgs::Locality locality(name, CreateEndpointsForBackends(0, 1));
    args.locality_list.emplace_back(std::move(locality));
  }
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // The second ADS response contains 1 locality, which contains backend 1.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                BuildEdsResource(args, DefaultEdsServiceName()), 60 * 1000));
  // Wait until backend 0 is ready, before which kNumLocalities localities are
  // received and handled by the xds policy.
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // Wait until backend 1 is ready, before which kNumLocalities localities are
  // removed by the xds policy.
  WaitForBackend(1);
  delayed_resource_setter.join();
}

// Tests that the localities in a locality map are picked correctly after
// update (addition, modification, deletion).
TEST_P(LocalityMapTest, UpdateMap) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 3000;
  // The locality weight for the first 3 localities.
  const std::vector<int> kLocalityWeights0 = {2, 3, 4};
  const double kTotalLocalityWeight0 =
      std::accumulate(kLocalityWeights0.begin(), kLocalityWeights0.end(), 0);
  std::vector<double> locality_weight_rate_0;
  locality_weight_rate_0.reserve(kLocalityWeights0.size());
  for (int weight : kLocalityWeights0) {
    locality_weight_rate_0.push_back(weight / kTotalLocalityWeight0);
  }
  // Delete the first locality, keep the second locality, change the third
  // locality's weight from 4 to 2, and add a new locality with weight 6.
  const std::vector<int> kLocalityWeights1 = {3, 2, 6};
  const double kTotalLocalityWeight1 =
      std::accumulate(kLocalityWeights1.begin(), kLocalityWeights1.end(), 0);
  std::vector<double> locality_weight_rate_1 = {
      0 /* placeholder for locality 0 */};
  for (int weight : kLocalityWeights1) {
    locality_weight_rate_1.push_back(weight / kTotalLocalityWeight1);
  }
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), 2},
      {"locality1", CreateEndpointsForBackends(1, 2), 3},
      {"locality2", CreateEndpointsForBackends(2, 3), 4},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for the first 3 backends to be ready.
  WaitForAllBackends(0, 3);
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The picking rates of the first 3 backends should be roughly equal to the
  // expectation.
  std::vector<double> locality_picked_rates;
  for (size_t i = 0; i < 3; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  const double kErrorTolerance = 0.2;
  for (size_t i = 0; i < 3; ++i) {
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_0[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_0[i] * (1 + kErrorTolerance))));
  }
  args = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(1, 2), 3},
      {"locality2", CreateEndpointsForBackends(2, 3), 2},
      {"locality3", CreateEndpointsForBackends(3, 4), 6},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Backend 3 hasn't received any request.
  EXPECT_EQ(0U, backends_[3]->backend_service()->request_count());
  // Wait until the locality update has been processed, as signaled by backend
  // 3 receiving a request.
  WaitForAllBackends(3, 4);
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // Backend 0 no longer receives any request.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  // The picking rates of the last 3 backends should be roughly equal to the
  // expectation.
  locality_picked_rates = {0 /* placeholder for backend 0 */};
  for (size_t i = 1; i < 4; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  for (size_t i = 1; i < 4; ++i) {
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_1[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_1[i] * (1 + kErrorTolerance))));
  }
}

// Tests that we don't fail RPCs when replacing all of the localities in
// a given priority.
TEST_P(LocalityMapTest, ReplaceAllLocalitiesInPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(1, 2)},
  });
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                BuildEdsResource(args, DefaultEdsServiceName()), 5000));
  // Wait for the first backend to be ready.
  WaitForBackend(0);
  // Keep sending RPCs until we switch over to backend 1, which tells us
  // that we received the update.  No RPCs should fail during this
  // transition.
  WaitForBackend(1);
  delayed_resource_setter.join();
}

class FailoverTest : public BasicTest {
 public:
  FailoverTest() { ResetStub(500); }
};

// Localities with the highest priority are used when multiple priority exist.
TEST_P(FailoverTest, ChooseHighestPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForBackend(3, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose priority with no endpoints.
TEST_P(FailoverTest, DoesNotUsePriorityWithNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", {}, kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 1; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose locality with no endpoints.
TEST_P(FailoverTest, DoesNotUseLocalityWithNoEndpoints) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", {}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(), kDefaultLocalityWeight, 0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for all backends to be used.
  std::tuple<int, int, int> counts = WaitForAllBackends();
  // Make sure no RPCs failed in the transition.
  EXPECT_EQ(0, std::get<1>(counts));
}

// If the higher priority localities are not reachable, failover to the
// highest priority among the rest.
TEST_P(FailoverTest, Failover) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  ShutdownBackend(3);
  ShutdownBackend(0);
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForBackend(1, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// If a locality with higher priority than the current one becomes ready,
// switch to it.
TEST_P(FailoverTest, SwitchBackToHigherPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForBackend(3);
  ShutdownBackend(3);
  ShutdownBackend(0);
  WaitForBackend(
      1, WaitForBackendOptions().set_reset_counters(false).set_allow_failures(
             true));
  for (size_t i = 0; i < 4; ++i) {
    if (i == 1) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  StartBackend(0);
  WaitForBackend(0);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[0]->backend_service()->request_count());
}

// The first update only contains unavailable priorities. The second update
// contains available priorities.
TEST_P(FailoverTest, UpdateInitialUnavailable) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       2},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       3},
  });
  ShutdownBackend(0);
  ShutdownBackend(1);
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                BuildEdsResource(args, DefaultEdsServiceName()), 1000));
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                       gpr_time_from_millis(500, GPR_TIMESPAN));
  // Send 0.5 second worth of RPCs.
  do {
    CheckRpcSendFailure();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  WaitForBackend(
      2, WaitForBackendOptions().set_reset_counters(false).set_allow_failures(
             true));
  for (size_t i = 0; i < 4; ++i) {
    if (i == 2) continue;
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  delayed_resource_setter.join();
}

// Tests that after the localities' priorities are updated, we still choose
// the highest READY priority with the updated localities.
TEST_P(FailoverTest, UpdatePriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       2},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       0},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       3},
  });
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                BuildEdsResource(args, DefaultEdsServiceName()), 1000));
  WaitForBackend(3, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
  WaitForBackend(1);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[1]->backend_service()->request_count());
  delayed_resource_setter.join();
}

// Moves all localities in the current priority to a higher priority.
TEST_P(FailoverTest, MoveAllLocalitiesInCurrentPriorityToHigherPriority) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // First update:
  // - Priority 0 is locality 0, containing backend 0, which is down.
  // - Priority 1 is locality 1, containing backends 1 and 2, which are up.
  ShutdownBackend(0);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 3), kDefaultLocalityWeight,
       1},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Second update:
  // - Priority 0 contains both localities 0 and 1.
  // - Priority 1 is not present.
  // - We add backend 3 to locality 1, just so we have a way to know
  //   when the update has been seen by the client.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 4), kDefaultLocalityWeight,
       0},
  });
  std::thread delayed_resource_setter(
      std::bind(&BasicTest::SetEdsResourceWithDelay, this, 0,
                BuildEdsResource(args, DefaultEdsServiceName()), 1000));
  // When we get the first update, all backends in priority 0 are down,
  // so we will create priority 1.  Backends 1 and 2 should have traffic,
  // but backend 3 should not.
  WaitForAllBackends(1, 3, WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0UL, backends_[3]->backend_service()->request_count());
  // When backend 3 gets traffic, we know the second update has been seen.
  WaitForBackend(3);
  // The ADS service of balancer 0 got at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  delayed_resource_setter.join();
}

using DropTest = BasicTest;

// Tests that RPCs are dropped according to the drop config.
TEST_P(DropTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The ADS response contains two drop categories.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that drop config is converted correctly from per hundred.
TEST_P(DropTest, DropPerHundred) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const uint32_t kDropPerHundredForLb = 10;
  const double kDropRateForLb = kDropPerHundredForLb / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  args.drop_categories = {{kLbDropType, kDropPerHundredForLb}};
  args.drop_denominator = FractionalPercent::HUNDRED;
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop config is converted correctly from per ten thousand.
TEST_P(DropTest, DropPerTenThousand) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const uint32_t kDropPerTenThousandForLb = 1000;
  const double kDropRateForLb = kDropPerTenThousandForLb / 10000.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  args.drop_categories = {{kLbDropType, kDropPerTenThousandForLb}};
  args.drop_denominator = FractionalPercent::TEN_THOUSAND;
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop is working correctly after update.
TEST_P(DropTest, Update) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kErrorTolerance = 0.05;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const size_t kNumRpcsLbOnly =
      ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  const size_t kNumRpcsBoth =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The first ADS response contains one drop category.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb}};
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
  // Send kNumRpcsLbOnly RPCs and count the drops.
  size_t num_drops = 0;
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  for (size_t i = 0; i < kNumRpcsLbOnly; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The drop rate should be roughly equal to the expectation.
  double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcsLbOnly;
  gpr_log(GPR_INFO, "First batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
  // The second ADS response contains two drop categories, send an update EDS
  // response.
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until the drop rate increases to the middle of the two configs,
  // which implies that the update has been in effect.
  const double kDropRateThreshold =
      (kDropRateForLb + kDropRateForLbAndThrottle) / 2;
  size_t num_rpcs = kNumRpcsBoth;
  while (seen_drop_rate < kDropRateThreshold) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    ++num_rpcs;
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
    seen_drop_rate = static_cast<double>(num_drops) / num_rpcs;
  }
  // Send kNumRpcsBoth RPCs and count the drops.
  num_drops = 0;
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  for (size_t i = 0; i < kNumRpcsBoth; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // The new drop rate should be roughly equal to the expectation.
  seen_drop_rate = static_cast<double>(num_drops) / kNumRpcsBoth;
  gpr_log(GPR_INFO, "Second batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that all the RPCs are dropped if any drop category drops 100%.
TEST_P(DropTest, DropAll) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 1000000;
  // The ADS response contains two drop categories.
  EdsResourceArgs args;
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Send kNumRpcs RPCs and all of them are dropped.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
    EXPECT_THAT(status.error_message(),
                ::testing::StartsWith("EDS-configured drop: "));
  }
}

class BalancerUpdateTest : public XdsEnd2endTest {
 public:
  BalancerUpdateTest() : XdsEnd2endTest(4, 3) { StartAllBackends(); }
};

// Tests that the old LB call is still used after the balancer address update
// as long as that call is still alive.
TEST_P(BalancerUpdateTest, UpdateBalancersButKeepUsingOriginalBalancer) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancers_[1]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[1]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel({balancers_[1]->port()});
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // The current LB call is still working, so xds continued using it to the
  // first balancer, which doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[1]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
}

// Tests that the old LB call is still used after multiple balancer address
// updates as long as that call is still alive. Send an update with the same
// set of LBs as the one in SetUp() in order to verify that the LB channel
// inside xds keeps the initial connection (which by definition is also
// present in the update).
TEST_P(BalancerUpdateTest, Repeated) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancers_[1]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until the first backend is ready.
  WaitForBackend(0);
  // Send 10 requests.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[1]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
  std::vector<int> ports;
  ports.emplace_back(balancers_[0]->port());
  ports.emplace_back(balancers_[1]->port());
  ports.emplace_back(balancers_[2]->port());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel(ports);
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // xds continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  ports.clear();
  ports.emplace_back(balancers_[0]->port());
  ports.emplace_back(balancers_[1]->port());
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 2 ==========");
  SetNextResolutionForLbChannel(ports);
  gpr_log(GPR_INFO, "========= UPDATE 2 DONE ==========");
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                          gpr_time_from_millis(10000, GPR_TIMESPAN));
  // Send 10 seconds worth of RPCs
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0);
  // xds continued using the original LB call to the first balancer, which
  // doesn't assign the second backend.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
}

// Tests that if the balancer is down, the RPCs will still be sent to the
// backends according to the last balancer response, until a new balancer is
// reachable.
TEST_P(BalancerUpdateTest, DeadUpdate) {
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancers_[1]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Start servers and send 10 RPCs per server.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // All 10 requests should have gone to the first backend.
  EXPECT_EQ(10U, backends_[0]->backend_service()->request_count());
  // The ADS service of balancer 0 sent at least 1 response.
  EXPECT_GT(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[1]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
  // Kill balancer 0
  gpr_log(GPR_INFO, "********** ABOUT TO KILL BALANCER 0 *************");
  balancers_[0]->Shutdown();
  gpr_log(GPR_INFO, "********** KILLED BALANCER 0 *************");
  // This is serviced by the existing child policy.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // All 10 requests should again have gone to the first backend.
  EXPECT_EQ(20U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // The ADS service of no balancers sent anything
  EXPECT_EQ(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[0]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[1]->ads_service()->eds_response_state().error_message;
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
  gpr_log(GPR_INFO, "========= ABOUT TO UPDATE 1 ==========");
  SetNextResolutionForLbChannel({balancers_[1]->port()});
  gpr_log(GPR_INFO, "========= UPDATE 1 DONE ==========");
  // Wait until update has been processed, as signaled by the second backend
  // receiving a request. In the meantime, the client continues to be serviced
  // (by the first backend) without interruption.
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  WaitForBackend(1);
  // This is serviced by the updated RR policy
  backends_[1]->backend_service()->ResetCounters();
  gpr_log(GPR_INFO, "========= BEFORE THIRD BATCH ==========");
  CheckRpcSendOk(10);
  gpr_log(GPR_INFO, "========= DONE WITH THIRD BATCH ==========");
  // All 10 requests should have gone to the second backend.
  EXPECT_EQ(10U, backends_[1]->backend_service()->request_count());
  // The ADS service of balancer 1 sent at least 1 response.
  EXPECT_EQ(balancers_[0]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[0]->ads_service()->eds_response_state().error_message;
  EXPECT_GT(balancers_[1]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT);
  EXPECT_EQ(balancers_[2]->ads_service()->eds_response_state().state,
            AdsServiceImpl::ResponseState::NOT_SENT)
      << "Error Message:"
      << balancers_[2]->ads_service()->eds_response_state().error_message;
}

class ClientLoadReportingTest : public XdsEnd2endTest {
 public:
  ClientLoadReportingTest() : XdsEnd2endTest(4, 1, 3) { StartAllBackends(); }
};

// Tests that the load report received at the balancer is correct.
TEST_P(ClientLoadReportingTest, Vanilla) {
  if (GetParam().use_fake_resolver()) {
    balancers_[0]->lrs_service()->set_cluster_names({kServerName});
  }
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  // TODO(juanlishen): Partition the backends after multiple localities is
  // tested.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  CheckRpcSendFailure(CheckRpcSendFailureOptions()
                          .set_times(kNumFailuresPerAddress * num_backends_)
                          .set_rpc_options(RpcOptions().set_server_fail(true)));
  // Check that each backend got the right number of requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress + kNumFailuresPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ((kNumRpcsPerAddress + kNumFailuresPerAddress) * num_backends_ +
                num_ok + num_failure,
            client_stats.total_issued_requests());
  EXPECT_EQ(kNumFailuresPerAddress * num_backends_ + num_failure,
            client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
}

// Tests send_all_clusters.
TEST_P(ClientLoadReportingTest, SendAllClusters) {
  balancers_[0]->lrs_service()->set_send_all_clusters(true);
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  // TODO(juanlishen): Partition the backends after multiple localities is
  // tested.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  CheckRpcSendFailure(CheckRpcSendFailureOptions()
                          .set_times(kNumFailuresPerAddress * num_backends_)
                          .set_rpc_options(RpcOptions().set_server_fail(true)));
  // Check that each backend got the right number of requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress + kNumFailuresPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(kNumRpcsPerAddress * num_backends_ + num_ok,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ((kNumRpcsPerAddress + kNumFailuresPerAddress) * num_backends_ +
                num_ok + num_failure,
            client_stats.total_issued_requests());
  EXPECT_EQ(kNumFailuresPerAddress * num_backends_ + num_failure,
            client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
}

// Tests that we don't include stats for clusters that are not requested
// by the LRS server.
TEST_P(ClientLoadReportingTest, HonorsClustersRequestedByLrsServer) {
  balancers_[0]->lrs_service()->set_cluster_names({"bogus"});
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until all backends are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * num_backends_);
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->request_count());
  EXPECT_EQ(1U, balancers_[0]->lrs_service()->response_count());
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 0UL);
}

// Tests that if the balancer restarts, the client load report contains the
// stats before and after the restart correctly.
TEST_P(ClientLoadReportingTest, BalancerRestart) {
  if (GetParam().use_fake_resolver()) {
    balancers_[0]->lrs_service()->set_cluster_names({kServerName});
  }
  SetNextResolution({});
  SetNextResolutionForLbChannel({balancers_[0]->port()});
  const size_t kNumBackendsFirstPass = backends_.size() / 2;
  const size_t kNumBackendsSecondPass =
      backends_.size() - kNumBackendsFirstPass;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait until all backends returned by the balancer are ready.
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ 0,
                         /* stop_index */ kNumBackendsFirstPass);
  std::vector<ClientStats> load_report =
      balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats client_stats = std::move(load_report.front());
  EXPECT_EQ(static_cast<size_t>(num_ok),
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // Shut down the balancer.
  balancers_[0]->Shutdown();
  // We should continue using the last EDS response we received from the
  // balancer before it was shut down.
  // Note: We need to use WaitForAllBackends() here instead of just
  // CheckRpcSendOk(kNumBackendsFirstPass), because when the balancer
  // shuts down, the XdsClient will generate an error to the
  // ServiceConfigWatcher, which will cause the xds resolver to send a
  // no-op update to the LB policy.  When this update gets down to the
  // round_robin child policy for the locality, it will generate a new
  // subchannel list, which resets the start index randomly.  So we need
  // to be a little more permissive here to avoid spurious failures.
  ResetBackendCounters();
  int num_started = std::get<0>(WaitForAllBackends(
      /* start_index */ 0, /* stop_index */ kNumBackendsFirstPass));
  // Now restart the balancer, this time pointing to the new backends.
  balancers_[0]->Start();
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(kNumBackendsFirstPass)},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  std::tie(num_ok, num_failure, num_drops) =
      WaitForAllBackends(/* start_index */ kNumBackendsFirstPass);
  num_started += num_ok + num_failure + num_drops;
  // Send one RPC per backend.
  CheckRpcSendOk(kNumBackendsSecondPass);
  num_started += kNumBackendsSecondPass;
  // Check client stats.
  load_report = balancers_[0]->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  client_stats = std::move(load_report.front());
  EXPECT_EQ(num_started, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
}

class ClientLoadReportingWithDropTest : public XdsEnd2endTest {
 public:
  ClientLoadReportingWithDropTest() : XdsEnd2endTest(4, 1, 20) {
    StartAllBackends();
  }
};

// Tests that the drop stats are correctly reported by client load reporting.
TEST_P(ClientLoadReportingWithDropTest, Vanilla) {
  if (GetParam().use_fake_resolver()) {
    balancers_[0]->lrs_service()->set_cluster_names({kServerName});
  }
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kErrorTolerance = 0.05;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The ADS response contains two drop categories.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  int num_ok = 0;
  int num_failure = 0;
  int num_drops = 0;
  std::tie(num_ok, num_failure, num_drops) = WaitForAllBackends();
  const size_t num_warmup = num_ok + num_failure + num_drops;
  // Send kNumRpcs RPCs and count the drops.
  for (size_t i = 0; i < kNumRpcs; ++i) {
    EchoResponse response;
    const Status status = SendRpc(RpcOptions(), &response);
    if (!status.ok() &&
        absl::StartsWith(status.error_message(), "EDS-configured drop: ")) {
      ++num_drops;
    } else {
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
      EXPECT_EQ(response.message(), kRequestMessage);
    }
  }
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
  // Check client stats.
  const size_t total_rpc = num_warmup + kNumRpcs;
  ClientStats client_stats;
  do {
    std::vector<ClientStats> load_reports =
        balancers_[0]->lrs_service()->WaitForLoadReport();
    for (const auto& load_report : load_reports) {
      client_stats += load_report;
    }
  } while (client_stats.total_issued_requests() +
               client_stats.total_dropped_requests() <
           total_rpc);
  EXPECT_EQ(num_drops, client_stats.total_dropped_requests());
  EXPECT_THAT(static_cast<double>(client_stats.dropped_requests(kLbDropType)) /
                  total_rpc,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(client_stats.dropped_requests(kThrottleDropType)) /
          (total_rpc * (1 - kDropRateForLb)),
      ::testing::DoubleNear(kDropRateForThrottle, kErrorTolerance));
}

class FaultInjectionTest : public XdsEnd2endTest {
 public:
  FaultInjectionTest() : XdsEnd2endTest(1, 1) { StartAllBackends(); }

  // Builds a Listener with Fault Injection filter config. If the http_fault
  // is nullptr, then assign an empty filter config. This filter config is
  // required to enable the fault injection features.
  static Listener BuildListenerWithFaultInjection(
      const HTTPFault& http_fault = HTTPFault()) {
    HttpConnectionManager http_connection_manager;
    Listener listener;
    listener.set_name(kServerName);
    HttpFilter* fault_filter = http_connection_manager.add_http_filters();
    fault_filter->set_name("envoy.fault");
    fault_filter->mutable_typed_config()->PackFrom(http_fault);
    HttpFilter* router_filter = http_connection_manager.add_http_filters();
    router_filter->set_name("router");
    router_filter->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());
    listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    return listener;
  }

  RouteConfiguration BuildRouteConfigurationWithFaultInjection(
      const HTTPFault& http_fault) {
    // Package as Any
    google::protobuf::Any filter_config;
    filter_config.PackFrom(http_fault);
    // Plug into the RouteConfiguration
    RouteConfiguration new_route_config = default_route_config_;
    auto* config_map = new_route_config.mutable_virtual_hosts(0)
                           ->mutable_routes(0)
                           ->mutable_typed_per_filter_config();
    (*config_map)["envoy.fault"] = std::move(filter_config);
    return new_route_config;
  }

  void SetFilterConfig(HTTPFault& http_fault) {
    switch (GetParam().filter_config_setup()) {
      case TestType::FilterConfigSetup::kRouteOverride: {
        Listener listener = BuildListenerWithFaultInjection();
        RouteConfiguration route =
            BuildRouteConfigurationWithFaultInjection(http_fault);
        SetListenerAndRouteConfiguration(0, listener, route);
        break;
      }
      case TestType::FilterConfigSetup::kHTTPConnectionManagerOriginal: {
        Listener listener = BuildListenerWithFaultInjection(http_fault);
        SetListenerAndRouteConfiguration(0, listener, default_route_config_);
      }
    };
  }
};

// Test to ensure the most basic fault injection config works.
TEST_P(FaultInjectionTest, XdsFaultInjectionAlwaysAbort) {
  const uint32_t kAbortPercentagePerHundred = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Fire several RPCs, and expect all of them to be aborted.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_times(5)
          .set_rpc_options(RpcOptions().set_wait_for_ready(true))
          .set_expected_error_code(StatusCode::ABORTED));
}

// Without the listener config, the fault injection won't be enabled.
TEST_P(FaultInjectionTest, XdsFaultInjectionWithoutListenerFilter) {
  const uint32_t kAbortPercentagePerHundred = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  // Turn on fault injection
  RouteConfiguration route =
      BuildRouteConfigurationWithFaultInjection(http_fault);
  SetListenerAndRouteConfiguration(0, default_listener_, route);
  // Fire several RPCs, and expect all of them to be pass.
  CheckRpcSendOk(5, RpcOptions().set_wait_for_ready(true));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbort) {
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the aborts.
  int num_total = 0, num_ok = 0, num_failure = 0, num_aborted = 0;
  for (size_t i = 0; i < kNumRpcs; ++i) {
    SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_aborted,
                    RpcOptions(), "Fault injected");
  }
  EXPECT_EQ(kNumRpcs, num_total);
  EXPECT_EQ(0, num_failure);
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbortViaHeaders) {
  const uint32_t kAbortPercentageCap = 100;
  const uint32_t kAbortPercentage = 50;
  const double kAbortRate = kAbortPercentage / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  http_fault.mutable_abort()->mutable_header_abort();
  http_fault.mutable_abort()->mutable_percentage()->set_numerator(
      kAbortPercentageCap);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the aborts.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"x-envoy-fault-abort-grpc-request", "10"},
      {"x-envoy-fault-abort-percentage", std::to_string(kAbortPercentage)},
  };
  int num_total = 0, num_ok = 0, num_failure = 0, num_aborted = 0;
  RpcOptions options = RpcOptions().set_metadata(metadata);
  for (size_t i = 0; i < kNumRpcs; ++i) {
    SendRpcAndCount(&num_total, &num_ok, &num_failure, &num_aborted, options,
                    "Fault injected");
  }
  EXPECT_EQ(kNumRpcs, num_total);
  EXPECT_EQ(0, num_failure);
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageDelay) {
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 3000;
  const uint32_t kFixedDelaySeconds = 100;
  const uint32_t kDelayPercentagePerHundred = 50;
  const double kDelayRate = kDelayPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the delays.
  RpcOptions rpc_options = RpcOptions()
                               .set_timeout_ms(kRpcTimeoutMilliseconds)
                               .set_skip_cancelled_check(true);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(stub_.get(), kNumRpcs, rpc_options);
  size_t num_delayed = 0;
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // The delay rate should be roughly equal to the expectation.
  const double seen_delay_rate = static_cast<double>(num_delayed) / kNumRpcs;
  EXPECT_THAT(seen_delay_rate,
              ::testing::DoubleNear(kDelayRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageDelayViaHeaders) {
  const uint32_t kFixedDelayMilliseconds = 100000;
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 3000;
  const uint32_t kDelayPercentageCap = 100;
  const uint32_t kDelayPercentage = 50;
  const double kDelayRate = kDelayPercentage / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  http_fault.mutable_delay()->mutable_header_delay();
  http_fault.mutable_delay()->mutable_percentage()->set_numerator(
      kDelayPercentageCap);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the delays.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"x-envoy-fault-delay-request", std::to_string(kFixedDelayMilliseconds)},
      {"x-envoy-fault-delay-request-percentage",
       std::to_string(kDelayPercentage)},
  };
  RpcOptions rpc_options = RpcOptions()
                               .set_metadata(metadata)
                               .set_timeout_ms(kRpcTimeoutMilliseconds)
                               .set_skip_cancelled_check(true);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(stub_.get(), kNumRpcs, rpc_options);
  size_t num_delayed = 0;
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // The delay rate should be roughly equal to the expectation.
  const double seen_delay_rate = static_cast<double>(num_delayed) / kNumRpcs;
  EXPECT_THAT(seen_delay_rate,
              ::testing::DoubleNear(kDelayRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionAbortAfterDelayForStreamCall) {
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(100);  // Always inject ABORT!
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(100);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send a stream RPC and check its status code
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeoutMilliseconds));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_EQ(StatusCode::ABORTED, status.error_code())
      << status.error_message() << ", " << status.error_details() << ", "
      << context.debug_error_string();
}

TEST_P(FaultInjectionTest, XdsFaultInjectionAlwaysDelayPercentageAbort) {
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  const uint32_t kConnectionTimeoutMilliseconds =
      10 * 1000;  // 10s should not reach
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(1000000);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::MILLION);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Allow the channel to connect to one backends, so the herd of queued RPCs
  // won't be executed on the same ExecCtx object and using the cached Now()
  // value, which causes millisecond level delay error.
  channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds));
  // Send kNumRpcs RPCs and count the aborts.
  int num_aborted = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout_ms(kRpcTimeoutMilliseconds);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    EXPECT_GE(rpc.elapsed_time, kFixedDelaySeconds * 1000);
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ("Fault injected", rpc.status.error_message());
    ++num_aborted;
  }
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

// This test and the above test apply different denominators to delay and
// abort. This ensures that we are using the right denominator for each
// injected fault in our code.
TEST_P(FaultInjectionTest,
       XdsFaultInjectionAlwaysDelayPercentageAbortSwitchDenominator) {
  const uint32_t kAbortPercentagePerMillion = 500000;
  const double kAbortRate = kAbortPercentagePerMillion / 1000000.0;
  const uint32_t kFixedDelaySeconds = 1;                // 1s
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  const uint32_t kConnectionTimeoutMilliseconds =
      10 * 1000;  // 10s should not reach
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerMillion);
  abort_percentage->set_denominator(FractionalPercent::MILLION);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(100);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Allow the channel to connect to one backends, so the herd of queued RPCs
  // won't be executed on the same ExecCtx object and using the cached Now()
  // value, which causes millisecond level delay error.
  channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds));
  // Send kNumRpcs RPCs and count the aborts.
  int num_aborted = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout_ms(kRpcTimeoutMilliseconds);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    EXPECT_GE(rpc.elapsed_time, kFixedDelaySeconds * 1000);
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ("Fault injected", rpc.status.error_message());
    ++num_aborted;
  }
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionMaxFault) {
  const uint32_t kMaxFault = 10;
  const uint32_t kNumRpcs = 30;  // kNumRpcs should be bigger than kMaxFault
  const uint32_t kRpcTimeoutMs = 4000;     // 4 seconds
  const uint32_t kLongDelaySeconds = 100;  // 100 seconds
  const uint32_t kAlwaysDelayPercentage = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(
      kAlwaysDelayPercentage);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kLongDelaySeconds);
  http_fault.mutable_max_active_faults()->set_value(kMaxFault);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Sends a batch of long running RPCs with long timeout to consume all
  // active faults quota.
  int num_delayed = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout_ms(kRpcTimeoutMs);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // Only kMaxFault number of RPC should be fault injected..
  EXPECT_EQ(kMaxFault, num_delayed);
}

TEST_P(FaultInjectionTest, XdsFaultInjectionBidiStreamDelayOk) {
  // kRpcTimeoutMilliseconds is 10s should never be reached.
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 10000;
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kDelayPercentagePerHundred = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeoutMilliseconds));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message() << ", "
                           << status.error_details() << ", "
                           << context.debug_error_string();
}

// This case catches a bug in the retry code that was triggered by a bad
// interaction with the FI code.  See https://github.com/grpc/grpc/pull/27217
// for description.
TEST_P(FaultInjectionTest, XdsFaultInjectionBidiStreamDelayError) {
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 500;
  const uint32_t kFixedDelaySeconds = 100;
  const uint32_t kDelayPercentagePerHundred = 100;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create an EDS resource
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  auto* fixed_delay = http_fault.mutable_delay()->mutable_fixed_delay();
  fixed_delay->set_seconds(kFixedDelaySeconds);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeoutMilliseconds));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, status.error_code())
      << status.error_message() << ", " << status.error_details() << ", "
      << context.debug_error_string();
}

class BootstrapSourceTest : public XdsEnd2endTest {
 public:
  BootstrapSourceTest() : XdsEnd2endTest(4, 1) { StartAllBackends(); }
};

TEST_P(BootstrapSourceTest, Vanilla) {
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  WaitForAllBackends();
}

#ifndef DISABLED_XDS_PROTO_IN_CC
class ClientStatusDiscoveryServiceTest : public XdsEnd2endTest {
 public:
  explicit ClientStatusDiscoveryServiceTest(
      int xds_resource_does_not_exist_timeout_ms = 0)
      : XdsEnd2endTest(1, 1, 100, xds_resource_does_not_exist_timeout_ms) {
    StartAllBackends();
    admin_server_thread_ = absl::make_unique<AdminServerThread>(this);
    admin_server_thread_->Start();
    std::string admin_server_address = absl::StrCat(
        ipv6_only_ ? "[::1]:" : "127.0.0.1:", admin_server_thread_->port());
    admin_channel_ = grpc::CreateChannel(
        admin_server_address,
        std::make_shared<SecureChannelCredentials>(
            grpc_fake_transport_security_credentials_create()));
    csds_stub_ =
        envoy::service::status::v3::ClientStatusDiscoveryService::NewStub(
            admin_channel_);
    if (GetParam().use_csds_streaming()) {
      stream_ = csds_stub_->StreamClientStatus(&stream_context_);
    }
  }

  ~ClientStatusDiscoveryServiceTest() override {
    if (stream_ != nullptr) {
      EXPECT_TRUE(stream_->WritesDone());
      Status status = stream_->Finish();
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
    }
    admin_server_thread_->Shutdown();
  }

  envoy::service::status::v3::ClientStatusResponse FetchCsdsResponse() {
    envoy::service::status::v3::ClientStatusResponse response;
    if (!GetParam().use_csds_streaming()) {
      // Fetch through unary pulls
      ClientContext context;
      Status status = csds_stub_->FetchClientStatus(
          &context, envoy::service::status::v3::ClientStatusRequest(),
          &response);
      EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                               << " message=" << status.error_message();
    } else {
      // Fetch through streaming pulls
      EXPECT_TRUE(
          stream_->Write(envoy::service::status::v3::ClientStatusRequest()));
      EXPECT_TRUE(stream_->Read(&response));
    }
    return response;
  }

 private:
  std::unique_ptr<AdminServerThread> admin_server_thread_;
  std::shared_ptr<Channel> admin_channel_;
  std::unique_ptr<
      envoy::service::status::v3::ClientStatusDiscoveryService::Stub>
      csds_stub_;
  ClientContext stream_context_;
  std::unique_ptr<
      ClientReaderWriter<envoy::service::status::v3::ClientStatusRequest,
                         envoy::service::status::v3::ClientStatusResponse>>
      stream_;
};

MATCHER_P4(EqNode, id, user_agent_name, user_agent_version, client_features,
           "equals Node") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(id, arg.id(), result_listener);
  ok &= ::testing::ExplainMatchResult(user_agent_name, arg.user_agent_name(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(
      user_agent_version, arg.user_agent_version(), result_listener);
  ok &= ::testing::ExplainMatchResult(client_features, arg.client_features(),
                                      result_listener);
  return ok;
}

MATCHER_P6(EqGenericXdsConfig, type_url, name, version_info, xds_config,
           client_status, error_state, "equals GenericXdsConfig") {
  bool ok = true;
  ok &=
      ::testing::ExplainMatchResult(type_url, arg.type_url(), result_listener);
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  ok &= ::testing::ExplainMatchResult(version_info, arg.version_info(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(xds_config, arg.xds_config(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(client_status, arg.client_status(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(error_state, arg.error_state(),
                                      result_listener);
  return ok;
}

MATCHER_P2(EqListener, name, api_listener, "equals Listener") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  ok &= ::testing::ExplainMatchResult(
      api_listener, arg.api_listener().api_listener(), result_listener);
  return ok;
}

MATCHER_P(EqHttpConnectionManagerNotRds, route_config,
          "equals HttpConnectionManager") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(route_config, arg.route_config(),
                                      result_listener);
  return ok;
}

MATCHER_P(EqRouteConfigurationName, name, "equals RouteConfiguration") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  return ok;
}

MATCHER_P2(EqRouteConfiguration, name, cluster_name,
           "equals RouteConfiguration") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  ok &= ::testing::ExplainMatchResult(
      ::testing::ElementsAre(::testing::Property(
          &envoy::config::route::v3::VirtualHost::routes,
          ::testing::ElementsAre(::testing::Property(
              &envoy::config::route::v3::Route::route,
              ::testing::Property(
                  &envoy::config::route::v3::RouteAction::cluster,
                  cluster_name))))),
      arg.virtual_hosts(), result_listener);
  return ok;
}

MATCHER_P(EqCluster, name, "equals Cluster") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(name, arg.name(), result_listener);
  return ok;
}

MATCHER_P(EqEndpoint, port, "equals Endpoint") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(
      port, arg.address().socket_address().port_value(), result_listener);
  return ok;
}

MATCHER_P2(EqLocalityLbEndpoints, port, weight, "equals LocalityLbEndpoints") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(
      ::testing::ElementsAre(::testing::Property(
          &envoy::config::endpoint::v3::LbEndpoint::endpoint,
          EqEndpoint(port))),
      arg.lb_endpoints(), result_listener);
  ok &= ::testing::ExplainMatchResult(
      weight, arg.load_balancing_weight().value(), result_listener);
  return ok;
}

MATCHER_P(EqClusterLoadAssignmentName, cluster_name,
          "equals ClusterLoadAssignment") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(cluster_name, arg.cluster_name(),
                                      result_listener);
  return ok;
}

MATCHER_P3(EqClusterLoadAssignment, cluster_name, port, weight,
           "equals ClusterLoadAssignment") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(cluster_name, arg.cluster_name(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(
      ::testing::ElementsAre(EqLocalityLbEndpoints(port, weight)),
      arg.endpoints(), result_listener);
  return ok;
}

MATCHER_P2(EqUpdateFailureState, details, version_info,
           "equals UpdateFailureState") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(details, arg.details(), result_listener);
  ok &= ::testing::ExplainMatchResult(version_info, arg.version_info(),
                                      result_listener);
  return ok;
}

MATCHER_P(UnpackListener, matcher, "is a Listener") {
  Listener config;
  if (!::testing::ExplainMatchResult(true, arg.UnpackTo(&config),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, config, result_listener);
}

MATCHER_P(UnpackRouteConfiguration, matcher, "is a RouteConfiguration") {
  RouteConfiguration config;
  if (!::testing::ExplainMatchResult(true, arg.UnpackTo(&config),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, config, result_listener);
}

MATCHER_P(UnpackHttpConnectionManager, matcher, "is a HttpConnectionManager") {
  HttpConnectionManager config;
  if (!::testing::ExplainMatchResult(true, arg.UnpackTo(&config),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, config, result_listener);
}

MATCHER_P(UnpackCluster, matcher, "is a Cluster") {
  Cluster config;
  if (!::testing::ExplainMatchResult(true, arg.UnpackTo(&config),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, config, result_listener);
}

MATCHER_P(UnpackClusterLoadAssignment, matcher, "is a ClusterLoadAssignment") {
  ClusterLoadAssignment config;
  if (!::testing::ExplainMatchResult(true, arg.UnpackTo(&config),
                                     result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, config, result_listener);
}

MATCHER(IsRdsEnabledHCM, "is a RDS enabled HttpConnectionManager") {
  return ::testing::ExplainMatchResult(
      UnpackHttpConnectionManager(
          ::testing::Property(&HttpConnectionManager::has_rds, true)),
      arg, result_listener);
}

MATCHER_P2(EqNoRdsHCM, route_configuration_name, cluster_name,
           "equals RDS disabled HttpConnectionManager") {
  return ::testing::ExplainMatchResult(
      UnpackHttpConnectionManager(EqHttpConnectionManagerNotRds(
          EqRouteConfiguration(route_configuration_name, cluster_name))),
      arg, result_listener);
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpVanilla) {
  const size_t kNumRpcs = 5;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Send several RPCs to ensure the xDS setup works
  CheckRpcSendOk(kNumRpcs);
  // Fetches the client config
  auto csds_response = FetchCsdsResponse();
  gpr_log(GPR_INFO, "xDS config dump: %s", csds_response.DebugString().c_str());
  EXPECT_EQ(1, csds_response.config_size());
  const auto& client_config = csds_response.config(0);
  // Validate the Node information
  EXPECT_THAT(client_config.node(),
              EqNode("xds_end2end_test", ::testing::HasSubstr("C-core"),
                     ::testing::HasSubstr(grpc_version_string()),
                     ::testing::ElementsAre(
                         "envoy.lb.does_not_support_overprovisioning")));
  // Listener matcher depends on whether RDS is enabled.
  ::testing::Matcher<google::protobuf::Any> api_listener_matcher;
  if (GetParam().enable_rds_testing()) {
    api_listener_matcher = IsRdsEnabledHCM();
  } else {
    api_listener_matcher =
        EqNoRdsHCM(kDefaultRouteConfigurationName, kDefaultClusterName);
  }
  // Construct list of all matchers.
  std::vector<::testing::Matcher<
      envoy::service::status::v3::ClientConfig_GenericXdsConfig>>
      matchers = {
          // Listener
          EqGenericXdsConfig(
              kLdsTypeUrl, kServerName, /*version_info=*/"1",
              UnpackListener(EqListener(kServerName, api_listener_matcher)),
              ClientResourceStatus::ACKED, /*error_state=*/::testing::_),
          // Cluster
          EqGenericXdsConfig(
              kCdsTypeUrl, kDefaultClusterName, /*version_info=*/"1",
              UnpackCluster(EqCluster(kDefaultClusterName)),
              ClientResourceStatus::ACKED, /*error_state=*/::testing::_),
          // ClusterLoadAssignment
          EqGenericXdsConfig(
              kEdsTypeUrl, kDefaultEdsServiceName, /*version_info=*/"1",
              UnpackClusterLoadAssignment(EqClusterLoadAssignment(
                  kDefaultEdsServiceName, backends_[0]->port(),
                  kDefaultLocalityWeight)),
              ClientResourceStatus::ACKED, /*error_state=*/::testing::_),
      };
  // If RDS is enabled, add matcher for RDS resource.
  if (GetParam().enable_rds_testing()) {
    matchers.push_back(EqGenericXdsConfig(
        kRdsTypeUrl, kDefaultRouteConfigurationName, /*version_info=*/"1",
        UnpackRouteConfiguration(EqRouteConfiguration(
            kDefaultRouteConfigurationName, kDefaultClusterName)),
        ClientResourceStatus::ACKED, /*error_state=*/::testing::_));
  }
  // Validate the dumped xDS configs
  EXPECT_THAT(client_config.generic_xds_configs(),
              ::testing::UnorderedElementsAreArray(matchers))
      << "Actual: " << client_config.DebugString();
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpEmpty) {
  // The CSDS service should not fail if XdsClient is not initialized or there
  // is no working xDS configs.
  FetchCsdsResponse();
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpListenerError) {
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad Listener should be rejected.
  Listener listener;
  listener.set_name(kServerName);
  balancers_[0]->ads_service()->SetLdsResource(listener);
  // The old xDS configs should still be effective.
  CheckRpcSendOk();
  ::testing::Matcher<google::protobuf::Any> api_listener_matcher;
  if (GetParam().enable_rds_testing()) {
    api_listener_matcher = IsRdsEnabledHCM();
  } else {
    api_listener_matcher =
        EqNoRdsHCM(kDefaultRouteConfigurationName, kDefaultClusterName);
  }
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    // Check if error state is propagated
    bool ok = ::testing::Value(
        csds_response.config(0).generic_xds_configs(),
        ::testing::Contains(EqGenericXdsConfig(
            kLdsTypeUrl, kServerName, /*version_info=*/"1",
            UnpackListener(EqListener(kServerName, api_listener_matcher)),
            ClientResourceStatus::NACKED,
            EqUpdateFailureState(
                ::testing::HasSubstr(
                    "Listener has neither address nor ApiListener"),
                "2"))));
    if (ok) return;  // TEST PASSED!
    gpr_sleep_until(
        grpc_timeout_milliseconds_to_deadline(kFetchIntervalMilliseconds));
  }
  FAIL() << "error_state not seen in CSDS responses";
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpRouteError) {
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad route config will be rejected.
  RouteConfiguration route_config;
  route_config.set_name(kDefaultRouteConfigurationName);
  route_config.add_virtual_hosts();
  SetRouteConfiguration(0, route_config);
  // The old xDS configs should still be effective.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    bool ok = false;
    if (GetParam().enable_rds_testing()) {
      ok = ::testing::Value(
          csds_response.config(0).generic_xds_configs(),
          ::testing::Contains(EqGenericXdsConfig(
              kRdsTypeUrl, kDefaultRouteConfigurationName, /*version_info=*/"1",
              UnpackRouteConfiguration(EqRouteConfiguration(
                  kDefaultRouteConfigurationName, kDefaultClusterName)),
              ClientResourceStatus::NACKED,
              EqUpdateFailureState(
                  ::testing::HasSubstr("VirtualHost has no domains"), "2"))));
    } else {
      ok = ::testing::Value(
          csds_response.config(0).generic_xds_configs(),
          ::testing::Contains(EqGenericXdsConfig(
              kLdsTypeUrl, kServerName, /*version_info=*/"1",
              UnpackListener(EqListener(
                  kServerName, EqNoRdsHCM(kDefaultRouteConfigurationName,
                                          kDefaultClusterName))),
              ClientResourceStatus::NACKED,
              EqUpdateFailureState(
                  ::testing::HasSubstr("VirtualHost has no domains"), "2"))));
    }
    if (ok) return;  // TEST PASSED!
    gpr_sleep_until(
        grpc_timeout_milliseconds_to_deadline(kFetchIntervalMilliseconds));
  }
  FAIL() << "error_state not seen in CSDS responses";
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpClusterError) {
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Listener without any route, will be rejected.
  Cluster cluster;
  cluster.set_name(kDefaultClusterName);
  balancers_[0]->ads_service()->SetCdsResource(cluster);
  // The old xDS configs should still be effective.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    // Check if error state is propagated
    bool ok = ::testing::Value(
        csds_response.config(0).generic_xds_configs(),
        ::testing::Contains(EqGenericXdsConfig(
            kCdsTypeUrl, kDefaultClusterName, /*version_info=*/"1",
            UnpackCluster(EqCluster(kDefaultClusterName)),
            ClientResourceStatus::NACKED,
            EqUpdateFailureState(
                ::testing::HasSubstr("DiscoveryType not found"), "2"))));
    if (ok) return;  // TEST PASSED!
    gpr_sleep_until(
        grpc_timeout_milliseconds_to_deadline(kFetchIntervalMilliseconds));
  }
  FAIL() << "error_state not seen in CSDS responses";
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpEndpointError) {
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancers_[0]->ads_service()->SetEdsResource(
      BuildEdsResource(args, DefaultEdsServiceName()));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad endpoint config will be rejected.
  ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name(kDefaultEdsServiceName);
  auto* endpoints = cluster_load_assignment.add_endpoints();
  endpoints->mutable_load_balancing_weight()->set_value(1);
  auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
  endpoint->mutable_address()->mutable_socket_address()->set_port_value(1 << 1);
  balancers_[0]->ads_service()->SetEdsResource(cluster_load_assignment);
  // The old xDS configs should still be effective.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    // Check if error state is propagated
    bool ok = ::testing::Value(
        csds_response.config(0).generic_xds_configs(),
        ::testing::Contains(EqGenericXdsConfig(
            kEdsTypeUrl, kDefaultEdsServiceName, /*version_info=*/"1",
            UnpackClusterLoadAssignment(EqClusterLoadAssignment(
                kDefaultEdsServiceName, backends_[0]->port(),
                kDefaultLocalityWeight)),
            ClientResourceStatus::NACKED,
            EqUpdateFailureState(::testing::HasSubstr("Empty locality"),
                                 "2"))));
    if (ok) return;  // TEST PASSED!
    gpr_sleep_until(
        grpc_timeout_milliseconds_to_deadline(kFetchIntervalMilliseconds));
  }
  FAIL() << "error_state not seen in CSDS responses";
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpListenerRequested) {
  int kTimeoutMillisecond = 1000;
  balancers_[0]->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kLdsTypeUrl, kServerName, /*version_info=*/::testing::_, ::testing::_,
          ClientResourceStatus::REQUESTED, /*error_state=*/::testing::_)));
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpClusterRequested) {
  int kTimeoutMillisecond = 1000;
  std::string kClusterName1 = "cluster-1";
  std::string kClusterName2 = "cluster-2";
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  // Create a route config requesting two non-existing clusters
  RouteConfiguration route_config;
  route_config.set_name(kDefaultRouteConfigurationName);
  auto* vh = route_config.add_virtual_hosts();
  // The VirtualHost must match the domain name, otherwise will cause resolver
  // transient failure.
  vh->add_domains("*");
  auto* routes1 = vh->add_routes();
  routes1->mutable_match()->set_prefix("");
  routes1->mutable_route()->set_cluster(kClusterName1);
  auto* routes2 = vh->add_routes();
  routes2->mutable_match()->set_prefix("");
  routes2->mutable_route()->set_cluster(kClusterName2);
  SetRouteConfiguration(0, route_config);
  // Try to get the configs plumb through
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::AllOf(
                  ::testing::Contains(EqGenericXdsConfig(
                      kCdsTypeUrl, kClusterName1, /*version_info=*/::testing::_,
                      ::testing::_, ClientResourceStatus::REQUESTED,
                      /*error_state=*/::testing::_)),
                  ::testing::Contains(EqGenericXdsConfig(
                      kCdsTypeUrl, kClusterName2, /*version_info=*/::testing::_,
                      ::testing::_, ClientResourceStatus::REQUESTED,
                      /*error_state=*/::testing::_))));
}

class CsdsShortAdsTimeoutTest : public ClientStatusDiscoveryServiceTest {
 protected:
  // Shorten the ADS subscription timeout to speed up the test run.
  CsdsShortAdsTimeoutTest()
      : ClientStatusDiscoveryServiceTest(
            /* xds_resource_does_not_exist_timeout_ms_ = */ 2000) {}
};

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpListenerDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  balancers_[0]->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kLdsTypeUrl, kServerName, /*version_info=*/::testing::_, ::testing::_,
          ClientResourceStatus::DOES_NOT_EXIST, /*error_state=*/::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpRouteConfigDoesNotExist) {
  if (!GetParam().enable_rds_testing()) return;
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  balancers_[0]->ads_service()->UnsetResource(kRdsTypeUrl,
                                              kDefaultRouteConfigurationName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kRdsTypeUrl, kDefaultRouteConfigurationName,
          /*version_info=*/::testing::_, ::testing::_,
          ClientResourceStatus::DOES_NOT_EXIST, /*error_state=*/::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpClusterDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  balancers_[0]->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kCdsTypeUrl, kDefaultClusterName, /*version_info=*/::testing::_,
          ::testing::_, ClientResourceStatus::DOES_NOT_EXIST,
          /*error_state=*/::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpEndpointDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  SetNextResolution({});
  SetNextResolutionForLbChannelAllBalancers();
  balancers_[0]->ads_service()->UnsetResource(kEdsTypeUrl,
                                              kDefaultEdsServiceName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::Contains(EqGenericXdsConfig(
                  kEdsTypeUrl, kDefaultEdsServiceName,
                  /*version_info=*/::testing::_, ::testing::_,
                  ClientResourceStatus::DOES_NOT_EXIST,
                  /*error_state=*/::testing::_)));
}

#endif  // DISABLED_XDS_PROTO_IN_CC

std::string TestTypeName(const ::testing::TestParamInfo<TestType>& info) {
  return info.param.AsString();
}

// Run with all combinations of xds/fake resolver and enabling load reporting.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, BasicTest,
    ::testing::Values(
        TestType(), TestType().set_enable_load_reporting(),
        TestType().set_use_fake_resolver(),
        TestType().set_use_fake_resolver().set_enable_load_reporting()),
    &TestTypeName);

// Run with both fake resolver and xds resolver.
// Don't run with load reporting or v2 or RDS, since they are irrelevant to
// the tests.
INSTANTIATE_TEST_SUITE_P(XdsTest, SecureNamingTest,
                         ::testing::Values(TestType(),
                                           TestType().set_use_fake_resolver()),
                         &TestTypeName);

// LDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsTest, ::testing::Values(TestType()),
                         &TestTypeName);
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsV2Test,
                         ::testing::Values(TestType().set_use_v2()),
                         &TestTypeName);

// LDS/RDS commmon tests depend on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, LdsRdsTest,
    ::testing::Values(TestType(), TestType().set_enable_rds_testing(),
                      // Also test with xDS v2.
                      TestType().set_enable_rds_testing().set_use_v2()),
    &TestTypeName);

// CDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, CdsTest,
    ::testing::Values(TestType(), TestType().set_enable_load_reporting()),
    &TestTypeName);

// CDS depends on XdsResolver.
// Security depends on v3.
// Not enabling load reporting or RDS, since those are irrelevant to these
// tests.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsSecurityTest,
    ::testing::Values(TestType().set_use_xds_credentials()), &TestTypeName);

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsEnabledServerTest,
                         ::testing::Values(TestType().set_bootstrap_source(
                             TestType::kBootstrapFromEnvVar)),
                         &TestTypeName);

// We are only testing the server here.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerSecurityTest,
                         ::testing::Values(TestType()
                                               .set_use_fake_resolver()
                                               .set_use_xds_credentials()),
                         &TestTypeName);

// We are only testing the server here.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsEnabledServerStatusNotificationTest,
                         ::testing::Values(TestType()
                                               .set_use_fake_resolver()
                                               .set_use_xds_credentials()),
                         &TestTypeName);

// We are only testing the server here.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerFilterChainMatchTest,
                         ::testing::Values(TestType()
                                               .set_use_fake_resolver()
                                               .set_use_xds_credentials()),
                         &TestTypeName);

// We are only testing the server here.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerRdsTest,
                         ::testing::Values(TestType()
                                               .set_use_fake_resolver()
                                               .set_use_xds_credentials(),
                                           TestType()
                                               .set_use_fake_resolver()
                                               .set_use_xds_credentials()
                                               .set_enable_rds_testing()),
                         &TestTypeName);

// EDS could be tested with or without XdsResolver, but the tests would
// be the same either way, so we test it only with XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, EdsTest,
    ::testing::Values(TestType(), TestType().set_enable_load_reporting()),
    &TestTypeName);

// Test initial resource timeouts for each resource type.
// Do this only for XdsResolver with RDS enabled, so that we can test
// all resource types.
// Run with V3 only, since the functionality is no different in V2.
// Run with bootstrap from env var so that multiple channels share the same
// XdsClient (needed for testing the timeout for the 2nd LDS and RDS resource).
INSTANTIATE_TEST_SUITE_P(
    XdsTest, TimeoutTest,
    ::testing::Values(TestType().set_enable_rds_testing().set_bootstrap_source(
        TestType::kBootstrapFromEnvVar)),
    &TestTypeName);

// XdsResolverOnlyTest depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsResolverOnlyTest,
    ::testing::Values(TestType(), TestType().set_enable_load_reporting()),
    &TestTypeName);

// Runs with bootstrap from env var, so that there's a global XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, GlobalXdsClientTest,
    ::testing::Values(
        TestType().set_bootstrap_source(TestType::kBootstrapFromEnvVar),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting()),
    &TestTypeName);

// XdsResolverLoadReprtingOnlyTest depends on XdsResolver and load reporting.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsResolverLoadReportingOnlyTest,
    ::testing::Values(TestType().set_enable_load_reporting()), &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, LocalityMapTest,
    ::testing::Values(
        TestType(), TestType().set_enable_load_reporting(),
        TestType().set_use_fake_resolver(),
        TestType().set_use_fake_resolver().set_enable_load_reporting()),
    &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, FailoverTest,
    ::testing::Values(
        TestType(), TestType().set_enable_load_reporting(),
        TestType().set_use_fake_resolver(),
        TestType().set_use_fake_resolver().set_enable_load_reporting()),
    &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, DropTest,
    ::testing::Values(
        TestType(), TestType().set_enable_load_reporting(),
        TestType().set_use_fake_resolver(),
        TestType().set_use_fake_resolver().set_enable_load_reporting()),
    &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, BalancerUpdateTest,
    ::testing::Values(
        TestType().set_use_fake_resolver(),
        TestType().set_use_fake_resolver().set_enable_load_reporting(),
        TestType().set_enable_load_reporting()),
    &TestTypeName);

// Load reporting tests are not run with load reporting disabled.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientLoadReportingTest,
    ::testing::Values(
        TestType().set_enable_load_reporting(),
        TestType().set_enable_load_reporting().set_use_fake_resolver()),
    &TestTypeName);

// Load reporting tests are not run with load reporting disabled.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientLoadReportingWithDropTest,
    ::testing::Values(
        TestType().set_enable_load_reporting(),
        TestType().set_enable_load_reporting().set_use_fake_resolver()),
    &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, FaultInjectionTest,
    ::testing::Values(
        TestType(), TestType().set_enable_rds_testing(),
        TestType().set_filter_config_setup(
            TestType::FilterConfigSetup::kRouteOverride),
        TestType().set_enable_rds_testing().set_filter_config_setup(
            TestType::FilterConfigSetup::kRouteOverride)),
    &TestTypeName);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, BootstrapSourceTest,
    ::testing::Values(
        TestType().set_bootstrap_source(TestType::kBootstrapFromEnvVar),
        TestType().set_bootstrap_source(TestType::kBootstrapFromFile)),
    &TestTypeName);

#ifndef DISABLED_XDS_PROTO_IN_CC
// Run CSDS tests with RDS enabled and disabled.
// These need to run with the bootstrap from an env var instead of from
// a channel arg, since there needs to be a global XdsClient instance.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientStatusDiscoveryServiceTest,
    ::testing::Values(
        TestType().set_bootstrap_source(TestType::kBootstrapFromEnvVar),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing(),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_use_csds_streaming(),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()
            .set_use_csds_streaming()),
    &TestTypeName);
INSTANTIATE_TEST_SUITE_P(
    XdsTest, CsdsShortAdsTimeoutTest,
    ::testing::Values(
        TestType().set_bootstrap_source(TestType::kBootstrapFromEnvVar),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing(),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_use_csds_streaming(),
        TestType()
            .set_bootstrap_source(TestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()
            .set_use_csds_streaming()),
    &TestTypeName);
#endif  // DISABLED_XDS_PROTO_IN_CC

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::WriteBootstrapFiles();
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  gpr_setenv("grpc_cfstream", "0");
#endif
  grpc_core::CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<grpc::testing::FakeCertificateProviderFactory>(
          "fake1", &grpc::testing::g_fake1_cert_data_map));
  grpc_core::CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<grpc::testing::FakeCertificateProviderFactory>(
          "fake2", &grpc::testing::g_fake2_cert_data_map));
  grpc_init();
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.client_only_http_filter",
          /* supported_on_clients = */ true, /* supported_on_servers = */ false,
          /* is_terminal_filter */ false),
      {"grpc.testing.client_only_http_filter"});
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.server_only_http_filter",
          /* supported_on_clients = */ false, /* supported_on_servers = */ true,
          /* is_terminal_filter */ false),
      {"grpc.testing.server_only_http_filter"});
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.terminal_http_filter",
          /* supported_on_clients = */ true, /* supported_on_servers = */ true,
          /* is_terminal_filter */ true),
      {"grpc.testing.terminal_http_filter"});
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
