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

#include "src/core/tsi/alts/frame_protector/alts_counter.h"

#include <string.h>

#include <grpc/support/alloc.h>

static void maybe_copy_error_msg(const char* src, char** dst) {
  if (dst != nullptr && src != nullptr) {
    *dst = static_cast<char*>(gpr_malloc(strlen(src) + 1));
    memcpy(*dst, src, strlen(src) + 1);
  }
}

grpc_status_code alts_counter_create(bool is_client, size_t counter_size,
                                     size_t overflow_size,
                                     alts_counter** crypter_counter,
                                     char** error_details) {
  /* Perform input sanity check. */
  if (counter_size == 0) {
    const char error_msg[] = "counter_size is invalid.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (overflow_size == 0 || overflow_size >= counter_size) {
    const char error_msg[] = "overflow_size is invalid.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (crypter_counter == nullptr) {
    const char error_msg[] = "crypter_counter is nullptr.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  *crypter_counter =
      static_cast<alts_counter*>(gpr_malloc(sizeof(**crypter_counter)));
  (*crypter_counter)->size = counter_size;
  (*crypter_counter)->overflow_size = overflow_size;
  (*crypter_counter)->counter =
      static_cast<unsigned char*>(gpr_zalloc(counter_size));
  if (is_client) {
    ((*crypter_counter)->counter)[counter_size - 1] = 0x80;
  }
  return GRPC_STATUS_OK;
}

grpc_status_code alts_counter_increment(alts_counter* crypter_counter,
                                        bool* is_overflow,
                                        char** error_details) {
  /* Perform input sanity check. */
  if (crypter_counter == nullptr) {
    const char error_msg[] = "crypter_counter is nullptr.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (is_overflow == nullptr) {
    const char error_msg[] = "is_overflow is nullptr.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  /* Increment the internal counter. */
  size_t i = 0;
  for (; i < crypter_counter->overflow_size; i++) {
    (crypter_counter->counter)[i]++;
    if ((crypter_counter->counter)[i] != 0x00) {
      break;
    }
  }
  /**
   * If the lower overflow_size bytes are all zero, the counter has overflowed.
   */
  if (i == crypter_counter->overflow_size) {
    *is_overflow = true;
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  *is_overflow = false;
  return GRPC_STATUS_OK;
}

size_t alts_counter_get_size(alts_counter* crypter_counter) {
  if (crypter_counter == nullptr) {
    return 0;
  }
  return crypter_counter->size;
}

unsigned char* alts_counter_get_counter(alts_counter* crypter_counter) {
  if (crypter_counter == nullptr) {
    return nullptr;
  }
  return crypter_counter->counter;
}

void alts_counter_destroy(alts_counter* crypter_counter) {
  if (crypter_counter != nullptr) {
    gpr_free(crypter_counter->counter);
    gpr_free(crypter_counter);
  }
}
