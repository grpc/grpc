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

TEST(SpiffeId, EmptyFails) {
  EXPECT_EQ(
      SpiffeId::FromString("").status(),
      absl::InvalidArgumentError("SPIFFE ID cannot be parsed from empty URI"));
}

TEST(SpiffeId, TooLongFails) {
  EXPECT_EQ(SpiffeId::FromString(std::string(2049, 'a')).status(),
            absl::InvalidArgumentError(
                "URI length is 2049, maximum allowed for SPIFFE ID is 2048"));
}

TEST(SpiffeId, ContainsHashtagFails) {
  EXPECT_EQ(
      SpiffeId::FromString("ab#de").status(),
      absl::InvalidArgumentError("SPIFFE ID cannot contain query fragments"));
}

TEST(SpiffeId, ContainsQuestionMarkFails) {
  EXPECT_EQ(
      SpiffeId::FromString("ab?de").status(),
      absl::InvalidArgumentError("SPIFFE ID cannot contain query parameters"));
}

TEST(SpiffeId, DoesNotStartWithSpiffeFails) {
  EXPECT_EQ(SpiffeId::FromString("www://foo/bar").status(),
            absl::InvalidArgumentError("SPIFFE ID must start with spiffe://"));
}

TEST(SpiffeId, EndWithSlashFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://foo/bar/").status(),
            absl::InvalidArgumentError("SPIFFE ID cannot end with a /"));
}

TEST(SpiffeId, NoTrustDomainFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://").status(),
            absl::InvalidArgumentError("SPIFFE ID cannot end with a /"));
}

TEST(SpiffeId, NoTrustDomainWithPathFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe:///path").status(),
            absl::InvalidArgumentError("The trust domain cannot be empty"));
}

TEST(SpiffeId, TrustDomainTooLongFails) {
  EXPECT_EQ(
      SpiffeId::FromString(absl::StrCat("spiffe://", std::string(256, 'a')))
          .status(),
      absl::InvalidArgumentError(
          "Trust domain maximum length is 255 characters"));
}

TEST(SpiffeId, TrustDomainInvalidCharacterFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://bad@domain").status(),
            absl::InvalidArgumentError(
                "Trust domain contains invalid character @. MUST contain only "
                "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, TrustDomainInvalidCharacterUppercaseFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://BadDdomain").status(),
            absl::InvalidArgumentError(
                "Trust domain contains invalid character B. MUST contain only "
                "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, PathContainsRelativeModifier1Fails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://example/path/./foo").status(),
            absl::InvalidArgumentError(
                "Path segment cannot be a relative modifier (. or ..)"));
}

TEST(SpiffeId, PathContainsRelativeModifier2Fails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://example/path/../foo").status(),
            absl::InvalidArgumentError(
                "Path segment cannot be a relative modifier (. or ..)"));
}

TEST(SpiffeId, PathSegmentBadCharacterFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://example/path/foo.bar/foo@bar").status(),
      absl::InvalidArgumentError(
          "Path segment contains invalid character @. MUST contain only "
          "letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, ContainsNonASCIITrustDomainFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://µ/path").status(),
            absl::InvalidArgumentError(
                "SPIFFE ID URI cannot contain non-ascii characters"));
}

TEST(SpiffeId, ContainsNonASCIIPathFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://foo.bar/µ").status(),
            absl::InvalidArgumentError(
                "SPIFFE ID URI cannot contain non-ascii characters"));
}

TEST(SpiffeId, NoPathSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://example.com");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "");
}

TEST(SpiffeId, BasicSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, WeirdCapitalizationSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("sPiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, AllSpiffeCapitalizedSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("SPIFFE://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, FirstSpiffeCapitalizedSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("Spiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, LongPathSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id = SpiffeId::FromString(
      "spiffe://example.com/country/us/state/FL/city/Miami");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/country/us/state/FL/city/Miami");
}

TEST(SpiffeId, TrustDomainDashesSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trust-domain-name/path");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trust-domain-name");
  EXPECT_EQ(spiffe_id->path(), "/path");
}

TEST(SpiffeId, DotsInTrustDomainSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://staging.example.com/payments/mysql");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "staging.example.com");
  EXPECT_EQ(spiffe_id->path(), "/payments/mysql");
}

TEST(SpiffeId, DashesInPathSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://staging.example.com/payments/web-fe");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "staging.example.com");
  EXPECT_EQ(spiffe_id->path(), "/payments/web-fe");
}

TEST(SpiffeId, K8sAddressSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id = SpiffeId::FromString(
      "spiffe://k8s-west.example.com/ns/staging/sa/default");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "k8s-west.example.com");
  EXPECT_EQ(spiffe_id->path(), "/ns/staging/sa/default");
}

TEST(SpiffeId, PathIsIDSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id = SpiffeId::FromString(
      "spiffe://example.com/9eebccd2-12bf-40a6-b262-65fe0487d453");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/9eebccd2-12bf-40a6-b262-65fe0487d453");
}

TEST(SpiffeId, NonRelativePathDotsSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain/.a..");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/.a..");
}

TEST(SpiffeId, TripleDotsSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain/...");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/...");
}

TEST(SpiffeId, AllLowercaseCharactersSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain/abcdefghijklmnopqrstuvwxyz");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/abcdefghijklmnopqrstuvwxyz");
}

TEST(SpiffeId, MixOfCharactersPathSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain/abc0123.-_");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/abc0123.-_");
}

TEST(SpiffeId, AllNumbersPathSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain/0123456789");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/0123456789");
}

TEST(SpiffeId, NumbersTrustDomainSuccess) {
  absl::StatusOr<SpiffeId> spiffe_id =
      SpiffeId::FromString("spiffe://trustdomain0123456789/path");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain0123456789");
  EXPECT_EQ(spiffe_id->path(), "/path");
}

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
