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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/compression_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test_compression_algorithm_parse(void) {
  size_t i;
  const char* valid_names[] = {"identity", "gzip", "deflate", "stream/gzip"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE,
      GRPC_COMPRESS_STREAM_GZIP};
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
  const char* valid_names[] = {"identity", "gzip", "deflate", "stream/gzip"};
  const grpc_compression_algorithm valid_algorithms[] = {
      GRPC_COMPRESS_NONE, GRPC_COMPRESS_GZIP, GRPC_COMPRESS_DEFLATE,
      GRPC_COMPRESS_STREAM_GZIP};

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

  {
    /* accept stream gzip */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_STREAM_GZIP);

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
    /* accept all algorithms */
    uint32_t accepted_encodings = 0;
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_NONE); /* always */
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_GZIP);
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_DEFLATE);
    GPR_BITSET(&accepted_encodings, GRPC_COMPRESS_STREAM_GZIP);

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

static void test_channel_args_set_compression_algorithm(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* ch_args;

  ch_args = grpc_channel_args_set_channel_default_compression_algorithm(
      nullptr, GRPC_COMPRESS_GZIP);
  GPR_ASSERT(ch_args->num_args == 1);
  GPR_ASSERT(strcmp(ch_args->args[0].key,
                    GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM) == 0);
  GPR_ASSERT(ch_args->args[0].type == GRPC_ARG_INTEGER);

  grpc_channel_args_destroy(ch_args);
}

static void test_channel_args_compression_algorithm_states(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args *ch_args, *ch_args_wo_gzip, *ch_args_wo_gzip_deflate,
      *ch_args_wo_gzip_deflate_gzip;
  unsigned states_bitset;
  size_t i;

  ch_args = grpc_channel_args_copy_and_add(nullptr, nullptr, 0);
  /* by default, all enabled */
  states_bitset = static_cast<unsigned>(
      grpc_channel_args_compression_algorithm_get_states(ch_args));

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    GPR_ASSERT(GPR_BITGET(states_bitset, i));
  }

  /* disable gzip and deflate and stream/gzip */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args, GRPC_COMPRESS_GZIP, 0);
  GPR_ASSERT(ch_args == ch_args_wo_gzip);
  ch_args_wo_gzip_deflate = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip, GRPC_COMPRESS_DEFLATE, 0);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate);
  ch_args_wo_gzip_deflate_gzip =
      grpc_channel_args_compression_algorithm_set_state(
          &ch_args_wo_gzip_deflate, GRPC_COMPRESS_STREAM_GZIP, 0);
  GPR_ASSERT(ch_args_wo_gzip_deflate == ch_args_wo_gzip_deflate_gzip);

  states_bitset =
      static_cast<unsigned>(grpc_channel_args_compression_algorithm_get_states(
          ch_args_wo_gzip_deflate));
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_GZIP || i == GRPC_COMPRESS_DEFLATE ||
        i == GRPC_COMPRESS_STREAM_GZIP) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  /* re-enabled gzip and stream/gzip only */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip_deflate_gzip, GRPC_COMPRESS_GZIP, 1);
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip, GRPC_COMPRESS_STREAM_GZIP, 1);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate_gzip);

  states_bitset = static_cast<unsigned>(
      grpc_channel_args_compression_algorithm_get_states(ch_args_wo_gzip));
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_DEFLATE) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  grpc_channel_args_destroy(ch_args);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_compression_algorithm_parse();
  test_compression_algorithm_name();
  test_compression_algorithm_for_level();
  test_compression_enable_disable_algorithm();
  test_channel_args_set_compression_algorithm();
  test_channel_args_compression_algorithm_states();
  grpc_shutdown();
  return 0;
}
