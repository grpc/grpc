
#include <grpc/grpc.h>
#include <google/protobuf/text_format.h>
#include <functional>

#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.pb.h"

#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::ElementsAre;

class MatcherTest : public ::testing::Test {
     protected:
  MatcherTest() {}
  const xds_type_matcher_v3_Matcher* ConvertToUpb(xds::type::matcher::v3::Matcher proto) {
    // Serialize the protobuf proto.
    std::string serialized_proto;
    if (!proto.SerializeToString(&serialized_proto)) {
      EXPECT_TRUE(false) << "protobuf serialization failed";
      return nullptr;
    }
    // Deserialize as upb proto.
    const auto* upb_proto = xds_type_matcher_v3_Matcher_parse(
        serialized_proto.data(), serialized_proto.size(), upb_arena_.ptr());
    if (upb_proto == nullptr) {
      EXPECT_TRUE(false) << "upb parsing failed";
      return nullptr;
    }
    return upb_proto;
  }
  upb::Arena upb_arena_;
};

// Dummy Action for testing
class TestAction : public matcher::Action {
public:
    explicit TestAction(std::string id) : id_(std::move(id)) {}
    std::string id() const { return id_; }
private:
    std::string id_;
};

// Helper to track executed actions for MatcherList tests
std::vector<std::string> executed_actions;
matcher::ActionCb CreateTrackedActionCb(const std::string& id) {
    return [&, id]() {
        executed_actions.push_back(id);
        return std::make_unique<TestAction>(id);
    };
}

TEST_F(MatcherTest, ParseCheck) {
  xds::type::matcher::v3::Matcher matcher;
  std::string text_proto = R"pb(
  matcher_list {
  matchers {
    predicate {
      single_predicate {
        input {
          name: "envoy.matching.inputs.uri_path"
          typed_config {
            "@type": "type.googleapis.com/envoy.extensions.matching.common_inputs.uri_path.v3.UriPathInput"
          }
        }
        value_match {
          exact: "/foo"
        }
      }
    }
    on_match {
      action {
        name: "my_action_name"
        typed_config {
          "@type": "type.googleapis.com/google.protobuf.StringValue"
          value: "matched_foo_path"
        }
      }
    }
  }
}
on_no_match {
  action {
    name: "default_action"
    typed_config {
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "no_match_found"
    }
  }
}
)pb";
std::string text_proto1 = R"pb(
  matcher_tree {
  input {
    name: "envoy.matching.inputs.request_headers"
    typed_config {
      "@type": "type.googleapis.com/envoy.extensions.matching.common_inputs.request_headers.v3.HttpRequestHeadersInput"
      header_name: ":authority"
    }
  }
  exact_match_map {
    map {
      key: "example.com"
      value {
        action {
          name: "domain_match_action"
          typed_config {
            "@type": "type.googleapis.com/google.protobuf.StringValue"
            value: "matched_example_domain"
          }
        }
      }
    }
    map {
      key: "test.org"
      value {
        action {
          name: "test_org_action"
          typed_config {
            "@type": "type.googleapis.com/google.protobuf.StringValue"
            value: "matched_test_org"
          }
        }
      }
    }
  }
}
)pb";
  google::protobuf::TextFormat::ParseFromString(text_proto,&matcher);
  const auto *m_upb = ConvertToUpb(matcher);
  ASSERT_EQ(matcher::parseFunc(m_upb), 1);
}

TEST_F(MatcherTest, ExactMatcher) {
    matcher::ExactMatcher exact_matcher("hello");
    EXPECT_TRUE(exact_matcher.match("hello"));
    EXPECT_FALSE(exact_matcher.match("world"));
    EXPECT_FALSE(exact_matcher.match("hell"));
    EXPECT_FALSE(exact_matcher.match("helloo"));
    EXPECT_FALSE(exact_matcher.match(""));

    matcher::ExactMatcher empty_matcher("");
    EXPECT_TRUE(empty_matcher.match(""));
    EXPECT_FALSE(empty_matcher.match("hello"));
}

TEST_F(MatcherTest, HttpData) {
    matcher::HttpData http_data("test_payload");
    // The parameter to GetInput is ignored by HttpData's current implementation.
    EXPECT_EQ(http_data.GetInput("ignored_param"), "test_payload");
    EXPECT_EQ(http_data.GetInput(""), "test_payload");

    matcher::HttpData empty_http_data("");
    EXPECT_EQ(empty_http_data.GetInput("ignored_param"), "");
}

TEST_F(MatcherTest, SinglePredicate) {
    auto data_input = std::make_unique<matcher::HttpData>("match_this");
    auto input_matcher = std::make_unique<matcher::ExactMatcher>("match_this");
    matcher::SinglePredicate<matcher::MatchDataType> predicate(std::move(data_input), std::move(input_matcher));
    EXPECT_TRUE(predicate.match("ignored_data_for_HttpData"));

    auto data_input_no_match = std::make_unique<matcher::HttpData>("match_this");
    auto input_matcher_no_match = std::make_unique<matcher::ExactMatcher>("dont_match_this");
    matcher::SinglePredicate<matcher::MatchDataType> predicate_no_match(std::move(data_input_no_match), std::move(input_matcher_no_match));
    EXPECT_FALSE(predicate_no_match.match("ignored_data_for_HttpData"));

    // Test with DataInput returning nullopt (not directly possible with HttpData,
    // but SinglePredicate handles it)
    class NullOptDataInput : public matcher::DataInput<matcher::MatchDataType> {
    public:
        std::optional<matcher::MatchDataType> GetInput(const matcher::MatchDataType&) override {
            return std::nullopt;
        }
    };
    auto nullopt_data_input = std::make_unique<NullOptDataInput>();
    auto exact_matcher_for_nullopt = std::make_unique<matcher::ExactMatcher>("any_value");
    matcher::SinglePredicate<matcher::MatchDataType> predicate_nullopt_data(std::move(nullopt_data_input), std::move(exact_matcher_for_nullopt));
    EXPECT_FALSE(predicate_nullopt_data.match("any_data"));

    // Test with null DataInput
    std::unique_ptr<matcher::DataInput<matcher::MatchDataType>> null_data_input_ptr = nullptr;
    auto exact_matcher_for_null_di = std::make_unique<matcher::ExactMatcher>("any_value");
    matcher::SinglePredicate<matcher::MatchDataType> predicate_null_di(std::move(null_data_input_ptr), std::move(exact_matcher_for_null_di));
    EXPECT_FALSE(predicate_null_di.match("any_data"));

    // Test with null InputMatcher
    auto http_data_for_null_im = std::make_unique<matcher::HttpData>("any_value");
    std::unique_ptr<matcher::InputMatcher> null_input_matcher_ptr = nullptr;
    matcher::SinglePredicate<matcher::MatchDataType> predicate_null_im(std::move(http_data_for_null_im), std::move(null_input_matcher_ptr));
    EXPECT_FALSE(predicate_null_im.match("any_data"));
}

std::unique_ptr<matcher::Predicate<matcher::MatchDataType>> CreateExactSinglePredicate(const std::string& data_to_provide, const std::string& value_to_match) {
    return std::make_unique<matcher::SinglePredicate<matcher::MatchDataType>>(
        std::make_unique<matcher::HttpData>(data_to_provide),
        std::make_unique<matcher::ExactMatcher>(value_to_match)
    );
}

TEST_F(MatcherTest, AndMatcher) {
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> matchers;
    matchers.push_back(CreateExactSinglePredicate("data1", "data1"));
    matchers.push_back(CreateExactSinglePredicate("data2", "data2")); // This HttpData will be used by AndMatcher
    matcher::AndMatcher<matcher::MatchDataType> and_matcher(std::move(matchers));
    // AndMatcher uses the input "data2" for all its sub-predicates.
    // The first predicate expects "data1" but gets "data2" from HttpData("data2") -> false
    // The second predicate expects "data2" and gets "data2" from HttpData("data2") -> true
    // So, this test needs to be rethought. AndMatcher passes the *same* input data to all sub-matchers.
    // Let's make HttpData return the input string directly for these tests.

    class EchoDataInput : public matcher::DataInput<matcher::MatchDataType> {
    public:
        std::optional<matcher::MatchDataType> GetInput(const matcher::MatchDataType& data) override {
            return data;
        }
    };

    auto create_echo_predicate = [](const std::string& val_to_match) {
        return std::make_unique<matcher::SinglePredicate<matcher::MatchDataType>>(
            std::make_unique<EchoDataInput>(),
            std::make_unique<matcher::ExactMatcher>(val_to_match)
        );
    };

    // All match
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> all_match_v;
    all_match_v.push_back(create_echo_predicate("input_data"));
    all_match_v.push_back(create_echo_predicate("input_data"));
    matcher::AndMatcher<matcher::MatchDataType> and_all_match(std::move(all_match_v));
    EXPECT_TRUE(and_all_match.match("input_data"));

    // One fails
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> one_fails_v;
    one_fails_v.push_back(create_echo_predicate("input_data"));
    one_fails_v.push_back(create_echo_predicate("different_data"));
    matcher::AndMatcher<matcher::MatchDataType> and_one_fails(std::move(one_fails_v));
    EXPECT_FALSE(and_one_fails.match("input_data"));

    // Empty list
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> empty_v;
    matcher::AndMatcher<matcher::MatchDataType> and_empty(std::move(empty_v));
    EXPECT_TRUE(and_empty.match("any_data")); // AndMatcher returns true for empty list
}

TEST_F(MatcherTest, AnyMatcher) {
    class EchoDataInput : public matcher::DataInput<matcher::MatchDataType> {
    public:
        std::optional<matcher::MatchDataType> GetInput(const matcher::MatchDataType& data) override {
            return data;
        }
    };
     auto create_echo_predicate = [](const std::string& val_to_match) {
        return std::make_unique<matcher::SinglePredicate<matcher::MatchDataType>>(
            std::make_unique<EchoDataInput>(),
            std::make_unique<matcher::ExactMatcher>(val_to_match)
        );
    };

    // One matches
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> one_matches_v;
    one_matches_v.push_back(create_echo_predicate("different_data"));
    one_matches_v.push_back(create_echo_predicate("input_data"));
    matcher::AnyMatcher<matcher::MatchDataType> any_one_matches(std::move(one_matches_v));
    EXPECT_TRUE(any_one_matches.match("input_data"));

    // None match
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> none_matches_v;
    none_matches_v.push_back(create_echo_predicate("different_data1"));
    none_matches_v.push_back(create_echo_predicate("different_data2"));
    matcher::AnyMatcher<matcher::MatchDataType> any_none_matches(std::move(none_matches_v));
    EXPECT_FALSE(any_none_matches.match("input_data"));

    // Empty list
    std::vector<matcher::PredicatePtr<matcher::MatchDataType>> empty_v;
    matcher::AnyMatcher<matcher::MatchDataType> any_empty(std::move(empty_v));
    EXPECT_FALSE(any_empty.match("any_data")); // AnyMatcher returns false for empty list
}

TEST_F(MatcherTest, MatcherList) {
    executed_actions.clear();
    matcher::MatcherList<matcher::MatchDataType> matcher_list;
    matcher_list.addMatcher(CreateExactSinglePredicate("data", "data"), {CreateTrackedActionCb("action1"), nullptr, false});
    EXPECT_TRUE(matcher_list.match("data"));
    EXPECT_THAT(executed_actions, ElementsAre("action1"));

    executed_actions.clear();
    matcher_list.addMatcher(CreateExactSinglePredicate("other_data", "other_data"), {CreateTrackedActionCb("action2"), nullptr, false});
    EXPECT_TRUE(matcher_list.match("data")); // Still matches the first one
    EXPECT_THAT(executed_actions, ElementsAre("action1"));

    executed_actions.clear();
    matcher::MatcherList<matcher::MatchDataType> matcher_list_keep_matching;
    matcher_list_keep_matching.addMatcher(CreateExactSinglePredicate("data", "data"), {CreateTrackedActionCb("actionA"), nullptr, true});
    matcher_list_keep_matching.addMatcher(CreateExactSinglePredicate("data", "data"), {CreateTrackedActionCb("actionB"), nullptr, false});
    EXPECT_TRUE(matcher_list_keep_matching.match("data"));
    EXPECT_THAT(executed_actions, ElementsAre("actionA", "actionB"));

    executed_actions.clear();
    matcher::MatcherList<matcher::MatchDataType> matcher_list_all_keep_matching;
    matcher_list_all_keep_matching.addMatcher(CreateExactSinglePredicate("data", "data"), {CreateTrackedActionCb("actionC"), nullptr, true});
    matcher_list_all_keep_matching.addMatcher(CreateExactSinglePredicate("data", "data"), {CreateTrackedActionCb("actionD"), nullptr, true});
    EXPECT_FALSE(matcher_list_all_keep_matching.match("data")); // Returns false because no !keepMatching
    EXPECT_THAT(executed_actions, ElementsAre("actionC", "actionD"));

    // Test onNoMatch
    // TODO: Need to set onNoMatch_. The current API `addMatcher` only adds to fieldMatchers_.
    // For now, testing the case where onNoMatch_ is not set and no match occurs.
    executed_actions.clear();
    matcher::MatcherList<matcher::MatchDataType> no_match_list;
    no_match_list.addMatcher(CreateExactSinglePredicate("data", "not_data"), {CreateTrackedActionCb("no_action"), nullptr, false});
    EXPECT_FALSE(no_match_list.match("data"));
    EXPECT_TRUE(executed_actions.empty());
}
}
}
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  //grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}