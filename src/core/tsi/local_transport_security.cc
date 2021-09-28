/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/tsi/local_transport_security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/tsi/transport_security_grpc.h"

namespace {

/* Main struct for local TSI zero-copy frame protector. */
typedef struct local_zero_copy_grpc_protector {
  tsi_zero_copy_grpc_protector base;
} local_zero_copy_grpc_protector;

/* Main struct for local TSI handshaker result. */
typedef struct local_tsi_handshaker_result {
  tsi_handshaker_result base;
  bool is_client;
} local_tsi_handshaker_result;

/* Main struct for local TSI handshaker. */
typedef struct local_tsi_handshaker {
  tsi_handshaker base;
  bool is_client;
} local_tsi_handshaker;

/* --- tsi_handshaker_result methods implementation. --- */

static tsi_result handshaker_result_extract_peer(
    const tsi_handshaker_result* /*self*/, tsi_peer* /*peer*/) {
  return TSI_OK;
}

static tsi_frame_protector_type handshaker_result_frame_protector_type(
    const tsi_handshaker_result* /*self*/) {
  return TSI_FRAME_PROTECTOR_NONE;
}

static void handshaker_result_destroy(tsi_handshaker_result* self) {
  if (self == nullptr) {
    return;
  }
  local_tsi_handshaker_result* result =
      reinterpret_cast<local_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  gpr_free(result);
}

static const tsi_handshaker_result_vtable result_vtable = {
    handshaker_result_extract_peer,
    handshaker_result_frame_protector_type,
    nullptr, /* handshaker_result_create_zero_copy_grpc_protector */
    nullptr, /* handshaker_result_create_frame_protector */
    nullptr, /* handshaker_result_get_unused_bytes */
    handshaker_result_destroy};

static tsi_result create_handshaker_result(bool is_client,
                                           tsi_handshaker_result** self) {
  if (self == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to create_handshaker_result()");
    return TSI_INVALID_ARGUMENT;
  }
  local_tsi_handshaker_result* result =
      static_cast<local_tsi_handshaker_result*>(gpr_zalloc(sizeof(*result)));
  result->is_client = is_client;
  result->base.vtable = &result_vtable;
  *self = &result->base;
  return TSI_OK;
}

/* --- tsi_handshaker methods implementation. --- */

static tsi_result handshaker_next(
    tsi_handshaker* self, const unsigned char* /*received_bytes*/,
    size_t /*received_bytes_size*/, const unsigned char** /*bytes_to_send*/,
    size_t* bytes_to_send_size, tsi_handshaker_result** result,
    tsi_handshaker_on_next_done_cb /*cb*/, void* /*user_data*/) {
  if (self == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_next()");
    return TSI_INVALID_ARGUMENT;
  }
  /* Note that there is no interaction between TSI peers, and all operations are
   * local.
   */
  local_tsi_handshaker* handshaker =
      reinterpret_cast<local_tsi_handshaker*>(self);
  *bytes_to_send_size = 0;
  create_handshaker_result(handshaker->is_client, result);
  return TSI_OK;
}

static void handshaker_destroy(tsi_handshaker* self) {
  if (self == nullptr) {
    return;
  }
  local_tsi_handshaker* handshaker =
      reinterpret_cast<local_tsi_handshaker*>(self);
  gpr_free(handshaker);
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr, /* get_bytes_to_send_to_peer -- deprecated */
    nullptr, /* process_bytes_from_peer   -- deprecated */
    nullptr, /* get_result                -- deprecated */
    nullptr, /* extract_peer              -- deprecated */
    nullptr, /* create_frame_protector    -- deprecated */
    handshaker_destroy,
    handshaker_next,
    nullptr, /* shutdown */
};

}  // namespace

tsi_result tsi_local_handshaker_create(bool is_client, tsi_handshaker** self) {
  if (self == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to local_tsi_handshaker_create()");
    return TSI_INVALID_ARGUMENT;
  }
  local_tsi_handshaker* handshaker =
      static_cast<local_tsi_handshaker*>(gpr_zalloc(sizeof(*handshaker)));
  handshaker->is_client = is_client;
  handshaker->base.vtable = &handshaker_vtable;
  *self = &handshaker->base;
  return TSI_OK;
}
