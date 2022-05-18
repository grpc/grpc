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
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/lookup/v1/rls.grpc.pb.h"
#include "src/proto/grpc/lookup/v1/rls.pb.h"
#include "src/proto/grpc/lookup/v1/rls_config.pb.h"
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
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/rls_server.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
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
using ::envoy::config::rbac::v3::Policy;
using ::envoy::config::rbac::v3::RBAC_Action;
using ::envoy::config::rbac::v3::RBAC_Action_ALLOW;
using ::envoy::config::rbac::v3::RBAC_Action_DENY;
using ::envoy::config::rbac::v3::RBAC_Action_LOG;
using ::envoy::config::route::v3::RouteConfiguration;
using ::envoy::extensions::clusters::aggregate::v3::ClusterConfig;
using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::rbac::v3::RBACPerRoute;
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
using ::grpc::lookup::v1::RouteLookupClusterSpecifier;
using ::grpc::lookup::v1::RouteLookupConfig;

constexpr char kLbDropType[] = "lb";
constexpr char kThrottleDropType[] = "throttle";

constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kBadClientCertPath[] = "src/core/tsi/test_creds/badclient.pem";
constexpr char kBadClientKeyPath[] = "src/core/tsi/test_creds/badclient.key";

constexpr char kRlsTestKey[] = "test_key";
constexpr char kRlsTestKey1[] = "key1";
constexpr char kRlsTestValue[] = "test_value";
constexpr char kRlsHostKey[] = "host_key";
constexpr char kRlsServiceKey[] = "service_key";
constexpr char kRlsServiceValue[] = "grpc.testing.EchoTestService";
constexpr char kRlsMethodKey[] = "method_key";
constexpr char kRlsMethodValue[] = "Echo";
constexpr char kRlsConstantKey[] = "constant_key";
constexpr char kRlsConstantValue[] = "constant_value";
constexpr char kRlsClusterSpecifierPluginInstanceName[] = "rls_plugin_instance";

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

  const char* type() const override { return "fake"; }

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

// A No-op HTTP filter used for verifying parsing logic.
class NoOpHttpFilter : public grpc_core::XdsHttpFilterImpl {
 public:
  NoOpHttpFilter(std::string name, bool supported_on_clients,
                 bool supported_on_servers, bool is_terminal_filter)
      : name_(std::move(name)),
        supported_on_clients_(supported_on_clients),
        supported_on_servers_(supported_on_servers),
        is_terminal_filter_(is_terminal_filter) {}

  void PopulateSymtab(upb_DefPool* /* symtab */) const override {}

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfig(upb_StringView /* serialized_filter_config */,
                       upb_Arena* /* arena */) const override {
    return grpc_core::XdsHttpFilterImpl::FilterConfig{name_, grpc_core::Json()};
  }

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfigOverride(upb_StringView /*serialized_filter_config*/,
                               upb_Arena* /*arena*/) const override {
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

using BasicTest = XdsEnd2endTest;

// Tests that the balancer sends the correct response to the client, and the
// client sends RPCs to the backends using the default child policy.
TEST_P(BasicTest, Vanilla) {
  CreateAndStartBackends(3);
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * backends_.size());
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("xds_cluster_manager_experimental",
            channel_->GetLoadBalancingPolicyName());
}

// Tests that the client can handle resource wrapped in a Resource message.
TEST_P(BasicTest, ResourceWrappedInResourceMessage) {
  CreateAndStartBackends(1);
  balancer_->ads_service()->set_wrap_resources(true);
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * backends_.size());
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("xds_cluster_manager_experimental",
            channel_->GetLoadBalancingPolicyName());
}

TEST_P(BasicTest, IgnoresUnhealthyEndpoints) {
  CreateAndStartBackends(2);
  const size_t kNumRpcsPerAddress = 100;
  auto endpoints = CreateEndpointsForBackends();
  endpoints.push_back(MakeNonExistantEndpoint());
  endpoints.back().health_status = HealthStatus::DRAINING;
  EdsResourceArgs args({
      {"locality0", std::move(endpoints), kDefaultLocalityWeight,
       kDefaultLocalityPriority},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * backends_.size());
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
}

// Tests that subchannel sharing works when the same backend is listed
// multiple times.
TEST_P(BasicTest, SameBackendListedMultipleTimes) {
  CreateAndStartBackends(1);
  // Same backend listed twice.
  auto endpoints = CreateEndpointsForBackends();
  endpoints.push_back(endpoints.front());
  EdsResourceArgs args({{"locality0", endpoints}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for the backend to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  const size_t kNumRpcsPerAddress = 10;
  CheckRpcSendOk(kNumRpcsPerAddress * endpoints.size());
  // Backend should have gotten 20 requests.
  EXPECT_EQ(kNumRpcsPerAddress * endpoints.size(),
            backends_[0]->backend_service()->request_count());
}

// Tests that RPCs will be blocked until a non-empty serverlist is received.
TEST_P(BasicTest, InitiallyEmptyServerlist) {
  CreateAndStartBackends(1);
  // First response is an empty serverlist.
  EdsResourceArgs::Locality empty_locality("locality0", {});
  EdsResourceArgs args({std::move(empty_locality)});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should fail.
  CheckRpcSendFailure();
  // Send non-empty serverlist.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should eventually succeed.
  WaitForAllBackends(0, 1, WaitForBackendOptions().set_allow_failures(true));
}

// Tests that RPCs will fail with UNAVAILABLE instead of DEADLINE_EXCEEDED if
// all the servers are unreachable.
TEST_P(BasicTest, AllServersUnreachableFailFast) {
  // Set Rpc timeout to 5 seconds to ensure there is enough time
  // for communication with the xDS server to take place upon test start up.
  const uint32_t kRpcTimeoutMs = 5000;
  const size_t kNumUnreachableServers = 5;
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    endpoints.emplace_back(MakeNonExistantEndpoint());
  }
  EdsResourceArgs args({{"locality0", std::move(endpoints)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const Status status = SendRpc(RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  // The error shouldn't be DEADLINE_EXCEEDED because timeout is set to 5
  // seconds, and we should disocver in that time that the target backend is
  // down.
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
}

// Tests that RPCs fail when the backends are down, and will succeed again
// after the backends are restarted.
TEST_P(BasicTest, BackendsRestart) {
  CreateAndStartBackends(3);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(backends_.size()));
  // Restart all backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(1, RpcOptions().set_timeout_ms(2000).set_wait_for_ready(true));
}

TEST_P(BasicTest, IgnoresDuplicateUpdates) {
  CreateAndStartBackends(1);
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for all backends to come online.
  WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server, but send an EDS update in
  // between.  If the update is not ignored, this will cause the
  // round_robin policy to see an update, which will randomly reset its
  // position in the address list.
  for (size_t i = 0; i < kNumRpcsPerAddress; ++i) {
    CheckRpcSendOk(2);
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
    CheckRpcSendOk(2);
  }
  // Each backend should have gotten the right number of requests.
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
}

using XdsResolverOnlyTest = XdsEnd2endTest;

TEST_P(XdsResolverOnlyTest, ResourceTypeVersionPersistsAcrossStreamRestarts) {
  CreateAndStartBackends(2);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for backends to come online.
  WaitForAllBackends(0, 1);
  // Stop balancer.
  balancer_->Shutdown();
  // Tell balancer to require minimum version 1 for all resource types.
  balancer_->ads_service()->SetResourceMinVersion(kLdsTypeUrl, 1);
  balancer_->ads_service()->SetResourceMinVersion(kRdsTypeUrl, 1);
  balancer_->ads_service()->SetResourceMinVersion(kCdsTypeUrl, 1);
  balancer_->ads_service()->SetResourceMinVersion(kEdsTypeUrl, 1);
  // Update backend, just so we can be sure that the client has
  // reconnected to the balancer.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Restart balancer.
  balancer_->Start();
  // Make sure client has reconnected.
  WaitForAllBackends(1, 2);
}

// Tests switching over from one cluster to another.
TEST_P(XdsResolverOnlyTest, ChangeClusters) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 1);
  // Populate new EDS resource.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Wait for all new backends to be used.
  WaitForAllBackends(1, 2);
}

// Tests that we go into TRANSIENT_FAILURE if the Cluster disappears.
TEST_P(XdsResolverOnlyTest, ClusterRemoved) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Unset CDS resource.
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  // Wait for RPCs to start failing.
  do {
  } while (SendRpc(RpcOptions(), nullptr).ok());
  // Make sure RPCs are still failing.
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(1000));
  // Make sure we ACK'ed the update.
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we restart all xDS requests when we reestablish the ADS call.
TEST_P(XdsResolverOnlyTest, RestartsRequestsUponReconnection) {
  CreateAndStartBackends(2);
  // Manually configure use of RDS.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  balancer_->ads_service()->SetRdsResource(default_route_config_);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(0, 1);
  // Now shut down and restart the balancer.  When the client
  // reconnects, it should automatically restart the requests for all
  // resource types.
  balancer_->Shutdown();
  balancer_->Start();
  // Make sure things are still working.
  CheckRpcSendOk(100);
  // Populate new EDS resource.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(new_route_config);
  // Wait for all new backends to be used.
  WaitForAllBackends(1, 2);
}

TEST_P(XdsResolverOnlyTest, DefaultRouteSpecifiesSlashPrefix) {
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
}

TEST_P(XdsResolverOnlyTest, CircuitBreaking) {
  CreateAndStartBackends(1);
  constexpr size_t kMaxConcurrentRequests = 10;
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
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
}

TEST_P(XdsResolverOnlyTest, CircuitBreakingMultipleChannelsShareCallCounter) {
  CreateAndStartBackends(1);
  constexpr size_t kMaxConcurrentRequests = 10;
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto channel2 = CreateChannel();
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
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
}

TEST_P(XdsResolverOnlyTest, ClusterChangeAfterAdsCallFails) {
  CreateAndStartBackends(2);
  const char* kNewEdsResourceName = "new_eds_resource_name";
  // Populate EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Check that the channel is working.
  CheckRpcSendOk();
  // Stop and restart the balancer.
  balancer_->Shutdown();
  balancer_->Start();
  // Create new EDS resource.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  // Change CDS resource to point to new EDS resource.
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
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

// Tests that if the balancer is down, the RPCs will still be sent to the
// backends according to the last balancer response, until a new balancer is
// reachable.
TEST_P(XdsResolverOnlyTest, KeepUsingLastDataIfBalancerGoesDown) {
  CreateAndStartBackends(2);
  // Set up EDS resource pointing to backend 0.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Start the client and make sure it sees the backend.
  WaitForBackend(0);
  // Stop the balancer, and verify that RPCs continue to flow to backend 0.
  balancer_->Shutdown();
  auto deadline = grpc_timeout_seconds_to_deadline(5);
  do {
    CheckRpcSendOk();
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
  // Check the EDS resource to point to backend 1 and bring the balancer
  // back up.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->Start();
  // Wait for client to see backend 1.
  WaitForBackend(1);
}

TEST_P(XdsResolverOnlyTest, XdsStreamErrorPropagation) {
  const std::string kErrorMessage = "test forced ADS stream failure";
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto status = SendRpc();
  gpr_log(GPR_INFO,
          "XdsStreamErrorPropagation test: RPC got error: code=%d message=%s",
          status.error_code(), status.error_message().c_str());
  EXPECT_THAT(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_THAT(status.error_message(), ::testing::HasSubstr(kErrorMessage));
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("(node ID:xds_end2end_test)"));
}

using GlobalXdsClientTest = XdsEnd2endTest;

TEST_P(GlobalXdsClientTest, MultipleChannelsShareXdsClient) {
  CreateAndStartBackends(1);
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  // Create second channel and tell it to connect to kNewServerName.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  // Make sure there's only one client connected.
  EXPECT_EQ(1UL, balancer_->ads_service()->clients().size());
}

TEST_P(
    GlobalXdsClientTest,
    MultipleChannelsShareXdsClientWithResourceUpdateAfterOneChannelGoesAway) {
  CreateAndStartBackends(2);
  // Test for https://github.com/grpc/grpc/issues/28468. Makes sure that the
  // XdsClient properly handles the case where there are multiple watchers on
  // the same resource and one of them unsubscribes.
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  WaitForBackend(0);
  // Create second channel and tell it to connect to kNewServerName.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  // Now, destroy the new channel, send an EDS update to use a different backend
  // and test that the channel switches to that backend.
  channel2.reset();
  // This sleep is needed to be able to reproduce the bug and to give time for
  // the buggy unsubscription to take place.
  // TODO(yashykt): Figure out a way to do this without the sleep.
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  })));
  WaitForBackend(1);
}

// Tests that the NACK for multiple bad LDS resources includes both errors.
TEST_P(GlobalXdsClientTest, MultipleBadResources) {
  CreateAndStartBackends(1);
  constexpr char kServerName2[] = "server.other.com";
  constexpr char kServerName3[] = "server.another.com";
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  listener.set_name(kServerName2);
  balancer_->ads_service()->SetLdsResource(listener);
  listener = default_listener_;
  listener.set_name(kServerName3);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::ContainsRegex(absl::StrCat(
                  kServerName,
                  ": validation error.*"
                  "Listener has neither address nor ApiListener.*")));
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
    const auto response_state = WaitForLdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::ContainsRegex(absl::StrCat(
                    kServerName,
                    ": validation error.*"
                    "Listener has neither address nor ApiListener.*")));
    EXPECT_THAT(response_state->error_message,
                ::testing::ContainsRegex(absl::StrCat(
                    kServerName2,
                    ": validation error.*"
                    "Listener has neither address nor ApiListener.*")));
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
  CreateAndStartBackends(1);
  // Set up valid resources and check that the channel works.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk();
  // Now send an update changing the Listener to be invalid.
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(StatusCode::OK);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::ContainsRegex(absl::StrCat(
                  kServerName,
                  ": validation error.*"
                  "Listener has neither address nor ApiListener")));
  CheckRpcSendOk();
}

class XdsFederationTest : public XdsEnd2endTest {
 protected:
  XdsFederationTest() : authority_balancer_(CreateAndStartBalancer()) {}

  void SetUp() override {
    // Each test will use a slightly different bootstrapfile,
    // so SetUp() is intentionally empty here and the real
    // setup (calling of InitClient()) is moved into each test.
  }

  void TearDown() override {
    authority_balancer_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  std::unique_ptr<BalancerServerThread> authority_balancer_;
};

// Channel is created with URI "xds:server.example.com".
// Bootstrap config default client listener template uses new-style name with
// authority "xds.example.com".
TEST_P(XdsFederationTest, FederationTargetNoAuthorityWithResourceTemplate) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "new_edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "new_cluster_name";
  BootstrapBuilder builder = BootstrapBuilder();
  builder.SetClientDefaultListenerResourceNameTemplate(kNewListenerTemplate);
  builder.AddAuthority(
      kAuthority, absl::StrCat("localhost:", authority_balancer_->port()),
      // Note we will not use the client_listener_resource_name_template field
      // in the authority.
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener"
      "client/%s?client_listener_resource_name_template_not_in_use");
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for the new authority balancer.
  EdsResourceArgs args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  WaitForAllBackends();
}

// Channel is created with URI "xds://xds.example.com/server.example.com".
// In bootstrap config, authority has no client listener template, so we use the
// default.
TEST_P(XdsFederationTest, FederationTargetAuthorityDefaultResourceTemplate) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "whee%25/server.example.com";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  BootstrapBuilder builder = BootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()));
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Ensure update has reached and send 10 RPCs to the current stub.
  WaitForAllBackends(0, 1);
  // Create second channel to new target uri and send 1 RPC .
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  request.set_message(kRequestMessage);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  // We should be reaching backend 1, not 0, as balanced by the authority xds
  // server.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1U, backends_[1]->backend_service()->request_count());
}

// Channel is created with URI "xds://xds.example.com/server.example.com".
// Bootstrap entry for that authority specifies a client listener name template.
TEST_P(XdsFederationTest, FederationTargetAuthorityWithResourceTemplate) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  BootstrapBuilder builder = BootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Ensure update has reached and send 10 RPCs to the current stub.
  WaitForAllBackends(0, 1);
  // Create second channel to new target uri and send 1 RPC .
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  request.set_message(kRequestMessage);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  // We should be reaching backend 1, not 0, as balanced by the authority xds
  // server.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1U, backends_[1]->backend_service()->request_count());
}

// Setting server_listener_resource_name_template to start with "xdstp:" and
// look up xds server under an authority map.
TEST_P(XdsFederationTest, FederationServer) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewServerListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "server/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "new_edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "new_cluster_name";
  BootstrapBuilder builder = BootstrapBuilder();
  builder.SetClientDefaultListenerResourceNameTemplate(kNewListenerTemplate);
  builder.SetServerListenerResourceNameTemplate(kNewServerListenerTemplate);
  builder.AddAuthority(
      kAuthority, absl::StrCat("localhost:", authority_balancer_->port()),
      // Note we will not use the client_listener_resource_name_template field
      // in the authority.
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener"
      "client/%s?client_listener_resource_name_template_not_in_use");
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for new authority balancer.
  EdsResourceArgs args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // New Server Listeners
  for (int port : GetBackendPorts()) {
    Listener server_listener = default_server_listener_;
    server_listener.set_name(absl::StrCat(
        "xdstp://xds.example.com/envoy.config.listener.v3.Listener/server/",
        ipv6_only_ ? "%5B::1%5D:" : "127.0.0.1:", port,
        "?psm_project_id=1234"));
    server_listener.mutable_address()->mutable_socket_address()->set_port_value(
        port);
    authority_balancer_->ads_service()->SetLdsResource(server_listener);
  }
  WaitForAllBackends();
}

using XdsFederationDisabledTest = XdsEnd2endTest;

TEST_P(XdsFederationDisabledTest, FederationDisabledWithNewStyleNames) {
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  const char* kNewEdsResourceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  InitClient();
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // New RouteConfig
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Channel should work.
  CheckRpcSendOk();
}

using XdsFederationLoadReportingTest = XdsFederationTest;

// Channel is created with URI "xds://xds.example.com/server.example.com".
// Bootstrap entry for that authority specifies a client listener name template.
// Sending traffic to both default balancer and authority balancer and checking
// load reporting with each one.
TEST_P(XdsFederationLoadReportingTest, FederationMultipleLoadReportingTest) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  const size_t kNumRpcsToDefaultBalancer = 5;
  const size_t kNumRpcsToAuthorityBalancer = 10;
  BootstrapBuilder builder = BootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  authority_balancer_->lrs_service()->set_cluster_names({kNewClusterName});
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_lrs_server()->mutable_self();
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Ensure update has reached and send 10 RPCs to the current stub.
  CheckRpcSendOk(kNumRpcsToDefaultBalancer);
  // Create second channel to new target uri and send 1 RPC .
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  for (size_t i = 0; i < kNumRpcsToAuthorityBalancer; ++i) {
    ClientContext context;
    EchoRequest request;
    request.set_message(kRequestMessage);
    EchoResponse response;
    grpc::Status status = stub2->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
  }
  // Each backend should have received the expected number of RPCs,
  // and the load report also reflect the correct numbers.
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            backends_[1]->backend_service()->request_count());
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            backends_[0]->backend_service()->request_count());
  // Load report for authority LRS.
  std::vector<ClientStats> authority_load_report =
      authority_balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(authority_load_report.size(), 1UL);
  ClientStats& authority_client_stats = authority_load_report.front();
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            authority_client_stats.total_successful_requests());
  EXPECT_EQ(0U, authority_client_stats.total_requests_in_progress());
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            authority_client_stats.total_issued_requests());
  EXPECT_EQ(0U, authority_client_stats.total_error_requests());
  EXPECT_EQ(0U, authority_client_stats.total_dropped_requests());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->response_count());
  // Load report for default LRS.
  std::vector<ClientStats> default_load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(default_load_report.size(), 1UL);
  ClientStats& default_client_stats = default_load_report.front();
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            default_client_stats.total_successful_requests());
  EXPECT_EQ(0U, default_client_stats.total_requests_in_progress());
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            default_client_stats.total_issued_requests());
  EXPECT_EQ(0U, default_client_stats.total_error_requests());
  EXPECT_EQ(0U, default_client_stats.total_dropped_requests());
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

class SecureNamingTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    // Each test calls InitClient() on its own.
  }
};

// Tests that secure naming check passes if target name is expected.
TEST_P(SecureNamingTest, TargetNameIsExpected) {
  InitClient(BootstrapBuilder(), /*lb_expected_authority=*/"localhost:%d");
  CreateAndStartBackends(4);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk();
}

// Tests that secure naming check fails if target name is unexpected.
TEST_P(SecureNamingTest, TargetNameIsUnexpected) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  InitClient(BootstrapBuilder(),
             /*lb_expected_authority=*/"incorrect_server_name");
  CreateAndStartBackends(4);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED({ CheckRpcSendOk(); }, "");
}

using LdsTest = XdsEnd2endTest;

// Tests that LDS client should send a NACK if there is no API listener in the
// Listener in the LDS response.
TEST_P(LdsTest, NoApiListener) {
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "HttpConnectionManager missing config_source for RDS."));
}

// Tests that LDS client should send a NACK if the rds message in the
// http_connection_manager has a config_source field that does not specify
// ADS or SELF.
TEST_P(LdsTest, RdsConfigSourceDoesNotSpecifyAdsOrSelf) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->set_path("/foo/bar");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("HttpConnectionManager ConfigSource for "
                                   "RDS does not specify ADS or SELF."));
}

// Tests that LDS client accepts the rds message in the
// http_connection_manager with a config_source field that specifies ADS.
TEST_P(LdsTest, AcceptsRdsConfigSourceOfTypeAds) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_ads();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we NACK non-terminal filters at the end of the list.
TEST_P(LdsTest, NacksNonTerminalHttpFilterAtEndOfList) {
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "non-terminal filter for config type grpc.testing"
                  ".client_only_http_filter is the last filter in the chain"));
}

// Test that we NACK terminal filters that are not at the end of the list.
TEST_P(LdsTest, NacksTerminalFilterBeforeEndOfList) {
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types.
TEST_P(LdsTest, IgnoresOptionalUnknownHttpFilterType) {
  CreateAndStartBackends(1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs.
TEST_P(LdsTest, IgnoresOptionalHttpFilterWithoutConfig) {
  CreateAndStartBackends(1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Filter grpc.testing.server_only_http_filter is not "
                           "supported on clients"));
}

// Test that we ignore optional HTTP filters unsupported on client-side.
TEST_P(LdsTest, IgnoresOptionalHttpFiltersNotSupportedOnClients) {
  CreateAndStartBackends(1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK non-zero xff_num_trusted_hops
TEST_P(LdsTest, RejectsNonZeroXffNumTrusterHops) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.set_xff_num_trusted_hops(1);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("'xff_num_trusted_hops' must be zero"));
}

// Test that we NACK non-empty original_ip_detection_extensions
TEST_P(LdsTest, RejectsNonEmptyOriginalIpDetectionExtensions) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.add_original_ip_detection_extensions();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("'original_ip_detection_extensions' must be empty"));
}

using LdsV2Test = XdsEnd2endTest;

// Tests that we ignore the HTTP filter list in v2.
// TODO(roth): The test framework is not set up to allow us to test
// the server sending v2 resources when the client requests v3, so this
// just tests a pure v2 setup.  When we have time, fix this.
TEST_P(LdsV2Test, IgnoresHttpFilters) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk();
}

using LdsRdsTest = XdsEnd2endTest;

MATCHER_P2(AdjustedClockInRange, t1, t2, "equals time") {
  gpr_cycle_counter cycle_now = gpr_get_cycle_counter();
  grpc_core::Timestamp cycle_time =
      grpc_core::Timestamp::FromCycleCounterRoundDown(cycle_now);
  grpc_core::Timestamp time_spec =
      grpc_core::Timestamp::FromTimespecRoundDown(gpr_now(GPR_CLOCK_MONOTONIC));
  grpc_core::Timestamp now = arg + (time_spec - cycle_time);
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(::testing::Ge(t1), now, result_listener);
  ok &= ::testing::ExplainMatchResult(::testing::Lt(t2), now, result_listener);
  return ok;
}

// Tests that LDS client should send an ACK upon correct LDS response (with
// inlined RDS result).
TEST_P(LdsRdsTest, Vanilla) {
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Make sure we actually used the RPC service for the right version of xDS.
  EXPECT_EQ(balancer_->ads_service()->seen_v2_client(), GetParam().use_v2());
  EXPECT_NE(balancer_->ads_service()->seen_v3_client(), GetParam().use_v2());
}

// Tests that we go into TRANSIENT_FAILURE if the Listener is removed.
TEST_P(LdsRdsTest, ListenerRemoved) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends();
  // Unset LDS resource.
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  // Wait for RPCs to start failing.
  do {
  } while (SendRpc(RpcOptions(), nullptr).ok());
  // Make sure RPCs are still failing.
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_times(1000));
  // Make sure we ACK'ed the update.
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client ACKs but fails if matching domain can't be found in
// the LDS response.
TEST_P(LdsRdsTest, NoMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure();
  // Do a bit of polling, to allow the ACK to get to the ADS server.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100));
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the virtual host with matching domain
// if multiple virtual hosts exist in the LDS response.
TEST_P(LdsRdsTest, ChooseMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should ignore route which has query_parameters.
TEST_P(LdsRdsTest, RouteMatchHasQueryParameters) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_match()->add_query_parameters();
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  const auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should ignore route which has a path
// prefix string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string with more than 2 slashes.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixExtraContent) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string "//".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixDoubleSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("//");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// but it's empty.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathEmptyPath) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("grpc.testing.EchoTest1Service/Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has too many slashes; for example, ends with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathTooManySlashes) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has only 1 slash: missing "/" between service and method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathOnlyOneSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service.Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing service.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingService) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("//Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingMethod) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Test that LDS client should reject route which has invalid path regex.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathRegex) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->mutable_safe_regex()->set_regex("a[z-a]");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "header matcher: Invalid range specifier specified: end cannot be "
          "smaller than start."));
}

// Tests that LDS client should choose the default route (with no matching
// specified) after unable to find a match with previous routes.
TEST_P(LdsRdsTest, XdsRoutingPathMatching) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(3);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(3);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Send Route Configuration.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(0, 1);
  CheckRpcSendOk(kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  // Change Route Configurations: new default cluster.
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(1, 2);
  CheckRpcSendOk(kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingClusterUpdateClustersWithPickingDelays) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Bring down the current backend: 0, this will delay route picking time,
  // resulting in un-committed RPCs.
  ShutdownBackend(0);
  // Send a RouteConfiguration with a default route that points to
  // backend 0.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args3({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
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
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   new_route_config);
  // Test grpc_timeout_header_max of 1.5 seconds applied
  grpc_core::Timestamp t0 = NowFromCycleCounter();
  grpc_core::Timestamp t1 =
      t0 + grpc_core::Duration::Seconds(kTimeoutGrpcTimeoutHeaderMaxSecond) +
      grpc_core::Duration::Milliseconds(kTimeoutMillis);
  grpc_core::Timestamp t2 =
      t0 + grpc_core::Duration::Seconds(kTimeoutMaxStreamDurationSecond) +
      grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions()
                               .set_rpc_service(SERVICE_ECHO1)
                               .set_rpc_method(METHOD_ECHO1)
                               .set_wait_for_ready(true)
                               .set_timeout_ms(grpc_core::Duration::Seconds(
                                                   kTimeoutApplicationSecond)
                                                   .millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test max_stream_duration of 2.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + grpc_core::Duration::Seconds(kTimeoutMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  t2 = t0 + grpc_core::Duration::Seconds(kTimeoutHttpMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions()
                               .set_rpc_service(SERVICE_ECHO2)
                               .set_rpc_method(METHOD_ECHO2)
                               .set_wait_for_ready(true)
                               .set_timeout_ms(grpc_core::Duration::Seconds(
                                                   kTimeoutApplicationSecond)
                                                   .millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test http_stream_duration of 3.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + grpc_core::Duration::Seconds(kTimeoutHttpMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  t2 = t0 + grpc_core::Duration::Seconds(kTimeoutApplicationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
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
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   new_route_config);
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
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   default_route_config_);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
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
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

TEST_P(LdsRdsTest, XdsRetryPolicyNumRetries) {
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* retry_policy =
      new_route_config.mutable_virtual_hosts(0)->mutable_retry_policy();
  retry_policy->set_retry_on(
      "cancelled,deadline-exceeded,internal,resource-exhausted,unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we retried the correct number of times on a supported status.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyLongBackOff) {
  CreateAndStartBackends(1);
  // Set num retries to 3, but due to longer back off, we expect only 1 retry
  // will take place.
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(1);
  // Set num retries to 3, but due to longer back off, we expect only 2 retry
  // will take place, while the 2nd one will obey the max backoff.
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("5xx");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
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
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // We expect no retry.
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyInvalidNumRetriesZero) {
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("deadline-exceeded");
  // Setting num_retries to zero is not valid.
  retry_policy->mutable_num_retries()->set_value(0);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy num_retries set to invalid value 0."));
}

TEST_P(LdsRdsTest, XdsRetryPolicyRetryBackOffMissingBaseInterval) {
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy RetryBackoff missing base interval."));
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatching) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialHeaderContentType) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 100;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
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
  SetRouteConfiguration(balancer_.get(), route_config);
  // Make sure the backend is up.
  WaitForAllBackends(0, 1);
  // Send RPCs.
  CheckRpcSendOk(kNumEchoRpcs);
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialCasesToIgnore) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const size_t kNumEchoRpcs = 100;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingRuntimeFractionMatching) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const double kErrorTolerance = 0.05;
  const size_t kRouteMatchNumerator = 25;
  const double kRouteMatchPercent =
      static_cast<double>(kRouteMatchNumerator) / 100;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kRouteMatchPercent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingUnmatchCases) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingChangeRoutesWithoutChangingClusters) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
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
  SetRouteConfiguration(balancer_.get(), route_config);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInVirtualHost) {
  CreateAndStartBackends(1);
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in VirtualHost.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in VirtualHost.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in Route.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in Route.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in Route.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

class CdsTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    logical_dns_cluster_resolver_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    InitClient();
    ChannelArguments args;
    args.SetPointerWithVtable(
        GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR,
        logical_dns_cluster_resolver_response_generator_.get(),
        &grpc_core::FakeResolverResponseGenerator::kChannelArgPointerVtable);
    ResetStub(/*failover_timeout_ms=*/0, &args);
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

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      logical_dns_cluster_resolver_response_generator_;
};

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  (void)SendRpc();
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(CdsTest, LogicalDNSClusterType) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(1);
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
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts());
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // RPCs should succeed.
  CheckRpcSendOk();
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingLoadAssignment) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "load_assignment not present for LOGICAL_DNS cluster"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 0"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeMultipleLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* load_assignment = cluster.mutable_load_assignment();
  load_assignment->add_endpoints();
  load_assignment->add_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 2"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeMissingEndpoints) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 0"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeMultipleEndpoints) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* locality = cluster.mutable_load_assignment()->add_endpoints();
  locality->add_lb_endpoints();
  locality->add_lb_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 2"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeEmptyEndpoint) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LbEndpoint endpoint field not set"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeEndpointMissingAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Endpoint address field not set"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeAddressMissingSocketAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Address socket_address field not set"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressHasResolverName) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LOGICAL_DNS clusters must NOT have a "
                                   "custom resolver name set"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressMissingAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("SocketAddress address field not set"));
}

TEST_P(CdsTest, LogicalDNSClusterTypeSocketAddressMissingPort) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("SocketAddress port_value field not set"));
}

TEST_P(CdsTest, AggregateClusterType) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for traffic to go to backend 0.
  WaitForBackend(0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  ShutdownBackend(0);
  WaitForBackend(1, WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  StartBackend(0);
  WaitForBackend(0);
}

TEST_P(CdsTest, AggregateClusterFallBackFromRingHashAtStartup) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()}},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends()},
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
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set up route with channel id hashing
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Verifying that we are using ring hash as only 1 endpoint is receiving all
  // the traffic.
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

TEST_P(CdsTest, AggregateClusterEdsToLogicalDns) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args1({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
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
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(1, 2));
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  ShutdownBackend(0);
  WaitForBackend(1, WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  StartBackend(0);
  WaitForBackend(0);
}

TEST_P(CdsTest, AggregateClusterLogicalDnsToEds) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
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
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kLogicalDNSClusterName);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(0, 1));
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  ShutdownBackend(0);
  WaitForBackend(1, WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  StartBackend(0);
  WaitForBackend(0);
}

// This test covers a bug seen in the wild where the
// xds_cluster_resolver policy's code to reuse child policy names did
// not correctly handle the case where the LOGICAL_DNS priority failed,
// thus returning a priority with no localities.  This caused the child
// name to be reused incorrectly, which triggered an assertion failure
// in the xds_cluster_impl policy caused by changing its cluster name.
TEST_P(CdsTest, AggregateClusterReconfigEdsWhileLogicalDnsChildFails) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate EDS resource with all unreachable endpoints.
  // - Priority 0: locality0
  // - Priority 1: locality1, locality2
  EdsResourceArgs args1({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality1", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
      {"locality2", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
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
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = absl::UnavailableError("injected error");
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // When an RPC fails, we know the channel has seen the update.
  CheckRpcSendFailure();
  // Send an EDS update that moves locality1 to priority 0.
  args1 = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
}

TEST_P(CdsTest, AggregateClusterMultipleClustersWithSameLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  CreateAndStartBackends(2);
  const char* kNewClusterName1 = "new_cluster_1";
  const char* kNewEdsServiceName1 = "new_eds_service_name_1";
  const char* kNewClusterName2 = "new_cluster_2";
  const char* kNewEdsServiceName2 = "new_eds_service_name_2";
  // Populate EDS resource for cluster 1 with unreachable endpoint.
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName1));
  // Populate CDS resource for cluster 1.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewClusterName1);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName1);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Populate EDS resource for cluster 2.
  args1 = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName2));
  // Populate CDS resource for cluster 2.
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewClusterName2);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName2);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName1);
  cluster_config.add_clusters(kNewClusterName2);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for channel to get the resources and get connected.
  WaitForBackend(0);
  // Send an EDS update for cluster 1 that reuses the locality name from
  // cluster 1 and points traffic to backend 1.
  args1 = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName1));
  WaitForBackend(1);
}

TEST_P(CdsTest, AggregateClusterRecursionLoop) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  const char* kNewClusterName = "new_cluster";
  // Populate EDS resource.
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populate Aggregate Cluster.
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName);
  cluster_config.add_clusters(kDefaultClusterName);  // Self-reference.
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // RPCs should fail with the right status.
  const Status status = SendRpc();
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr(absl::StrCat(
                  "aggregate cluster graph contains a loop for cluster ",
                  kDefaultClusterName)));
}

// Test that CDS client should send a NACK if cluster type is Logical DNS but
// the feature is not yet supported.
TEST_P(CdsTest, LogicalDNSClusterTypeDisabled) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

// Tests that CDS client should send a NACK if the cluster type in CDS
// response is unsupported.
TEST_P(CdsTest, UnsupportedClusterType) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

// Tests that the NACK for multiple bad resources includes both errors.
TEST_P(CdsTest, MultipleBadResources) {
  constexpr char kClusterName2[] = "cluster_name_2";
  constexpr char kClusterName3[] = "cluster_name_3";
  CreateAndStartBackends(1);
  // Add cluster with unsupported type.
  auto cluster = default_cluster_;
  cluster.set_name(kClusterName2);
  cluster.set_type(Cluster::STATIC);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Add second cluster with the same error.
  cluster.set_name(kClusterName3);
  balancer_->ads_service()->SetCdsResource(cluster);
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
  SetRouteConfiguration(balancer_.get(), route_config);
  // Add EDS resource.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send RPC.
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Check that everything works.
  CheckRpcSendOk();
  // Now send an update changing the Cluster to be invalid.
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(StatusCode::OK);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::ContainsRegex(absl::StrCat(
                  kDefaultClusterName,
                  ": validation error.*DiscoveryType is not valid")));
  CheckRpcSendOk();
}

// Tests that CDS client should send a NACK if the eds_config in CDS response
// is other than ADS or SELF.
TEST_P(CdsTest, EdsConfigSourceDoesNotSpecifyAdsOrSelf) {
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->set_path(
      "/foo/bar");
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("EDS ConfigSource is not ADS or SELF."));
}

// Tests that CDS client accepts an eds_config of type ADS.
TEST_P(CdsTest, AcceptsEdsConfigSourceOfTypeAds) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_ads();
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that CDS client should send a NACK if the lb_policy in CDS response
// is other than ROUND_ROBIN.
TEST_P(CdsTest, WrongLbPolicy) {
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::LEAST_REQUEST);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LB policy is not supported."));
}

// Tests that CDS client should send a NACK if the lrs_server in CDS response
// is other than SELF.
TEST_P(CdsTest, WrongLrsServer) {
  auto cluster = default_cluster_;
  cluster.mutable_lrs_server()->mutable_ads();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LRS ConfigSource is not self."));
}

// Tests that ring hash policy that hashes using channel id ensures all RPCs
// to go 1 particular backend.
TEST_P(CdsTest, RingHashChannelIdHashing) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  hash_policy->set_terminal(true);
  auto* hash_policy2 = route->mutable_route()->add_hash_policy();
  hash_policy2->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(cluster);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0",
                         {CreateEndpoint(0, HealthStatus::UNKNOWN, 1),
                          CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args(
      {{"locality0", {CreateEndpoint(0, HealthStatus::UNKNOWN, 1)}, 1},
       {"locality1", {CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}, 2}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("fixed_string");
  hash_policy->set_terminal(true);
  auto* hash_policy_to_be_ignored = route->mutable_route()->add_hash_policy();
  hash_policy_to_be_ignored->mutable_header()->set_header_name("random_string");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  CheckRpcSendOk();
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(false));
}

// Test that when the first pick is down leading to a transient failure, we
// will move on to the next ring hash entry.
TEST_P(CdsTest, RingHashTransientFailureCheckNextOne) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  const int unused_port = grpc_pick_unused_port_or_die();
  endpoints.emplace_back(unused_port);
  endpoints.emplace_back(backends_[0]->port());
  EdsResourceArgs args({{"locality0", std::move(endpoints)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash",
       CreateMetadataValueThatHashesToBackendPort(unused_port)}};
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  WaitForBackend(0, WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(100, rpc_options);
}

// Test that when a backend goes down, we will move on to the next subchannel
// (with a lower priority).  When the backend comes back up, traffic will move
// back.
TEST_P(CdsTest, RingHashSwitchToLowerPrioirtyAndThenBack) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args(
      {{"locality0", {MakeNonExistantEndpoint(), CreateEndpoint(0)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  ShutdownBackend(0);
  CheckRpcSendFailure(CheckRpcSendFailureOptions().set_rpc_options(
      RpcOptions().set_metadata(std::move(metadata))));
  StartBackend(0);
  // Ensure we are actively connecting without any traffic.
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
}

// Test that when all backends are down and then up, we may pick a TF backend
// and we will then jump to ready backend.
TEST_P(CdsTest, RingHashTransientFailureSkipToAvailableReady) {
  CreateAndStartBackends(2);
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Make sure we include some unused ports to fill the ring.
  EdsResourceArgs args({
      {"locality0",
       {CreateEndpoint(0), CreateEndpoint(1), MakeNonExistantEndpoint(),
        MakeNonExistantEndpoint()}},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->set_hash_function(
      Cluster::RingHashLbConfig::MURMUR_HASH_2);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("ring hash lb config has invalid hash function."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidMinimumRingSize) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      0);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "min_ring_size is not in the range of 1 to 8388608."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidMaxmumRingSize) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_maximum_ring_size()->set_value(
      8388609);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "max_ring_size is not in the range of 1 to 8388608."));
}

// Test we nack when ring hash policy has invalid ring size.
TEST_P(CdsTest, RingHashPolicyHasInvalidRingSizeMinGreaterThanMax) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  cluster.mutable_ring_hash_lb_config()->mutable_maximum_ring_size()->set_value(
      5000);
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      5001);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "min_ring_size cannot be greater than max_ring_size."));
}

class RlsTest : public XdsEnd2endTest {
 protected:
  class RlsServerThread : public ServerThread {
   public:
    explicit RlsServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false),
          rls_service_(new RlsServiceImpl()) {}

    RlsServiceImpl* rls_service() { return rls_service_.get(); }

   private:
    const char* Type() override { return "Rls"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(rls_service_.get());
    }

    void StartAllServices() override { rls_service_->Start(); }

    void ShutdownAllServices() override { rls_service_->Shutdown(); }

    std::shared_ptr<RlsServiceImpl> rls_service_;
  };

  RlsTest() {
    rls_server_ = absl::make_unique<RlsServerThread>(this);
    rls_server_->Start();
  }

  void TearDown() override {
    rls_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  std::unique_ptr<RlsServerThread> rls_server_;
};

TEST_P(RlsTest, XdsRoutingClusterSpecifierPlugin) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Prepare the RLSLookupConfig and configure all the keys; change route
  // configurations to use cluster specifier plugin.
  rls_server_->rls_service()->SetResponse(
      BuildRlsRequest({{kRlsTestKey, kRlsTestValue},
                       {kRlsHostKey, kServerName},
                       {kRlsServiceKey, kRlsServiceValue},
                       {kRlsMethodKey, kRlsMethodValue},
                       {kRlsConstantKey, kRlsConstantValue}}),
      BuildRlsResponse({kNewClusterName}));
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  header->add_names("key2");
  auto* extra_keys = key_builder->mutable_extra_keys();
  extra_keys->set_host(kRlsHostKey);
  extra_keys->set_service(kRlsServiceKey);
  extra_keys->set_method(kRlsMethodKey);
  (*key_builder->mutable_constant_keys())[kRlsConstantKey] = kRlsConstantValue;
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  auto rpc_options = RpcOptions().set_metadata({{kRlsTestKey1, kRlsTestValue}});
  WaitForAllBackends(1, 2, WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(kNumEchoRpcs, rpc_options);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[1]->backend_service()->request_count());
}

TEST_P(RlsTest, XdsRoutingClusterSpecifierPluginNacksUndefinedSpecifier) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration new_route_config = default_route_config_;
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  // Set Cluster Specifier Plugin to something that does not exist.
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(absl::StrCat(
                  "RouteAction cluster contains cluster specifier plugin "
                  "name not configured: ",
                  kRlsClusterSpecifierPluginInstanceName)));
}

TEST_P(RlsTest, XdsRoutingClusterSpecifierPluginNacksDuplicateSpecifier) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  // Prepare the RLSLookupConfig: change route configurations to use cluster
  // specifier plugin.
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* duplicate_plugin = new_route_config.add_cluster_specifier_plugins();
  duplicate_plugin->mutable_extension()->set_name(
      kRlsClusterSpecifierPluginInstanceName);
  duplicate_plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(absl::StrCat(
                  "Duplicated definition of cluster_specifier_plugin ",
                  kRlsClusterSpecifierPluginInstanceName)));
}

TEST_P(RlsTest,
       XdsRoutingClusterSpecifierPluginNacksUnknownSpecifierProtoNotOptional) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  // Prepare the RLSLookupConfig: change route configurations to use cluster
  // specifier plugin.
  RouteLookupConfig route_lookup_config;
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  // Instead of grpc.lookup.v1.RouteLookupClusterSpecifier, let's say we
  // mistakenly packed the inner RouteLookupConfig instead.
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(
      route_lookup_config);
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Unknown ClusterSpecifierPlugin type "
                                   "grpc.lookup.v1.RouteLookupConfig"));
}

TEST_P(RlsTest,
       XdsRoutingClusterSpecifierPluginIgnoreUnknownSpecifierProtoOptional) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Prepare the RLSLookupConfig: change route configurations to use cluster
  // specifier plugin.
  RouteLookupConfig route_lookup_config;
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  // Instead of grpc.lookup.v1.RouteLookupClusterSpecifier, let's say we
  // mistakenly packed the inner RouteLookupConfig instead.
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(
      route_lookup_config);
  plugin->set_is_optional(true);
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we ignore the cluster specifier plugin and send traffic according to
  // the default route.
  WaitForAllBackends();
}

TEST_P(RlsTest, XdsRoutingRlsClusterSpecifierPluginNacksRequiredMatch) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  // Prepare the RLSLookupConfig and configure all the keys; add required_match
  // field which should not be there.
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  header->set_required_match(true);
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("field:requiredMatch error:must not be present"));
}

TEST_P(RlsTest, XdsRoutingClusterSpecifierPluginDisabled) {
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Prepare the RLSLookupConfig and configure all the keys; change route
  // configurations to use cluster specifier plugin.
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we ignore the cluster specifier plugin and send traffic according to
  // the default route.
  auto rpc_options = RpcOptions().set_metadata({{kRlsTestKey1, kRlsTestValue}});
  WaitForAllBackends(0, 1, WaitForBackendOptions(), rpc_options);
}

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
    CreateAndStartBackends(1);
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

  void TearDown() override {
    g_fake1_cert_data_map = nullptr;
    g_fake2_cert_data_map = nullptr;
    XdsEnd2endTest::TearDown();
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
    balancer_->ads_service()->SetCdsResource(cluster);
    // The updates might take time to have an effect, so use a retry loop.
    constexpr int kRetryCount = 100;
    int num_tries = 0;
    for (; num_tries < kRetryCount; num_tries++) {
      // Restart the servers to force a reconnection so that previously
      // connected subchannels are not used for the RPC.
      ShutdownBackend(0);
      StartBackend(0);
      if (test_expects_failure) {
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "Unrecognized transport socket: unknown_transport_socket"));
}

TEST_P(XdsSecurityTest,
       TLSConfigurationWithoutValidationContextCertificateProviderInstance) {
  auto cluster = default_cluster_;
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  balancer_->ads_service()->SetCdsResource(cluster);
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
  balancer_->ads_service()->SetCdsResource(cluster);
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
  void SetUp() override {
    XdsEnd2endTest::SetUp();
    CreateBackends(1, /*xds_enabled=*/true);
    EdsResourceArgs args({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    });
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  balancer_->ads_service()->SetLdsResource(listener);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Listener has neither address nor ApiListener"));
}

TEST_P(XdsEnabledServerTest, BadLdsUpdateBothApiListenerAndAddress) {
  Listener listener = default_server_listener_;
  listener.mutable_api_listener();
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Listener has both address and ApiListener"));
}

TEST_P(XdsEnabledServerTest, NacksNonZeroXffNumTrusterHops) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.set_xff_num_trusted_hops(1);
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("'xff_num_trusted_hops' must be zero"));
}

TEST_P(XdsEnabledServerTest, NacksNonEmptyOriginalIpDetectionExtensions) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.add_original_ip_detection_extensions();
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("'original_ip_detection_extensions' must be empty"));
}

TEST_P(XdsEnabledServerTest, UnsupportedL4Filter) {
  Listener listener = default_server_listener_;
  listener.mutable_default_filter_chain()->clear_filters();
  listener.mutable_default_filter_chain()->add_filters()->mutable_typed_config()->PackFrom(default_listener_ /* any proto object other than HttpConnectionManager */);
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Unsupported filter type"));
}

TEST_P(XdsEnabledServerTest, NacksEmptyHttpFilterList) {
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  http_connection_manager.clear_http_filters();
  ServerHcmAccessor().Pack(http_connection_manager, &listener);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  WaitForBackend(0);
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Verify that a mismatch of listening address results in "not serving"
// status.
TEST_P(XdsEnabledServerTest, ListenerAddressMismatch) {
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

TEST_P(XdsEnabledServerTest, UseOriginalDstNotSupported) {
  Listener listener = default_server_listener_;
  listener.mutable_use_original_dst()->set_value(true);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Field \'use_original_dst\' is not supported."));
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

  void TearDown() override {
    g_fake1_cert_data_map = nullptr;
    g_fake2_cert_data_map = nullptr;
    XdsEnd2endTest::TearDown();
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

TEST_P(XdsServerSecurityTest, UnknownTransportSocket) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.mutable_default_filter_chain();
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("unknown_transport_socket");
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("match_subject_alt_names not supported on servers"));
}

TEST_P(XdsServerSecurityTest, UnknownIdentityCertificateProvider) {
  SetLdsUpdate("", "", "unknown", "", false);
  SendRpc([this]() { return CreateTlsChannel(); }, {}, {},
          true /* test_expects_failure */);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "Unrecognized certificate provider instance name: unknown"));
}

TEST_P(XdsServerSecurityTest, UnknownRootCertificateProvider) {
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  SetLdsUpdate("unknown", "", "fake_plugin1", "", false);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
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
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
      ServerHcmAccessor().Unpack(listener));
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
  balancer_->ads_service()->SetLdsResource(
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  if (ipv6_only_) {
    EXPECT_THAT(
        response_state->error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{prefix_ranges={{address_prefix=[::]:0, prefix_len=16}, "
            "{address_prefix=[::]:0, prefix_len=32}}}"));
  } else {
    EXPECT_THAT(
        response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  if (ipv6_only_) {
    EXPECT_THAT(
        response_state->error_message,
        ::testing::HasSubstr(
            "Duplicate matching rules detected when adding filter chain: "
            "{source_prefix_ranges={{address_prefix=[::]:0, prefix_len=16}, "
            "{address_prefix=[::]:0, prefix_len=32}}}"));
  } else {
    EXPECT_THAT(
        response_state->error_message,
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
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  backends_[0]->Start();
  const auto response_state = WaitForLdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Duplicate matching rules detected when adding "
                           "filter chain: {source_ports={8080}}"));
}

class XdsServerRdsTest : public XdsEnabledServerStatusNotificationTest {
 protected:
  XdsServerRdsTest() : env_var_("GRPC_XDS_EXPERIMENTAL_RBAC") {}

  ScopedExperimentalEnvVar env_var_;
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
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      route_config);
  backends_[0]->Start();
  const auto response_state = WaitForRouteConfigNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Invalid domain pattern \"\""));
}

TEST_P(XdsServerRdsTest, NacksEmptyDomainsList) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()->at(0).clear_domains();
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      route_config);
  backends_[0]->Start();
  const auto response_state = WaitForRouteConfigNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("VirtualHost has no domains"));
}

TEST_P(XdsServerRdsTest, NacksEmptyRoutesList) {
  RouteConfiguration route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts()->at(0).clear_routes();
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      route_config);
  backends_[0]->Start();
  const auto response_state = WaitForRouteConfigNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
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
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      route_config);
  backends_[0]->Start();
  const auto response_state = WaitForRouteConfigNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Match can't be null"));
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
  rules->set_action(envoy::config::rbac::v3::RBAC_Action_LOG);
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // A Log action is identical to no rbac policy being configured.
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {});
}

using XdsRbacNackTest = XdsRbacTest;

TEST_P(XdsRbacNackTest, NacksSchemePrincipalHeader) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(envoy::config::rbac::v3::RBAC_Action_ALLOW);
  Policy policy;
  auto* header = policy.add_principals()->mutable_header();
  header->set_name(":scheme");
  header->set_exact_match("http");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  if (GetParam().enable_rds_testing() &&
      GetParam().filter_config_setup() ==
          XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute) {
    const auto response_state = WaitForRdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("':scheme' not allowed in header"));
  } else {
    const auto response_state = WaitForLdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("':scheme' not allowed in header"));
  }
}

TEST_P(XdsRbacNackTest, NacksGrpcPrefixedPrincipalHeaders) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(envoy::config::rbac::v3::RBAC_Action_ALLOW);
  Policy policy;
  auto* header = policy.add_principals()->mutable_header();
  header->set_name("grpc-status");
  header->set_exact_match("0");
  policy.add_permissions()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  if (GetParam().enable_rds_testing() &&
      GetParam().filter_config_setup() ==
          XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute) {
    const auto response_state = WaitForRdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("'grpc-' prefixes not allowed in header"));
  } else {
    const auto response_state = WaitForLdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("'grpc-' prefixes not allowed in header"));
  }
}

TEST_P(XdsRbacNackTest, NacksSchemePermissionHeader) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(envoy::config::rbac::v3::RBAC_Action_ALLOW);
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":scheme");
  header->set_exact_match("http");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  if (GetParam().enable_rds_testing() &&
      GetParam().filter_config_setup() ==
          XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute) {
    const auto response_state = WaitForRdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("':scheme' not allowed in header"));
  } else {
    const auto response_state = WaitForLdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("':scheme' not allowed in header"));
  }
}

TEST_P(XdsRbacNackTest, NacksGrpcPrefixedPermissionHeaders) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(envoy::config::rbac::v3::RBAC_Action_ALLOW);
  Policy policy;
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name("grpc-status");
  header->set_exact_match("0");
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["policy"] = policy;
  SetServerRbacPolicy(rbac);
  backends_[0]->Start();
  if (GetParam().enable_rds_testing() &&
      GetParam().filter_config_setup() ==
          XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute) {
    const auto response_state = WaitForRdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("'grpc-' prefixes not allowed in header"));
  } else {
    const auto response_state = WaitForLdsNack();
    ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
    EXPECT_THAT(response_state->error_message,
                ::testing::HasSubstr("'grpc-' prefixes not allowed in header"));
  }
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // TODO(yashykt): When we start supporting GET/PUT requests in the future,
  // this should be modified to test that they are NOT accepted with this rule.
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // TODO(yashykt): When we start supporting PUT requests in the future, this
  // should be modified to test that they are accepted with this rule.
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // All RPCs use POST method by default
  SendRpc([this]() { return CreateInsecureChannel(); }, {}, {},
          /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_DENY,
          grpc::StatusCode::PERMISSION_DENIED);
  // TODO(yashykt): When we start supporting GET/PUT requests in the future,
  // this should be modified to test that they are NOT accepted with this rule.
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
  backends_[0]->Start();
  backends_[0]->notifier()->WaitOnServingStatusChange(
      absl::StrCat(ipv6_only_ ? "[::1]:" : "127.0.0.1:", backends_[0]->port()),
      grpc::StatusCode::OK);
  // Test that an RPC with a POST method gets rejected
  SendRpc(
      [this]() { return CreateInsecureChannel(); }, {}, {},
      /*test_expects_failure=*/GetParam().rbac_action() == RBAC_Action_ALLOW,
      grpc::StatusCode::PERMISSION_DENIED);
  // TODO(yashykt): When we start supporting PUT requests in the future, this
  // should be modified to test that they are accepted with this rule.
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
  FakeCertificateProvider::CertDataMap fake1_cert_map = {
      {"", {root_cert_, identity_pair_}}};
  g_fake1_cert_data_map = &fake1_cert_map;
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

using EdsTest = XdsEnd2endTest;

// Tests that EDS client should send a NACK if the EDS update contains
// sparse priorities.
TEST_P(EdsTest, NacksSparsePriorityList) {
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForEdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("sparse priority list"));
}

// Tests that EDS client should send a NACK if the EDS update contains
// multiple instances of the same locality in the same priority.
TEST_P(EdsTest, NacksDuplicateLocalityInSamePriority) {
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForEdsNack();
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "duplicate locality {region=\"xds_default_locality_region\", "
                  "zone=\"xds_default_locality_zone\", sub_zone=\"locality0\"} "
                  "found in priority 0"));
}

// In most of our tests, we use different names for different resource
// types, to make sure that there are no cut-and-paste errors in the code
// that cause us to look at data for the wrong resource type.  So we add
// this test to make sure that the EDS resource name defaults to the
// cluster name if not specified in the CDS resource.
TEST_P(EdsTest, EdsServiceNameDefaultsToClusterName) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kDefaultClusterName));
  Cluster cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->clear_service_name();
  balancer_->ads_service()->SetCdsResource(cluster);
  CheckRpcSendOk();
}

class TimeoutTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    InitClient(BootstrapBuilder(), /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/500);
  }
};

TEST_P(TimeoutTest, LdsServerIgnoresRequest) {
  balancer_->ads_service()->IgnoreResourceType(kLdsTypeUrl);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, LdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, LdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(),
            XdsTestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  // Create second channel for a new server name.
  // This should fail because there is no LDS resource for this server name.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, "new-server.example.com");
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
  balancer_->ads_service()->IgnoreResourceType(kRdsTypeUrl);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, RdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kRdsTypeUrl,
                                          kDefaultRouteConfigurationName);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, RdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(),
            XdsTestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Add listener for 2nd channel, but no RDS resource.
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name("rds_resource_does_not_exist");
  rds->mutable_config_source()->mutable_self();
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  WaitForAllBackends();
  // Create second channel for a new server name.
  // This should fail because the LDS resource points to a non-existent RDS
  // resource.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
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
  balancer_->ads_service()->IgnoreResourceType(kCdsTypeUrl);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, CdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, CdsSecondResourceNotPresentInRequest) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  // Change route config to point to non-existing cluster.
  const char* kNewClusterName = "new_cluster_name";
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(route_config);
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
  balancer_->ads_service()->IgnoreResourceType(kEdsTypeUrl);
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, EdsResourceNotPresentInRequest) {
  // No need to remove EDS resource, since the test suite does not add it
  // by default.
  CheckRpcSendFailure();
}

TEST_P(TimeoutTest, EdsSecondResourceNotPresentInRequest) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
  // New cluster that points to a non-existant EDS resource.
  const char* kNewClusterName = "new_cluster_name";
  Cluster cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(
      "eds_service_name_does_not_exist");
  balancer_->ads_service()->SetCdsResource(cluster);
  // Now add a route pointing to the new cluster.
  RouteConfiguration route_config = default_route_config_;
  auto* route = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  *route_config.mutable_virtual_hosts(0)->add_routes() = *route;
  route->mutable_match()->set_path("/grpc.testing.EchoTestService/Echo1");
  route->mutable_route()->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(route_config);
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

using LocalityMapTest = XdsEnd2endTest;

// Tests that the localities in a locality map are picked according to their
// weights.
TEST_P(LocalityMapTest, WeightedRoundRobin) {
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
  const size_t kNumRpcs = 5000;
  // EDS response contains 2 localities, one with no endpoints.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
      {"locality1", {}},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for both backends to be ready.
  WaitForAllBackends();
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(kNumRpcs);
  // All traffic should go to the reachable locality.
  EXPECT_EQ(backends_[0]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[1]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
}

// EDS update with no localities.
TEST_P(LocalityMapTest, NoLocalities) {
  balancer_->ads_service()->SetEdsResource(BuildEdsResource({}));
  Status status = SendRpc();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
}

// Tests that the locality map can work properly even when it contains a large
// number of localities.
TEST_P(LocalityMapTest, StressTest) {
  CreateAndStartBackends(2);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until backend 0 is ready.
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  // The second ADS response contains 1 locality, which contains backend 1.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until backend 1 is ready.
  WaitForBackend(1);
}

// Tests that the localities in a locality map are picked correctly after
// update (addition, modification, deletion).
TEST_P(LocalityMapTest, UpdateMap) {
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(2);
  // Initial EDS update has backend 0.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for the first backend to be ready.
  WaitForBackend(0);
  // Send EDS update that replaces the locality and switches to backend 1.
  args = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When the client sees the update, RPCs should start going to backend 1.
  // No RPCs should fail during this change.
  WaitForBackend(1);
}

TEST_P(LocalityMapTest, ConsistentWeightedTargetUpdates) {
  CreateAndStartBackends(4);
  // Initial update has two localities.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(1, 2)},
      {"locality1", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(1, 3);
  // Next update removes locality1.
  // Also add backend 0 to locality0, so that we can tell when the
  // update has been seen.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(0);
  // Next update re-adds locality1.
  // Also add backend 3 to locality1, so that we can tell when the
  // update has been seen.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 2)},
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(3);
}

class FailoverTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    XdsEnd2endTest::SetUp();
    ResetStub(/*failover_timeout_ms=*/500);
  }
};

// Localities with the highest priority are used when multiple priority exist.
TEST_P(FailoverTest, ChooseHighestPriority) {
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(3, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose priority with no endpoints.
TEST_P(FailoverTest, DoesNotUsePriorityWithNoEndpoints) {
  CreateAndStartBackends(3);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", {}, kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 1; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose locality with no endpoints.
TEST_P(FailoverTest, DoesNotUseLocalityWithNoEndpoints) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({
      {"locality0", {}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(), kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for all backends to be used.
  WaitForAllBackends();
}

// If the higher priority localities are not reachable, failover to the
// highest priority among the rest.
TEST_P(FailoverTest, Failover) {
  CreateAndStartBackends(2);
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       3},
      {"locality3", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(0, WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
}

// If a locality with higher priority than the current one becomes ready,
// switch to it.
TEST_P(FailoverTest, SwitchBackToHigherPriority) {
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(3);
  ShutdownBackend(3);
  ShutdownBackend(0);
  WaitForBackend(
      1, WaitForBackendOptions().set_reset_counters(false).set_allow_failures(
             true));
  for (size_t i = 0; i < backends_.size(); ++i) {
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
  CreateAndStartBackends(2);
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality1", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendFailure();
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
}

// Tests that after the localities' priorities are updated, we still choose
// the highest READY priority with the updated localities.
TEST_P(FailoverTest, UpdatePriority) {
  CreateAndStartBackends(4);
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(3, WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[2]->backend_service()->request_count());
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(1);
  CheckRpcSendOk(kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[1]->backend_service()->request_count());
}

// Moves all localities in the current priority to a higher priority.
TEST_P(FailoverTest, MoveAllLocalitiesInCurrentPriorityToHigherPriority) {
  CreateAndStartBackends(3);
  auto non_existant_endpoint = MakeNonExistantEndpoint();
  // First update:
  // - Priority 0 is locality 0, containing an unreachable backend.
  // - Priority 1 is locality 1, containing backends 0 and 1.
  EdsResourceArgs args({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When we get the first update, all backends in priority 0 are down,
  // so we will create priority 1.  Backends 0 and 1 should have traffic,
  // but backend 2 should not.
  WaitForAllBackends(0, 2, WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0UL, backends_[2]->backend_service()->request_count());
  // Second update:
  // - Priority 0 contains both localities 0 and 1.
  // - Priority 1 is not present.
  // - We add backend 2 to locality 1, just so we have a way to know
  //   when the update has been seen by the client.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 3), kDefaultLocalityWeight,
       0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When backend 2 gets traffic, we know the second update has been seen.
  WaitForBackend(2);
  // The xDS server got at least 1 response.
  EXPECT_TRUE(balancer_->ads_service()->eds_response_state().has_value());
}

// This tests a bug triggered by the xds_cluster_resolver policy reusing
// a child name for the priority policy when that child name was still
// present but deactivated.
TEST_P(FailoverTest, PriorityChildNameChurn) {
  CreateAndStartBackends(4);
  auto non_existant_endpoint = MakeNonExistantEndpoint();
  // Initial update:
  // - P0:locality0, child number 0 (unreachable)
  // - P1:locality1, child number 1
  // - P2:locality2, child number 2
  EdsResourceArgs args({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(0);
  // Next update:
  // - P0:locality0, child number 0 (still unreachable)
  // - P1:locality2, child number 2 (moved from P2 to P1)
  // - P2:locality3, child number 3 (new child)
  // Child number 1 will be deactivated.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(1);
  // Next update:
  // - P0:locality0, child number 0 (still unreachable)
  // - P1:locality4, child number 4 (new child number -- should not reuse #1)
  // - P2:locality3, child number 3
  // Child number 1 will be deactivated.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality4", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(3, WaitForBackendOptions().set_reset_counters(false));
  // P2 should not have gotten any traffic in this change.
  EXPECT_EQ(0UL, backends_[2]->backend_service()->request_count());
}

using DropTest = XdsEnd2endTest;

// Tests that RPCs are dropped according to the drop config.
TEST_P(DropTest, Vanilla) {
  CreateAndStartBackends(1);
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
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, "EDS-configured drop: ");
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that drop config is converted correctly from per hundred.
TEST_P(DropTest, DropPerHundred) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerHundredForLb = 10;
  const double kDropRateForLb = kDropPerHundredForLb / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerHundredForLb}};
  args.drop_denominator = FractionalPercent::HUNDRED;
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, "EDS-configured drop: ");
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop config is converted correctly from per ten thousand.
TEST_P(DropTest, DropPerTenThousand) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerTenThousandForLb = 1000;
  const double kDropRateForLb = kDropPerTenThousandForLb / 10000.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerTenThousandForLb}};
  args.drop_denominator = FractionalPercent::TEN_THOUSAND;
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, "EDS-configured drop: ");
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop is working correctly after update.
TEST_P(DropTest, Update) {
  CreateAndStartBackends(1);
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
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcsLbOnly RPCs and count the drops.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      kNumRpcsLbOnly, "EDS-configured drop: ");
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
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  num_drops = SendRpcsAndCountFailuresWithMessage(kNumRpcsBoth,
                                                  "EDS-configured drop: ");
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // The new drop rate should be roughly equal to the expectation.
  seen_drop_rate = static_cast<double>(num_drops) / kNumRpcsBoth;
  gpr_log(GPR_INFO, "Second batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that all the RPCs are dropped if any drop category drops 100%.
TEST_P(DropTest, DropAll) {
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 1000000;
  // The ADS response contains two drop categories.
  EdsResourceArgs args;
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and all of them are dropped.
  size_t num_drops =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, "EDS-configured drop: ");
  EXPECT_EQ(num_drops, kNumRpcs);
}

using ClientLoadReportingTest = XdsEnd2endTest;

// Tests that the load report received at the balancer is correct.
TEST_P(ClientLoadReportingTest, Vanilla) {
  CreateAndStartBackends(4);
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  size_t num_warmup_rpcs = WaitForAllBackends(
      0, 4, WaitForBackendOptions().set_reset_counters(false));
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * backends_.size());
  CheckRpcSendFailure(CheckRpcSendFailureOptions()
                          .set_times(kNumFailuresPerAddress * backends_.size())
                          .set_rpc_options(RpcOptions().set_server_fail(true)));
  const size_t total_successful_rpcs_sent =
      (kNumRpcsPerAddress * backends_.size()) + num_warmup_rpcs;
  const size_t total_failed_rpcs_sent =
      kNumFailuresPerAddress * backends_.size();
  // Check that the backends got the right number of requests.
  size_t total_rpcs_sent = 0;
  for (const auto& backend : backends_) {
    total_rpcs_sent += backend->backend_service()->request_count();
  }
  EXPECT_EQ(total_rpcs_sent,
            total_successful_rpcs_sent + total_failed_rpcs_sent);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(client_stats.cluster_name(), kDefaultClusterName);
  EXPECT_EQ(client_stats.eds_service_name(), kDefaultEdsServiceName);
  EXPECT_EQ(total_successful_rpcs_sent,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(total_rpcs_sent, client_stats.total_issued_requests());
  EXPECT_EQ(total_failed_rpcs_sent, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  ASSERT_THAT(
      client_stats.locality_stats(),
      ::testing::ElementsAre(::testing::Pair("locality0", ::testing::_),
                             ::testing::Pair("locality1", ::testing::_)));
  size_t num_successful_rpcs = 0;
  size_t num_failed_rpcs = 0;
  for (const auto& p : client_stats.locality_stats()) {
    EXPECT_EQ(p.second.total_requests_in_progress, 0U);
    EXPECT_EQ(
        p.second.total_issued_requests,
        p.second.total_successful_requests + p.second.total_error_requests);
    num_successful_rpcs += p.second.total_successful_requests;
    num_failed_rpcs += p.second.total_error_requests;
  }
  EXPECT_EQ(num_successful_rpcs, total_successful_rpcs_sent);
  EXPECT_EQ(num_failed_rpcs, total_failed_rpcs_sent);
  EXPECT_EQ(num_successful_rpcs + num_failed_rpcs, total_rpcs_sent);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests send_all_clusters.
TEST_P(ClientLoadReportingTest, SendAllClusters) {
  CreateAndStartBackends(2);
  balancer_->lrs_service()->set_send_all_clusters(true);
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  size_t num_warmup_rpcs = WaitForAllBackends();
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(kNumRpcsPerAddress * backends_.size());
  CheckRpcSendFailure(CheckRpcSendFailureOptions()
                          .set_times(kNumFailuresPerAddress * backends_.size())
                          .set_rpc_options(RpcOptions().set_server_fail(true)));
  // Check that each backend got the right number of requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress + kNumFailuresPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(kNumRpcsPerAddress * backends_.size() + num_warmup_rpcs,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ((kNumRpcsPerAddress + kNumFailuresPerAddress) * backends_.size() +
                num_warmup_rpcs,
            client_stats.total_issued_requests());
  EXPECT_EQ(kNumFailuresPerAddress * backends_.size(),
            client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that we don't include stats for clusters that are not requested
// by the LRS server.
TEST_P(ClientLoadReportingTest, HonorsClustersRequestedByLrsServer) {
  CreateAndStartBackends(1);
  balancer_->lrs_service()->set_cluster_names({"bogus"});
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  WaitForAllBackends();
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 0UL);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that if the balancer restarts, the client load report contains the
// stats before and after the restart correctly.
TEST_P(ClientLoadReportingTest, BalancerRestart) {
  CreateAndStartBackends(4);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends returned by the balancer are ready.
  size_t num_rpcs = WaitForAllBackends(0, 2);
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats client_stats = std::move(load_report.front());
  EXPECT_EQ(num_rpcs, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  // Shut down the balancer.
  balancer_->Shutdown();
  // We should continue using the last EDS response we received from the
  // balancer before it was shut down.
  // Note: We need to use WaitForAllBackends() here instead of just
  // CheckRpcSendOk(kNumBackendsFirstPass), because when the balancer
  // shuts down, the XdsClient will generate an error to the
  // ListenerWatcher, which will cause the xds resolver to send a
  // no-op update to the LB policy.  When this update gets down to the
  // round_robin child policy for the locality, it will generate a new
  // subchannel list, which resets the start index randomly.  So we need
  // to be a little more permissive here to avoid spurious failures.
  ResetBackendCounters();
  num_rpcs = WaitForAllBackends(0, 2);
  // Now restart the balancer, this time pointing to the new backends.
  balancer_->Start();
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(2, 4)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  num_rpcs += WaitForAllBackends(2, 4);
  // Send one RPC per backend.
  CheckRpcSendOk(2);
  num_rpcs += 2;
  // Check client stats.
  load_report = balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  client_stats = std::move(load_report.front());
  EXPECT_EQ(num_rpcs, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
}

// Tests load reporting when switching over from one cluster to another.
TEST_P(ClientLoadReportingTest, ChangeClusters) {
  CreateAndStartBackends(4);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  balancer_->lrs_service()->set_cluster_names(
      {kDefaultClusterName, kNewClusterName});
  // cluster kDefaultClusterName -> locality0 -> backends 0 and 1
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // cluster kNewClusterName -> locality1 -> backends 2 and 3
  EdsResourceArgs args2({
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName));
  // CDS resource for kNewClusterName.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Wait for all backends to come online.
  size_t num_rpcs = WaitForAllBackends(0, 2);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&ClientStats::cluster_name, kDefaultClusterName),
          ::testing::Property(&ClientStats::eds_service_name,
                              kDefaultEdsServiceName),
          ::testing::Property(
              &ClientStats::locality_stats,
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::AllOf(
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_successful_requests,
                                       num_rpcs),
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_requests_in_progress,
                                       0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_error_requests,
                          0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_issued_requests,
                          num_rpcs))))),
          ::testing::Property(&ClientStats::total_dropped_requests, 0UL))));
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Wait for all new backends to be used.
  num_rpcs = WaitForAllBackends(2, 4);
  // The load report received at the balancer should be correct.
  load_report = balancer_->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name,
                                  kDefaultClusterName),
              ::testing::Property(&ClientStats::eds_service_name,
                                  kDefaultEdsServiceName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality0",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Lt(num_rpcs)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              0UL),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_issued_requests,
                                           ::testing::Le(num_rpcs)))))),
              ::testing::Property(&ClientStats::total_dropped_requests, 0UL)),
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name, kNewClusterName),
              ::testing::Property(&ClientStats::eds_service_name,
                                  kNewEdsServiceName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality1",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Le(num_rpcs)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              0UL),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_issued_requests,
                                           ::testing::Le(num_rpcs)))))),
              ::testing::Property(&ClientStats::total_dropped_requests, 0UL))));
  size_t total_ok = 0;
  for (const ClientStats& client_stats : load_report) {
    total_ok += client_stats.total_successful_requests();
  }
  EXPECT_EQ(total_ok, num_rpcs);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that the drop stats are correctly reported by client load reporting.
TEST_P(ClientLoadReportingTest, DropStats) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kErrorTolerance = 0.05;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  const char kStatusMessageDropPrefix[] = "EDS-configured drop: ";
  // The ADS response contains two drop categories.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, kStatusMessageDropPrefix);
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
  // Check client stats.
  ClientStats client_stats;
  do {
    std::vector<ClientStats> load_reports =
        balancer_->lrs_service()->WaitForLoadReport();
    for (const auto& load_report : load_reports) {
      client_stats += load_report;
    }
  } while (client_stats.total_issued_requests() +
               client_stats.total_dropped_requests() <
           kNumRpcs);
  EXPECT_EQ(num_drops, client_stats.total_dropped_requests());
  EXPECT_THAT(static_cast<double>(client_stats.dropped_requests(kLbDropType)) /
                  kNumRpcs,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(client_stats.dropped_requests(kThrottleDropType)) /
          (kNumRpcs * (1 - kDropRateForLb)),
      ::testing::DoubleNear(kDropRateForThrottle, kErrorTolerance));
}

class FaultInjectionTest : public XdsEnd2endTest {
 public:
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
      case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute: {
        Listener listener = BuildListenerWithFaultInjection();
        RouteConfiguration route =
            BuildRouteConfigurationWithFaultInjection(http_fault);
        SetListenerAndRouteConfiguration(balancer_.get(), listener, route);
        break;
      }
      case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInListener: {
        Listener listener = BuildListenerWithFaultInjection(http_fault);
        SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                         default_route_config_);
      }
    };
  }
};

// Test to ensure the most basic fault injection config works.
TEST_P(FaultInjectionTest, XdsFaultInjectionAlwaysAbort) {
  const uint32_t kAbortPercentagePerHundred = 100;
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
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_, route);
  // Fire several RPCs, and expect all of them to be pass.
  CheckRpcSendOk(5, RpcOptions().set_wait_for_ready(true));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbort) {
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  size_t num_aborted =
      SendRpcsAndCountFailuresWithMessage(kNumRpcs, "Fault injected");
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbortViaHeaders) {
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentageCap = 100;
  const uint32_t kAbortPercentage = 50;
  const double kAbortRate = kAbortPercentage / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  size_t num_aborted = SendRpcsAndCountFailuresWithMessage(
      kNumRpcs, "Fault injected", RpcOptions().set_metadata(metadata));
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageDelay) {
  CreateAndStartBackends(1);
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 3000;
  const uint32_t kFixedDelaySeconds = 100;
  const uint32_t kDelayPercentagePerHundred = 50;
  const double kDelayRate = kDelayPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
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
  CreateAndStartBackends(1);
  const uint32_t kFixedDelayMilliseconds = 100000;
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 3000;
  const uint32_t kDelayPercentageCap = 100;
  const uint32_t kDelayPercentage = 50;
  const double kDelayRate = kDelayPercentage / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
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
  CreateAndStartBackends(1);
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  const uint32_t kConnectionTimeoutMilliseconds =
      10 * 1000;  // 10s should not reach
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
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
    EXPECT_GE(rpc.elapsed_time,
              grpc_core::Duration::Seconds(kFixedDelaySeconds));
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
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerMillion = 500000;
  const double kAbortRate = kAbortPercentagePerMillion / 1000000.0;
  const uint32_t kFixedDelaySeconds = 1;                // 1s
  const uint32_t kRpcTimeoutMilliseconds = 100 * 1000;  // 100s should not reach
  const uint32_t kConnectionTimeoutMilliseconds =
      10 * 1000;  // 10s should not reach
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
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
    EXPECT_GE(rpc.elapsed_time,
              grpc_core::Duration::Seconds(kFixedDelaySeconds));
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
  CreateAndStartBackends(1);
  const uint32_t kMaxFault = 10;
  const uint32_t kNumRpcs = 30;  // kNumRpcs should be bigger than kMaxFault
  const uint32_t kRpcTimeoutMs = 4000;     // 4 seconds
  const uint32_t kLongDelaySeconds = 100;  // 100 seconds
  const uint32_t kAlwaysDelayPercentage = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  // kRpcTimeoutMilliseconds is 10s should never be reached.
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 10000;
  const uint32_t kFixedDelaySeconds = 1;
  const uint32_t kDelayPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
  CreateAndStartBackends(1);
  const uint32_t kRpcTimeoutMilliseconds = grpc_test_slowdown_factor() * 500;
  const uint32_t kFixedDelaySeconds = 100;
  const uint32_t kDelayPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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

using BootstrapSourceTest = XdsEnd2endTest;

TEST_P(BootstrapSourceTest, Vanilla) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends();
}

#ifndef DISABLED_XDS_PROTO_IN_CC
class ClientStatusDiscoveryServiceTest : public XdsEnd2endTest {
 public:
  ClientStatusDiscoveryServiceTest() {
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
  // Server thread for CSDS server.
  class AdminServerThread : public ServerThread {
   public:
    explicit AdminServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj) {}

   private:
    const char* Type() override { return "Admin"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&csds_service_);
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    grpc::xds::experimental::ClientStatusDiscoveryService csds_service_;
  };

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
  CreateAndStartBackends(1);
  const size_t kNumRpcs = 5;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
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
              kLdsTypeUrl, kServerName, "1",
              UnpackListener(EqListener(kServerName, api_listener_matcher)),
              ClientResourceStatus::ACKED, ::testing::_),
          // Cluster
          EqGenericXdsConfig(kCdsTypeUrl, kDefaultClusterName, "1",
                             UnpackCluster(EqCluster(kDefaultClusterName)),
                             ClientResourceStatus::ACKED, ::testing::_),
          // ClusterLoadAssignment
          EqGenericXdsConfig(
              kEdsTypeUrl, kDefaultEdsServiceName, "1",
              UnpackClusterLoadAssignment(EqClusterLoadAssignment(
                  kDefaultEdsServiceName, backends_[0]->port(),
                  kDefaultLocalityWeight)),
              ClientResourceStatus::ACKED, ::testing::_),
      };
  // If RDS is enabled, add matcher for RDS resource.
  if (GetParam().enable_rds_testing()) {
    matchers.push_back(EqGenericXdsConfig(
        kRdsTypeUrl, kDefaultRouteConfigurationName, "1",
        UnpackRouteConfiguration(EqRouteConfiguration(
            kDefaultRouteConfigurationName, kDefaultClusterName)),
        ClientResourceStatus::ACKED, ::testing::_));
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
  CreateAndStartBackends(1);
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad Listener should be rejected.
  Listener listener;
  listener.set_name(kServerName);
  balancer_->ads_service()->SetLdsResource(listener);
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
            kLdsTypeUrl, kServerName, "1",
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
  CreateAndStartBackends(1);
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad route config will be rejected.
  RouteConfiguration route_config;
  route_config.set_name(kDefaultRouteConfigurationName);
  route_config.add_virtual_hosts();
  SetRouteConfiguration(balancer_.get(), route_config);
  // The old xDS configs should still be effective.
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    bool ok = false;
    if (GetParam().enable_rds_testing()) {
      ok = ::testing::Value(
          csds_response.config(0).generic_xds_configs(),
          ::testing::Contains(EqGenericXdsConfig(
              kRdsTypeUrl, kDefaultRouteConfigurationName, "1",
              UnpackRouteConfiguration(EqRouteConfiguration(
                  kDefaultRouteConfigurationName, kDefaultClusterName)),
              ClientResourceStatus::NACKED,
              EqUpdateFailureState(
                  ::testing::HasSubstr("VirtualHost has no domains"), "2"))));
    } else {
      ok = ::testing::Value(
          csds_response.config(0).generic_xds_configs(),
          ::testing::Contains(EqGenericXdsConfig(
              kLdsTypeUrl, kServerName, "1",
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
  CreateAndStartBackends(1);
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Listener without any route, will be rejected.
  Cluster cluster;
  cluster.set_name(kDefaultClusterName);
  balancer_->ads_service()->SetCdsResource(cluster);
  // The old xDS configs should still be effective.
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    // Check if error state is propagated
    bool ok = ::testing::Value(
        csds_response.config(0).generic_xds_configs(),
        ::testing::Contains(EqGenericXdsConfig(
            kCdsTypeUrl, kDefaultClusterName, "1",
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
  CreateAndStartBackends(1);
  int kFetchConfigRetries = 3;
  int kFetchIntervalMilliseconds = 200;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Ensure the xDS resolver has working configs.
  CheckRpcSendOk();
  // Bad endpoint config will be rejected.
  ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name(kDefaultEdsServiceName);
  auto* endpoints = cluster_load_assignment.add_endpoints();
  endpoints->mutable_load_balancing_weight()->set_value(1);
  auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
  endpoint->mutable_address()->mutable_socket_address()->set_port_value(1 << 1);
  balancer_->ads_service()->SetEdsResource(cluster_load_assignment);
  // The old xDS configs should still be effective.
  CheckRpcSendOk();
  for (int i = 0; i < kFetchConfigRetries; ++i) {
    auto csds_response = FetchCsdsResponse();
    // Check if error state is propagated
    bool ok = ::testing::Value(
        csds_response.config(0).generic_xds_configs(),
        ::testing::Contains(EqGenericXdsConfig(
            kEdsTypeUrl, kDefaultEdsServiceName, "1",
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
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::Contains(EqGenericXdsConfig(
                  kLdsTypeUrl, kServerName, ::testing::_, ::testing::_,
                  ClientResourceStatus::REQUESTED, ::testing::_)));
}

TEST_P(ClientStatusDiscoveryServiceTest, XdsConfigDumpClusterRequested) {
  int kTimeoutMillisecond = 1000;
  std::string kClusterName1 = "cluster-1";
  std::string kClusterName2 = "cluster-2";
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
  SetRouteConfiguration(balancer_.get(), route_config);
  // Try to get the configs plumb through
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::AllOf(
                  ::testing::Contains(EqGenericXdsConfig(
                      kCdsTypeUrl, kClusterName1, ::testing::_, ::testing::_,
                      ClientResourceStatus::REQUESTED, ::testing::_)),
                  ::testing::Contains(EqGenericXdsConfig(
                      kCdsTypeUrl, kClusterName2, ::testing::_, ::testing::_,
                      ClientResourceStatus::REQUESTED, ::testing::_))));
}

class CsdsShortAdsTimeoutTest : public ClientStatusDiscoveryServiceTest {
 protected:
  void SetUp() override {
    // Shorten the ADS subscription timeout to speed up the test run.
    InitClient(BootstrapBuilder(), /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/2000);
  }
};

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpListenerDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::StatusCode::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::Contains(EqGenericXdsConfig(
                  kLdsTypeUrl, kServerName, ::testing::_, ::testing::_,
                  ClientResourceStatus::DOES_NOT_EXIST, ::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpRouteConfigDoesNotExist) {
  if (!GetParam().enable_rds_testing()) return;
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  balancer_->ads_service()->UnsetResource(kRdsTypeUrl,
                                          kDefaultRouteConfigurationName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::StatusCode::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kRdsTypeUrl, kDefaultRouteConfigurationName, ::testing::_,
          ::testing::_, ClientResourceStatus::DOES_NOT_EXIST, ::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpClusterDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::StatusCode::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(csds_response.config(0).generic_xds_configs(),
              ::testing::Contains(EqGenericXdsConfig(
                  kCdsTypeUrl, kDefaultClusterName, ::testing::_, ::testing::_,
                  ClientResourceStatus::DOES_NOT_EXIST, ::testing::_)));
}

TEST_P(CsdsShortAdsTimeoutTest, XdsConfigDumpEndpointDoesNotExist) {
  int kTimeoutMillisecond = 1000000;  // 1000s wait for the transient failure.
  balancer_->ads_service()->UnsetResource(kEdsTypeUrl, kDefaultEdsServiceName);
  CheckRpcSendFailure(
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_timeout_ms(kTimeoutMillisecond))
          .set_expected_error_code(grpc::StatusCode::UNAVAILABLE));
  auto csds_response = FetchCsdsResponse();
  EXPECT_THAT(
      csds_response.config(0).generic_xds_configs(),
      ::testing::Contains(EqGenericXdsConfig(
          kEdsTypeUrl, kDefaultEdsServiceName, ::testing::_, ::testing::_,
          ClientResourceStatus::DOES_NOT_EXIST, ::testing::_)));
}

#endif  // DISABLED_XDS_PROTO_IN_CC

// Run both with and without load reporting.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, BasicTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Don't run with load reporting or v2 or RDS, since they are irrelevant to
// the tests.
INSTANTIATE_TEST_SUITE_P(XdsTest, SecureNamingTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// LDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);
INSTANTIATE_TEST_SUITE_P(XdsTest, LdsV2Test,
                         ::testing::Values(XdsTestType().set_use_v2()),
                         &XdsTestType::Name);

// LDS/RDS commmon tests depend on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, LdsRdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_rds_testing(),
                      // Also test with xDS v2.
                      XdsTestType().set_enable_rds_testing().set_use_v2()),
    &XdsTestType::Name);

// Rls tests depend on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, RlsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_rds_testing(),
                      // Also test with xDS v2.
                      XdsTestType().set_enable_rds_testing().set_use_v2()),
    &XdsTestType::Name);

// CDS depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, CdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

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
// Note that we are simply using the default fake credentials instead of xds
// credentials for NACK tests to avoid a mismatch between the client and the
// server's security settings when using the WaitForNack() infrastructure.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsRbacNackTest,
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

// EDS could be tested with or without XdsResolver, but the tests would
// be the same either way, so we test it only with XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, EdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Test initial resource timeouts for each resource type.
// Do this only for XdsResolver with RDS enabled, so that we can test
// all resource types.
// Run with V3 only, since the functionality is no different in V2.
// Run with bootstrap from env var so that multiple channels share the same
// XdsClient (needed for testing the timeout for the 2nd LDS and RDS resource).
INSTANTIATE_TEST_SUITE_P(
    XdsTest, TimeoutTest,
    ::testing::Values(
        XdsTestType().set_enable_rds_testing().set_bootstrap_source(
            XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

// XdsResolverOnlyTest depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsResolverOnlyTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Runs with bootstrap from env var, so that there's a global XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, GlobalXdsClientTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationDisabledTest,
    ::testing::Values(XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationLoadReportingTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting()
            .set_enable_rds_testing()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, LocalityMapTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);
INSTANTIATE_TEST_SUITE_P(
    XdsTest, FailoverTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);
INSTANTIATE_TEST_SUITE_P(
    XdsTest, DropTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Load reporting tests are not run with load reporting disabled.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientLoadReportingTest,
    ::testing::Values(XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, FaultInjectionTest,
    ::testing::Values(
        XdsTestType(), XdsTestType().set_enable_rds_testing(),
        XdsTestType().set_filter_config_setup(
            XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute),
        XdsTestType().set_enable_rds_testing().set_filter_config_setup(
            XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute)),
    &XdsTestType::Name);

INSTANTIATE_TEST_SUITE_P(
    XdsTest, BootstrapSourceTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromFile)),
    &XdsTestType::Name);

#ifndef DISABLED_XDS_PROTO_IN_CC
// Run CSDS tests with RDS enabled and disabled.
// These need to run with the bootstrap from an env var instead of from
// a channel arg, since there needs to be a global XdsClient instance.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientStatusDiscoveryServiceTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_csds_streaming(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()
            .set_use_csds_streaming()),
    &XdsTestType::Name);
INSTANTIATE_TEST_SUITE_P(
    XdsTest, CsdsShortAdsTimeoutTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_use_csds_streaming(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()
            .set_use_csds_streaming()),
    &XdsTestType::Name);
#endif  // DISABLED_XDS_PROTO_IN_CC

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
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
