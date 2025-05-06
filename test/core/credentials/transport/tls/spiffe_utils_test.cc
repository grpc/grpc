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
#include <openssl/x509.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/load_file.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

namespace grpc_core {
namespace testing {

namespace {

constexpr absl::string_view kCertificatePrefix =
    "-----BEGIN CERTIFICATE-----\n";
constexpr absl::string_view kCertificateSuffix =
    "\n-----END CERTIFICATE-----\n";

absl::StatusOr<X509*> ReadCertificate(absl::string_view raw_cert) {
  std::string pem_cert =
      absl::StrCat(kCertificatePrefix, raw_cert.data(), kCertificateSuffix);

  auto chain = ParsePemCertificateChain(pem_cert);
  GRPC_RETURN_IF_ERROR(chain.status());
  return (*chain)[0];
}

absl::StatusOr<X509*> ReadCertificateFromFile(absl::string_view filepath) {
  FILE* file = fopen(filepath.data(), "r");
  if (!file) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to read file %s", filepath));
  }

  X509* cert = PEM_read_X509(file, nullptr, nullptr, nullptr);
  fclose(file);

  if (!cert) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to load certificate from file %s", filepath));
  }
  return cert;
}
}  // namespace

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

TEST(SpiffeBundle, EmptyKeysFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_empty_keys.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: [field:trust_domains error:map key '' is "
          "not a valid trust domain. INVALID_ARGUMENT: Trust domain cannot be "
          "empty]"));
}

TEST(SpiffeBundle, CorruptedCertFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_corrupted_cert.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: "
          "[field:trust_domains[\"example.com\"].keys[0]"
          ".x5c error:FAILED_PRECONDITION: Invalid PEM.]"));
}

TEST(SpiffeBundle, EmptyStringKeyFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_empty_string_key.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: [field:trust_domains error:map key '' is "
          "not a valid trust domain. INVALID_ARGUMENT: Trust domain cannot be "
          "empty]"));
}

TEST(SpiffeBundle, InvalidTrustDomainFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_invalid_trustdomain.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: [field:trust_domains error:map key "
          "'invalid#character' is not a valid trust domain. INVALID_ARGUMENT: "
          "Trust domain contains invalid character '#'. MUST contain only "
          "lowercase letters, numbers, dots, dashes, and underscores]"));
}

TEST(SpiffeBundle, MalformedJsonFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_malformed.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: [field: error:is not an object]"));
}

TEST(SpiffeBundle, WrongKtyFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_wrong_kty.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: "
          "[field:trust_domains[\"example.com\"].keys[0].kty error:value must "
          "be \"RSA\", got \"EC\"]"));
}

TEST(SpiffeBundle, WrongKidFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_wrong_kid.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: "
          "[field:trust_domains[\"example.com\"].keys[0].kty "
          "error:field not present]"));
}

TEST(SpiffeBundle, MultiCertsFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_wrong_multi_certs.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: "
          "[field:trust_domains[\"google.com\"].keys[0] "
          "error:Cannot get root certificate: INVALID_ARGUMENT: SPIFFE Bundle "
          "key entry "
          "has x5c field with length != 1. Key entry x5c field MUST have "
          "length of "
          "exactly 1.; field:trust_domains[\"google.com\"].keys[0].x5c "
          "error:got vector "
          "length 2. Expected length of exactly 1.]"));
}

TEST(SpiffeBundle, WrongRootFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_wrong_root.json")
          .status(),
      absl::InvalidArgumentError("errors validating JSON: [field:trust_domains "
                                 "error:field not present]"));
}

TEST(SpiffeBundle, WrongUseFails) {
  EXPECT_EQ(
      SpiffeBundleMap::FromFile(
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_wrong_use.json")
          .status(),
      absl::InvalidArgumentError(
          "errors validating JSON: "
          "[field:trust_domains[\"example.com\"].keys[0].use error:value must "
          "be \"x509-svid\", got \"NOT-x509-svid\"]"));
}

TEST(SpiffeBundle, MultipleTrustDomainsSuccess) {
  std::string path =
      "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
      "spiffebundle.json";
  std::string json_str = testing::GetFileContents(path);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok());

  auto bundle_map = LoadFromJson<SpiffeBundleMap>(*json);
  ASSERT_TRUE(bundle_map.ok()) << bundle_map.status();
  ASSERT_EQ(bundle_map->size(), 2);
  {
    // check the example.com bundle
    auto roots = bundle_map->GetRoots("example.com");
    ASSERT_TRUE(roots.ok());
    EXPECT_EQ(roots->size(), 1);
    auto certificate = ReadCertificate((*roots)[0]);
    ASSERT_TRUE(certificate.ok()) << certificate.status();
    auto expected_certificate = ReadCertificateFromFile(
        "test/core/credentials/transport/tls/test_data/spiffe/"
        "spiffe_cert.pem");
    ASSERT_TRUE(expected_certificate.ok()) << expected_certificate.status();
    EXPECT_EQ(X509_cmp(*certificate, *expected_certificate), 0);
    X509_free(*certificate);
    X509_free(*expected_certificate);
  }
  {
    // check the test.example.com bundle
    auto roots = bundle_map->GetRoots("test.example.com");
    ASSERT_TRUE(roots.ok());
    EXPECT_EQ(roots->size(), 1);
    auto certificate = ReadCertificate((*roots)[0]);
    ASSERT_TRUE(certificate.ok()) << certificate.status();
    auto expected_certificate = ReadCertificateFromFile(
        "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
        "server1_spiffe.pem");
    ASSERT_TRUE(expected_certificate.ok()) << expected_certificate.status();
    EXPECT_EQ(X509_cmp(*certificate, *expected_certificate), 0);
    X509_free(*certificate);
    X509_free(*expected_certificate);
  }
}

TEST(SpiffeBundle, MultipleRootsSuccess) {
  std::string path =
      "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
      "spiffebundle2.json";
  std::string json_str = testing::GetFileContents(path);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok());

  auto bundle_map = LoadFromJson<SpiffeBundleMap>(*json);
  ASSERT_TRUE(bundle_map.ok()) << bundle_map.status();
  ASSERT_EQ(bundle_map->size(), 1);
  // check the example.com bundle
  auto roots = bundle_map->GetRoots("example.com");
  ASSERT_TRUE(roots.ok());
  EXPECT_EQ(roots->size(), 2);
  {
    // Check the first root
    auto certificate = ReadCertificate((*roots)[0]);
    ASSERT_TRUE(certificate.ok()) << certificate.status();
    auto expected_certificate = ReadCertificateFromFile(
        "test/core/credentials/transport/tls/test_data/spiffe/"
        "spiffe_cert.pem");
    ASSERT_TRUE(expected_certificate.ok()) << expected_certificate.status();
    EXPECT_EQ(X509_cmp(*certificate, *expected_certificate), 0);
    X509_free(*certificate);
    X509_free(*expected_certificate);
  }
  {
    // Check the second root
    auto certificate = ReadCertificate((*roots)[1]);
    ASSERT_TRUE(certificate.ok()) << certificate.status();
    auto expected_certificate = ReadCertificateFromFile(
        "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
        "server1_spiffe.pem");
    ASSERT_TRUE(expected_certificate.ok()) << expected_certificate.status();
    EXPECT_EQ(X509_cmp(*certificate, *expected_certificate), 0);
    X509_free(*certificate);
    X509_free(*expected_certificate);
  }
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
