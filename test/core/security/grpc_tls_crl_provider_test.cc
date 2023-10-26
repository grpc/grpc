//
//
// Copyright 2023 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_crl_provider.h>

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

static constexpr absl::string_view kCrlPath =
    "test/core/tsi/test_creds/crl_data/crls/current.crl";
static constexpr absl::string_view kCrlIssuer =
    "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=testca";
static constexpr absl::string_view kCrlIntermediateIssuer =
    "/CN=intermediatecert.example.com";
static constexpr absl::string_view kCrlDirectory =
    "test/core/tsi/test_creds/crl_data/crls";
static constexpr absl::string_view kRootCert =
    "test/core/tsi/test_creds/crl_data/ca.pem";

using ::grpc_core::experimental::CertificateInfoImpl;
using ::grpc_core::experimental::Crl;
using ::grpc_core::experimental::CrlImpl;
using ::grpc_core::experimental::CrlProvider;
using ::grpc_core::experimental::StaticCrlProvider;

namespace grpc_core {
namespace testing {

absl::StatusOr<std::shared_ptr<CrlProvider>>
CreateDirectoryReloaderCrlProviderForTest(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::shared_ptr<Directory> directory_impl) {
  if (directory_impl == nullptr) {
    directory_impl = std::make_shared<Directory>(directory);
  }
  if (event_engine == nullptr) {
    event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  }

  auto provider = std::make_shared<experimental::DirectoryReloaderCrlProvider>(
      refresh_duration, reload_error_callback, event_engine, directory_impl);
  absl::Status initial_status = provider->Update();
  if (!initial_status.ok()) {
    return initial_status;
  }
  provider->ScheduleReload();
  return provider;
}

class DirectoryForTest : public Directory {
 public:
  DirectoryForTest() : Directory("") {}
  ~DirectoryForTest() override = default;
  absl::StatusOr<std::vector<std::string>> GetFilesInDirectory() override {
    return files_in_directory_;
  }
  std::vector<std::string> files_in_directory_;
};

TEST(CrlProviderTest, CanParseCrl) {
  std::string crl_string = GetFileContents(kCrlPath.data());
  absl::StatusOr<std::shared_ptr<Crl>> crl = Crl::Parse(crl_string);
  ASSERT_TRUE(crl.ok());
  ASSERT_NE(*crl, nullptr);
  EXPECT_EQ((*crl)->Issuer(), kCrlIssuer);
}

TEST(CrlProviderTest, InvalidFile) {
  std::string crl_string = "INVALID CRL FILE";
  absl::StatusOr<std::shared_ptr<Crl>> crl = Crl::Parse(crl_string);
  EXPECT_EQ(crl.status(),
            absl::InvalidArgumentError(
                "Conversion from PEM string to X509 CRL failed."));
}

TEST(CrlProviderTest, StaticCrlProviderLookup) {
  std::vector<std::string> crl_strings = {GetFileContents(kCrlPath.data())};
  absl::StatusOr<std::shared_ptr<CrlProvider>> provider =
      experimental::CreateStaticCrlProvider(crl_strings);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
}

TEST(CrlProviderTest, StaticCrlProviderLookupIssuerNotFound) {
  std::vector<std::string> crl_strings = {GetFileContents(kCrlPath.data())};
  absl::StatusOr<std::shared_ptr<CrlProvider>> provider =
      experimental::CreateStaticCrlProvider(crl_strings);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl bad_cert("BAD CERT");
  auto crl = (*provider)->GetCrl(bad_cert);
  EXPECT_EQ(crl, nullptr);
}

TEST(CrlProviderTest, DirectoryReloaderCrlLookupGood) {
  auto provider = experimental::CreateDirectoryReloaderCrlProvider(
      kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok());
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);

  CertificateInfoImpl intermediate(kCrlIntermediateIssuer);
  auto intermediate_crl = (*provider)->GetCrl(intermediate);
  ASSERT_NE(intermediate_crl, nullptr);
  EXPECT_EQ(intermediate_crl->Issuer(), kCrlIntermediateIssuer);
}

TEST(CrlProviderTest, DirectoryReloaderCrlLookupMissingIssuer) {
  auto provider = experimental::CreateDirectoryReloaderCrlProvider(
      kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok());

  CertificateInfoImpl bad_cert("BAD CERT");
  auto crl = (*provider)->GetCrl(bad_cert);
  ASSERT_EQ(crl, nullptr);
}

TEST(CrlProviderTest, DirectoryReloaderReloadsAndDeletes) {
  auto fuzzing_ee =
      std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
          grpc_event_engine::experimental::FuzzingEventEngine::Options(),
          fuzzing_event_engine::Actions());
  auto directory = std::make_shared<DirectoryForTest>();
  int refresh_duration = 60;
  auto provider = CreateDirectoryReloaderCrlProviderForTest(
      "", std::chrono::seconds(refresh_duration), nullptr, fuzzing_ee,
      directory);
  ASSERT_TRUE(provider.ok());
  CertificateInfoImpl cert(kCrlIssuer);
  auto should_be_no_crl = (*provider)->GetCrl(cert);
  ASSERT_EQ(should_be_no_crl, nullptr);

  // Give the provider files to find in the directory
  directory->files_in_directory_ = {std::string(kCrlPath)};
  fuzzing_ee->TickForDuration(Duration::FromSecondsAsDouble(refresh_duration));
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);

  // Now we won't see any files in our directory
  directory->files_in_directory_ = {};
  fuzzing_ee->TickForDuration(Duration::FromSecondsAsDouble(refresh_duration));
  auto crl_should_be_deleted = (*provider)->GetCrl(cert);
  ASSERT_EQ(crl_should_be_deleted, nullptr);
}

TEST(CrlProviderTest, DirectoryReloaderWithCorruption) {
  auto directory = std::make_shared<DirectoryForTest>();
  directory->files_in_directory_ = {std::string(kCrlPath)};
  int refresh_duration = 60;
  auto fuzzing_ee =
      std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
          grpc_event_engine::experimental::FuzzingEventEngine::Options(),
          fuzzing_event_engine::Actions());
  std::vector<absl::Status> reload_errors;
  std::function<void(absl::Status)> reload_error_callback =
      [&](const absl::Status& status) { reload_errors.push_back(status); };
  auto provider = CreateDirectoryReloaderCrlProviderForTest(
      "", std::chrono::seconds(refresh_duration), reload_error_callback,
      fuzzing_ee, directory);
  ASSERT_TRUE(provider.ok());
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 0);
  // Point the provider at a non-crl file so loading fails
  // Should result in the CRL Reloader keeping the old CRL data
  directory->files_in_directory_ = {std::string(kRootCert)};
  fuzzing_ee->TickForDuration(Duration::FromSecondsAsDouble(refresh_duration));
  auto crl_post_update = (*provider)->GetCrl(cert);
  ASSERT_NE(crl_post_update, nullptr);
  EXPECT_EQ(crl_post_update->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 2);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
