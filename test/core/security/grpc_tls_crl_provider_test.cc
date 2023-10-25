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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
static constexpr absl::string_view kIntermediateCrlPath =
    "test/core/tsi/test_creds/crl_data/crls/intermediate.crl";
static constexpr absl::string_view kCrlIntermediateIssuer =
    "/CN=intermediatecert.example.com";

static constexpr absl::string_view kCrlDirectory =
    "test/core/tsi/test_creds/crl_data/crls";
static constexpr absl::string_view kCrlDynamicDirectory =
    "test/core/tsi/test_creds/crl_data/crl_provider_test_dir";

using ::grpc_core::experimental::CertificateInfoImpl;
using ::grpc_core::experimental::Crl;
using ::grpc_core::experimental::CrlImpl;
using ::grpc_core::experimental::CrlProvider;
using ::grpc_core::experimental::StaticCrlProvider;

namespace grpc_core {
namespace testing {

absl::StatusOr<std::shared_ptr<CrlProvider>>
CreateDirectoryReloaderCrlProviderWithEngine(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  auto provider = std::make_shared<experimental::DirectoryReloaderCrlProvider>(
      directory, refresh_duration, reload_error_callback, event_engine,
      std::make_shared<Directory>(directory));
  absl::Status initial_status = provider->Update();
  if (!initial_status.ok()) {
    return initial_status;
  }
  provider->ScheduleReload();
  return provider;
}

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

std::string MakeTempDir() {
  char directory_template[] = "/tmp/tmpdir.XXXXXX";
  std::string dir_path = mkdtemp(directory_template);
  return dir_path;
}

std::string TempDirNameFromPath(absl::string_view dir_path) {
  std::vector<absl::string_view> split = absl::StrSplit(dir_path, "/");
  return absl::StrCat(split[2], "/");
}

TEST(CrlProviderTest, DirectoryReloaderReloadsAndDeletes) {
  std::string dir_path = MakeTempDir();
  std::string dir_name = TempDirNameFromPath(dir_path);
  auto fuzzing_ee =
      std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
          grpc_event_engine::experimental::FuzzingEventEngine::Options(),
          fuzzing_event_engine::Actions());
  int refresh_duration = 60;
  auto provider = CreateDirectoryReloaderCrlProviderWithEngine(
      dir_path, std::chrono::seconds(refresh_duration), nullptr, fuzzing_ee);
  ASSERT_TRUE(provider.ok());
  CertificateInfoImpl cert(kCrlIssuer);
  auto should_be_no_crl = (*provider)->GetCrl(cert);
  ASSERT_EQ(should_be_no_crl, nullptr);

  {
    std::string raw_crl = GetFileContents(kCrlPath.data());
    TmpFile tmp_crl(raw_crl, dir_name);
    fuzzing_ee->TickForDuration(
        Duration::FromSecondsAsDouble(refresh_duration));
    auto crl = (*provider)->GetCrl(cert);
    ASSERT_NE(crl, nullptr);
    EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  }
  // After this provider shouldn't give a CRL, because everything should be
  // read cleanly and there is no CRL because TmpFile went out of scope and
  // was deleted
  fuzzing_ee->TickForDuration(Duration::FromSecondsAsDouble(refresh_duration));
  auto crl_should_be_deleted = (*provider)->GetCrl(cert);
  ASSERT_EQ(crl_should_be_deleted, nullptr);
  rmdir(dir_path.c_str());
}

TEST(CrlProviderTest, DirectoryReloaderWithCorruption) {
  std::string dir_path = MakeTempDir();
  std::string dir_name = TempDirNameFromPath(dir_path);
  std::string raw_crl = GetFileContents(kCrlPath.data());
  TmpFile tmp_crl(raw_crl, dir_name);
  int refresh_duration = 60;
  auto fuzzing_ee =
      std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
          grpc_event_engine::experimental::FuzzingEventEngine::Options(),
          fuzzing_event_engine::Actions());
  std::vector<absl::Status> reload_errors;
  std::function<void(absl::Status)> reload_error_callback =
      [&](const absl::Status& status) { reload_errors.push_back(status); };
  auto provider = CreateDirectoryReloaderCrlProviderWithEngine(
      dir_path, std::chrono::seconds(refresh_duration), reload_error_callback,
      fuzzing_ee);
  ASSERT_TRUE(provider.ok());
  CertificateInfoImpl cert(kCrlIssuer);
  auto crl = (*provider)->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  EXPECT_EQ(crl->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 0);
  // Rewrite the crl file with invalid data for a crl
  // Should result in the CRL Reloader keeping the old CRL data
  tmp_crl.RewriteFile("BAD_DATA");
  fuzzing_ee->TickForDuration(Duration::FromSecondsAsDouble(refresh_duration));
  auto crl_post_update = (*provider)->GetCrl(cert);
  ASSERT_NE(crl_post_update, nullptr);
  EXPECT_EQ(crl_post_update->Issuer(), kCrlIssuer);
  EXPECT_EQ(reload_errors.size(), 2);
  // TODO(gtcooke94) check the actual content of the error
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
