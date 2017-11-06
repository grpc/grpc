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

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

static void test_compression_algorithm_parse(void) {
  size_t i;
  const char* valid_names[] = {"identity", "gzip", "deflate"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE};
  const char* invalid_names[] = {"gzip2", "foo", "", "2gzip"};

  gpr_log(GPR_DEBUG, "test_compression_algorithm_parse");

  for (i = 0; i < GPR_ARRAY_SIZE(valid_names); i++) {
    const char* valid_name = valid_names[i];
    grpc_compression_algorithm algorithm;
    const int success = grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(valid_name), &algorithm);
    GPR_ASSERT(success != 0);
    GPR_ASSERT(algorithm == valid_algorithms[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(invalid_names); i++) {
    const char* invalid_name = invalid_names[i];
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
  const char* name;
  size_t i;
  const char* valid_names[] = {"identity", "gzip", "deflate"};
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
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    /* all algorithms are enabled by default */
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) != 0);
  }
  /* disable one by one */
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    grpc_compression_options_disable_algorithm(&options, algorithm);
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) == 0);
  }
  /* re-enable one by one */
  for (algorithm = GRPC_COMPRESS_NONE;
       algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT;
       algorithm = static_cast<grpc_compression_algorithm>(
           static_cast<int>(algorithm) + 1)) {
    grpc_compression_options_enable_algorithm(&options, algorithm);
    GPR_ASSERT(grpc_compression_options_is_algorithm_enabled(&options,
                                                             algorithm) != 0);
  }
}

int main(int argc, char** argv) {
  grpc_init();
  test_compression_algorithm_parse();
  test_compression_algorithm_name();
  test_compression_algorithm_for_level();
  test_compression_enable_disable_algorithm();
  grpc_shutdown();

  return 0;
}
