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

#include "src/core/ext/xds/file_watcher_certificate_provider_factory.h"

#include <initializer_list>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/json/json_reader.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

const char* kIdentityCertFile = "/path/to/identity_cert_file";
const char* kPrivateKeyFile = "/path/to/private_key_file";
const char* kRootCertFile = "/path/to/root_cert_file";
const int kRefreshInterval = 400;

TEST(FileWatcherConfigTest, Basic) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\","
      "  \"ca_certificate_file\": \"%s\","
      "  \"refresh_interval\": \"%ds\""
      "}",
      kIdentityCertFile, kPrivateKeyFile, kRootCertFile, kRefreshInterval);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  ASSERT_EQ(error, absl::OkStatus()) << StatusToString(error);
  EXPECT_EQ(config->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ(config->private_key_file(), kPrivateKeyFile);
  EXPECT_EQ(config->root_cert_file(), kRootCertFile);
  EXPECT_EQ(config->refresh_interval(), Duration::Seconds(kRefreshInterval));
}

TEST(FileWatcherConfigTest, DefaultRefreshInterval) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\","
      "  \"ca_certificate_file\": \"%s\""
      "}",
      kIdentityCertFile, kPrivateKeyFile, kRootCertFile);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  ASSERT_EQ(error, absl::OkStatus()) << StatusToString(error);
  EXPECT_EQ(config->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ(config->private_key_file(), kPrivateKeyFile);
  EXPECT_EQ(config->root_cert_file(), kRootCertFile);
  EXPECT_EQ(config->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, OnlyRootCertificatesFileProvided) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"ca_certificate_file\": \"%s\""
      "}",
      kRootCertFile);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  ASSERT_EQ(error, absl::OkStatus()) << StatusToString(error);
  EXPECT_TRUE(config->identity_cert_file().empty());
  EXPECT_TRUE(config->private_key_file().empty());
  EXPECT_EQ(config->root_cert_file(), kRootCertFile);
  EXPECT_EQ(config->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, OnlyIdenityCertificatesAndPrivateKeyProvided) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\","
      "  \"private_key_file\": \"%s\""
      "}",
      kIdentityCertFile, kPrivateKeyFile);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  ASSERT_EQ(error, absl::OkStatus()) << StatusToString(error);
  EXPECT_EQ(config->identity_cert_file(), kIdentityCertFile);
  EXPECT_EQ(config->private_key_file(), kPrivateKeyFile);
  EXPECT_TRUE(config->root_cert_file().empty());
  EXPECT_EQ(config->refresh_interval(), Duration::Seconds(600));
}

TEST(FileWatcherConfigTest, WrongTypes) {
  const char* json_str =
      "{"
      "  \"certificate_file\": 123,"
      "  \"private_key_file\": 123,"
      "  \"ca_certificate_file\": 123,"
      "  \"refresh_interval\": 123"
      "}";
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  EXPECT_THAT(StatusToString(error),
              ::testing::ContainsRegex(
                  "field:certificate_file error:type should be STRING.*"
                  "field:private_key_file error:type should be STRING.*"
                  "field:ca_certificate_file error:type should be STRING.*"
                  "field:refresh_interval error:type should be STRING of the "
                  "form given by "
                  "google.proto.Duration.*"));
}

TEST(FileWatcherConfigTest, IdentityCertProvidedButPrivateKeyMissing) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"certificate_file\": \"%s\""
      "}",
      kIdentityCertFile);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  EXPECT_THAT(StatusToString(error),
              ::testing::ContainsRegex(
                  "fields \"certificate_file\" and \"private_key_file\" must "
                  "be both set or both unset."));
}

TEST(FileWatcherConfigTest, PrivateKeyProvidedButIdentityCertMissing) {
  std::string json_str = absl::StrFormat(
      "{"
      "  \"private_key_file\": \"%s\""
      "}",
      kPrivateKeyFile);
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  EXPECT_THAT(StatusToString(error),
              ::testing::ContainsRegex(
                  "fields \"certificate_file\" and \"private_key_file\" must "
                  "be both set or both unset."));
}

TEST(FileWatcherConfigTest, EmptyJsonObject) {
  std::string json_str = "{}";
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_error_handle error;
  auto config =
      FileWatcherCertificateProviderFactory::Config::Parse(*json, &error);
  EXPECT_THAT(
      StatusToString(error),
      ::testing::ContainsRegex("At least one of \"certificate_file\" and "
                               "\"ca_certificate_file\" must be specified."));
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
