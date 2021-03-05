//
// Copyright 2021 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"

#include <gmock/gmock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {

namespace testing {

class GrpcTlsCertificateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
  }

  static int SyncExternalVerifierGoodVerify(
      grpc_tls_certificate_verifier_external* external_verifier,
      grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg) {
    request->status = GRPC_STATUS_OK;
    return false;
  }

};

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierGood) {
  auto* external_verifier = new grpc_tls_certificate_verifier_external();
  external_verifier->verify = SyncExternalVerifierGoodVerify;
  external_verifier->cancel = nullptr;
  external_verifier->destruct = nullptr;
  // Takes the ownership of external_verifier.
  ExternalCertificateVerifier external_certificate_verifier(external_verifier);
  grpc_tls_custom_verification_check_request request;
  external_certificate_verifier.Verify(&request, []{});
  EXPECT_EQ(request.status, GRPC_STATUS_OK);
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
