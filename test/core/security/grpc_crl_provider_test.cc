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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "absl/status/statusor.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_crl_provider.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define CRL_PATH "test/core/tsi/test_creds/crl_data/crls/ab06acdd.r0"
#define CRL_ISSUER "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=testca"

namespace grpc_core {
namespace testing {

using experimental::Crl;
using experimental::CrlImpl;

TEST(CrlProviderTest, CanParseCrl) {
  std::string crl_string = GetFileContents(CRL_PATH);
  absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
  ASSERT_TRUE(result.ok());
  ASSERT_NE(*result, nullptr);
  auto* crl = static_cast<CrlImpl*>(result->get());
  const X509_CRL* x509_crl = &crl->crl();
  X509_NAME* issuer = X509_CRL_get_issuer(x509_crl);
  const char* buf = X509_NAME_oneline(issuer, nullptr, 0);
  EXPECT_STREQ(buf, CRL_ISSUER);
}

TEST(CrlProviderTest, InvalidFile) {
  std::string crl_string = "INVALID CRL FILE";
  absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "Conversion from PEM string to X509 CRL failed."));
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