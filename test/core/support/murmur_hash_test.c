/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/support/murmur_hash.h"
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/test_config.h"

#include <string.h>

typedef uint32_t (*hash_func)(const void *key, size_t len, uint32_t seed);

/* From smhasher:
   This should hopefully be a thorough and uambiguous test of whether a hash
   is correctly implemented on a given platform */

static void verification_test(hash_func hash, uint32_t expected) {
  uint8_t key[256];
  uint32_t hashes[256];
  uint32_t final = 0;
  size_t i;

  memset(key, 0, sizeof(key));
  memset(hashes, 0, sizeof(hashes));

  /* Hash keys of the form {0}, {0,1}, {0,1,2}... up to N=255,using 256-N as
     the seed */

  for (i = 0; i < 256; i++) {
    key[i] = (uint8_t)i;
    hashes[i] = hash(key, i, (uint32_t)(256u - i));
  }

  /* Then hash the result array */

  final = hash(hashes, sizeof(hashes), 0);

  /* The first four bytes of that hash, interpreted as a little-endian integer,
     is our
     verification value */

  if (expected != final) {
    gpr_log(GPR_INFO, "Verification value 0x%08X : Failed! (Expected 0x%08x)",
            final, expected);
    abort();
  } else {
    gpr_log(GPR_INFO, "Verification value 0x%08X : Passed!", final);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  /* basic tests to verify that things don't crash */
  gpr_murmur_hash3("", 0, 0);
  gpr_murmur_hash3("xyz", 3, 0);
  verification_test(gpr_murmur_hash3, 0xB0F57EE3);
  return 0;
}
