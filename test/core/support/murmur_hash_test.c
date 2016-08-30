/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
