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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#include <gmock/gmock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_KEY_CERT_DIR "src/core/tsi/test_creds"
#define SERVER_CERT_FILE "server1.pem"
#define SERVER_KEY_PATH "server1.key"

namespace testing {

constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertContents = "identity_cert_contents";
constexpr const char* kPrivateKeyContents = "private_key_contents";

TEST(GrpcTlsCertificateProviderTest, StaticDataCertificateProviderCreation) {
  grpc_core::PemKeyCertPairList key_cert_pair_list;
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kPrivateKeyContents);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCertContents);
  key_cert_pair_list.emplace_back(ssl_pair);
  grpc_core::StaticDataCertificateProvider provider(kRootCertContents,
                                                    key_cert_pair_list);
}

TEST(GrpcTlsCertificateProviderTest, FileWatcherCertificateProviderCreation) {
  grpc_core::FileWatcherCertificateProvider provider(
      SERVER_KEY_CERT_DIR, SERVER_KEY_PATH, SERVER_CERT_FILE, CA_CERT_PATH, 1);
}

}  // namespace testing

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
