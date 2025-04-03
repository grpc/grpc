//
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
//

#include "src/core/credentials/transport/tls/spiffe_utils.h"

#include <grpc/grpc.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {

using ::testing::TestWithParam;

struct SpiffeIdFailureTestCase {
  std::string test_name;
  std::string spiffe_id;
  std::string status_contains;
};

using SpiffeIdFailureTest = TestWithParam<SpiffeIdFailureTestCase>;

TEST_P(SpiffeIdFailureTest, SpiffeIdTestFailure) {
  const SpiffeIdFailureTestCase& test_case = GetParam();
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString(test_case.spiffe_id);
  EXPECT_EQ(spiffe_id.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(spiffe_id.status().message(),
              ::testing::HasSubstr(test_case.status_contains));
}

INSTANTIATE_TEST_SUITE_P(
    SpifeIdTestFailureSuiteInstantiation, SpiffeIdFailureTest,
    ::testing::ValuesIn<SpiffeIdFailureTestCase>({
        {"Empty", "", "empty URI"},
        {"TooLong", std::string(2049, 'a'),
         "maximum allowed for SPIFFE ID is 2048"},
        {"ContainsHashtag", "ab#de", "cannot contain query fragments"},
        {"ContainsQuestionMark", "ab?de", "cannot contain query parameters"},
        {"DoesNotStartWithSpiffe", "www://foo/bar",
         "must start with spiffe://"},
        {"EndsWithSlash", "spiffe://foo/bar/", "cannot end with a /"},
        {"NoTrustDomain", "spiffe://", "cannot end with a /"},
        {"NoTrustDomainWithPath", "spiffe:///path",
         "The trust domain cannot be empty"},
        {"TrustDomainTooLong", absl::StrCat("spiffe://", std::string(256, 'a')),
         "Trust domain maximum length is 255 characters"},
        {"TrustDomainInvalidCharacter1", "spiffe://bad@domain",
         "contains invalid character @"},
        {"TrustDomainInvalidCharacter2", "spiffe://BadDomain",
         "contains invalid character B"},
        {"PathContainsRelativeModifier1", "spiffe://example/path/./foo",
         ". or .."},
        {"PathContainsRelativeModifier2", "spiffe://example/path/../foo",
         ". or .."},
        {"PathSegmentBadCharacter", "spiffe://example/path/foo.bar/foo@bar",
         "invalid character @"},
    }),
    [](const ::testing::TestParamInfo<SpiffeIdFailureTest::ParamType>& info) {
      return info.param.test_name;
    });

struct SpiffeIdSuccessTestCase {
  std::string spiffe_id;
  std::string trust_domain;
  std::string path;
};

using SpiffeIdSuccessTest = TestWithParam<SpiffeIdSuccessTestCase>;

TEST_P(SpiffeIdSuccessTest, SpiffeIdTestSuccess) {
  const SpiffeIdSuccessTestCase& test_case = GetParam();
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString(test_case.spiffe_id);
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), test_case.trust_domain);
  EXPECT_EQ(spiffe_id->path(), test_case.path);
}

INSTANTIATE_TEST_SUITE_P(
    SpifeIdTestSuccessSuiteInstantiation, SpiffeIdSuccessTest,
    ::testing::ValuesIn<SpiffeIdSuccessTestCase>({
        {"spiffe://example.com", "example.com", ""},
        {"spiffe://example.com/us", "example.com", "/us"},
        {"sPiffe://example.com/us", "example.com", "/us"},
        {"SPIFFE://example.com/us", "example.com", "/us"},
        {"Spiffe://example.com/us", "example.com", "/us"},
        {"spiffe://example.com/country/us/state/FL/city/Miami", "example.com",
         "/country/us/state/FL/city/Miami"},
        {"spiffe://trust-domain-name/path", "trust-domain-name", "/path"},
        {"spiffe://staging.example.com/payments/mysql", "staging.example.com",
         "/payments/mysql"},
        {"spiffe://staging.example.com/payments/web-fe", "staging.example.com",
         "/payments/web-fe"},
        {"spiffe://k8s-west.example.com/ns/staging/sa/default",
         "k8s-west.example.com", "/ns/staging/sa/default"},
        {"spiffe://example.com/9eebccd2-12bf-40a6-b262-65fe0487d453",
         "example.com", "/9eebccd2-12bf-40a6-b262-65fe0487d453"},
        {"spiffe://trustdomain/.a..", "trustdomain", "/.a.."},
        {"spiffe://trustdomain/...", "trustdomain", "/..."},
        {"spiffe://trustdomain/abcdefghijklmnopqrstuvwxyz", "trustdomain",
         "/abcdefghijklmnopqrstuvwxyz"},
        {"spiffe://trustdomain/abc0123.-_", "trustdomain", "/abc0123.-_"},
        {"spiffe://trustdomain/0123456789", "trustdomain", "/0123456789"},
        {"spiffe://trustdomain0123456789/path", "trustdomain0123456789",
         "/path"},
    }));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
