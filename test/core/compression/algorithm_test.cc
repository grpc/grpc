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

#include "src/core/lib/compression/algorithm_metadata.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/util/test_config.h"

const uint32_t message_prefix_length = 0;
const uint32_t stream_prefix_length = 7;
static void test_algorithm_mesh(void) {
  int i;

  gpr_log(GPR_DEBUG, "test_algorithm_mesh");

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    const char* name;
    grpc_compression_algorithm parsed;
    grpc_slice mdstr;
    grpc_mdelem mdelem;
    grpc_core::ExecCtx exec_ctx;
    GPR_ASSERT(
        grpc_compression_algorithm_name((grpc_compression_algorithm)i, &name));
    GPR_ASSERT(grpc_compression_algorithm_parse(
        grpc_slice_from_static_string(name), &parsed));
    GPR_ASSERT((int)parsed == i);
    mdstr = grpc_slice_from_copied_string(name);
    GPR_ASSERT(grpc_slice_eq(mdstr, grpc_compression_algorithm_slice(parsed)));
    GPR_ASSERT(parsed == grpc_compression_algorithm_from_slice(mdstr));
    if (parsed == 0) {
      continue;
    } else if (grpc_compression_algorithm_is_message(parsed)) {
      mdelem = grpc_message_compression_encoding_mdelem(
          grpc_compression_algorithm_to_message_compression_algorithm(parsed));
      grpc_slice value = GRPC_MDVALUE(mdelem);
      GPR_ASSERT(0 == memcmp(&name[message_prefix_length],
                             GRPC_SLICE_START_PTR(value),
                             GRPC_SLICE_LENGTH(value)));
      GPR_ASSERT(grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_GRPC_ENCODING));
    } else {
      mdelem = grpc_stream_compression_encoding_mdelem(
          grpc_compression_algorithm_to_stream_compression_algorithm(parsed));
      grpc_slice value = GRPC_MDVALUE(mdelem);
      GPR_ASSERT(0 == memcmp(&name[stream_prefix_length],
                             GRPC_SLICE_START_PTR(value),
                             GRPC_SLICE_LENGTH(value)));
      GPR_ASSERT(
          grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_CONTENT_ENCODING));
    }
    grpc_slice_unref_internal(mdstr);
    GRPC_MDELEM_UNREF(mdelem);
  }

  /* test failure */
  GPR_ASSERT(GRPC_MDISNULL(
      grpc_compression_encoding_mdelem(GRPC_COMPRESS_ALGORITHMS_COUNT)));
}

static void test_algorithm_failure(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_slice mdstr;

  gpr_log(GPR_DEBUG, "test_algorithm_failure");

  GPR_ASSERT(grpc_compression_algorithm_name(GRPC_COMPRESS_ALGORITHMS_COUNT,
                                             nullptr) == 0);
  GPR_ASSERT(
      grpc_compression_algorithm_name(static_cast<grpc_compression_algorithm>(
                                          GRPC_COMPRESS_ALGORITHMS_COUNT + 1),
                                      nullptr) == 0);
  mdstr = grpc_slice_from_static_string("this-is-an-invalid-algorithm");
  GPR_ASSERT(grpc_compression_algorithm_from_slice(mdstr) ==
             GRPC_COMPRESS_ALGORITHMS_COUNT);
  GPR_ASSERT(grpc_slice_eq(
      grpc_compression_algorithm_slice(GRPC_COMPRESS_ALGORITHMS_COUNT),
      grpc_empty_slice()));
  GPR_ASSERT(grpc_slice_eq(
      grpc_compression_algorithm_slice(static_cast<grpc_compression_algorithm>(
          static_cast<int>(GRPC_COMPRESS_ALGORITHMS_COUNT) + 1)),
      grpc_empty_slice()));
  grpc_slice_unref_internal(mdstr);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_algorithm_mesh();
  test_algorithm_failure();

  grpc_shutdown();

  return 0;
}
