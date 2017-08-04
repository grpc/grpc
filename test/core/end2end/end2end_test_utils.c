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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/support/log.h>

const char *get_host_override_string(const char *str,
                                     grpc_end2end_test_config config) {
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER) {
    return str;
  } else {
    return NULL;
  }
}

const grpc_slice *get_host_override_slice(const char *str,
                                          grpc_end2end_test_config config) {
  const char *r = get_host_override_string(str, config);
  if (r != NULL) {
    static grpc_slice ret;
    ret = grpc_slice_from_static_string(r);
    return &ret;
  }
  return NULL;
}

void validate_host_override_string(const char *pattern, grpc_slice str,
                                   grpc_end2end_test_config config) {
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER) {
    GPR_ASSERT(0 == grpc_slice_str_cmp(str, pattern));
  }
}
