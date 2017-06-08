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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"
#include "test/core/util/memory_counters.h"

bool squelch = true;
bool leak_check = true;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char *s;
  struct grpc_memory_counters counters;
  grpc_memory_counters_init();
  s = gpr_malloc(size);
  memcpy(s, data, size);
  grpc_json *x;
  if ((x = grpc_json_parse_string_with_len(s, size))) {
    grpc_json_destroy(x);
  }
  gpr_free(s);
  counters = grpc_memory_counters_snapshot();
  grpc_memory_counters_destroy();
  GPR_ASSERT(counters.total_size_relative == 0);
  return 0;
}
