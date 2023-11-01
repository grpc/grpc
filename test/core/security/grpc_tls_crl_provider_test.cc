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
#include "src/core/lib/iomgr/timer_manager.h"
#include "test/core/event_engine/event_engine_test_utils.h"
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
using ::grpc_core::experimental::CrlProvider;

namespace grpc_core {
namespace testing {

class FakeDirectoryReader : public DirectoryReader {
 public:
  ~FakeDirectoryReader() override = default;
  absl::Status ForEach(
      absl::FunctionRef<void(absl::string_view)> callback) override {
    if (!files_in_directory_.ok()) {
      return files_in_directory_.status();
    }
    for (const auto& file : *files_in_directory_) {
      callback(file);
    }
    return absl::OkStatus();
  }
  absl::string_view Name() const override { return kCrlDirectory; }

  void SetFilesInDirectory(std::vector<std::string> files) {
    files_in_directory_ = std::move(files);
  }

  void SetStatus(absl::Status status) { files_in_directory_ = status; }

 private:
  absl::StatusOr<std::vector<std::string>> files_in_directory_ =
      std::vector<std::string>();
};

class DirectoryReloaderCrlProviderTest : public ::testing::Test {
 public:
  void SetUp() override {
    event_engine_ =
        std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
            grpc_event_engine::experimental::FuzzingEventEngine::Options(),
            fuzzing_event_engine::Actions());
    // Without this the test had a failure dealing with grpc timers on TSAN
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
  }
  void TearDown() override {
    ExecCtx exec_ctx;
    event_engine_->FuzzingDone();
    exec_ctx.Flush();
    event_engine_->TickUntilIdle();
    grpc_event_engine::experimental::WaitForSingleOwner(
        std::move(event_engine_));
    grpc_shutdown_blocking();
    event_engine_.reset();
  }

 protected:
  // Tests that want a fake directory reader can call this without setting the
  // last parameter.
  absl::StatusOr<std::shared_ptr<CrlProvider>> CreateCrlProvider(
      std::chrono::seconds refresh_duration,
      std::function<void(absl::Status)> reload_error_callback,
      std::shared_ptr<DirectoryReader> directory_reader = nullptr) {
    if (directory_reader == nullptr) directory_reader = directory_reader_;
    auto provider =
        std::make_shared<experimental::DirectoryReloaderCrlProvider>(
            refresh_duration, std::move(reload_error_callback), event_engine_,
            std::move(directory_reader));
    provider->UpdateAndStartTimer();
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
      event_engine_;
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

TEST_F(DirectoryReloaderCrlProviderTest, CrlLookupGood) {
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

TEST_F(DirectoryReloaderCrlProviderTest, CrlLookupMissingIssuer) {
  auto provider =
      CreateCrlProvider(kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl bad_cert("BAD CERT");
  auto crl = (*provider)->GetCrl(bad_cert);
  ASSERT_EQ(crl, nullptr);
}

TEST_F(DirectoryReloaderCrlProviderTest, ReloadsAndDeletes) {
  const std::chrono::seconds kRefreshDuration(60);
  auto provider = CreateCrlProvider(kRefreshDuration, nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
  CertificateInfoImpl cert(kCrlIssuer);
  auto should_be_no_crl = (*provider)->GetCrl(cert);
  ASSERT_EQ(should_be_no_crl, nullptr);
  // Give the provider files to find in the directory
  directory_reader_->SetFilesInDirectory({std::string(kCrlName)});
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  // Now we won't see any files in our directory
  directory_reader_->SetFilesInDirectory({});
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl_should_be_deleted = (*provider)->GetCrl(cert);
  ASSERT_EQ(crl_should_be_deleted, nullptr);
}

TEST_F(DirectoryReloaderCrlProviderTest, WithCorruption) {
  directory_reader_->SetFilesInDirectory({std::string(kCrlName)});
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
  directory_reader_->SetFilesInDirectory({std::string(kRootCert)});
  event_engine_->TickForDuration(kRefreshDuration);
  auto crl_post_update = (*provider)->GetCrl(cert);
  ASSERT_NE(crl_post_update, nullptr);
  EXPECT_EQ(crl_post_update->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 1);
}

TEST_F(DirectoryReloaderCrlProviderTest, WithBadInitialDirectoryStatus) {
  absl::Status status = absl::UnknownError("");
  directory_reader_->SetStatus(status);
  std::vector<absl::Status> reload_errors;
  std::function<void(absl::Status)> reload_error_callback =
      [&](const absl::Status& status) { reload_errors.push_back(status); };
  const std::chrono::seconds kRefreshDuration(60);
  auto provider =
      CreateCrlProvider(kRefreshDuration, reload_error_callback, nullptr);
  // We expect the provider to be created successfully, but the reload error
  // callback will have been called
  ASSERT_TRUE(provider.ok()) << provider.status();
  EXPECT_EQ(reload_errors.size(), 1);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
