/*
 *
 * Copyright 2016 gRPC authors.
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

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args *args) {}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_slice slice = grpc_slice_from_copied_buffer((const char *)data, size);
  grpc_grpclb_initial_response *response;
  if ((response = grpc_grpclb_initial_response_parse(slice))) {
    grpc_grpclb_initial_response_destroy(response);
  }
  grpc_slice_unref(slice);
  return 0;
}
