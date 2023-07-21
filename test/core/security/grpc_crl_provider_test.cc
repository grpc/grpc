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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_crl_provider.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define CRL_PATH "test/core/tsi/test_creds/crl_data/crls/ab06acdd.r0"

namespace grpc_core {
namespace testing {

using experimental::CertificateInfo;
using experimental::Crl;
using experimental::CrlProvider;

namespace {

class TestCrlProvider : public experimental::CrlProvider {
 public:
  std::shared_ptr<Crl> GetCrl(const CertificateInfo& cert) { return test_crl_; }
  void SetCrl(absl::string_view crl_string) {
    absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
    if (result.ok()) {
      test_crl_ = *result;
    } else {
      test_crl_ = nullptr;
    }
  }

 private:
  std::shared_ptr<Crl> test_crl_;
};

class CrlProviderTest : public ::testing::Test {};

}  // namespace

TEST_F(CrlProviderTest, CanParseCrl) {
  std::string crl_string = GetFileContents(CRL_PATH);
  absl::StatusOr<std::shared_ptr<Crl>> result = Crl::Parse(crl_string);
  ASSERT_TRUE(result.ok());
  ASSERT_NE(*result, nullptr);
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