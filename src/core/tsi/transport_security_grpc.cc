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

#include "src/core/tsi/transport_security_grpc.h"

/* This method creates a tsi_zero_copy_grpc_protector object.  */
tsi_result tsi_handshaker_result_create_zero_copy_grpc_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_zero_copy_grpc_protector** protector) {
  if (self == nullptr || self->vtable == nullptr || protector == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->create_zero_copy_grpc_protector == nullptr) {
    return TSI_UNIMPLEMENTED;
  }
  return self->vtable->create_zero_copy_grpc_protector(
      self, max_output_protected_frame_size, protector);
}

/* --- tsi_zero_copy_grpc_protector common implementation. ---

   Calls specific implementation after state/input validation. */

tsi_result tsi_zero_copy_grpc_protector_protect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* unprotected_slices,
    grpc_slice_buffer* protected_slices) {
  if (self == nullptr || self->vtable == nullptr ||
      unprotected_slices == nullptr || protected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->protect == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->protect(self, unprotected_slices, protected_slices);
}

tsi_result tsi_zero_copy_grpc_protector_unprotect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* protected_slices,
    grpc_slice_buffer* unprotected_slices, int* min_progress_size) {
  if (self == nullptr || self->vtable == nullptr ||
      protected_slices == nullptr || unprotected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->unprotect == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->unprotect(self, protected_slices, unprotected_slices,
                                 min_progress_size);
}

void tsi_zero_copy_grpc_protector_destroy(tsi_zero_copy_grpc_protector* self) {
  if (self == nullptr) return;
  self->vtable->destroy(self);
}

tsi_result tsi_zero_copy_grpc_protector_max_frame_size(
    tsi_zero_copy_grpc_protector* self, size_t* max_frame_size) {
  if (self == nullptr || max_frame_size == nullptr) return TSI_INVALID_ARGUMENT;
  if (self->vtable->max_frame_size == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->max_frame_size(self, max_frame_size);
}
