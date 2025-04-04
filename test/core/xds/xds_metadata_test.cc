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

#include "src/core/xds/grpc/xds_metadata.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/struct.pb.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/crash.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_metadata_parser.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"

using envoy::config::core::v3::Metadata;
using envoy::extensions::filters::http::gcp_authn::v3::Audience;

namespace grpc_core {
namespace testing {
namespace {

class XdsMetadataTest : public ::testing::Test {
 protected:
  XdsMetadataTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(),
                        *xds_client_->bootstrap().servers().front(),
                        upb_def_pool_.ptr(), upb_arena_.ptr()} {}

  static RefCountedPtr<XdsClient> MakeXdsClient() {
    auto bootstrap = GrpcXdsBootstrap::Create(
        "{\n"
        "  \"xds_servers\": [\n"
        "    {\n"
        "      \"server_uri\": \"xds.example.com\",\n"
        "      \"channel_creds\": [\n"
        "        {\"type\": \"google_default\"}\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}");
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

  // For convenience, tests build protos using the protobuf API and then
  // use this function to convert it to a upb object, which can be
  // passed to ParseXdsMetadataMap() for validation.
  const envoy_config_core_v3_Metadata* ConvertToUpb(Metadata proto) {
    // Serialize the protobuf proto.
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    // Deserialize as upb proto.
    const auto* upb_proto = envoy_config_core_v3_Metadata_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }

  absl::StatusOr<XdsMetadataMap> Parse(
      const envoy_config_core_v3_Metadata* upb_proto) {
    ValidationErrors errors;
    XdsMetadataMap metadata_map =
        ParseXdsMetadataMap(decode_context_, upb_proto, &errors);
    if (!errors.ok()) {
      return errors.status(absl::StatusCode::kInvalidArgument,
                           "validation failed");
    }
    return metadata_map;
  }

  absl::StatusOr<XdsMetadataMap> Decode(Metadata proto) {
    const envoy_config_core_v3_Metadata* upb_proto =
        ConvertToUpb(std::move(proto));
    return Parse(upb_proto);
  }

  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
};

MATCHER_P(JsonEq, json_str, "") {
  std::string actual = JsonDump(arg);
  bool ok = ::testing::ExplainMatchResult(json_str, actual, result_listener);
  if (!ok) *result_listener << "Actual: " << actual;
  return ok;
}

TEST_F(XdsMetadataTest, UntypedMetadata) {
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_filter_metadata();
  auto& label_map = *filter_map["filter_key"].mutable_fields();
  *label_map["string_value"].mutable_string_value() = "abc";
  label_map["bool_value"].set_bool_value(true);
  label_map["number_value"].set_number_value(3.14);
  label_map["null_value"].set_null_value(::google::protobuf::NULL_VALUE);
  auto& list_value_values =
      *label_map["list_value"].mutable_list_value()->mutable_values();
  *list_value_values.Add()->mutable_string_value() = "efg";
  list_value_values.Add()->set_number_value(3.14);
  auto& struct_value_fields =
      *label_map["struct_value"].mutable_struct_value()->mutable_fields();
  struct_value_fields["bool_value"].set_bool_value(false);
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  ASSERT_TRUE(metadata_map.ok()) << metadata_map.status();
  ASSERT_EQ(metadata_map->size(), 1);
  auto* entry = metadata_map->Find("filter_key");
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(XdsStructMetadataValue::Type(), entry->type());
  EXPECT_THAT(DownCast<const XdsStructMetadataValue*>(entry)->json(),
              JsonEq("{"
                     "\"bool_value\":true,"
                     "\"list_value\":[\"efg\",3.14],"
                     "\"null_value\":null,"
                     "\"number_value\":3.14,"
                     "\"string_value\":\"abc\","
                     "\"struct_value\":{\"bool_value\":false}"
                     "}"));
}

TEST_F(XdsMetadataTest, TypedMetadataTakesPrecendenceOverUntyped) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_filter_metadata();
  auto& label_map = *filter_map["filter_key"].mutable_fields();
  *label_map["string_value"].mutable_string_value() = "abc";
  Audience audience_proto;
  audience_proto.set_url("foo");
  auto& typed_filter_map = *metadata_proto.mutable_typed_filter_metadata();
  typed_filter_map["filter_key"].PackFrom(audience_proto);
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  ASSERT_TRUE(metadata_map.ok()) << metadata_map.status();
  ASSERT_EQ(metadata_map->size(), 1);
  auto* entry = metadata_map->Find("filter_key");
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(XdsGcpAuthnAudienceMetadataValue::Type(), entry->type());
  EXPECT_EQ(DownCast<const XdsGcpAuthnAudienceMetadataValue*>(entry)->url(),
            "foo");
}

TEST_F(XdsMetadataTest, AudienceMetadata) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  Audience audience_proto;
  audience_proto.set_url("foo");
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_typed_filter_metadata();
  filter_map["filter_key"].PackFrom(audience_proto);
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  ASSERT_TRUE(metadata_map.ok()) << metadata_map.status();
  ASSERT_EQ(metadata_map->size(), 1);
  auto* entry = metadata_map->Find("filter_key");
  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(XdsGcpAuthnAudienceMetadataValue::Type(), entry->type());
  EXPECT_EQ(DownCast<const XdsGcpAuthnAudienceMetadataValue*>(entry)->url(),
            "foo");
}

TEST_F(XdsMetadataTest, AudienceMetadataUnparseable) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_typed_filter_metadata();
  auto& entry = filter_map["filter_key"];
  entry.PackFrom(Audience());
  entry.set_value(std::string("\0", 1));
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  EXPECT_EQ(metadata_map.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(metadata_map.status().message(),
            "validation failed: ["
            "field:typed_filter_metadata[filter_key].value["
            "envoy.extensions.filters.http.gcp_authn.v3.Audience] "
            "error:could not parse audience metadata]")
      << metadata_map.status();
}

TEST_F(XdsMetadataTest, AudienceMetadataMissingUrl) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_typed_filter_metadata();
  filter_map["filter_key"].PackFrom(Audience());
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  EXPECT_EQ(metadata_map.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(metadata_map.status().message(),
            "validation failed: ["
            "field:typed_filter_metadata[filter_key].value["
            "envoy.extensions.filters.http.gcp_authn.v3.Audience].url "
            "error:must be non-empty]")
      << metadata_map.status();
}

TEST_F(XdsMetadataTest, AudienceIgnoredIfNotEnabled) {
  Audience audience_proto;
  audience_proto.set_url("foo");
  Metadata metadata_proto;
  auto& filter_map = *metadata_proto.mutable_typed_filter_metadata();
  filter_map["filter_key"].PackFrom(audience_proto);
  // Decode.
  auto metadata_map = Decode(std::move(metadata_proto));
  ASSERT_TRUE(metadata_map.ok()) << metadata_map.status();
  EXPECT_EQ(metadata_map->size(), 0);
}

TEST_F(XdsMetadataTest, MetadataUnset) {
  auto metadata_map = Parse(nullptr);
  ASSERT_TRUE(metadata_map.ok()) << metadata_map.status();
  EXPECT_EQ(metadata_map->size(), 0);
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
