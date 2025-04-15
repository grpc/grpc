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

TEST(SpiffeId, ShortSchemePrefixFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe:/foo/bar/").status(),
            absl::InvalidArgumentError("SPIFFE ID must start with spiffe://"));
}

TEST(SpiffeId, SchemeInvalidCharacterFails) {
  EXPECT_EQ(
      SpiffeId::FromString("ſPiffe://trustdomain/path").status(),
      absl::InvalidArgumentError(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains 0xc5"));
}

TEST(SpiffeId, SchemePrefixAndTrustDomainMissingFails) {
  EXPECT_EQ(SpiffeId::FromString("://").status(),
            absl::InvalidArgumentError("SPIFFE ID must start with spiffe://"));
}

TEST(SpiffeId, SchemeMissingSuffixFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe").status(),
            absl::InvalidArgumentError("SPIFFE ID must start with spiffe://"));
}

TEST(SpiffeId, SchemeMissingPrefix) {
  EXPECT_EQ(SpiffeId::FromString("piffe://foo/bar/").status(),
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

TEST(SpiffeId, TrustDomainWithUserInfoFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://domain@userinfo").status(),
      absl::InvalidArgumentError(
          "Trust domain contains invalid character '@'. MUST contain only "
          "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, TrustDomainInvalidCharacterFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo$bar").status(),
      absl::InvalidArgumentError(
          "Trust domain contains invalid character '$'. MUST contain only "
          "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, TrustDomainInvalidCharacterUppercaseFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://BadDdomain").status(),
      absl::InvalidArgumentError(
          "Trust domain contains invalid character 'B'. MUST contain only "
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
          "Path segment contains invalid character '@'. MUST contain only "
          "letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, ContainsNonASCIITrustDomainFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://µ/path").status(),
      absl::InvalidArgumentError(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains 0xc2"));
}

TEST(SpiffeId, TrustDomainContainsNonASCIIFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://fooµbar/path").status(),
      absl::InvalidArgumentError(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains 0xc2"));
}

TEST(SpiffeId, TrustDomainPercentEncodingFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo%21bar/path").status(),
      absl::InvalidArgumentError(
          "Trust domain contains invalid character '%'. MUST contain only "
          "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, TrustDomainTrailingSlashFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://foo/").status(),
            absl::InvalidArgumentError("SPIFFE ID cannot end with a /"));
}

TEST(SpiffeId, PortInTrustDomainFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo:1234/path").status(),
      absl::InvalidArgumentError(
          "Trust domain contains invalid character ':'. MUST contain only "
          "lowercase letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, PathQueryParameterFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo/bar?query").status(),
      absl::InvalidArgumentError("SPIFFE ID cannot contain query parameters"));
}

TEST(SpiffeId, EscapedCharacterInPathFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo/p\ath").status(),
      absl::InvalidArgumentError(
          "Path segment contains invalid character '\a'. MUST contain only "
          "letters, numbers, dots, dashes, and underscores"));
}

TEST(SpiffeId, FragmentInPathFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo/pa#h").status(),
      absl::InvalidArgumentError("SPIFFE ID cannot contain query fragments"));
}

TEST(SpiffeId, MultipleSlashesInPathFails) {
  EXPECT_EQ(SpiffeId::FromString("spiffe://foo/bar//baz").status(),
            absl::InvalidArgumentError("Path segment cannot be empty"));
}

TEST(SpiffeId, NonASCIIPathFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo.bar/µ").status(),
      absl::InvalidArgumentError(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains 0xc2"));
}

TEST(SpiffeId, ContainsNonASCIIPathFails) {
  EXPECT_EQ(
      SpiffeId::FromString("spiffe://foo.bar/fooµbar").status(),
      absl::InvalidArgumentError(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains 0xc2"));
}

TEST(SpiffeId, NoPathSuccess) {
  auto spiffe_id = SpiffeId::FromString("spiffe://example.com");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "");
}

TEST(SpiffeId, BasicSuccess) {
  auto spiffe_id = SpiffeId::FromString("spiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, WeirdCapitalizationSuccess) {
  auto spiffe_id = SpiffeId::FromString("sPiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, AllSpiffeCapitalizedSuccess) {
  auto spiffe_id = SpiffeId::FromString("SPIFFE://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, FirstSpiffeCapitalizedSuccess) {
  auto spiffe_id = SpiffeId::FromString("Spiffe://example.com/us");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/us");
}

TEST(SpiffeId, LongPathSuccess) {
  auto spiffe_id = SpiffeId::FromString(
      "spiffe://example.com/country/us/state/FL/city/Miami");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "example.com");
  EXPECT_EQ(spiffe_id->path(), "/country/us/state/FL/city/Miami");
}

TEST(SpiffeId, AcceptedCharactersSuccess) {
  auto spiffe_id = SpiffeId::FromString(
      "spiffe://abcdefghijklmnopqrstuvwxyz1234567890.-_/"
      "abcdefghijklmnopqrstuvwxyz1234567890.-_");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(),
            "abcdefghijklmnopqrstuvwxyz1234567890.-_");
  EXPECT_EQ(spiffe_id->path(), "/abcdefghijklmnopqrstuvwxyz1234567890.-_");
}

TEST(SpiffeId, NonRelativePathDotsSuccess) {
  auto spiffe_id = SpiffeId::FromString("spiffe://trustdomain/.a..");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/.a..");
}

TEST(SpiffeId, TripleDotsSuccess) {
  auto spiffe_id = SpiffeId::FromString("spiffe://trustdomain/...");
  ASSERT_TRUE(spiffe_id.ok()) << spiffe_id.status();
  EXPECT_EQ(spiffe_id->trust_domain(), "trustdomain");
  EXPECT_EQ(spiffe_id->path(), "/...");
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
