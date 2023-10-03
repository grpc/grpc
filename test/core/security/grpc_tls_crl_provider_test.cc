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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/status/statusor.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_crl_provider.h>

#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define CRL_PATH "test/core/tsi/test_creds/crl_data/crls/ab06acdd.r0"
#define CRL_ISSUER "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=testca"

namespace grpc_core {
namespace testing {

const std::string kCrlDirectory = "test/core/tsi/test_creds/crl_data/crls";

using ::grpc_core::experimental::CertificateInfoImpl;
using ::grpc_core::experimental::Crl;
using ::grpc_core::experimental::CrlImpl;
using ::grpc_core::experimental::CrlProvider;
using ::grpc_core::experimental::StaticCrlProvider;

TEST(CrlProviderTest, CanParseCrl) {
  std::string crl_string = GetFileContents(CRL_PATH);
  absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
  ASSERT_TRUE(result.ok());
  ASSERT_NE(*result, nullptr);
  auto* crl = static_cast<CrlImpl*>(result->get());
  EXPECT_STREQ(crl->Issuer().data(), CRL_ISSUER);
}

TEST(CrlProviderTest, InvalidFile) {
  std::string crl_string = "INVALID CRL FILE";
  absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "Conversion from PEM string to X509 CRL failed."));
}

TEST(CrlProviderTest, StaticCrlProviderLookup) {
  std::vector<std::string> crl_strings = {GetFileContents(CRL_PATH)};
  absl::StatusOr<std::shared_ptr<CrlProvider>> result =
      StaticCrlProvider::FromVector(crl_strings);
  std::shared_ptr<CrlProvider> provider = std::move(*result);

  CertificateInfoImpl cert = CertificateInfoImpl(CRL_ISSUER);

  auto crl = provider->GetCrl(cert);
  ASSERT_NE(crl, nullptr);
  ASSERT_EQ(crl->Issuer(), CRL_ISSUER);
}

TEST(CrlProviderTest, StaticCrlProviderLookupBad) {
  std::vector<std::string> crl_strings = {GetFileContents(CRL_PATH)};
  absl::StatusOr<std::shared_ptr<CrlProvider>> result =
      StaticCrlProvider::FromVector(crl_strings);
  std::shared_ptr<CrlProvider> provider = std::move(*result);

  CertificateInfoImpl bad_cert = CertificateInfoImpl("BAD CERT");
  auto crl = provider->GetCrl(bad_cert);
  ASSERT_EQ(crl, nullptr);
}

TEST(CrlProviderTest, DirectoryReloaderCrl) {
  auto result = experimental::DirectoryReloaderCrlProvider::
      CreateDirectoryReloaderProvider(kCrlDirectory, absl::Seconds(1), nullptr);
  ASSERT_TRUE(result.ok());
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
