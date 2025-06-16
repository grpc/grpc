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

#include "src/core/xds/grpc/xds_matcher.h"

#include <google/protobuf/text_format.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// For XdsClient and DecodeContext setup
#include "src/core/util/json/json.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
// End XdsClient setup includes

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/common/matcher/v3/matcher.upb.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/validation_errors.h"  // For ValidationErrors
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "src/core/xds/grpc/xds_matcher_parse.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"  // For upb_DefPool

namespace grpc_core {
namespace testing {
namespace {

class MatcherTest : public ::testing::Test {
 protected:
  MatcherTest()
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
        "        {\"type\": \"fake\"}\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}");
    CHECK_OK(bootstrap.status()) << bootstrap.status().ToString();
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr,
                                     /*event_engine=*/nullptr,
                                     /*metrics_reporter=*/nullptr, "test_agent",
                                     "test_version");
  }

  const envoy_config_common_matcher_v3_Matcher* ConvertToUpb(
      const envoy::config::common::matcher::v3::Matcher& proto) {
    // Serialize the protobuf proto.
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    // Deserialize as upb proto.
    const auto* upb_proto = envoy_config_common_matcher_v3_Matcher_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }

  upb::Arena upb_arena_;
  upb::DefPool upb_def_pool_;
  RefCountedPtr<XdsClient> xds_client_;
  XdsResourceType::DecodeContext decode_context_;
};

TEST_F(MatcherTest, ParseEnd2End) {
  envoy::config::common::matcher::v3::Matcher matcher_proto;
  const char* text_proto = R"pb(
    matcher_list {
      matchers {
        predicate {
          single_predicate {
            input {
              name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
              typed_config {
                [type.googleapis.com/envoy.type.matcher.v3
                     .HttpRequestHeaderMatchInput] { header_name: "x-foo" }
              }
            }
            value_match { exact: "foo" }
          }
        }
        on_match {
          action {
            name: "on-match-action"
            typed_config {
              [type.googleapis.com/envoy.extensions.filters.http
                   .rate_limit_quota.v3.RateLimitQuotaBucketSettings] {
                bucket_id_builder {
                  bucket_id_builder {
                    key: "bucket-key-match"
                    value { string_value: "bucket-key-match" }
                  }
                }
              }
            }
          }
        }
      }
    }
    on_no_match {
      action {
        name: "on-no-match-action"
        typed_config {
          [type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3
               .RateLimitQuotaBucketSettings] {
            bucket_id_builder {
              bucket_id_builder {
                key: "bucket-key-nomatch"
                value { string_value: "bucket-val-nomatch" }
              }
            }
          }
        }
      }
    }
  )pb";
  auto val =
      google::protobuf::TextFormat::ParseFromString(text_proto, &matcher_proto);
  if (!val) {
    std::cout << matcher_proto.DebugString() << "\n";
  }
  EXPECT_TRUE(val);
  const auto* m_upb = ConvertToUpb(matcher_proto);
  ASSERT_NE(m_upb, nullptr);
  ValidationErrors errors;
  auto matcher = ParseMatcher(decode_context_, m_upb, &errors);
  ASSERT_NE(matcher, nullptr);
  ASSERT_EQ(matcher->getType(), XdsMatcher::MatcherType::MatcherList);
  auto matcher_list = DownCast<XdsMatcherList*>(matcher.get());
  // Fake RPC comtext to get metadata
  grpc_metadata_batch metadata;
  metadata.Append("x-foo", Slice::FromStaticString("foo"),
                  [](absl::string_view, const Slice&) {
                    // We should never ever see an error here.
                    abort();
                  });

  RpcMatchContext context(&metadata);
  // match
  XdsMatcher::Result result;
  ASSERT_TRUE(matcher_list->FindMatches(context, result));

  for (auto a : result) {
    ASSERT_EQ(a->type_url(), "sampleAction");
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
