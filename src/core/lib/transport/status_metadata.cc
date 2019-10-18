/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/status_metadata.h"

#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

/* we offset status by a small amount when storing it into transport metadata
   as metadata cannot store a 0 value (which is used as OK for grpc_status_codes
   */
#define STATUS_OFFSET 1

static void destroy_status(void* /*ignored*/) {}

grpc_status_code grpc_get_status_code_from_metadata(grpc_mdelem md) {
  if (grpc_mdelem_static_value_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) {
    return GRPC_STATUS_OK;
  }
  if (grpc_mdelem_static_value_eq(md, GRPC_MDELEM_GRPC_STATUS_1)) {
    return GRPC_STATUS_CANCELLED;
  }
  if (grpc_mdelem_static_value_eq(md, GRPC_MDELEM_GRPC_STATUS_2)) {
    return GRPC_STATUS_UNKNOWN;
  }
  void* user_data = grpc_mdelem_get_user_data(md, destroy_status);
  if (user_data != nullptr) {
    return static_cast<grpc_status_code>((intptr_t)user_data - STATUS_OFFSET);
  }
  uint32_t status;
  if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(md), &status)) {
    status = GRPC_STATUS_UNKNOWN; /* could not parse status code */
  }
  grpc_mdelem_set_user_data(
      md, destroy_status, (void*)static_cast<intptr_t>(status + STATUS_OFFSET));
  return static_cast<grpc_status_code>(status);
}
