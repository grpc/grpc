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

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

static void test_compression_algorithm_parse(void) {
  size_t i;
  const char *valid_names[] = {"identity", "gzip", "deflate"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE};
  const char *invalid_names[] = {"gzip2", "foo", "", "2gzip"};

  gpr_log(GPR_DEBUG, "test_compression_algorithm_parse");

  for (i = 0; i < GPR_ARRAY_SIZE(valid_names); i++) {
    const char *valid_name = valid_names[i];
    grpc_compression_algorithm algorithm;
    int success;
    success = grpc_compression_algorithm_parse(valid_name, strlen(valid_name),
                                               &algorithm);
    GPR_ASSERT(success != 0);
    GPR_ASSERT(algorithm == valid_algorithms[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(invalid_names); i++) {
    const char *invalid_name = invalid_names[i];
    grpc_compression_algorithm algorithm;
    int success;
    success = grpc_compression_algorithm_parse(
        invalid_name, strlen(invalid_name), &algorithm);
    GPR_ASSERT(success == 0);
    /* the value of "algorithm" is undefined upon failure */
  }
}

int main(int argc, char **argv) {
  test_compression_algorithm_parse();

  return 0;
}
