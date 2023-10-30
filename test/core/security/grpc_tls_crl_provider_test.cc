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

#include "src/core/lib/event_engine/default_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

static constexpr absl::string_view kCrlPath =
    "test/core/tsi/test_creds/crl_data/crls/current.crl";
static constexpr absl::string_view kCrlName = "current.crl";
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

class FakeDirectoryReader : public DirectoryReader {
 public:
  ~FakeDirectoryReader() override = default;
  absl::StatusOr<std::vector<std::string>> GetDirectoryContents() override {
    return files_in_directory_;
  }
  absl::string_view Name() override { return kCrlDirectory; }

  void SetFilesInDirectory(absl::StatusOr<std::vector<std::string>> files) {
    files_in_directory_ = std::move(files);
  }
  void SetName(absl::string_view value) {
    directory_path_ = std::string(value);
  }

 private:
  absl::StatusOr<std::vector<std::string>> files_in_directory_;
  std::string directory_path_;
};

class DirectoryReloaderCrlProviderTest : public ::testing::Test {
 protected:
  // Tests that want a fake directory reader can call this without setting the
  // last parameter.
  absl::StatusOr<std::shared_ptr<CrlProvider>> CreateCrlProvider(
      std::chrono::seconds refresh_duration,
      std::function<void(absl::Status)> reload_error_callback,
      std::shared_ptr<DirectoryReader> directory_reader = nullptr) {
    if (directory_reader == nullptr) directory_reader = directory_reader_;
    directory_reader_->SetName(kCrlDirectory);
    auto provider =
        std::make_shared<experimental::DirectoryReloaderCrlProvider>(
            refresh_duration, std::move(reload_error_callback), event_engine_,
            std::move(directory_reader));
    absl::Status initial_status = provider->Update();
    if (!initial_status.ok()) return initial_status;
    provider->ScheduleReload();
    return provider;
  }

  // Tests that want a real directory can call this instead of the above.
  absl::StatusOr<std::shared_ptr<CrlProvider>> CreateCrlProvider(
      absl::string_view directory, std::chrono::seconds refresh_duration,
      std::function<void(absl::Status)> reload_error_callback) {
    return CreateCrlProvider(refresh_duration, std::move(reload_error_callback),
                             MakeDirectoryReader(directory));
  }

  std::shared_ptr<FakeDirectoryReader> directory_reader_ =
      std::make_shared<FakeDirectoryReader>();
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_ =
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              grpc_event_engine::experimental::FuzzingEventEngine::Options(),
              fuzzing_event_engine::Actions());
};

TEST(CrlProviderTest, CanParseCrl) {
  std::string crl_string = GetFileContents(kCrlPath.data());
  absl::StatusOr<std::shared_ptr<Crl>> crl = Crl::Parse(crl_string);
  ASSERT_TRUE(crl.ok()) << crl.status();
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

TEST(CrlProviderTest, Temp) {
  static constexpr char one[] = "abc";
  const char* two = "abc";
  ASSERT_TRUE(strcmp(one, two) == 0);
}

TEST_F(DirectoryReloaderCrlProviderTest, DirectoryReloaderCrlLookupGood) {
  auto provider =
      CreateCrlProvider(kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  CertificateInfoImpl intermediate(kCrlIntermediateIssuer);
  auto intermediate_crl = (*provider)->GetCrl(intermediate);
  ASSERT_NE(intermediate_crl, nullptr);
  EXPECT_EQ(intermediate_crl->Issuer(), kCrlIntermediateIssuer);
}

TEST_F(DirectoryReloaderCrlProviderTest,
       DirectoryReloaderCrlLookupMissingIssuer) {
  auto provider =
      CreateCrlProvider(kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl bad_cert("BAD CERT");
  auto crl = (*provider)->GetCrl(bad_cert);
  ASSERT_EQ(crl, nullptr);
}

TEST_F(DirectoryReloaderCrlProviderTest, DirectoryReloaderReloadsAndDeletes) {
  const std::chrono::seconds kRefreshDuration(60);
  absl::StatusOr<std::vector<std::string>> files = std::vector<std::string>();
  directory_reader_->SetFilesInDirectory(files);
  auto provider = CreateCrlProvider(kRefreshDuration, nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl cert(kCrlIssuer);
  auto should_be_no_crl = (*provider)->GetCrl(cert);
  ASSERT_EQ(should_be_no_crl, nullptr);
  // Give the provider files to find in the directory
  files = {std::string(kCrlName)};
  directory_reader_->SetFilesInDirectory(files);
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  // Now we won't see any files in our directory
  files = std::vector<std::string>();
  directory_reader_->SetFilesInDirectory(files);
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl_should_be_deleted = (*provider)->GetCrl(cert);
  ASSERT_EQ(crl_should_be_deleted, nullptr);
}

TEST_F(DirectoryReloaderCrlProviderTest, DirectoryReloaderWithCorruption) {
  absl::StatusOr<std::vector<std::string>> files =
      std::vector<std::string>({std::string(kCrlName)});
  directory_reader_->SetFilesInDirectory(files);
  const std::chrono::seconds kRefreshDuration(60);
  std::vector<absl::Status> reload_errors;
  std::function<void(absl::Status)> reload_error_callback =
      [&](const absl::Status& status) { reload_errors.push_back(status); };
  auto provider =
      CreateCrlProvider(kRefreshDuration, std::move(reload_error_callback));
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 0);
  // Point the provider at a non-crl file so loading fails
  // Should result in the CRL Reloader keeping the old CRL data
  files = {std::string(kRootCert)};
  directory_reader_->SetFilesInDirectory(files);
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl_post_update = (*provider)->GetCrl(cert);
  ASSERT_NE(crl_post_update, nullptr);
  EXPECT_EQ(crl_post_update->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 1);
}

TEST_F(DirectoryReloaderCrlProviderTest, DirectoryReloaderWithBadStatus) {
  absl::StatusOr<std::vector<std::string>> files = absl::UnknownError("");
  directory_reader_->SetFilesInDirectory(files);
  const std::chrono::seconds kRefreshDuration(60);
  auto provider = CreateCrlProvider(kRefreshDuration, nullptr, nullptr);
  ASSERT_FALSE(provider.ok()) << provider.status();
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
