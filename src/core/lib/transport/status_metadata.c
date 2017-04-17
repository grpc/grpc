/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/transport/status_metadata.h"

#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

/* we offset status by a small amount when storing it into transport metadata
   as metadata cannot store a 0 value (which is used as OK for grpc_status_codes
   */
#define STATUS_OFFSET 1

static void destroy_status(void *ignored) {}

grpc_status_code grpc_get_status_from_metadata(grpc_mdelem md) {
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) {
    return GRPC_STATUS_OK;
  }
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_1)) {
    return GRPC_STATUS_CANCELLED;
  }
  if (grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_2)) {
    return GRPC_STATUS_UNKNOWN;
  }
  void* user_data = grpc_mdelem_get_user_data(md, destroy_status);
  if (user_data != NULL) {
    return ((grpc_status_code)(intptr_t)user_data) - STATUS_OFFSET;
  }
  uint32_t status;
  if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(md), &status)) {
    status = GRPC_STATUS_UNKNOWN; /* could not parse status code */
  }
  grpc_mdelem_set_user_data(md, destroy_status,
                            (void*)(intptr_t)(status + STATUS_OFFSET));
  return (grpc_status_code)status;
}
