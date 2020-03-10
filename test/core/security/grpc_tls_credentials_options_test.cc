/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "test/core/end2end/data/ssl_test_data.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

namespace testing {

static void SetKeyMaterials(grpc_tls_key_materials_config* config) {
  const grpc_ssl_pem_key_cert_pair pem_key_pair = {
      test_server1_key,
      test_server1_cert,
  };
  const auto* pem_key_pair_ptr = &pem_key_pair;
  grpc_tls_key_materials_config_set_key_materials(config, test_root_cert,
                                                  &pem_key_pair_ptr, 1);
}

TEST(GrpcTlsCredentialsOptionsTest, SetKeyMaterials) {
  grpc_tls_key_materials_config* config =
      grpc_tls_key_materials_config_create();
  SetKeyMaterials(config);
  EXPECT_STREQ(config->pem_root_certs(), test_root_cert);
  EXPECT_EQ(config->pem_key_cert_pair_list().size(), 1);
  EXPECT_STREQ(config->pem_key_cert_pair_list()[0].private_key(),
               test_server1_key);
  EXPECT_STREQ(config->pem_key_cert_pair_list()[0].cert_chain(),
               test_server1_cert);
  delete config;
}

}  // namespace testing

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
