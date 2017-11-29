/*
 *
 * Copyright 2017 gRPC authors.
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

#include <stdio.h>
#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "test/core/util/test_config.h"

static void test_convert_grpc_to_tsi_cert_pairs() {
  grpc_ssl_pem_key_cert_pair grpc_pairs[] = {{"private_key1", "cert_chain1"},
                                             {"private_key2", "cert_chain2"},
                                             {"private_key3", "cert_chain3"}};
  const size_t num_pairs = 3;

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, 0);
    GPR_ASSERT(tsi_pairs == nullptr);
  }

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, num_pairs);

    GPR_ASSERT(tsi_pairs != nullptr);
    for (size_t i = 0; i < num_pairs; i++) {
      GPR_ASSERT(strncmp(grpc_pairs[i].private_key, tsi_pairs[i].private_key,
                         strlen(grpc_pairs[i].private_key)) == 0);
      GPR_ASSERT(strncmp(grpc_pairs[i].cert_chain, tsi_pairs[i].cert_chain,
                         strlen(grpc_pairs[i].cert_chain)) == 0);
    }

    grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_pairs, num_pairs);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_convert_grpc_to_tsi_cert_pairs();

  grpc_shutdown();
  return 0;
}
