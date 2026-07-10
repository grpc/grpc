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

#include "src/core/xds/grpc/xds_common_types.h"

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <grpc/grpc.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/type/matcher/v3/regex.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "re2/re2.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/crash.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/config_grpc_cli.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "xds/type/v3/typed_struct.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

using envoy::config::core::v3::GrpcService;
using CommonTlsContextProto =
    envoy::extensions::transport_sockets::tls::v3::CommonTlsContext;
using xds::type::v3::TypedStruct;
using HeaderMutationRulesProto =
    envoy::config::common::mutation_rules::v3::HeaderMutationRules;
using HeaderValueOptionProto = envoy::config::core::v3::HeaderValueOption;
using HeaderValueProto = envoy::config::core::v3::HeaderValue;

namespace grpc_core {
namespace testing {
namespace {

class XdsCommonTypesTest : public ::testing::Test {
 protected:
  XdsCommonTypesTest() : xds_client_(MakeXdsClient()) {}

  static RefCountedPtr<XdsClient> MakeXdsClient(
      absl::string_view extra_bootstrap_text = "",
      bool trusted_xds_server = false) {
    auto bootstrap = GrpcXdsBootstrap::Create(
        absl::StrCat("{\n"
                     "  \"xds_servers\": [\n"
                     "    {\n"
                     "      \"server_uri\": \"xds.example.com\",\n",
                     trusted_xds_server ? "      \"server_features\": "
                                          "[\"trusted_xds_server\"],\n"
                                        : "",
                     "      \"channel_creds\": [\n"
                     "        {\"type\": \"google_default\"}\n"
                     "      ]\n"
                     "    }\n"
                     "  ],\n",
                     extra_bootstrap_text,
                     "  \"certificate_providers\": {\n"
                     "    \"provider1\": {\n"
                     "      \"plugin_name\": \"file_watcher\",\n"
                     "      \"config\": {\n"
                     "        \"certificate_file\": \"/path/to/cert\",\n"
                     "        \"private_key_file\": \"/path/to/key\"\n"
                     "      }\n"
                     "    }\n"
                     "  }\n",
                     "}"));
    if (!bootstrap.ok()) {
      Crash(absl::StrFormat("Error parsing bootstrap: %s",
                            bootstrap.status().ToString().c_str()));
    }
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr,
                                     /*event_engine=*/nullptr,
                                     /*metrics_reporter=*/nullptr, "foo agent",
                                     "foo version");
  }

  XdsResourceType::DecodeContext MakeDecodeContext() {
    return XdsResourceType::DecodeContext{
        xds_client_.get(), *xds_client_->bootstrap().servers().front(),
        upb_def_pool_.ptr(), upb_arena_.ptr()};
  }

  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
};

//
// ParseDuration() tests
//

using DurationTest = XdsCommonTypesTest;

TEST_F(DurationTest, Basic) {
  google_protobuf_Duration* duration_proto =
      google_protobuf_Duration_new(upb_arena_.ptr());
  google_protobuf_Duration_set_seconds(duration_proto, 1);
  google_protobuf_Duration_set_nanos(duration_proto, 2000000);
  ValidationErrors errors;
  Duration duration = ParseDuration(duration_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(duration, Duration::Milliseconds(1002));
}

TEST_F(DurationTest, NegativeNumbers) {
  google_protobuf_Duration* duration_proto =
      google_protobuf_Duration_new(upb_arena_.ptr());
  google_protobuf_Duration_set_seconds(duration_proto, -1);
  google_protobuf_Duration_set_nanos(duration_proto, -2);
  ValidationErrors errors;
  ParseDuration(duration_proto, &errors);
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed");
  EXPECT_EQ(status.message(),
            "validation failed: ["
            "field:nanos error:value must be in the range [0, 999999999]; "
            "field:seconds error:value must be in the range [0, 315576000000]]")
      << status;
}

TEST_F(DurationTest, ValuesTooHigh) {
  google_protobuf_Duration* duration_proto =
      google_protobuf_Duration_new(upb_arena_.ptr());
  google_protobuf_Duration_set_seconds(duration_proto, 315576000001);
  google_protobuf_Duration_set_nanos(duration_proto, 1000000000);
  ValidationErrors errors;
  ParseDuration(duration_proto, &errors);
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed");
  EXPECT_EQ(status.message(),
            "validation failed: ["
            "field:nanos error:value must be in the range [0, 999999999]; "
            "field:seconds error:value must be in the range [0, 315576000000]]")
      << status;
}

//
// ParseFractionalPercent() tests
//

using FractionalPercentTest = XdsCommonTypesTest;

TEST_F(FractionalPercentTest, AlwaysIfUnset) {
  EXPECT_EQ(1000000, ParseFractionalPercent(nullptr));
}

TEST_F(FractionalPercentTest, PerHundred) {
  envoy_type_v3_FractionalPercent* proto =
      envoy_type_v3_FractionalPercent_new(upb_arena_.ptr());
  envoy_type_v3_FractionalPercent_set_numerator(proto, 30);
  envoy_type_v3_FractionalPercent_set_denominator(
      proto, envoy_type_v3_FractionalPercent_HUNDRED);
  EXPECT_EQ(300000, ParseFractionalPercent(proto));
}

TEST_F(FractionalPercentTest, PerTenThousand) {
  envoy_type_v3_FractionalPercent* proto =
      envoy_type_v3_FractionalPercent_new(upb_arena_.ptr());
  envoy_type_v3_FractionalPercent_set_numerator(proto, 30);
  envoy_type_v3_FractionalPercent_set_denominator(
      proto, envoy_type_v3_FractionalPercent_TEN_THOUSAND);
  EXPECT_EQ(3000, ParseFractionalPercent(proto));
}

TEST_F(FractionalPercentTest, PerMillion) {
  envoy_type_v3_FractionalPercent* proto =
      envoy_type_v3_FractionalPercent_new(upb_arena_.ptr());
  envoy_type_v3_FractionalPercent_set_numerator(proto, 30);
  envoy_type_v3_FractionalPercent_set_denominator(
      proto, envoy_type_v3_FractionalPercent_MILLION);
  EXPECT_EQ(30, ParseFractionalPercent(proto));
}

TEST_F(FractionalPercentTest, ClampsValue) {
  envoy_type_v3_FractionalPercent* proto =
      envoy_type_v3_FractionalPercent_new(upb_arena_.ptr());
  envoy_type_v3_FractionalPercent_set_numerator(proto, 105);
  envoy_type_v3_FractionalPercent_set_denominator(
      proto, envoy_type_v3_FractionalPercent_HUNDRED);
  EXPECT_EQ(1000000, ParseFractionalPercent(proto));
}

//
// CommonTlsContext tests
//

class CommonTlsConfigTest : public XdsCommonTypesTest {
 protected:
  // For convenience, tests build protos using the protobuf API and then
  // use this function to convert it to a upb object, which can be
  // passed to CommonTlsConfig::Parse() for validation.
  const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
  ConvertToUpb(CommonTlsContextProto proto) {
    // Serialize the protobuf proto.
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    // Deserialize as upb proto.
    const auto* upb_proto =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_parse(
            serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }

  absl::StatusOr<CommonTlsContext> Parse(
      const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
          upb_proto) {
    ValidationErrors errors;
    CommonTlsContext common_tls_context =
        CommonTlsContextParse(MakeDecodeContext(), upb_proto, &errors);
    if (!errors.ok()) {
      return errors.status(absl::StatusCode::kInvalidArgument,
                           "validation failed");
    }
    return common_tls_context;
  }
};

TEST_F(CommonTlsConfigTest, NoCaCerts) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      common_tls_context->certificate_validation_context.ca_certs));
  EXPECT_THAT(common_tls_context->certificate_validation_context
                  .match_subject_alt_names,
              ::testing::ElementsAre());
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, CaCertProviderInCombinedValidationContext) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* cert_provider =
      common_tls_context_proto.mutable_combined_validation_context()
          ->mutable_default_validation_context()
          ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  auto* ca_cert_provider =
      std::get_if<CommonTlsContext::CertificateProviderPluginInstance>(
          &common_tls_context->certificate_validation_context.ca_certs);
  ASSERT_NE(ca_cert_provider, nullptr);
  EXPECT_EQ(ca_cert_provider->instance_name, "provider1");
  EXPECT_EQ(ca_cert_provider->certificate_name, "cert_name");
  EXPECT_THAT(common_tls_context->certificate_validation_context
                  .match_subject_alt_names,
              ::testing::ElementsAre());
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, CaCertProviderInValidationContext) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* cert_provider = common_tls_context_proto.mutable_validation_context()
                            ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  auto* ca_cert_provider =
      std::get_if<CommonTlsContext::CertificateProviderPluginInstance>(
          &common_tls_context->certificate_validation_context.ca_certs);
  ASSERT_NE(ca_cert_provider, nullptr);
  EXPECT_EQ(ca_cert_provider->instance_name, "provider1");
  EXPECT_EQ(ca_cert_provider->certificate_name, "cert_name");
  EXPECT_THAT(common_tls_context->certificate_validation_context
                  .match_subject_alt_names,
              ::testing::ElementsAre());
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, SystemRootCerts) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.mutable_validation_context()
      ->mutable_system_root_certs();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  EXPECT_TRUE(std::holds_alternative<
              CommonTlsContext::CertificateValidationContext::SystemRootCerts>(
      common_tls_context->certificate_validation_context.ca_certs));
  EXPECT_THAT(common_tls_context->certificate_validation_context
                  .match_subject_alt_names,
              ::testing::ElementsAre());
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, CaCertProviderTakesPrecedenceOverSystemRootCerts) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* cert_provider = common_tls_context_proto.mutable_validation_context()
                            ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  common_tls_context_proto.mutable_validation_context()
      ->mutable_system_root_certs();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  auto* ca_cert_provider =
      std::get_if<CommonTlsContext::CertificateProviderPluginInstance>(
          &common_tls_context->certificate_validation_context.ca_certs);
  ASSERT_NE(ca_cert_provider, nullptr);
  EXPECT_EQ(ca_cert_provider->instance_name, "provider1");
  EXPECT_EQ(ca_cert_provider->certificate_name, "cert_name");
  EXPECT_THAT(common_tls_context->certificate_validation_context
                  .match_subject_alt_names,
              ::testing::ElementsAre());
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, ValidationSdsConfigUnsupported) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.mutable_validation_context_sds_secret_config();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:validation_context_sds_secret_config "
            "error:feature unsupported]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, TlsCertProvider) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* cert_provider =
      common_tls_context_proto.mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  EXPECT_TRUE(common_tls_context->certificate_validation_context.Empty())
      << common_tls_context->certificate_validation_context.ToString();
  EXPECT_EQ(common_tls_context->tls_certificate_provider_instance.instance_name,
            "provider1");
  EXPECT_EQ(
      common_tls_context->tls_certificate_provider_instance.certificate_name,
      "cert_name");
}

TEST_F(CommonTlsConfigTest, TlsCertificatesUnuspported) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.add_tls_certificates();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:tls_certificates error:feature unsupported]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, TlsCertificatesSdsConfigUnuspported) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.add_tls_certificate_sds_secret_configs();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:tls_certificate_sds_secret_configs "
            "error:feature unsupported]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, TlsParamsUnuspported) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.mutable_tls_params();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:tls_params error:feature unsupported]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, CustomHandshakerUnuspported) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  common_tls_context_proto.mutable_custom_handshaker();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:custom_handshaker error:feature unsupported]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, UnknownCertificateProviderInstance) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* cert_provider = common_tls_context_proto.mutable_validation_context()
                            ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("fake");
  cert_provider->set_certificate_name("cert_name");
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:validation_context.ca_certificate_provider_instance"
            ".instance_name "
            "error:unrecognized certificate provider instance name: fake]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, MatchSubjectAltNames) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* validation_context =
      common_tls_context_proto.mutable_validation_context();
  auto* string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_exact("exact");
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_prefix("prefix");
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_suffix("suffix");
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_contains("contains");
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->mutable_safe_regex()->set_regex("regex");
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  const auto& match_subject_alt_names =
      common_tls_context->certificate_validation_context
          .match_subject_alt_names;
  ASSERT_EQ(match_subject_alt_names.size(), 5);
  EXPECT_EQ(match_subject_alt_names[0].type(), StringMatcher::Type::kExact);
  EXPECT_EQ(match_subject_alt_names[0].string_matcher(), "exact");
  EXPECT_TRUE(match_subject_alt_names[0].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[1].type(), StringMatcher::Type::kPrefix);
  EXPECT_EQ(match_subject_alt_names[1].string_matcher(), "prefix");
  EXPECT_TRUE(match_subject_alt_names[1].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[2].type(), StringMatcher::Type::kSuffix);
  EXPECT_EQ(match_subject_alt_names[2].string_matcher(), "suffix");
  EXPECT_TRUE(match_subject_alt_names[2].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[3].type(), StringMatcher::Type::kContains);
  EXPECT_EQ(match_subject_alt_names[3].string_matcher(), "contains");
  EXPECT_TRUE(match_subject_alt_names[3].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[4].type(), StringMatcher::Type::kSafeRegex);
  EXPECT_EQ(match_subject_alt_names[4].regex_matcher()->pattern(), "regex");
  EXPECT_TRUE(match_subject_alt_names[4].case_sensitive());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      common_tls_context->certificate_validation_context.ca_certs));
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, MatchSubjectAltNamesCaseInsensitive) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* validation_context =
      common_tls_context_proto.mutable_validation_context();
  auto* string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_exact("exact");
  string_matcher->set_ignore_case(true);
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_prefix("prefix");
  string_matcher->set_ignore_case(true);
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_suffix("suffix");
  string_matcher->set_ignore_case(true);
  string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->set_contains("contains");
  string_matcher->set_ignore_case(true);
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_TRUE(common_tls_context.ok()) << common_tls_context.status();
  const auto& match_subject_alt_names =
      common_tls_context->certificate_validation_context
          .match_subject_alt_names;
  ASSERT_EQ(match_subject_alt_names.size(), 4);
  EXPECT_EQ(match_subject_alt_names[0].type(), StringMatcher::Type::kExact);
  EXPECT_EQ(match_subject_alt_names[0].string_matcher(), "exact");
  EXPECT_FALSE(match_subject_alt_names[0].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[1].type(), StringMatcher::Type::kPrefix);
  EXPECT_EQ(match_subject_alt_names[1].string_matcher(), "prefix");
  EXPECT_FALSE(match_subject_alt_names[1].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[2].type(), StringMatcher::Type::kSuffix);
  EXPECT_EQ(match_subject_alt_names[2].string_matcher(), "suffix");
  EXPECT_FALSE(match_subject_alt_names[2].case_sensitive());
  EXPECT_EQ(match_subject_alt_names[3].type(), StringMatcher::Type::kContains);
  EXPECT_EQ(match_subject_alt_names[3].string_matcher(), "contains");
  EXPECT_FALSE(match_subject_alt_names[3].case_sensitive());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      common_tls_context->certificate_validation_context.ca_certs));
  EXPECT_TRUE(common_tls_context->tls_certificate_provider_instance.Empty())
      << common_tls_context->tls_certificate_provider_instance.ToString();
}

TEST_F(CommonTlsConfigTest, MatchSubjectAltNamesInvalid) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* validation_context =
      common_tls_context_proto.mutable_validation_context();
  auto* string_matcher = validation_context->add_match_subject_alt_names();
  string_matcher->mutable_safe_regex()->set_regex("regex");
  string_matcher->set_ignore_case(true);
  string_matcher = validation_context->add_match_subject_alt_names();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:validation_context.match_subject_alt_names[0].ignore_case "
            "error:not supported for regex matcher; "
            "field:validation_context.match_subject_alt_names[1] "
            "error:invalid string matcher]")
      << common_tls_context.status();
}

TEST_F(CommonTlsConfigTest, ValidationContextUnsupportedFields) {
  // Construct proto.
  CommonTlsContextProto common_tls_context_proto;
  auto* validation_context =
      common_tls_context_proto.mutable_validation_context();
  validation_context->add_verify_certificate_spki("foo");
  validation_context->add_verify_certificate_hash("bar");
  validation_context->mutable_require_signed_certificate_timestamp()->set_value(
      true);
  validation_context->mutable_crl();
  validation_context->mutable_custom_validator_config();
  // Convert to upb.
  const auto* upb_proto = ConvertToUpb(common_tls_context_proto);
  ASSERT_NE(upb_proto, nullptr);
  // Run test.
  auto common_tls_context = Parse(upb_proto);
  ASSERT_FALSE(common_tls_context.ok());
  EXPECT_EQ(common_tls_context.status().message(),
            "validation failed: ["
            "field:validation_context.crl "
            "error:feature unsupported; "
            "field:validation_context.custom_validator_config "
            "error:feature unsupported; "
            "field:validation_context.require_signed_certificate_timestamp "
            "error:feature unsupported; "
            "field:validation_context.verify_certificate_hash "
            "error:feature unsupported; "
            "field:validation_context.verify_certificate_spki "
            "error:feature unsupported]")
      << common_tls_context.status();
}

//
// ExtractXdsExtension() tests
//

using ExtractXdsExtensionTest = XdsCommonTypesTest;

TEST_F(ExtractXdsExtensionTest, Basic) {
  constexpr absl::string_view kTypeUrl = "type.googleapis.com/MyType";
  constexpr absl::string_view kValue = "foobar";
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(any_proto, StdStringToUpbString(kTypeUrl));
  google_protobuf_Any_set_value(any_proto, StdStringToUpbString(kValue));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ASSERT_TRUE(extension.has_value());
  EXPECT_EQ(extension->type, "MyType");
  ASSERT_TRUE(std::holds_alternative<absl::string_view>(extension->value));
  EXPECT_EQ(std::get<absl::string_view>(extension->value), kValue);
}

TEST_F(ExtractXdsExtensionTest, TypedStruct) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/MyType");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ASSERT_TRUE(extension.has_value());
  EXPECT_EQ(extension->type, "MyType");
  ASSERT_TRUE(std::holds_alternative<Json>(extension->value));
  EXPECT_EQ(JsonDump(std::get<Json>(extension->value)), "{\"foo\":\"bar\"}");
}

TEST_F(ExtractXdsExtensionTest, UdpaTypedStruct) {
  udpa::type::v1::TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/MyType");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ASSERT_TRUE(extension.has_value());
  EXPECT_EQ(extension->type, "MyType");
  ASSERT_TRUE(std::holds_alternative<Json>(extension->value));
  EXPECT_EQ(JsonDump(std::get<Json>(extension->value)), "{\"foo\":\"bar\"}");
}

TEST_F(ExtractXdsExtensionTest, TypedStructWithoutValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/MyType");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ASSERT_TRUE(extension.has_value());
  EXPECT_EQ(extension->type, "MyType");
  ASSERT_TRUE(std::holds_alternative<Json>(extension->value));
  EXPECT_EQ(JsonDump(std::get<Json>(extension->value)), "{}");
}

TEST_F(ExtractXdsExtensionTest, TypedStructJsonConversion) {
  TypedStruct typed_struct;
  ASSERT_TRUE(grpc::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_url: "type.googleapis.com/envoy.ExtensionType"
        value {
          fields {
            key: "key"
            value { null_value: NULL_VALUE }
          }
          fields {
            key: "number"
            value { number_value: 123 }
          }
          fields {
            key: "string"
            value { string_value: "value" }
          }
          fields {
            key: "struct"
            value {
              struct_value {
                fields {
                  key: "key"
                  value { null_value: NULL_VALUE }
                }
              }
            }
          }
          fields {
            key: "list"
            value {
              list_value {
                values { null_value: NULL_VALUE }
                values { number_value: 234 }
              }
            }
          }
        }
      )pb",
      &typed_struct));
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  ASSERT_TRUE(extension.has_value());
  EXPECT_EQ(extension->type, "envoy.ExtensionType");
  ASSERT_TRUE(std::holds_alternative<Json>(extension->value));
  EXPECT_EQ(JsonDump(std::get<Json>(extension->value)),
            "{"
            "\"key\":null,"
            "\"list\":[null,234],"
            "\"number\":123,"
            "\"string\":\"value\","
            "\"struct\":{\"key\":null}"
            "}");
}

TEST_F(ExtractXdsExtensionTest, FieldMissing) {
  ValidationErrors errors;
  ValidationErrors::ScopedField field(&errors, "any");
  auto extension = ExtractXdsExtension(MakeDecodeContext(), nullptr, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: [field:any error:field not present]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypeUrlMissing) {
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: [field:type_url error:field not present]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypedStructTypeUrlMissing) {
  TypedStruct typed_struct;
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:value[xds.type.v3.TypedStruct].type_url "
            "error:field not present]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypeUrlNoSlash) {
  constexpr absl::string_view kTypeUrl = "MyType";
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(any_proto, StdStringToUpbString(kTypeUrl));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:type_url error:invalid value \"MyType\"]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypedStructTypeUrlNoSlash) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("MyType");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:value[xds.type.v3.TypedStruct].type_url "
            "error:invalid value \"MyType\"]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypeUrlNothingAfterSlash) {
  constexpr absl::string_view kTypeUrl = "type.googleapi.com/";
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(any_proto, StdStringToUpbString(kTypeUrl));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:type_url error:invalid value \"type.googleapi.com/\"]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypedStructTypeUrlNothingAfterSlash) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:value[xds.type.v3.TypedStruct].type_url "
            "error:invalid value \"type.googleapis.com/\"]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypedStructParseFailure) {
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  std::string serialized_type_struct("\0", 1);
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_type_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:value[xds.type.v3.TypedStruct] error:could not parse]")
      << status;
}

TEST_F(ExtractXdsExtensionTest, TypedStructWithInvalidProtobufStruct) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/xds.MyType");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].mutable_list_value()->add_values();
  std::string serialized_typed_struct = typed_struct.SerializeAsString();
  google_protobuf_Any* any_proto = google_protobuf_Any_new(upb_arena_.ptr());
  google_protobuf_Any_set_type_url(
      any_proto, StdStringToUpbString(absl::string_view(
                     "type.googleapis.com/xds.type.v3.TypedStruct")));
  google_protobuf_Any_set_value(any_proto,
                                StdStringToUpbString(serialized_typed_struct));
  ValidationErrors errors;
  auto extension = ExtractXdsExtension(MakeDecodeContext(), any_proto, &errors);
  ASSERT_FALSE(errors.ok());
  absl::Status status =
      errors.status(absl::StatusCode::kInvalidArgument, "validation errors");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "validation errors: ["
            "field:value[xds.type.v3.TypedStruct].value[xds.MyType] "
            "error:error encoding google::Protobuf::Struct as JSON: "
            "No value set in Value proto]")
      << status;
}

//
// ParseXdsGrpcService() tests
//

MATCHER_P3(EqCredsConfig, type, proto_type, config, "equals creds config") {
  bool ok = ::testing::ExplainMatchResult(type, arg->type(), result_listener);
  ok &= ::testing::ExplainMatchResult(proto_type, arg->proto_type(),
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(config, arg->ToString(), result_listener);
  return ok;
}

class ParseXdsGrpcServiceTest : public XdsCommonTypesTest {
 protected:
  // For convenience, tests build protos using the protobuf API, and
  // we convert it to a upb object, which is then passed to
  // ParseXdsGrpcService() for testing.
  absl::StatusOr<GrpcXdsServerTarget> Parse(const GrpcService& proto) {
    // Serialize the protobuf proto.
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      return absl::InternalError("protobuf serialization failed");
    }
    // Deserialize as upb proto.
    const auto* upb_proto = envoy_config_core_v3_GrpcService_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      return absl::InternalError("upb parsing failed");
    }
    // Now parse the upb proto.
    ValidationErrors errors;
    GrpcXdsServerTarget target =
        ParseXdsGrpcService(MakeDecodeContext(), upb_proto, &errors);
    if (!errors.ok()) {
      return errors.status(absl::StatusCode::kInvalidArgument,
                           "validation failed");
    }
    return target;
  }
};

TEST_F(ParseXdsGrpcServiceTest,
       NonTrustedXdsServerAndServicePresentInBootstrap) {
  ScopedExperimentalEnvVar env("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT");
  xds_client_ = MakeXdsClient(
      "  \"allowed_grpc_services\": {\n"
      "    \"dns:server.example.com\": {\n"
      "      \"channel_creds\": [{\"type\": \"insecure\"}],\n"
      "      \"call_creds\": [\n"
      "         {\"type\": \"jwt_token_file\",\n"
      "          \"config\": {\"jwt_token_file\": \"/path/to/file\"}}\n"
      "      ]\n"
      "    }\n"
      "  },\n");
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  // Creds specified in proto will be ignored because xDS server is not trusted.
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::google_default::v3::
          GoogleDefaultCredentials());
  envoy::extensions::grpc_service::call_credentials::access_token::v3::
      AccessTokenCredentials call_creds;
  call_creds.set_token("foo");
  google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
  auto xds_grpc_service = Parse(grpc_service);
  ASSERT_TRUE(xds_grpc_service.ok()) << xds_grpc_service.status();
  EXPECT_EQ(xds_grpc_service->server_uri(), "dns:server.example.com");
  ASSERT_NE(xds_grpc_service->channel_creds_config(), nullptr);
  EXPECT_EQ(xds_grpc_service->channel_creds_config()->type(), "insecure");
  EXPECT_THAT(xds_grpc_service->call_creds_configs(),
              ::testing::ElementsAre(EqCredsConfig(
                  "jwt_token_file", "", "{path=\"/path/to/file\"}")));
}

TEST_F(ParseXdsGrpcServiceTest,
       NonTrustedXdsServerAndServiceNotPresentInBootstrap) {
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(xds_grpc_service.status(),
            absl::InvalidArgumentError(
                "validation failed: [field:google_grpc.target_uri "
                "error:service not present in \"allowed_grpc_services\" in "
                "bootstrap config]"));
}

TEST_F(ParseXdsGrpcServiceTest, TrustedXdsServerWithCredentials) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  // Unsupported channel creds type, should be ignored.
  google_grpc->add_channel_credentials_plugin()->PackFrom(GrpcService());
  // Two supported channel creds types, should use the first one.
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::google_default::v3::
          GoogleDefaultCredentials());
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::
          InsecureCredentials());
  // Unsupported call creds type, should be ignored.
  google_grpc->add_call_credentials_plugin()->PackFrom(GrpcService());
  // Two supported call creds types, should use both.
  envoy::extensions::grpc_service::call_credentials::access_token::v3::
      AccessTokenCredentials call_creds;
  call_creds.set_token("foo");
  google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
  call_creds.set_token("bar");
  google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
  auto xds_grpc_service = Parse(grpc_service);
  ASSERT_TRUE(xds_grpc_service.ok()) << xds_grpc_service.status();
  EXPECT_EQ(xds_grpc_service->server_uri(), "dns:server.example.com");
  ASSERT_NE(xds_grpc_service->channel_creds_config(), nullptr);
  EXPECT_EQ(xds_grpc_service->channel_creds_config()->type(), "google_default");
  EXPECT_THAT(xds_grpc_service->call_creds_configs(),
              ::testing::ElementsAre(
                  EqCredsConfig("",
                                "envoy.extensions.grpc_service.call_credentials"
                                ".access_token.v3.AccessTokenCredentials",
                                "{token=\"foo\"}"),
                  EqCredsConfig("",
                                "envoy.extensions.grpc_service.call_credentials"
                                ".access_token.v3.AccessTokenCredentials",
                                "{token=\"bar\"}")));
  // Unset fields have default values.
  EXPECT_EQ(xds_grpc_service->timeout(), Duration::Zero());
  EXPECT_THAT(xds_grpc_service->initial_metadata(), ::testing::ElementsAre());
}

TEST_F(ParseXdsGrpcServiceTest, TrustedXdsServerWithChannelCredsUnset) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(
      xds_grpc_service.status(),
      absl::InvalidArgumentError("validation failed: ["
                                 "field:google_grpc.channel_credentials_plugin "
                                 "error:field not set]"));
}

TEST_F(ParseXdsGrpcServiceTest, TrustedXdsServerWithNoSupportedChannelCreds) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  google_grpc->add_channel_credentials_plugin()->PackFrom(GrpcService());
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(xds_grpc_service.status(),
            absl::InvalidArgumentError(
                "validation failed: ["
                "field:google_grpc.channel_credentials_plugin "
                "error:no supported channel credentials type found]"));
}

TEST_F(ParseXdsGrpcServiceTest, Timeout) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  grpc_service.mutable_timeout()->set_seconds(5);
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::
          InsecureCredentials());
  auto xds_grpc_service = Parse(grpc_service);
  ASSERT_TRUE(xds_grpc_service.ok()) << xds_grpc_service.status();
  EXPECT_EQ(xds_grpc_service->timeout(), Duration::Seconds(5));
}

TEST_F(ParseXdsGrpcServiceTest, InvalidTimeout) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  grpc_service.mutable_timeout()->set_seconds(0);
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::
          InsecureCredentials());
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(xds_grpc_service.status(),
            absl::InvalidArgumentError(
                "validation failed: ["
                "field:timeout error:duration must be positive]"));
}

TEST_F(ParseXdsGrpcServiceTest, HeaderValueForNonBinaryHeader) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* header_value = grpc_service.add_initial_metadata();
  header_value->set_key("foo");
  header_value->set_value("bar");
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::google_default::v3::
          GoogleDefaultCredentials());
  auto xds_grpc_service = Parse(grpc_service);
  ASSERT_TRUE(xds_grpc_service.ok()) << xds_grpc_service.status();
  EXPECT_THAT(xds_grpc_service->initial_metadata(),
              ::testing::ElementsAre(::testing::Pair("foo", "bar")));
}

// Note: This shows that we include validation errors from ParseXdsHeader()
// itself.  We don't need to test every possible matcher validation failure
// case here, because those are covered in the tests for ParseXdsHeader().
TEST_F(ParseXdsGrpcServiceTest, NoHeaderValueSet) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* header_value = grpc_service.add_initial_metadata();
  header_value->set_key("foo");
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("dns:server.example.com");
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::google_default::v3::
          GoogleDefaultCredentials());
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(xds_grpc_service.status(),
            absl::InvalidArgumentError(
                "validation failed: ["
                "field:initial_metadata[0] "
                "error:either value or raw_value must be set]"));
}

TEST_F(ParseXdsGrpcServiceTest, GoogleGrpcNotSet) {
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  auto xds_grpc_service = Parse(GrpcService());
  EXPECT_EQ(
      xds_grpc_service.status(),
      absl::InvalidArgumentError("validation failed: ["
                                 "field:google_grpc error:field not set]"));
}

TEST_F(ParseXdsGrpcServiceTest, InvalidTargetUri) {
  // Avoid using the default DNS URI prefix.
  CoreConfiguration::WithSubstituteBuilder builder(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->resolver_registry()->SetDefaultPrefix("");
      });
  xds_client_ = MakeXdsClient("", /*trusted_xds_server=*/true);
  GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri("/");
  google_grpc->add_channel_credentials_plugin()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::
          InsecureCredentials());
  auto xds_grpc_service = Parse(grpc_service);
  EXPECT_EQ(xds_grpc_service.status(),
            absl::InvalidArgumentError(
                "validation failed: ["
                "field:google_grpc.target_uri error:invalid target URI]"));
}

//
// ParseHeaderMutationRules() tests
//

class ParseHeaderMutationRulesTest : public XdsCommonTypesTest {
 protected:
  const envoy_config_common_mutation_rules_v3_HeaderMutationRules* ConvertToUpb(
      const HeaderMutationRulesProto& proto) {
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    const auto* upb_proto =
        envoy_config_common_mutation_rules_v3_HeaderMutationRules_parse(
            serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }

  HeaderMutationRules Parse(
      const envoy_config_common_mutation_rules_v3_HeaderMutationRules*
          upb_proto,
      ValidationErrors* errors) {
    return ParseHeaderMutationRules(upb_proto, errors);
  }
};

TEST_F(ParseHeaderMutationRulesTest, Empty) {
  HeaderMutationRulesProto proto;
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto rules = Parse(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_FALSE(rules.disallow_all);
  EXPECT_FALSE(rules.disallow_is_error);
  EXPECT_EQ(rules.allow_expression, nullptr);
  EXPECT_EQ(rules.disallow_expression, nullptr);
}

TEST_F(ParseHeaderMutationRulesTest, Basic) {
  HeaderMutationRulesProto proto;
  proto.mutable_allow_expression()->set_regex("allow");
  proto.mutable_disallow_expression()->set_regex("disallow");
  proto.mutable_disallow_all()->set_value(true);
  proto.mutable_disallow_is_error()->set_value(true);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto rules = Parse(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_TRUE(rules.disallow_all);
  EXPECT_TRUE(rules.disallow_is_error);
  ASSERT_NE(rules.allow_expression, nullptr);
  EXPECT_EQ(rules.allow_expression->pattern(), "allow");
  ASSERT_NE(rules.disallow_expression, nullptr);
  EXPECT_EQ(rules.disallow_expression->pattern(), "disallow");
}

TEST_F(ParseHeaderMutationRulesTest, ExplicitFalse) {
  HeaderMutationRulesProto proto;
  proto.mutable_disallow_all()->set_value(false);
  proto.mutable_disallow_is_error()->set_value(false);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto rules = Parse(upb_proto, &errors);
  EXPECT_TRUE(errors.ok());
  EXPECT_FALSE(rules.disallow_all);
  EXPECT_FALSE(rules.disallow_is_error);
}

TEST_F(ParseHeaderMutationRulesTest, InvalidRegex) {
  HeaderMutationRulesProto proto;
  proto.mutable_allow_expression()->set_regex("[");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  Parse(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:header_mutation_rules.allow_expression "
      "error:Invalid regex string specified in matcher: missing ]: []");
}

TEST(HeaderMutationRulesTest, DefaultAllowsAll) {
  HeaderMutationRules rules;
  EXPECT_TRUE(rules.IsMutationAllowed("foo"));
  EXPECT_TRUE(rules.IsMutationAllowed("bar"));
}

TEST(HeaderMutationRulesTest, DisallowAll) {
  HeaderMutationRules rules;
  rules.disallow_all = true;
  rules.allow_expression = std::make_unique<RE2>(".*header");
  EXPECT_FALSE(rules.IsMutationAllowed("allow_header"));
}

TEST(HeaderMutationRulesTest, DisallowExpression) {
  HeaderMutationRules rules;
  rules.disallow_expression = std::make_unique<RE2>("disallowed.*");
  EXPECT_FALSE(rules.IsMutationAllowed("disallowed_header"));
  EXPECT_TRUE(rules.IsMutationAllowed("allowed_header"));
}

TEST(HeaderMutationRulesTest, AllowExpression) {
  HeaderMutationRules rules;
  rules.allow_expression = std::make_unique<RE2>("allowed.*");
  // "allowed_header" matches allow_expression
  EXPECT_TRUE(rules.IsMutationAllowed("allowed_header"));
  // "other" does not match allow_expression
  EXPECT_FALSE(rules.IsMutationAllowed("other"));
}

TEST(HeaderMutationRulesTest, DisallowExpressionOverridesAllowExpression) {
  HeaderMutationRules rules;
  rules.disallow_expression = std::make_unique<RE2>("common.*");
  rules.allow_expression = std::make_unique<RE2>(".*header");
  // "common_header" matches both. Should be disallowed.
  EXPECT_FALSE(rules.IsMutationAllowed("common_header"));
  // "unique_header" matches only allow. Should be allowed.
  EXPECT_TRUE(rules.IsMutationAllowed("unique_header"));
  // "common_stuff" matches only disallow. Should be disallowed.
  EXPECT_FALSE(rules.IsMutationAllowed("common_stuff"));
  // "stuff" matches neither. Should be disallowed (because allow_expression is
  // set).
  EXPECT_FALSE(rules.IsMutationAllowed("stuff"));
}

TEST(HeaderMutationRulesTest, SomeHeadersNeverAllowed) {
  HeaderMutationRules rules;
  rules.allow_expression = std::make_unique<RE2>(".*");
  EXPECT_TRUE(rules.IsMutationAllowed("foo"));
  EXPECT_TRUE(rules.IsMutationAllowed("bar"));
  // Still does not allow certain headers.
  EXPECT_FALSE(rules.IsMutationAllowed("host"));
  EXPECT_FALSE(rules.IsMutationAllowed(":path"));
  EXPECT_FALSE(rules.IsMutationAllowed("grpc-foo"));
}

//
// ParseHeader() tests
//

class ParseHeaderTest : public XdsCommonTypesTest {
 protected:
  const envoy_config_core_v3_HeaderValue* ConvertToUpb(
      const HeaderValueProto& proto) {
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    const auto* upb_proto = envoy_config_core_v3_HeaderValue_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }
};

TEST_F(ParseHeaderTest, HeaderValueForNonBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo");
  proto.set_value("bar");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header = ParseXdsHeader(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header.first, "foo");
  EXPECT_EQ(header.second, "bar");
}

TEST_F(ParseHeaderTest, HeaderValueForBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo-bin");
  proto.set_value("YmFy");  // "bar" in base64
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header = ParseXdsHeader(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header.first, "foo-bin");
  EXPECT_EQ(header.second, "bar");
}

TEST_F(ParseHeaderTest, RawHeaderTakesPrecedenceForBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo-bin");
  proto.set_raw_value("Hw==");
  proto.set_value("ignored_value");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header = ParseXdsHeader(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header.first, "foo-bin");
  EXPECT_EQ(header.second, "\x1f");
}

TEST_F(ParseHeaderTest, RawHeaderValueInvalidBase64ForBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo-bin");
  proto.set_raw_value("invalid_base64!");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:raw_value error:invalid base64]");
}

TEST_F(ParseHeaderTest, HeaderValueInvalidBase64ForBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo-bin");
  proto.set_value("invalid_base64!");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:value error:invalid base64]");
}

TEST_F(ParseHeaderTest, RawHeaderValueValidationForNonBinaryHeader) {
  HeaderValueProto proto;
  proto.set_key("foo");
  proto.set_raw_value("\x1f");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:raw_value error:Illegal header value]");
}

TEST_F(ParseHeaderTest, NoHeaderValueSet) {
  HeaderValueProto proto;
  proto.set_key("foo");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field: error:either value or raw_value must be "
      "set]");
}

TEST_F(ParseHeaderTest, InvalidHeaderKeyAndValue) {
  HeaderValueProto proto;
  proto.set_key("Foo");
  proto.set_value("\x1f");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:key error:Illegal header key; "
      "field:value error:Illegal header value]");
}

TEST_F(ParseHeaderTest, HeaderKeyHostNotAllowed) {
  HeaderValueProto proto;
  proto.set_key("host");
  proto.set_value("x");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:key error:header \"host\" not allowed]");
}

TEST_F(ParseHeaderTest, HeaderKeyStartingWithColonNotAllowed) {
  HeaderValueProto proto;
  proto.set_key(":path");
  proto.set_value("x");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:key error:header \":path\" not allowed]");
}

TEST_F(ParseHeaderTest, HeaderKeyStartingWithGrpcNotAllowed) {
  HeaderValueProto proto;
  proto.set_key("grpc-foo");
  proto.set_value("x");
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeader(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:key error:header \"grpc-foo\" not allowed]");
}

//
// ParseHeaderValueOption() tests
//

class ParseHeaderValueOptionTest : public XdsCommonTypesTest {
 protected:
  const envoy_config_core_v3_HeaderValueOption* ConvertToUpb(
      const HeaderValueOptionProto& proto) {
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    const auto* upb_proto = envoy_config_core_v3_HeaderValueOption_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }
};

TEST_F(ParseHeaderValueOptionTest, ReturnsErrorWhenProtoIsNull) {
  ValidationErrors errors;
  ParseXdsHeaderValueOption(nullptr, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field: error:field is not present]");
}

TEST_F(ParseHeaderValueOptionTest, ReturnsErrorWhenHeaderFieldNotSet) {
  HeaderValueOptionProto proto;
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header_value_option = ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:header error:field not set]");
}

TEST_F(ParseHeaderValueOptionTest, SuccessfullyParsesAppendIfExistsOrAdd) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.mutable_header()->set_value("bar");
  proto.set_append_action(HeaderValueOptionProto::APPEND_IF_EXISTS_OR_ADD);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header_value_option = ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header_value_option.header.first, "foo");
  EXPECT_EQ(header_value_option.header.second, "bar");
  EXPECT_EQ(header_value_option.append_action,
            XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd);
}

TEST_F(ParseHeaderValueOptionTest, SuccessfullyParsesAddIfAbsent) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.mutable_header()->set_value("bar");
  proto.set_append_action(HeaderValueOptionProto::ADD_IF_ABSENT);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header_value_option = ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header_value_option.header.first, "foo");
  EXPECT_EQ(header_value_option.header.second, "bar");
  EXPECT_EQ(header_value_option.append_action,
            XdsHeaderValueOption::AppendAction::kAddIfAbsent);
}

TEST_F(ParseHeaderValueOptionTest, SuccessfullyParsesOverwriteIfExistsOrAdd) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.mutable_header()->set_value("bar");
  proto.set_append_action(HeaderValueOptionProto::OVERWRITE_IF_EXISTS_OR_ADD);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header_value_option = ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header_value_option.header.first, "foo");
  EXPECT_EQ(header_value_option.header.second, "bar");
  EXPECT_EQ(header_value_option.append_action,
            XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd);
}

TEST_F(ParseHeaderValueOptionTest, SuccessfullyParsesOverwriteIfExists) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.mutable_header()->set_value("bar");
  proto.set_append_action(HeaderValueOptionProto::OVERWRITE_IF_EXISTS);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  auto header_value_option = ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(header_value_option.header.first, "foo");
  EXPECT_EQ(header_value_option.header.second, "bar");
  EXPECT_EQ(header_value_option.append_action,
            XdsHeaderValueOption::AppendAction::kOverwriteIfExists);
}

TEST_F(ParseHeaderValueOptionTest, ReturnsErrorWhenAppendActionIsInvalid) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.mutable_header()->set_value("bar");
  proto.set_append_action(
      static_cast<HeaderValueOptionProto::HeaderAppendAction>(999));
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:append_action error:unsupported append "
      "action]");
}

// Note: This shows that we include validation errors from ParseXdsHeader()
// itself.  We don't need to test every possible matcher validation failure
// case here, because those are covered in the tests for ParseXdsHeader().
TEST_F(ParseHeaderValueOptionTest, NoHeaderValueSet) {
  HeaderValueOptionProto proto;
  proto.mutable_header()->set_key("foo");
  proto.set_append_action(HeaderValueOptionProto::APPEND_IF_EXISTS_OR_ADD);
  const auto* upb_proto = ConvertToUpb(proto);
  ASSERT_NE(upb_proto, nullptr);
  ValidationErrors errors;
  ParseXdsHeaderValueOption(upb_proto, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "validation failed")
          .message(),
      "validation failed: [field:header error:either value or raw_value must "
      "be set]");
}

//
// ApplyXdsHeaderMutationsRemoval() tests
//

TEST(ApplyXdsHeaderMutationsRemovalTest, RemovesHeaderWhenAllowed) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("foo"),
                  [](absl::string_view, const Slice&) {});
  metadata.Append("x-target-2", Slice::FromCopiedString("bar"),
                  [](absl::string_view, const Slice&) {});
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status =
      ApplyXdsHeaderMutationsRemoval("x-target-1", &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_FALSE(metadata.GetStringValue("x-target-1", &val).has_value());
  EXPECT_EQ(metadata.GetStringValue("x-target-2", &val), "bar");
}

TEST(ApplyXdsHeaderMutationsRemovalTest, SucceedsWhenHeaderIsAbsent) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("val"),
                  [](absl::string_view, const Slice&) {});
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>(".+");
  absl::Status status =
      ApplyXdsHeaderMutationsRemoval("x-target-2", &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "val");
  EXPECT_FALSE(metadata.GetStringValue("x-target-2", &val).has_value());
}

TEST(ApplyXdsHeaderMutationsRemovalTest, DisallowIsErrorReturnsNonOkStatus) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("secret"),
                  [](absl::string_view, const Slice&) {});
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.disallow_expression = std::make_unique<RE2>("x-target-1");
  absl::Status status =
      ApplyXdsHeaderMutationsRemoval("x-target-1", &rules, metadata);
  EXPECT_EQ(status,
            absl::InternalError("Forbidden header removal: x-target-1"));
}

TEST(ApplyXdsHeaderMutationsRemovalTest, DoesNotRemoveHeaderWhenNotAllowed) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("secret"),
                  [](absl::string_view, const Slice&) {});
  HeaderMutationRules rules;
  rules.disallow_is_error = false;
  rules.disallow_expression = std::make_unique<RE2>("x-target-1");
  absl::Status status =
      ApplyXdsHeaderMutationsRemoval("x-target-1", &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "secret");
}

TEST(ApplyXdsHeaderMutationsRemovalTest,
     RemovesHeaderWhenMutationRulesPointerIsNotSet) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("foo"),
                  [](absl::string_view, const Slice&) {});
  metadata.Append("x-target-2", Slice::FromCopiedString("bar"),
                  [](absl::string_view, const Slice&) {});
  absl::Status status =
      ApplyXdsHeaderMutationsRemoval("x-target-1", /*rules=*/nullptr, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_FALSE(metadata.GetStringValue("x-target-1", &val).has_value());
  EXPECT_EQ(metadata.GetStringValue("x-target-2", &val), "bar");
}

//
// ApplyXdsHeaderMutationsAddition() tests
//

TEST(ApplyHeaderMutationsAdditionTest,
     AddsHeaderWhenNotPresentWithAppendIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "val";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "val");
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddHeaderWhenPresentWithAppendIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "orig,new");
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddEmptyHeaderWhenNotPresentWithAppendIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "");
}

TEST(ApplyHeaderMutationsAdditionTest, AddHeaderWhenNotPresentWithAddIfAbsent) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "val";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAddIfAbsent;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "val");
}

TEST(ApplyHeaderMutationsAdditionTest,
     DoesNotAddHeaderWhenPresentWithAddIfAbsent) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAddIfAbsent;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "orig");
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddEmptyHeaderWhenNotPresentWithAddIfAbsent) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAddIfAbsent;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "");
}

TEST(ApplyHeaderMutationsAdditionTest,
     DoesNotAddHeaderWhenNotPresentWithOverwriteIfExists) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action = XdsHeaderValueOption::AppendAction::kOverwriteIfExists;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_FALSE(metadata.GetStringValue("x-target-1", &val).has_value());
}

TEST(ApplyHeaderMutationsAdditionTest,
     OverwriteHeaderWhenPresentWithOverwriteIfExists) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action = XdsHeaderValueOption::AppendAction::kOverwriteIfExists;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "new");
}

TEST(ApplyHeaderMutationsAdditionTest,
     OverwriteEmptyHeaderWhenPresentWithOverwriteIfExists) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "";
  opt.append_action = XdsHeaderValueOption::AppendAction::kOverwriteIfExists;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "");
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddHeaderWhenNotPresentWithOverwriteIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action =
      XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "new");
}

TEST(ApplyHeaderMutationsAdditionTest,
     OverwriteHeaderWhenPresentWithOverwriteIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action =
      XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "new");
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddEmptyHeaderWhenNotPresentWithOverwriteIfExistsOrAdd) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "";
  opt.append_action =
      XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.allow_expression = std::make_unique<RE2>("x-.*");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "");
}

TEST(ApplyHeaderMutationsAdditionTest, DisallowIsErrorReturnsNonOkStatus) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "val";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAddIfAbsent;
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  rules.disallow_expression = std::make_unique<RE2>("x-target-1");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_EQ(status,
            absl::InternalError("Forbidden header mutation: x-target-1"));
}

TEST(ApplyHeaderMutationsAdditionTest, DoesNotAddHeaderWhenNotAllowed) {
  grpc_metadata_batch metadata;
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "val";
  opt.append_action = XdsHeaderValueOption::AppendAction::kAddIfAbsent;
  HeaderMutationRules rules;
  rules.disallow_is_error = false;
  rules.disallow_expression = std::make_unique<RE2>("x-target-1");
  absl::Status status = ApplyXdsHeaderMutationsAddition(opt, &rules, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_FALSE(metadata.GetStringValue("x-target-1", &val).has_value());
}

TEST(ApplyHeaderMutationsAdditionTest,
     AddHeaderWhenMutationRulesPointerIsNotSet) {
  grpc_metadata_batch metadata;
  metadata.Append("x-target-1", Slice::FromCopiedString("orig"),
                  [](absl::string_view, const Slice&) {});
  XdsHeaderValueOption opt;
  opt.header.first = "x-target-1";
  opt.header.second = "new";
  opt.append_action =
      XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
  absl::Status status =
      ApplyXdsHeaderMutationsAddition(opt, /*rules=*/nullptr, metadata);
  EXPECT_TRUE(status.ok()) << status;
  std::string val;
  EXPECT_EQ(metadata.GetStringValue("x-target-1", &val), "new");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
