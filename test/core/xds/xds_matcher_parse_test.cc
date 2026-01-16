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

#include "src/core/xds/grpc/xds_matcher_parse.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/wrappers.pb.h>

#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_action.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"  // For upb_DefPool
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/matcher.upb.h"
#include "xds/type/v3/typed_struct.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

using ::envoy::type::matcher::v3::HttpRequestHeaderMatchInput;
using ::google::protobuf::StringValue;
using ::xds::type::matcher::v3::Matcher;
using ::xds::type::v3::TypedStruct;

// A simple action that holds a string, used for verification.
class StringAction : public XdsMatcher::Action {
 public:
  explicit StringAction(std::string str) : str_(std::move(str)) {}
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE(
        "type.googleapis.com/google.protobuf.StringValue");
  }
  UniqueTypeName type() const override { return Type(); }
  const std::string& str() const { return str_; }
  bool Equals(const XdsMatcher::Action& other) const override {
    if (other.type() != type()) return false;
    return str_ == DownCast<const StringAction&>(other).str_;
  }
  std::string ToString() const override {
    return absl::StrCat("StringAction{str=", str_, "}");
  }

 private:
  std::string str_;
};

// Factory for the StringAction.
class StringActionFactory : public XdsMatcherActionFactory {
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
                        upb_def_pool_.ptr(), upb_arena_.ptr()} {
    action_registry_.AddActionFactory(std::make_unique<StringActionFactory>());
  }

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

  // Convert protobuf to upb
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

  upb::Arena upb_arena_;
  upb::DefPool upb_def_pool_;
  RefCountedPtr<XdsClient> xds_client_;
  XdsResourceType::DecodeContext decode_context_;
  XdsMatcherActionRegistry action_registry_;
};

//
// Success Cases
//

TEST_F(MatcherTest, MatcherListSinglePredicate) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = predicate->mutable_input();
  input->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  predicate->mutable_value_match()->set_exact("bar");
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=SinglePredicate{input=MetadataInput(key=foo), "
      "matcher=StringMatcher{exact=bar}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}}");
}

TEST_F(MatcherTest, MatcherListWithMultipleMatchers) {
  Matcher matcher_proto;
  // First matcher
  auto* field_matcher_1 = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* predicate_1 =
      field_matcher_1->mutable_predicate()->mutable_single_predicate();
  auto* input_1 = predicate_1->mutable_input();
  input_1->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input_1;
  http_request_header_match_input_1.set_header_name("foo");
  input_1->mutable_typed_config()->PackFrom(http_request_header_match_input_1);
  predicate_1->mutable_value_match()->set_exact("bar");
  auto* action_1 = field_matcher_1->mutable_on_match()->mutable_action();
  action_1->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value_1;
  string_value_1.set_value("foobar");
  action_1->mutable_typed_config()->PackFrom(string_value_1);
  // Second matcher
  auto* field_matcher_2 = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* predicate_2 =
      field_matcher_2->mutable_predicate()->mutable_single_predicate();
  auto* input_2 = predicate_2->mutable_input();
  input_2->set_name("baz");
  HttpRequestHeaderMatchInput http_request_header_match_input_2;
  http_request_header_match_input_2.set_header_name("baz");
  input_2->mutable_typed_config()->PackFrom(http_request_header_match_input_2);
  predicate_2->mutable_value_match()->set_prefix("qux");
  auto* action_2 = field_matcher_2->mutable_on_match()->mutable_action();
  action_2->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value_2;
  string_value_2.set_value("bazqux");
  action_2->mutable_typed_config()->PackFrom(string_value_2);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=SinglePredicate{input=MetadataInput(key=foo), "
      "matcher=StringMatcher{exact=bar}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}, "
      "{predicate=SinglePredicate{input=MetadataInput(key=baz), "
      "matcher=StringMatcher{prefix=qux}}, "
      "on_match={action=StringAction{str=bazqux}, keep_matching=false}}}");
}

TEST_F(MatcherTest, MatcherListSinglePredicateWithOnNoMatch) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = predicate->mutable_input();
  input->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  predicate->mutable_value_match()->set_exact("bar");
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // on_no_match
  auto* on_no_match_action =
      matcher_proto.mutable_on_no_match()->mutable_action();
  on_no_match_action->set_name(
      "type.googleapis.com/google.protobuf.StringValue");
  StringValue on_no_match_string_value;
  on_no_match_string_value.set_value("default-action");
  on_no_match_action->mutable_typed_config()->PackFrom(
      on_no_match_string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(matcher->ToString(),
            "XdsMatcherList{{predicate=SinglePredicate{input=MetadataInput(key="
            "foo), matcher=StringMatcher{exact=bar}}, "
            "on_match={action=StringAction{str=foobar}, keep_matching=false}}, "
            "on_no_match={action=StringAction{str=default-action}, "
            "keep_matching=false}}");
}

TEST_F(MatcherTest, MatcherListAndMatcher) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* and_matcher = field_matcher->mutable_predicate()->mutable_and_matcher();
  // Predicate 1
  auto* predicate1 = and_matcher->add_predicate()->mutable_single_predicate();
  auto* input1 = predicate1->mutable_input();
  input1->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input1;
  http_request_header_match_input1.set_header_name("foo");
  input1->mutable_typed_config()->PackFrom(http_request_header_match_input1);
  predicate1->mutable_value_match()->set_exact("bar");
  // Predicate 2
  auto* predicate2 = and_matcher->add_predicate()->mutable_single_predicate();
  auto* input2 = predicate2->mutable_input();
  input2->set_name("baz");
  HttpRequestHeaderMatchInput http_request_header_match_input2;
  http_request_header_match_input2.set_header_name("baz");
  input2->mutable_typed_config()->PackFrom(http_request_header_match_input2);
  predicate2->mutable_value_match()->set_prefix("qux");
  // Action
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=And{SinglePredicate{input=MetadataInput(key="
      "foo), matcher=StringMatcher{exact=bar}}, "
      "SinglePredicate{input=MetadataInput(key=baz), "
      "matcher=StringMatcher{prefix=qux}}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}}");
}

TEST_F(MatcherTest, MatcherListOrMatcher) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* and_matcher = field_matcher->mutable_predicate()->mutable_or_matcher();
  // Predicate 1
  auto* predicate1 = and_matcher->add_predicate()->mutable_single_predicate();
  auto* input1 = predicate1->mutable_input();
  input1->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input1;
  http_request_header_match_input1.set_header_name("foo");
  input1->mutable_typed_config()->PackFrom(http_request_header_match_input1);
  predicate1->mutable_value_match()->set_exact("bar");
  // Predicate 2
  auto* predicate2 = and_matcher->add_predicate()->mutable_single_predicate();
  auto* input2 = predicate2->mutable_input();
  input2->set_name("baz");
  HttpRequestHeaderMatchInput http_request_header_match_input2;
  http_request_header_match_input2.set_header_name("baz");
  input2->mutable_typed_config()->PackFrom(http_request_header_match_input2);
  predicate2->mutable_value_match()->set_prefix("qux");
  // Action
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=Or{SinglePredicate{input=MetadataInput(key="
      "foo), matcher=StringMatcher{exact=bar}}, "
      "SinglePredicate{input=MetadataInput(key=baz), "
      "matcher=StringMatcher{prefix=qux}}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}}");
}

TEST_F(MatcherTest, MatcherListNotMatcher) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* not_matcher = field_matcher->mutable_predicate()->mutable_not_matcher();
  auto* predicate = not_matcher->mutable_single_predicate();
  auto* input = predicate->mutable_input();
  input->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  predicate->mutable_value_match()->set_exact("bar");
  // Action
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=Not{SinglePredicate{input=MetadataInput(key="
      "foo), matcher=StringMatcher{exact=bar}}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}}");
}

TEST_F(MatcherTest, MatcherTreeExactMatchMap) {
  Matcher matcher_proto;
  auto* tree = matcher_proto.mutable_matcher_tree();
  auto* input = tree->mutable_input();
  input->set_name("my-input");
  HttpRequestHeaderMatchInput header_match_input;
  header_match_input.set_header_name("my-header");
  input->mutable_typed_config()->PackFrom(header_match_input);
  auto* map = tree->mutable_exact_match_map()->mutable_map();
  // Entry 1
  auto* action1 = (*map)["match1"].mutable_action();
  action1->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value1;
  string_value1.set_value("matched-1");
  action1->mutable_typed_config()->PackFrom(string_value1);
  // Entry 2
  auto* action2 = (*map)["match2"].mutable_action();
  action2->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value2;
  string_value2.set_value("matched-2");
  action2->mutable_typed_config()->PackFrom(string_value2);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(matcher->ToString(),
            "XdsMatcherExactMap{input=MetadataInput(key=my-header), "
            "map={{\"match1\": {action=StringAction{str=matched-1}, "
            "keep_matching=false}}, {\"match2\": "
            "{action=StringAction{str=matched-2}, keep_matching=false}}}}");
}

TEST_F(MatcherTest, MatcherTreePrefixMatchMap) {
  Matcher matcher_proto;
  auto* tree = matcher_proto.mutable_matcher_tree();
  auto* input = tree->mutable_input();
  input->set_name("my-input");
  HttpRequestHeaderMatchInput header_match_input;
  header_match_input.set_header_name("my-header");
  input->mutable_typed_config()->PackFrom(header_match_input);
  auto* map = tree->mutable_prefix_match_map()->mutable_map();
  // Entry 1
  auto* action1 = (*map)["match1"].mutable_action();
  action1->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value1;
  string_value1.set_value("matched-1");
  action1->mutable_typed_config()->PackFrom(string_value1);
  // Entry 2
  auto* action2 = (*map)["match2"].mutable_action();
  action2->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value2;
  string_value2.set_value("matched-2");
  action2->mutable_typed_config()->PackFrom(string_value2);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(matcher->ToString(),
            "XdsMatcherPrefixMap{input=MetadataInput(key=my-header), "
            "map={{\"match1\": {action=StringAction{str=matched-1}, "
            "keep_matching=false}}, {\"match2\": "
            "{action=StringAction{str=matched-2}, keep_matching=false}}}}");
}

TEST_F(MatcherTest, NestedMatcher) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = predicate->mutable_input();
  input->set_name("foo");
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  predicate->mutable_value_match()->set_exact("bar");
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // on_no_match as another Matcher
  auto* on_no_match_field_matcher = matcher_proto.mutable_on_no_match()
                                        ->mutable_matcher()
                                        ->mutable_matcher_list()
                                        ->add_matchers();
  auto* on_no_match_predicate = on_no_match_field_matcher->mutable_predicate()
                                    ->mutable_single_predicate();
  auto* on_no_match_input = on_no_match_predicate->mutable_input();
  on_no_match_input->set_name("default");
  HttpRequestHeaderMatchInput on_no_match_http_request_header_match_input;
  on_no_match_http_request_header_match_input.set_header_name("default");
  on_no_match_input->mutable_typed_config()->PackFrom(
      on_no_match_http_request_header_match_input);
  on_no_match_predicate->mutable_value_match()->set_exact("baz");
  auto* on_no_match_action =
      on_no_match_field_matcher->mutable_on_match()->mutable_action();
  on_no_match_action->set_name(
      "type.googleapis.com/google.protobuf.StringValue");
  StringValue on_no_match_string_value;
  on_no_match_string_value.set_value("default-matcher");
  on_no_match_action->mutable_typed_config()->PackFrom(
      on_no_match_string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_TRUE(errors.ok())
      << errors.status(absl::StatusCode::kInvalidArgument, "").message();
  EXPECT_NE(matcher, nullptr);
  EXPECT_EQ(
      matcher->ToString(),
      "XdsMatcherList{{predicate=SinglePredicate{input=MetadataInput(key=foo), "
      "matcher=StringMatcher{exact=bar}}, "
      "on_match={action=StringAction{str=foobar}, keep_matching=false}}, "
      "on_no_match={matcher=XdsMatcherList{{predicate=SinglePredicate{input="
      "MetadataInput(key=default), matcher=StringMatcher{exact=baz}}, "
      "on_match={action=StringAction{str=default-matcher}, "
      "keep_matching=false}}}, keep_matching=false}}");
}

//
// Error Cases
//

TEST_F(MatcherTest, EmptyMatcher) {
  Matcher matcher_proto;
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field: error:no matcher_list or matcher_tree specified.]");
}

TEST_F(MatcherTest, EmptyMatcherList) {
  Matcher matcher_proto;
  matcher_proto.mutable_matcher_list();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list error:matcher_list is empty]");
}

TEST_F(MatcherTest, MatchTreeNoInputEmptyMap) {
  Matcher matcher_proto;
  matcher_proto.mutable_matcher_tree()->mutable_exact_match_map();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_tree.exact_match_map error:map is empty; "
            "field:matcher_tree.input error:field not present]");
}

TEST_F(MatcherTest, MatcherTreeUnknown) {
  // Both unknown and custom
  Matcher matcher_proto;
  auto* tree = matcher_proto.mutable_matcher_tree();
  auto* input = tree->mutable_input();
  input->set_name("some_input");
  HttpRequestHeaderMatchInput header_match_input;
  header_match_input.set_header_name("some_header");
  input->mutable_typed_config()->PackFrom(header_match_input);
  tree->mutable_custom_match();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_tree error:no known match tree type specified]");
}

TEST_F(MatcherTest, MatcherListFieldMatcherEmpty) {
  Matcher matcher_proto;
  matcher_proto.mutable_matcher_list()->add_matchers();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ": [field:matcher_list.matchers[0].on_match error:field not present; "
      "field:matcher_list.matchers[0].predicate error:field not present]");
}

TEST_F(MatcherTest, MatcherListFieldUnsupportedPredicate) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  field_matcher->mutable_predicate();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list.matchers[0].predicate error:unsupported "
            "predicate type]");
}

TEST_F(MatcherTest, MatcherListEmptyPredicateList) {
  // IF list is empty for AND/OR Matcher
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // And matcher but empty list
  field_matcher->mutable_predicate()->mutable_and_matcher();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list.matchers[0].predicate.and_matcher "
            "error:predicate_list is empty]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateEmpty) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Custom single predicate
  field_matcher->mutable_predicate()
      ->mutable_single_predicate()
      ->mutable_custom_match();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ": [field:matcher_list.matchers[0].predicate.single_predicate.input "
      "error:field not present;"
      " field:matcher_list.matchers[0].predicate.single_predicate.value_match "
      "error:field not present]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateInvalidValueMatch) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  // Invalid string matcher
  single_predicate->mutable_value_match();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": "
            "[field:matcher_list.matchers[0].predicate.single_predicate.value_"
            "match error:invalid string matcher]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateInvalidStringMatcherRegex) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  // Invalid string matcher (invalid regex)
  single_predicate->mutable_value_match()->mutable_safe_regex()->set_regex("[");
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_THAT(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ::testing::HasSubstr("field:matcher_list.matchers[0].predicate.single_"
                           "predicate.value_match error:Invalid regex string "
                           "specified in matcher: missing ]: []"));
}

// TODO(bpawan) : Add test for input type that produces something other than a
// string used with a string matcher
TEST_F(MatcherTest, MatcherListSinglePredicateInvalidInput) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  single_predicate->mutable_value_match()->set_exact("foo");
  // Invalid input
  auto* input = single_predicate->mutable_input();
  input->set_name("invalid");
  input->mutable_typed_config();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": "
            "[field:matcher_list.matchers[0].predicate.single_predicate.input."
            "type_url error:field not present]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateInputTypeNotInRegistry) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  single_predicate->mutable_value_match()->set_exact("foo");
  // Input with type not in registry
  auto* input = single_predicate->mutable_input();
  input->set_name("invalid");
  google::protobuf::BoolValue bool_value;
  bool_value.set_value(true);
  input->mutable_typed_config()->PackFrom(bool_value);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": "
            "[field:matcher_list.matchers[0].predicate.single_predicate.input."
            "value[google.protobuf.BoolValue] error:Unsupported Input "
            "type:google.protobuf.BoolValue]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateInputTypedStruct) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  single_predicate->mutable_value_match()->set_exact("foo");
  // Invalid input of type typed struct
  auto* input = single_predicate->mutable_input();
  input->set_name("my-typed-struct-input");
  TypedStruct typed_struct;
  typed_struct.set_type_url(
      "type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  input->mutable_typed_config()->PackFrom(typed_struct);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ": [field:matcher_list.matchers[0].predicate.single_predicate.input.value"
      "[xds.type.v3.TypedStruct].value[envoy.type.matcher.v3."
      "HttpRequestHeaderMatchInput]"
      " error:Unsuppored input format (Json found instead of string)]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateInputContextDifferent) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  ValidationErrors errors;
  auto matcher = ParseXdsMatcher(
      decode_context_, ConvertToUpb(matcher_proto), action_registry_,
      GRPC_UNIQUE_TYPE_NAME_HERE("invalid"), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": "
            "[field:matcher_list.matchers[0].predicate.single_predicate.input."
            "value[envoy.type.matcher.v3.HttpRequestHeaderMatchInput] "
            "error:Unsupported context:rpc_context. Parser supported "
            "context:invalid]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateActionTypeStruct) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  // Action typed struct
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("my-typed-struct-input");
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/google.protobuf.StringValue");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  action->mutable_typed_config()->PackFrom(typed_struct);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": "
            "[field:matcher_list.matchers[0].on_match.action.value[xds.type.v3."
            "TypedStruct].value[google.protobuf.StringValue]"
            " error:Unsuppored action format (Json found instead of string)]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateActionUnsupported) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  // Creating invalid action not in registry,
  // (using header input as action for test)
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("invalid_action");
  action->mutable_typed_config()->PackFrom(http_request_header_match_input);
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ": [field:matcher_list.matchers[0].on_match.action.value[envoy.type."
      "matcher.v3.HttpRequestHeaderMatchInput] "
      "error:Unsupported Action. Not found in registry]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateOnMatchEmpty) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  // On Match
  field_matcher->mutable_on_match();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list.matchers[0].on_match error:One of action or "
            "matcher should be present]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateOnMatchEmptyMatcher) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  // On Match with empty nested matcher
  field_matcher->mutable_on_match()->mutable_matcher();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list.matchers[0].on_match.matcher error:no "
            "matcher_list or matcher_tree specified.]");
}

TEST_F(MatcherTest, MatcherListSinglePredicateOnMatchEmptyAction) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  // On Match
  field_matcher->mutable_on_match()->mutable_action();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_list.matchers[0].on_match.action error:field not "
            "present]");
}

TEST_F(MatcherTest, MatcherOnNoMatchError) {
  Matcher matcher_proto;
  auto* field_matcher = matcher_proto.mutable_matcher_list()->add_matchers();
  // Single Predicate
  HttpRequestHeaderMatchInput http_request_header_match_input;
  http_request_header_match_input.set_header_name("foo");
  auto* single_predicate =
      field_matcher->mutable_predicate()->mutable_single_predicate();
  auto* input = single_predicate->mutable_input();
  input->set_name("foo");
  input->mutable_typed_config()->PackFrom(http_request_header_match_input);
  single_predicate->mutable_value_match()->set_exact("foo");
  auto* action = field_matcher->mutable_on_match()->mutable_action();
  action->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value;
  string_value.set_value("foobar");
  action->mutable_typed_config()->PackFrom(string_value);
  // Add On No Match with error
  matcher_proto.mutable_on_no_match();
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "").message(),
      ": [field:on_no_match error:One of action or matcher should be present]");
}

TEST_F(MatcherTest, ExceedsMaxDepth) {
  // Construct proto.
  Matcher matcher_proto;
  auto* tree = matcher_proto.mutable_matcher_tree();
  auto* input = tree->mutable_input();
  input->set_name("my-input");
  HttpRequestHeaderMatchInput header_match_input;
  header_match_input.set_header_name("my-header");
  input->mutable_typed_config()->PackFrom(header_match_input);
  auto* map = tree->mutable_exact_match_map()->mutable_map();
  auto* on_match = &(*map)["match1"];
  for (size_t i = 0; i < 16; ++i) {
    Matcher* next_matcher_proto = on_match->mutable_matcher();
    tree = next_matcher_proto->mutable_matcher_tree();
    *tree->mutable_input() = *input;
    map = tree->mutable_exact_match_map()->mutable_map();
    on_match = &(*map)["match1"];
  }
  auto* action1 = on_match->mutable_action();
  action1->set_name("type.googleapis.com/google.protobuf.StringValue");
  StringValue string_value1;
  string_value1.set_value("matched-1");
  action1->mutable_typed_config()->PackFrom(string_value1);
  // Try parsing.
  ValidationErrors errors;
  auto matcher =
      ParseXdsMatcher(decode_context_, ConvertToUpb(matcher_proto),
                      action_registry_, RpcMatchContext::Type(), true, &errors);
  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(errors.status(absl::StatusCode::kInvalidArgument, "").message(),
            ": [field:matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher."
            "matcher_tree.exact_match_map.on_match.matcher "
            "error:matcher tree exceeds max recursion depth]");
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
