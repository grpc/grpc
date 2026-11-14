//
//
// Copyright 2022 gRPC authors.
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
//

#include "src/core/xds/grpc/xds_lb_policy_registry.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/load_balancing_policies/autosharding/v3/autosharding.pb.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.pb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/util/crash.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "xds/type/v3/typed_struct.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace grpc_core {
namespace testing {
namespace {

using LoadBalancingPolicyProto =
    ::envoy::config::cluster::v3::LoadBalancingPolicy;
using ::envoy::extensions::grpc_service::call_credentials::access_token::v3::
    AccessTokenCredentials;
using ::envoy::extensions::grpc_service::channel_credentials::insecure::v3::
    InsecureCredentials;
using ::envoy::extensions::load_balancing_policies::autosharding::v3::
    Autosharding;
using ::envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using ::envoy::extensions::load_balancing_policies::pick_first::v3::PickFirst;
using ::envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;
using ::xds::type::v3::TypedStruct;

// Uses XdsLbPolicyRegistry to convert
// envoy::config::cluster::v3::LoadBalancingPolicy to gRPC's JSON form.
absl::StatusOr<std::string> ConvertXdsPolicy(
    const LoadBalancingPolicyProto& policy) {
  std::string serialized_policy = policy.SerializeAsString();
  upb::Arena arena;
  upb::DefPool def_pool;
  XdsResourceType::DecodeContext context = {nullptr, GrpcXdsServer(),
                                            def_pool.ptr(), arena.ptr()};
  auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
      serialized_policy.data(), serialized_policy.size(), arena.ptr());
  ValidationErrors errors;
  ValidationErrors::ScopedField field(&errors, ".load_balancing_policy");
  auto registry = GrpcXdsBootstrapBuilder::CreateXdsLbPolicyRegistry();
  XdsGrpcServiceMap grpc_services;
  auto config = registry.ConvertXdsLbPolicyConfig({context, &grpc_services},
                                                  upb_policy, &errors);
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "validation errors");
  }
  EXPECT_EQ(config.size(), 1);
  return JsonDump(Json{config[0]});
}

// A gRPC LB policy factory for a custom policy.  None of the methods
// will actually be used; we just need it to be present in the gRPC LB
// policy registry.
class CustomLbPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args /*args*/) const override {
    Crash("unreachable");
    return nullptr;
  }

  absl::string_view name() const override { return "test.CustomLb"; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return nullptr;
  }
};

//
// RoundRobin
//

TEST(RoundRobin, Basic) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"round_robin\":{}}");
}

//
// ClientSideWeightedRoundRobin
//

TEST(ClientSideWeightedRoundRobinTest, DefaultConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ClientSideWeightedRoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"weighted_round_robin\":{}}");
}

TEST(ClientSideWeightedRoundRobinTest, FieldsExplicitlySet) {
  ClientSideWeightedRoundRobin wrr;
  wrr.mutable_enable_oob_load_report()->set_value(true);
  wrr.mutable_oob_reporting_period()->set_seconds(1);
  wrr.mutable_blackout_period()->set_seconds(2);
  wrr.mutable_weight_expiration_period()->set_seconds(3);
  wrr.mutable_weight_update_period()->set_seconds(4);
  wrr.mutable_error_utilization_penalty()->set_value(5.0);
  wrr.add_metric_names_for_computing_utilization("cpu_usage");
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"weighted_round_robin\":{"
            "\"blackoutPeriod\":\"2.000000000s\","
            "\"enableOobLoadReport\":true,"
            "\"errorUtilizationPenalty\":5,"
            "\"metricNamesForComputingUtilization\":[\"cpu_usage\"],"
            "\"oobReportingPeriod\":\"1.000000000s\","
            "\"weightExpirationPeriod\":\"3.000000000s\","
            "\"weightUpdatePeriod\":\"4.000000000s\""
            "}}");
}

TEST(ClientSideWeightedRoundRobinTest, InvalidValues) {
  ClientSideWeightedRoundRobin wrr;
  wrr.mutable_oob_reporting_period()->set_seconds(-1);
  wrr.mutable_blackout_period()->set_seconds(-2);
  wrr.mutable_weight_expiration_period()->set_seconds(-3);
  wrr.mutable_weight_update_period()->set_seconds(-4);
  wrr.mutable_error_utilization_penalty()->set_value(-1);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".blackout_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".error_utilization_penalty error:value must be non-negative; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".oob_reporting_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".weight_expiration_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".weight_update_period.seconds "
            "error:value must be in the range [0, 315576000000]]")
      << result.status();
}

//
// RingHash
//

TEST(RingHashConfig, DefaultConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RingHash());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"ring_hash_experimental\":{"
            "\"maxRingSize\":8388608,\"minRingSize\":1024}}");
}

TEST(RingHashConfig, FieldsExplicitlySet) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::XX_HASH);
  ring_hash.mutable_minimum_ring_size()->set_value(1234);
  ring_hash.mutable_maximum_ring_size()->set_value(4567);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"ring_hash_experimental\":{"
            "\"maxRingSize\":4567,\"minRingSize\":1234}}");
}

TEST(RingHashConfig, InvalidHashFunction) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::MURMUR_HASH_2);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].hash_function "
            "error:unsupported value (must be XX_HASH)]")
      << result.status();
}

TEST(RingHashConfig, RingSizesTooHigh) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(8388609);
  ring_hash.mutable_maximum_ring_size()->set_value(8388609);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].maximum_ring_size "
            "error:value must be in the range [1, 8388608]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:value must be in the range [1, 8388608]]")
      << result.status();
}

TEST(RingHashConfig, RingSizesTooLow) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(0);
  ring_hash.mutable_maximum_ring_size()->set_value(0);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].maximum_ring_size "
            "error:value must be in the range [1, 8388608]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:value must be in the range [1, 8388608]]")
      << result.status();
}

TEST(RingHashConfig, MinRingSizeGreaterThanMaxRingSize) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(1000);
  ring_hash.mutable_maximum_ring_size()->set_value(999);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:cannot be greater than maximum_ring_size]")
      << result.status();
}

//
// WrrLocality
//

TEST(WrrLocality, RoundRobinChild) {
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"xds_wrr_locality_experimental\":{"
            "\"childPolicy\":[{\"round_robin\":{}}]}}");
}

TEST(WrrLocality, MissingEndpointPickingPolicy) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      WrrLocality());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy "
            "error:field not present]")
      << result.status();
}

TEST(WrrLocality, ChildPolicyInvalid) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::MURMUR_HASH_2);
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy.policies[0]"
            ".typed_extension_config.typed_config.value["
            "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash]"
            ".hash_function "
            "error:unsupported value (must be XX_HASH)]")
      << result.status();
}

TEST(WrrLocality, NoSupportedChildPolicy) {
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(WrrLocality, UnsupportedChildPolicyTypeSkipped) {
  // Create WrrLocality policy and add two policies to its list, an unsupported
  // type and then a known RoundRobin type. Expect that the unsupported type is
  // skipped and RoundRobin is selected.
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(*result,
            "{\"xds_wrr_locality_experimental\":{"
            "\"childPolicy\":[{\"round_robin\":{}}]}}");
}

//
// PickFirst
//

TEST(PickFirst, NoShuffle) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  PickFirst pick_first;
  pick_first.set_shuffle_address_list(false);
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      pick_first);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"pick_first\":{\"shuffleAddressList\":false}}");
}

TEST(PickFirst, Shuffle) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  PickFirst pick_first;
  pick_first.set_shuffle_address_list(true);
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      pick_first);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"pick_first\":{\"shuffleAddressList\":true}}");
}

TEST(PickFirst, ShuffleOmitted) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      PickFirst());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"pick_first\":{\"shuffleAddressList\":false}}");
}

//
// CustomPolicy
//

TEST(CustomPolicy, Basic) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"test.CustomLb\":{\"foo\":\"bar\"}}");
}

//
// AutoshardingTest
//

constexpr char kAutoshardingEnvVar[] =
    "GRPC_XDS_EXPERIMENTAL_ENABLE_AUTOSHARDING_LB";
constexpr absl::string_view kAutoshardingServiceUri =
    "dns:///autosharding.example.com";

class AutoshardingTest : public ::testing::Test {
 protected:
  struct ConvertResult {
    absl::StatusOr<Json::Array> config;
    XdsGrpcServiceMap grpc_services;
  };

  // Creates the XdsClient with a bootstrap config.  Call it after setting
  // the env var, because the bootstrap creates the xDS LB policy registry.
  void CreateXdsClient(absl::string_view extra_bootstrap_text,
                       bool trusted_xds_server) {
    auto bootstrap = GrpcXdsBootstrapBuilder::Build(absl::StrCat(
        "{\n"
        "  \"xds_servers\": [{\n"
        "    \"server_uri\": \"xds.example.com\",\n",
        trusted_xds_server
            ? "    \"server_features\": [\"trusted_xds_server\"],\n"
            : "",
        "    \"channel_creds\": [{\"type\": \"google_default\"}]\n"
        "  }]",
        extra_bootstrap_text, "\n}"));
    if (!bootstrap.ok()) {
      Crash(absl::StrFormat("Error parsing bootstrap: %s",
                            bootstrap.status().ToString().c_str()));
    }
    xds_client_ = MakeRefCounted<XdsClient>(
        std::move(*bootstrap), /*transport_factory=*/nullptr,
        /*event_engine=*/nullptr, /*metrics_reporter=*/nullptr, "foo agent",
        "foo version");
  }

  ConvertResult Convert(const LoadBalancingPolicyProto& policy) {
    std::string serialized_policy = policy.SerializeAsString();
    upb::Arena arena;
    upb::DefPool def_pool;
    XdsResourceType::DecodeContext context = {
        xds_client_.get(), *xds_client_->bootstrap().servers().front(),
        def_pool.ptr(), arena.ptr()};
    auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
        serialized_policy.data(), serialized_policy.size(), arena.ptr());
    ValidationErrors errors;
    ValidationErrors::ScopedField field(&errors, ".load_balancing_policy");
    const auto& registry =
        DownCast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
            .lb_policy_registry();
    ConvertResult result;
    result.config = registry.ConvertXdsLbPolicyConfig(
        {context, &result.grpc_services}, upb_policy, &errors);
    if (!errors.ok()) {
      result.config = errors.status(absl::StatusCode::kInvalidArgument,
                                    "validation errors");
    }
    return result;
  }

  // Returns an Autosharding policy whose GrpcService has insecure creds.
  // Returns an Autosharding config whose GrpcService has insecure creds.
  static Autosharding MakeAutosharding() {
    Autosharding autosharding;
    auto* google_grpc =
        autosharding.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri(kAutoshardingServiceUri);
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        InsecureCredentials());
    autosharding.set_autosharding_target("target-%s");
    autosharding.set_key_header_name("x-shard-key");
    autosharding.set_enable_fallback(true);
    autosharding.mutable_initial_assignment_timeout()->set_seconds(5);
    return autosharding;
  }

  static LoadBalancingPolicyProto MakePolicy(const Autosharding& autosharding) {
    LoadBalancingPolicyProto policy;
    policy.add_policies()
        ->mutable_typed_extension_config()
        ->mutable_typed_config()
        ->PackFrom(autosharding);
    return policy;
  }

  // Returns the channel factory key that the converter generates for
  // autosharding, or an error if the conversion does not give exactly one
  // GrpcService.
  absl::StatusOr<std::string> ConvertAndGetKey(
      const Autosharding& autosharding) {
    auto result = Convert(MakePolicy(autosharding));
    if (!result.config.ok()) return result.config.status();
    if (result.grpc_services.size() != 1) {
      return absl::InternalError(absl::StrCat("expected 1 GrpcService, got ",
                                              result.grpc_services.size()));
    }
    return result.grpc_services.begin()->first;
  }

  // Returns the expected JSON config for MakeAutosharding().
  static std::string ExpectedAutoshardingJson(absl::string_view key) {
    return JsonDump(Json::FromArray({Json::FromObject(
        {{"autosharding_experimental",
          Json::FromObject({
              {"channelFactoryKey", Json::FromString(std::string(key))},
              {"autoshardingTarget", Json::FromString("target-%s")},
              {"keyHeaderName", Json::FromString("x-shard-key")},
              {"enableFallback", Json::FromBool(true)},
              {"initialAssignmentTimeout", Json::FromString("5.000000000s")},
          })}})}));
  }

  RefCountedPtr<XdsClient> xds_client_;
};

TEST_F(AutoshardingTest, TrustedXdsServer) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/true);
  auto result = Convert(MakePolicy(MakeAutosharding()));
  ASSERT_TRUE(result.config.ok()) << result.config.status();
  ASSERT_EQ(result.grpc_services.size(), 1);
  const auto& [key, grpc_service] = *result.grpc_services.begin();
  // The key is the URI and a SHA-256 hash of the full GrpcService.
  EXPECT_THAT(key, ::testing::MatchesRegex(
                       "dns:///autosharding\\.example\\.com#[0-9a-f]{64}"));
  EXPECT_EQ(grpc_service->server_uri(), kAutoshardingServiceUri);
  ASSERT_NE(grpc_service->channel_creds_config(), nullptr);
  EXPECT_EQ(grpc_service->channel_creds_config()->type(), "insecure");
  EXPECT_EQ(JsonDump(Json::FromArray(*result.config)),
            ExpectedAutoshardingJson(key));
}

TEST_F(AutoshardingTest, SameGrpcServiceGivesSameKey) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/true);
  auto key1 = ConvertAndGetKey(MakeAutosharding());
  ASSERT_TRUE(key1.ok()) << key1.status();
  auto key2 = ConvertAndGetKey(MakeAutosharding());
  ASSERT_TRUE(key2.ok()) << key2.status();
  EXPECT_EQ(*key1, *key2);
}

TEST_F(AutoshardingTest, DifferentCallCredsGiveDifferentKeys) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/true);
  Autosharding autosharding1 = MakeAutosharding();
  AccessTokenCredentials call_creds;
  call_creds.set_token("secret-token-1");
  autosharding1.mutable_grpc_service()
      ->mutable_google_grpc()
      ->add_call_credentials_plugin()
      ->PackFrom(call_creds);
  Autosharding autosharding2 = MakeAutosharding();
  call_creds.set_token("secret-token-2");
  autosharding2.mutable_grpc_service()
      ->mutable_google_grpc()
      ->add_call_credentials_plugin()
      ->PackFrom(call_creds);
  auto key1 = ConvertAndGetKey(autosharding1);
  ASSERT_TRUE(key1.ok()) << key1.status();
  auto key2 = ConvertAndGetKey(autosharding2);
  ASSERT_TRUE(key2.ok()) << key2.status();
  EXPECT_NE(*key1, *key2);
  // The key must not leak the token.
  EXPECT_THAT(*key1, ::testing::Not(::testing::HasSubstr("secret")));
  EXPECT_THAT(*key2, ::testing::Not(::testing::HasSubstr("secret")));
}

TEST_F(AutoshardingTest, DifferentInitialMetadataGiveDifferentKeys) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/true);
  Autosharding autosharding1 = MakeAutosharding();
  auto* header = autosharding1.mutable_grpc_service()->add_initial_metadata();
  header->set_key("x-metadata");
  header->set_value("secret-value-1");
  Autosharding autosharding2 = MakeAutosharding();
  header = autosharding2.mutable_grpc_service()->add_initial_metadata();
  header->set_key("x-metadata");
  header->set_value("secret-value-2");
  auto key1 = ConvertAndGetKey(autosharding1);
  ASSERT_TRUE(key1.ok()) << key1.status();
  auto key2 = ConvertAndGetKey(autosharding2);
  ASSERT_TRUE(key2.ok()) << key2.status();
  EXPECT_NE(*key1, *key2);
  // The key must not leak the metadata value.
  EXPECT_THAT(*key1, ::testing::Not(::testing::HasSubstr("secret")));
  EXPECT_THAT(*key2, ::testing::Not(::testing::HasSubstr("secret")));
}

TEST_F(AutoshardingTest, UntrustedXdsServerUsesAllowedGrpcService) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient(absl::StrCat(",\n"
                               "  \"allowed_grpc_services\": {\n"
                               "    \"",
                               kAutoshardingServiceUri,
                               "\": {\n"
                               "      \"channel_creds\": [{\"type\": "
                               "\"google_default\"}]\n"
                               "    }\n"
                               "  }"),
                  /*trusted_xds_server=*/false);
  auto result = Convert(MakePolicy(MakeAutosharding()));
  ASSERT_TRUE(result.config.ok()) << result.config.status();
  ASSERT_EQ(result.grpc_services.size(), 1);
  const auto& [key, grpc_service] = *result.grpc_services.begin();
  EXPECT_THAT(key, ::testing::MatchesRegex(
                       "dns:///autosharding\\.example\\.com#[0-9a-f]{64}"));
  EXPECT_EQ(grpc_service->server_uri(), kAutoshardingServiceUri);
  // The creds come from the bootstrap, not from the resource.
  ASSERT_NE(grpc_service->channel_creds_config(), nullptr);
  EXPECT_EQ(grpc_service->channel_creds_config()->type(), "google_default");
  EXPECT_EQ(JsonDump(Json::FromArray(*result.config)),
            ExpectedAutoshardingJson(key));
}

TEST_F(AutoshardingTest, UntrustedXdsServerAndGrpcServiceNotAllowed) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/false);
  auto result = Convert(MakePolicy(MakeAutosharding()));
  EXPECT_EQ(result.config.status(),
            absl::InvalidArgumentError(
                "validation errors: [field:load_balancing_policy.policies[0]"
                ".typed_extension_config.typed_config.value["
                "envoy.extensions.load_balancing_policies.autosharding.v3."
                "Autosharding].grpc_service.google_grpc.target_uri "
                "error:service not present in \"allowed_grpc_services\" in "
                "bootstrap config]"));
  EXPECT_THAT(result.grpc_services, ::testing::IsEmpty());
}

TEST_F(AutoshardingTest, MissingGrpcService) {
  ScopedExperimentalEnvVar env_var(kAutoshardingEnvVar);
  CreateXdsClient("", /*trusted_xds_server=*/true);
  Autosharding autosharding;
  autosharding.set_autosharding_target("target");
  autosharding.set_key_header_name("x-shard-key");
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(autosharding);
  auto result = Convert(policy);
  EXPECT_EQ(result.config.status(),
            absl::InvalidArgumentError(
                "validation errors: [field:load_balancing_policy.policies[0]"
                ".typed_extension_config.typed_config.value["
                "envoy.extensions.load_balancing_policies.autosharding.v3."
                "Autosharding].grpc_service error:field not set]"));
  EXPECT_THAT(result.grpc_services, ::testing::IsEmpty());
}

TEST_F(AutoshardingTest, RejectedWithoutEnvVar) {
  CreateXdsClient("", /*trusted_xds_server=*/true);
  auto result = Convert(MakePolicy(MakeAutosharding()));
  EXPECT_EQ(result.config.status(),
            absl::InvalidArgumentError(
                "validation errors: [field:load_balancing_policy "
                "error:no supported load balancing policy config found]"));
  EXPECT_THAT(result.grpc_services, ::testing::IsEmpty());
}

//
// XdsLbPolicyRegistryTest
//

TEST(XdsLbPolicyRegistryTest, EmptyLoadBalancingPolicy) {
  auto result = ConvertXdsPolicy(LoadBalancingPolicyProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:load_balancing_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, MissingTypedExtensionConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config "
            "error:field not present]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, MissingTypedConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()->mutable_typed_extension_config();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config error:field not present]")
      << result.status();
}

// This tests that we pass along errors that are generated by
// ExtractXdsExtension().  An exhaustive list of error cases caught by
// ExtractXdsExtension() are covered in xds_common_types_test.
TEST(XdsLbPolicyRegistryTest, ErrorExtractingExtension) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[xds.type.v3.TypedStruct].type_url "
            "error:invalid value \"type.googleapis.com/\"]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, NoSupportedType) {
  LoadBalancingPolicyProto policy;
  // Unsupported built-in type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  // Unsupported custom type.
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:load_balancing_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, UnsupportedTypesSkipped) {
  LoadBalancingPolicyProto policy;
  // Unsupported built-in type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  // Unsupported custom type.
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(typed_struct);
  // Supported type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"round_robin\":{}}");
}

// Build a recurse load balancing policy that goes beyond the max allowable
// depth of 16.
LoadBalancingPolicyProto BuildRecursiveLoadBalancingPolicy(int depth) {
  LoadBalancingPolicyProto policy;
  if (depth >= 16) {
    policy.add_policies()
        ->mutable_typed_extension_config()
        ->mutable_typed_config()
        ->PackFrom(RoundRobin());
    return policy;
  }
  WrrLocality wrr_locality;
  *wrr_locality.mutable_endpoint_picking_policy() =
      BuildRecursiveLoadBalancingPolicy(depth + 1);
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  return policy;
}

TEST(XdsLbPolicyRegistryTest, MaxRecursion) {
  auto result = ConvertXdsPolicy(BuildRecursiveLoadBalancingPolicy(0));
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::EndsWith("error:exceeded max recursion depth of 16]"));
}

// A converter that adds a parsed GrpcService to the context's map, as
// the autosharding converter does (gRFC A119).
class FakeGrpcServiceLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  static constexpr absl::string_view kKey = "fake_key";

  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* /*registry*/,
      const XdsLbPolicyRegistry::ConvertContext& context,
      absl::string_view /*configuration*/, ValidationErrors* /*errors*/,
      int /*recursion_depth*/) override {
    (*context.grpc_services)[std::string(kKey)] =
        std::make_shared<GrpcXdsServerTarget>(
            "dns:///fake.example.com", /*channel_creds_config=*/nullptr,
            /*call_creds_configs=*/
            std::vector<RefCountedPtr<const CallCredsConfig>>());
    return Json::Object{
        {"fake_policy",
         Json::FromObject(
             {{"channelFactoryKey", Json::FromString(std::string(kKey))}})}};
  }

  absl::string_view type() override { return Type(); }

  // Any proto type works here.  The converter ignores the contents.
  static absl::string_view Type() { return "google.protobuf.Duration"; }
};

TEST(XdsLbPolicyRegistryTest, ConverterOutputsGrpcServiceMap) {
  // Put the fake policy under wrr_locality, to check that the map is
  // passed down to nested converters.
  LoadBalancingPolicyProto inner_policy;
  inner_policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(google::protobuf::Duration());
  WrrLocality wrr_locality;
  *wrr_locality.mutable_endpoint_picking_policy() = inner_policy;
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  std::string serialized_policy = policy.SerializeAsString();
  upb::Arena arena;
  upb::DefPool def_pool;
  XdsResourceType::DecodeContext context = {nullptr, GrpcXdsServer(),
                                            def_pool.ptr(), arena.ptr()};
  auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
      serialized_policy.data(), serialized_policy.size(), arena.ptr());
  auto registry = GrpcXdsBootstrapBuilder::CreateXdsLbPolicyRegistry();
  registry.RegisterFactory(
      std::make_unique<FakeGrpcServiceLbPolicyConfigFactory>());
  ValidationErrors errors;
  XdsGrpcServiceMap grpc_services;
  auto config = registry.ConvertXdsLbPolicyConfig({context, &grpc_services},
                                                  upb_policy, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "validation errors");
  ASSERT_EQ(config.size(), 1);
  EXPECT_EQ(JsonDump(Json{config[0]}),
            "{\"xds_wrr_locality_experimental\":{\"childPolicy\":["
            "{\"fake_policy\":{\"channelFactoryKey\":\"fake_key\"}}]}}");
  EXPECT_THAT(grpc_services,
              ::testing::ElementsAre(::testing::Pair(
                  "fake_key", ::testing::Pointee(::testing::Property(
                                  &GrpcXdsServerInterface::server_uri,
                                  "dns:///fake.example.com")))));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::CoreConfiguration::RegisterEphemeralBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
            std::make_unique<grpc_core::testing::CustomLbPolicyFactory>());
      });
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
