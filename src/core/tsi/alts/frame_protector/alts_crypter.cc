//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/tsi/alts/frame_protector/alts_crypter.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <string.h>

static void maybe_copy_error_msg(const char* src, char** dst) {
  if (dst != nullptr && src != nullptr) {
    *dst = static_cast<char*>(gpr_malloc(strlen(src) + 1));
    memcpy(*dst, src, strlen(src) + 1);
  }
}

grpc_status_code alts_crypter_process_in_place(
    alts_crypter* crypter, unsigned char* data, size_t data_allocated_size,
    size_t data_size, size_t* output_size, char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->process_in_place != nullptr) {
    return crypter->vtable->process_in_place(crypter, data, data_allocated_size,
                                             data_size, output_size,
                                             error_details);
  }
  // An error occurred.
  const char error_msg[] =
      "crypter or crypter->vtable has not been initialized properly.";
  maybe_copy_error_msg(error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

size_t alts_crypter_num_overhead_bytes(const alts_crypter* crypter) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->num_overhead_bytes != nullptr) {
    return crypter->vtable->num_overhead_bytes(crypter);
  }
  // An error occurred.
  return 0;
}

void alts_crypter_destroy(alts_crypter* crypter) {
  if (crypter != nullptr) {
    if (crypter->vtable != nullptr && crypter->vtable->destruct != nullptr) {
      crypter->vtable->destruct(crypter);
    }
    gpr_free(crypter);
  }
}
