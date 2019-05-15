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

#include "src/core/ext/transport/chttp2/alpn/alpn.h"

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void test_alpn_success(void) {
  GPR_ASSERT(grpc_chttp2_is_alpn_version_supported("h2", 2));
  GPR_ASSERT(grpc_chttp2_is_alpn_version_supported("grpc-exp", 8));
}

static void test_alpn_failure(void) {
  GPR_ASSERT(!grpc_chttp2_is_alpn_version_supported("h2-155", 6));
  GPR_ASSERT(!grpc_chttp2_is_alpn_version_supported("h1-15", 5));
}

// First index in ALPN supported version list of a given protocol. Returns a
// value one beyond the last valid element index if not found.
static size_t alpn_version_index(const char* version, size_t size) {
  size_t i;
  for (i = 0; i < grpc_chttp2_num_alpn_versions(); ++i) {
    if (!strncmp(version, grpc_chttp2_get_alpn_version_index(i), size)) {
      return i;
    }
  }
  return i;
}

static void test_alpn_grpc_before_h2(void) {
  // grpc-exp is preferred over h2.
  GPR_ASSERT(alpn_version_index("grpc-exp", 8) < alpn_version_index("h2", 2));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  test_alpn_success();
  test_alpn_failure();
  test_alpn_grpc_before_h2();
  return 0;
}
