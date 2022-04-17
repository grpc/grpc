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
#include "absl/synchronization/mutex.h"
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
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
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
#include "test/cpp/end2end/xds/no_op_http_filter.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::cluster::v3::CircuitBreakers;
using ::envoy::config::cluster::v3::RoutingPriority;
using ::envoy::config::endpoint::v3::HealthStatus;
using ::envoy::config::listener::v3::FilterChainMatch;
using ::envoy::config::rbac::v3::Policy;
using ::envoy::config::rbac::v3::RBAC_Action;
using ::envoy::config::rbac::v3::RBAC_Action_ALLOW;
using ::envoy::config::rbac::v3::RBAC_Action_DENY;
using ::envoy::config::rbac::v3::RBAC_Action_LOG;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::rbac::v3::RBACPerRoute;
using ::envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;
using ::envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using ::envoy::type::matcher::v3::StringMatcher;
using ::envoy::type::v3::FractionalPercent;

using ClientStats = LrsServiceImpl::ClientStats;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::IdentityKeyCertPair;
using ::grpc::experimental::StaticDataCertificateProvider;

constexpr char kLbDropType[] = "lb";
constexpr char kThrottleDropType[] = "throttle";

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
      absl::MutexLock lock(&mu_);
      return cert_data_map_;
    }

    void Set(CertDataMap data) {
      absl::MutexLock lock(&mu_);
      cert_data_map_ = std::move(data);
    }

   private:
    absl::Mutex mu_;
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

using CdsTest = XdsEnd2endTest;

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  (void)SendRpc();
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
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

// Tests round robin is not implacted by the endpoint weight, and that the
// localities in a locality map are picked according to their weights.
TEST_P(CdsTest, EndpointWeightDoesNotImpactWeightedRoundRobin) {
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
}

TEST_P(XdsSecurityTest,
       NacksCertificateValidationContextWithVerifyCertificateSpki) {
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
  Status status = SendRpc();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
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
  WaitForBackend(0, WaitForBackendOptions().set_allow_failures(true));
  Status status = SendRpc();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
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

  void TearDown() override { XdsEnd2endTest::TearDown(); }

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
  g_fake1_cert_data_map->Set({{"", {root_cert_, identity_pair_}}});
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

// Run both with and without load reporting.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, BasicTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
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

// XdsResolverOnlyTest depends on XdsResolver.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsResolverOnlyTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
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

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
  // Allow testing PUT requests.
  grpc_core::
      InternalOnlyDoNotUseUnlessYouHavePermissionFromGrpcTeamAllowBrokenPutRequests();
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  gpr_setenv("grpc_cfstream", "0");
#endif
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_1;
  grpc::testing::g_fake1_cert_data_map = &cert_data_map_1;
  grpc_core::CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<grpc::testing::FakeCertificateProviderFactory>(
          "fake1", grpc::testing::g_fake1_cert_data_map));
  grpc::testing::FakeCertificateProvider::CertDataMapWrapper cert_data_map_2;
  grpc::testing::g_fake2_cert_data_map = &cert_data_map_2;
  grpc_core::CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<grpc::testing::FakeCertificateProviderFactory>(
          "fake2", grpc::testing::g_fake2_cert_data_map));
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
