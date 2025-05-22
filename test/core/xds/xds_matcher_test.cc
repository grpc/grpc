
#include <grpc/grpc.h>
#include <google/protobuf/text_format.h>

#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.pb.h"

#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

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