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
#include <grpc/grpc.h>
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
    const int success = grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(valid_name), &algorithm);
    GPR_ASSERT(success != 0);
    GPR_ASSERT(algorithm == valid_algorithms[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(invalid_names); i++) {
    const char *invalid_name = invalid_names[i];
    grpc_compression_algorithm algorithm;
    int success;
    success = grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(invalid_name), &algorithm);
    GPR_ASSERT(success == 0);
    /* the value of "algorithm" is undefined upon failure */
  }
}

static void test_compression_algorithm_name(void) {
  int success;
  char *name;
  size_t i;
  const char *valid_names[] = {"identity", "gzip", "deflate"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE};

  gpr_log(GPR_DEBUG, "test_compression_algorithm_name");

  for (i = 0; i < GPR_ARRAY_SIZE(valid_algorithms); i++) {
    success = grpc_compression_algorithm_name(valid_algorithms[i], &name);
    GPR_ASSERT(success != 0);
    GPR_ASSERT(strcmp(name, valid_names[i]) == 0);
  }

  success =
      grpc_compression_algorithm_name(GRPC_COMPRESS_ALGORITHMS_COUNT, &name);
  GPR_ASSERT(success == 0);
  /* the value of "name" is undefined upon failure */
}

static void test_compression_algorithm_for_level(void) {
  gpr_log(GPR_DEBUG, "test_compression_algorithm_for_level");

  {
    /* accept only identity (aka none) */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                    accepted_encodings));
  }

  {
    /* accept only gzip */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_GZIP);

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_GZIP ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_GZIP ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_GZIP ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                    accepted_encodings));
  }

  {
    /* accept only deflate */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_DEFLATE);

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_DEFLATE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_DEFLATE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_DEFLATE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                    accepted_encodings));
  }

  {
    /* accept gzip and deflate */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_GZIP);
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_DEFLATE);

    GPR_ASSERT(GRPC_COMPRESS_NONE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_NONE,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_GZIP ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_LOW,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_DEFLATE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_MED,
                                                    accepted_encodings));

    GPR_ASSERT(GRPC_COMPRESS_DEFLATE ==
               grpc_compression_algorithm_for_level(GRPC_COMPRESS_LEVEL_HIGH,
                                                    accepted_encodings));
  }
}

static void test_compression_enable_disable_algorithm(void) {
  grpc_compression_options options;
  grpc_compression_algorithm algorithm;

  gpr_log(GPR_DEBUG, "test_compression_enable_disable_algorithm");

  grpc_compression_options_init(&options);
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT; algorithm++) {
    /* all algorithms are enabled by default */
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) != 0);
  }
  /* disable one by one */
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT; algorithm++) {
    grpc_compression_options_disable_algorithm(&options, algorithm);
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) == 0);
  }
  /* re-enable one by one */
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT; algorithm++) {
    grpc_compression_options_enable_algorithm(&options, algorithm);
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) != 0);
  }
}

int main(int argc, char **argv) {
  grpc_init();
  test_compression_algorithm_parse();
  test_compression_algorithm_name();
  test_compression_algorithm_for_level();
  test_compression_enable_disable_algorithm();
  grpc_shutdown();

  return 0;
}
