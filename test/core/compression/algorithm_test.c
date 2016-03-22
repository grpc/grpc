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

#include "src/core/compression/algorithm_metadata.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/transport/static_metadata.h"
#include "test/core/util/test_config.h"

static void test_algorithm_mesh(void) {
  int i;

  gpr_log(GPR_DEBUG, "test_algorithm_mesh");

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    char *name;
    grpc_compression_algorithm parsed;
    grpc_mdstr *mdstr;
    grpc_mdelem *mdelem;
    GPR_ASSERT(
        grpc_compression_algorithm_name((grpc_compression_algorithm)i, &name));
    GPR_ASSERT(grpc_compression_algorithm_parse(name, strlen(name), &parsed));
    GPR_ASSERT((int)parsed == i);
    mdstr = grpc_mdstr_from_string(name);
    GPR_ASSERT(mdstr == grpc_compression_algorithm_mdstr(parsed));
    GPR_ASSERT(parsed == grpc_compression_algorithm_from_mdstr(mdstr));
    mdelem = grpc_compression_encoding_mdelem(parsed);
    GPR_ASSERT(mdelem->value == mdstr);
    GPR_ASSERT(mdelem->key == GRPC_MDSTR_GRPC_ENCODING);
    GRPC_MDSTR_UNREF(mdstr);
    GRPC_MDELEM_UNREF(mdelem);
  }

  /* test failure */
  GPR_ASSERT(NULL ==
             grpc_compression_encoding_mdelem(GRPC_COMPRESS_ALGORITHMS_COUNT));
}

static void test_algorithm_failure(void) {
  grpc_mdstr *mdstr;

  gpr_log(GPR_DEBUG, "test_algorithm_failure");

  GPR_ASSERT(grpc_compression_algorithm_name(GRPC_COMPRESS_ALGORITHMS_COUNT,
                                             NULL) == 0);
  GPR_ASSERT(grpc_compression_algorithm_name(GRPC_COMPRESS_ALGORITHMS_COUNT + 1,
                                             NULL) == 0);
  mdstr = grpc_mdstr_from_string("this-is-an-invalid-algorithm");
  GPR_ASSERT(grpc_compression_algorithm_from_mdstr(mdstr) ==
             GRPC_COMPRESS_ALGORITHMS_COUNT);
  GPR_ASSERT(grpc_compression_algorithm_mdstr(GRPC_COMPRESS_ALGORITHMS_COUNT) ==
             NULL);
  GPR_ASSERT(grpc_compression_algorithm_mdstr(GRPC_COMPRESS_ALGORITHMS_COUNT +
                                              1) == NULL);
  GRPC_MDSTR_UNREF(mdstr);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_algorithm_mesh();
  test_algorithm_failure();

  grpc_shutdown();

  return 0;
}
