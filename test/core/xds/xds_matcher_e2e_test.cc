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

#include <google/protobuf/text_format.h>
#include <google/protobuf/wrappers.pb.h>

#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "google/protobuf/wrappers.upb.h"
#include "gtest/gtest.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"  // For ValidationErrors
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_action.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "src/core/xds/grpc/xds_matcher_parse.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"  // For upb_DefPool
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/matcher.upb.h"

namespace grpc_core {
namespace testing {
namespace {

// A simple action that holds a string, used for verification.
class StringAction : public XdsMatcher::Action {
 public:
  explicit StringAction(std::string str) : str_(std::move(str)) {}
  absl::string_view type_url() const override {
    return "type.googleapis.com/google.protobuf.StringValue";
  }
  const std::string& str() const { return str_; }
  bool Equal(const XdsMatcher::Action& other) const override {
    if (other.type_url() != type_url()) return false;
    return str_ == static_cast<const StringAction&>(other).str_;
  }

 private:
  std::string str_;
};

// Factory for the StringAction.
class StringActionFactory : public ActionFactory {
 public:
  absl::string_view type() const override {
    return "google.protobuf.StringValue";
  }

  // Parses a google.protobuf.StringValue proto.
  std::unique_ptr<XdsMatcher::Action> ParseAndCreateAction(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const override {
    const auto* string_proto = google_protobuf_StringValue_parse(
        serialized_value.data(), serialized_value.size(), context.arena);
    if (string_proto == nullptr) {
      errors->AddError("could not parse google.protobuf.StringValue");
      return nullptr;
    }
    auto string_value =
        UpbStringToStdString(google_protobuf_StringValue_value(string_proto));
    return std::make_unique<StringAction>(std::move(string_value));
  }
};

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

  const xds_type_matcher_v3_Matcher* ConvertToUpb(
      const xds::type::matcher::v3::Matcher& proto) {
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    const auto* upb_proto = xds_type_matcher_v3_Matcher_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }

  // Helper to parse the proto and return a validated matcher list
  std::unique_ptr<XdsMatcher> ParseMatcherProto(const std::string& text_proto) {
    xds::type::matcher::v3::Matcher matcher_proto;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(text_proto,
                                                              &matcher_proto));

    const auto* m_upb = ConvertToUpb(matcher_proto);
    EXPECT_NE(m_upb, nullptr) << "Failed to convert to UPB";
    if (m_upb == nullptr) return nullptr;

    ValidationErrors errors;
    auto context_name = RpcMatchContext::Type();
    // Create registry and register the test-only factory.
    XdsMatcherActionRegistry action_registry;
    action_registry.AddActionFactory(std::make_unique<StringActionFactory>());
    auto matcher = ParseXdsMatcher(decode_context_, m_upb, action_registry,
                                   context_name, &errors);
    EXPECT_TRUE(errors.ok()) << errors.status(
        absl::StatusCode::kInvalidArgument, "unexpected errors");
    EXPECT_NE(matcher, nullptr);
    return matcher;
  }

  // Helper function to parse a matcher proto and check for expected errors.
  void ParseMatcherProtoAndExpectError(const std::string& text_proto,
                                       const std::string& expected_error) {
    xds::type::matcher::v3::Matcher matcher_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(text_proto,
                                                              &matcher_proto));
    const auto* m_upb = ConvertToUpb(matcher_proto);
    ASSERT_NE(m_upb, nullptr);

    ValidationErrors errors;
    auto context_name = RpcMatchContext::Type();
    XdsMatcherActionRegistry action_registry;
    action_registry.AddActionFactory(std::make_unique<StringActionFactory>());
    auto matcher = ParseXdsMatcher(decode_context_, m_upb, action_registry,
                                   context_name, &errors);
    absl::Status status = errors.status(absl::StatusCode::kInvalidArgument,
                                        "Matcher parsing failed");
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(),
              absl::StrCat("Matcher parsing failed: [", expected_error, "]"))
        << status;
  }

  // Helper to append metadata safely
  void AppendMetadata(grpc_metadata_batch& batch, absl::string_view key,
                      absl::string_view value) {
    batch.Append(key, Slice::FromCopiedString(value),
                 [](absl::string_view, const Slice&) { abort(); });
  }

  // Helper to verify a match result
  void VerifyMatchResult(XdsMatcher* matcher, grpc_metadata_batch& metadata,
                         const std::string& expected_action_str) {
    ASSERT_NE(matcher, nullptr);
    auto* matcher_list = DownCast<XdsMatcherList*>(matcher);
    ASSERT_NE(matcher_list, nullptr);

    RpcMatchContext context(&metadata);
    XdsMatcher::Result result;
    ASSERT_TRUE(matcher_list->FindMatches(context, result));
    ASSERT_EQ(result.size(), 1);

    const char* kExpectedTypeUrl =
        "type.googleapis.com/google.protobuf.StringValue";
    ASSERT_EQ(result[0]->type_url(), kExpectedTypeUrl);

    auto* string_action = DownCast<StringAction*>(result[0]);
    ASSERT_NE(string_action, nullptr);
    ASSERT_EQ(string_action->str(), expected_action_str);
  }

  upb::Arena upb_arena_;
  upb::DefPool upb_def_pool_;
  RefCountedPtr<XdsClient> xds_client_;
  XdsResourceType::DecodeContext decode_context_;
};

TEST_F(MatcherTest, ParseEnd2End) {
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
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
    on_no_match {
      action {
        name: "on_no_match_action"
        typed_config {
          [type.googleapis.com/google.protobuf.StringValue] {
            value: "nomatch_action"
          }
        }
      }
    }
  )pb";
  auto matcher = ParseMatcherProto(text_proto);

  // Match case
  grpc_metadata_batch metadata_match;
  AppendMetadata(metadata_match, "x-foo", "foo");
  VerifyMatchResult(matcher.get(), metadata_match, "match_action");

  // No-match case
  grpc_metadata_batch metadata_nomatch;
  VerifyMatchResult(matcher.get(), metadata_nomatch, "nomatch_action");
}

TEST_F(MatcherTest, ParseAndMatcherEnd2End) {
  const char* text_proto = R"pb(
    matcher_list {
      matchers {
        predicate {
          and_matcher {
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
            predicate {
              single_predicate {
                input {
                  name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
                  typed_config {
                    [type.googleapis.com/envoy.type.matcher.v3
                         .HttpRequestHeaderMatchInput] { header_name: "x-bar" }
                  }
                }
                value_match { exact: "bar" }
              }
            }
          }
        }
        on_match {
          action {
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
    on_no_match {
      action {
        name: "on_no_match_action"
        typed_config {
          [type.googleapis.com/google.protobuf.StringValue] {
            value: "nomatch_action"
          }
        }
      }
    }
  )pb";
  auto matcher = ParseMatcherProto(text_proto);

  // Match case: Both headers match.
  grpc_metadata_batch metadata_match;
  AppendMetadata(metadata_match, "x-foo", "foo");
  AppendMetadata(metadata_match, "x-bar", "bar");
  VerifyMatchResult(matcher.get(), metadata_match, "match_action");

  // No match case 1: One header missing.
  grpc_metadata_batch metadata_nomatch1;
  AppendMetadata(metadata_nomatch1, "x-foo", "foo");
  VerifyMatchResult(matcher.get(), metadata_nomatch1, "nomatch_action");

  // No match case 2: Both headers missing.
  grpc_metadata_batch metadata_nomatch2;
  VerifyMatchResult(matcher.get(), metadata_nomatch2, "nomatch_action");

  // No match case 3: One header matches, but value is wrong.
  grpc_metadata_batch metadata_nomatch3;
  AppendMetadata(metadata_nomatch3, "x-foo", "foo");
  AppendMetadata(metadata_nomatch3, "x-bar", "wrong");
  VerifyMatchResult(matcher.get(), metadata_nomatch3, "nomatch_action");
}

TEST_F(MatcherTest, ParseOrMatcherEnd2End) {
  const char* text_proto = R"pb(
    matcher_list {
      matchers {
        predicate {
          or_matcher {
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
            predicate {
              single_predicate {
                input {
                  name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
                  typed_config {
                    [type.googleapis.com/envoy.type.matcher.v3
                         .HttpRequestHeaderMatchInput] { header_name: "x-bar" }
                  }
                }
                value_match { exact: "bar" }
              }
            }
          }
        }
        on_match {
          action {
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
    on_no_match {
      action {
        name: "on_no_match_action"
        typed_config {
          [type.googleapis.com/google.protobuf.StringValue] {
            value: "nomatch_action"
          }
        }
      }
    }
  )pb";
  auto matcher = ParseMatcherProto(text_proto);

  // Match case 1: First header matches.
  grpc_metadata_batch metadata_match1;
  AppendMetadata(metadata_match1, "x-foo", "foo");
  VerifyMatchResult(matcher.get(), metadata_match1, "match_action");

  // Match case 2: Second header matches.
  grpc_metadata_batch metadata_match2;
  AppendMetadata(metadata_match2, "x-bar", "bar");
  VerifyMatchResult(matcher.get(), metadata_match2, "match_action");

  // Match case 3: Both headers match.
  grpc_metadata_batch metadata_match3;
  AppendMetadata(metadata_match3, "x-foo", "foo");
  AppendMetadata(metadata_match3, "x-bar", "bar");
  VerifyMatchResult(matcher.get(), metadata_match3, "match_action");

  // No match case: Neither header matches.
  grpc_metadata_batch metadata_nomatch;
  AppendMetadata(metadata_nomatch, "x-baz", "baz");
  VerifyMatchResult(matcher.get(), metadata_nomatch, "nomatch_action");
}

TEST_F(MatcherTest, ParseNotMatcherEnd2End) {
  const char* text_proto = R"pb(
    matcher_list {
      matchers {
        predicate {
          not_matcher {
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
        }
        on_match {
          action {
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
    on_no_match {
      action {
        name: "on_no_match_action"
        typed_config {
          [type.googleapis.com/google.protobuf.StringValue] {
            value: "nomatch_action"
          }
        }
      }
    }
  )pb";
  auto matcher = ParseMatcherProto(text_proto);

  // Match case: Inner predicate does NOT match.
  grpc_metadata_batch metadata_match;
  AppendMetadata(metadata_match, "x-foo", "bar");
  VerifyMatchResult(matcher.get(), metadata_match, "match_action");

  // No match case: Inner predicate matches.
  grpc_metadata_batch metadata_nomatch;
  AppendMetadata(metadata_nomatch, "x-foo", "foo");
  VerifyMatchResult(matcher.get(), metadata_nomatch, "nomatch_action");
}

TEST_F(MatcherTest, OnMatchUnknownField) {
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
        on_match {}
      }
    }
  )pb";
  ParseMatcherProtoAndExpectError(
      text_proto,
      "field:matcher.matcher_list.matchers[0].on_match "
      "error:Unknown field in OnMatch");
}

TEST_F(MatcherTest, SinglePredicateNoValueMatch) {
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
          }
        }
        on_match {
          action {
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
  )pb";
  ParseMatcherProtoAndExpectError(
      text_proto,
      "field:matcher.matcher_list.matchers[0].predicate.single_predicate "
      "error:only value match supported");
}

TEST_F(MatcherTest, PredicateUnsupportedType) {
  const char* text_proto = R"pb(
    matcher_list {
      matchers {
        predicate {}
        on_match {
          action {
            name: "on_match_action"
            typed_config {
              [type.googleapis.com/google.protobuf.StringValue] {
                value: "match_action"
              }
            }
          }
        }
      }
    }
  )pb";
  ParseMatcherProtoAndExpectError(
      text_proto,
      "field:matcher.matcher_list.matchers[0].predicate "
      "error:unsupported predicate type");
}

TEST_F(MatcherTest, EmptyMatcherList) {
  const char* text_proto = R"pb(
    matcher_list {}
  )pb";
  ParseMatcherProtoAndExpectError(text_proto,
                                  "field:matcher.matcher_list "
                                  "error:matcher_list is empty");
}

TEST_F(MatcherTest, EmptyExactMatchMap) {
  const char* text_proto = R"pb(
    matcher_tree {
      input {
        name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
        typed_config {
          [type.googleapis.com/envoy.type.matcher.v3
               .HttpRequestHeaderMatchInput] { header_name: "x-foo" }
        }
      }
      exact_match_map {}
    }
  )pb";
  ParseMatcherProtoAndExpectError(text_proto,
                                  "field:matcher.matcher_tree.exact_match_map "
                                  "error:map is empty");
}

TEST_F(MatcherTest, CustomMatchUnsupported) {
  const char* text_proto = R"pb(
    matcher_tree {
      input {
        name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
        typed_config {
          [type.googleapis.com/envoy.type.matcher.v3
               .HttpRequestHeaderMatchInput] { header_name: "x-foo" }
        }
      }
      custom_match {}
    }
  )pb";
  ParseMatcherProtoAndExpectError(text_proto,
                                  "field:matcher.matcher_tree "
                                  "error:no known match tree type specified");
}

TEST_F(MatcherTest, NoMatcherType) {
  const char* text_proto = R"pb(
  )pb";
  ParseMatcherProtoAndExpectError(text_proto,
                                  "field:matcher error:no matcher_list or "
                                  "matcher_tree specified.");
}

TEST_F(MatcherTest, MatcherTreeNoMatchType) {
  const char* text_proto = R"pb(
    matcher_tree {
      input {
        name: "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
        typed_config {
          [type.googleapis.com/envoy.type.matcher.v3
               .HttpRequestHeaderMatchInput] { header_name: "x-foo" }
        }
      }
    }
  )pb";
  ParseMatcherProtoAndExpectError(text_proto,
                                  "field:matcher.matcher_tree error:no "
                                  "known match tree type specified");
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
