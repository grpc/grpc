//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/xds/grpc/file_watcher_certificate_provider_factory.h"

#include <grpc/grpc.h>

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json_reader.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

const char* kIdentityCertFile = "/path/to/identity_cert_file";
const char* kPrivateKeyFile = "/path/to/private_key_file";
const char* kRootCertFile = "/path/to/root_cert_file";
const int kRefreshInterval = 400;

absl::StatusOr<RefCountedPtr<FileWatcherCertificateProviderFactory::Config>>
ParseConfig(absl::string_view json_string) {
  auto json = JsonParse(json_string);
  if (!json.ok()) return json.status();
  ValidationErrors errors;
  auto config =
      FileWatcherCertificateProviderFactory().CreateCertificateProviderConfig(
          *json, JsonArgs(), &errors);
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "validation errors");
  }
  return config.TakeAsSubclass<FileWatcherCertificateProviderFactory::Config>();
}

TEST(FileWatcherConfigTest, Basic) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\","
      "  \"ca_certificate_file\": \"%s\","
      "  \"refresh_interval\": \"%ds\""
      "}",
      kIdentityCertFile, kPrivateKeyFile, kRootCertFile, kRefreshInterval);
  auto config = ParseConfig(json_str);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ((*config)->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ((*config)->private_key_file(), kPrivateKeyFile);
  EXPECT_EQ((*config)->root_cert_file(), kRootCertFile);
  EXPECT_EQ((*config)->refresh_interval(), Duration::Seconds(kRefreshInterval));
}

TEST(FileWatcherConfigTest, DefaultRefreshInterval) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\","
      "  \"ca_certificate_file\": \"%s\""
      "}",
      kIdentityCertFile, kPrivateKeyFile, kRootCertFile);
  auto config = ParseConfig(json_str);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ((*config)->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ((*config)->private_key_file(), kPrivateKeyFile);
  EXPECT_EQ((*config)->root_cert_file(), kRootCertFile);
  EXPECT_EQ((*config)->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, OnlyRootCertificatesFileProvided) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"ca_certificate_file\": \"%s\""
      "}",
      kRootCertFile);
  auto config = ParseConfig(json_str);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ((*config)->identity_cert_file(), "");
  EXPECT_EQ((*config)->private_key_file(), "");
  EXPECT_EQ((*config)->root_cert_file(), kRootCertFile);
  EXPECT_EQ((*config)->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, OnlyIdentityCertificatesAndPrivateKeyProvided) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\""
      "}",
      kIdentityCertFile, kPrivateKeyFile);
  auto config = ParseConfig(json_str);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ((*config)->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ((*config)->private_key_file(), kPrivateKeyFile);
  EXPECT_EQ((*config)->root_cert_file(), "");
  EXPECT_EQ((*config)->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, WrongTypes) {
  const char* json_str =
      "{"
      "  \"certificate_file\": 123,"
      "  \"private_key_file\": 123,"
      "  \"ca_certificate_file\": 123,"
      "  \"refresh_interval\": 123"
      "}";
  auto config = ParseConfig(json_str);
  EXPECT_EQ(config.status().message(),
            "validation errors: ["
            "field:ca_certificate_file error:is not a string; "
            "field:certificate_file error:is not a string; "
            "field:private_key_file error:is not a string; "
            "field:refresh_interval error:is not a string]")
      << config.status();
}

TEST(FileWatcherConfigTest, IdentityCertProvidedButPrivateKeyMissing) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\""
      "}",
      kIdentityCertFile);
  auto config = ParseConfig(json_str);
  EXPECT_EQ(config.status().message(),
            "validation errors: ["
            "field: error:fields \"certificate_file\" and "
            "\"private_key_file\" must be both set or both unset]")
      << config.status();
}

TEST(FileWatcherConfigTest, PrivateKeyProvidedButIdentityCertMissing) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"ca_certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\""
      "}",
      kRootCertFile, kPrivateKeyFile);
  auto config = ParseConfig(json_str);
  EXPECT_EQ(config.status().message(),
            "validation errors: ["
            "field: error:fields \"certificate_file\" and "
            "\"private_key_file\" must be both set or both unset]")
      << config.status();
}

TEST(FileWatcherConfigTest, EmptyJsonObject) {
  std::string json_str = "{}";
  auto config = ParseConfig(json_str);
  EXPECT_EQ(config.status().message(),
            "validation errors: ["
            "field: error:at least one of \"certificate_file\" and "
            "\"ca_certificate_file\" must be specified]")
      << config.status();
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
