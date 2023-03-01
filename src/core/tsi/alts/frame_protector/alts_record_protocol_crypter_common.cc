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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.h"

#include <grpc/support/alloc.h>

static void maybe_copy_error_msg(const char* src, char** dst) {
  if (dst != nullptr && src != nullptr) {
    *dst = static_cast<char*>(gpr_malloc(strlen(src) + 1));
    memcpy(*dst, src, strlen(src) + 1);
  }
}

grpc_status_code input_sanity_check(
    const alts_record_protocol_crypter* rp_crypter, const unsigned char* data,
    size_t* output_size, char** error_details) {
  if (rp_crypter == nullptr) {
    maybe_copy_error_msg("alts_crypter instance is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  } else if (data == nullptr) {
    maybe_copy_error_msg("data is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  } else if (output_size == nullptr) {
    maybe_copy_error_msg("output_size is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  return GRPC_STATUS_OK;
}

grpc_status_code increment_counter(alts_record_protocol_crypter* rp_crypter,
                                   char** error_details) {
  bool is_overflow = false;
  grpc_status_code status =
      alts_counter_increment(rp_crypter->ctr, &is_overflow, error_details);
  if (status != GRPC_STATUS_OK) {
    return status;
  }
  if (is_overflow) {
    const char error_msg[] =
        "crypter counter is wrapped. The connection"
        "should be closed and the key should be deleted.";
    maybe_copy_error_msg(error_msg, error_details);
    return GRPC_STATUS_INTERNAL;
  }
  return GRPC_STATUS_OK;
}

size_t alts_record_protocol_crypter_num_overhead_bytes(const alts_crypter* c) {
  if (c != nullptr) {
    size_t num_overhead_bytes = 0;
    char* error_details = nullptr;
    const alts_record_protocol_crypter* rp_crypter =
        reinterpret_cast<const alts_record_protocol_crypter*>(c);
    grpc_status_code status = gsec_aead_crypter_tag_length(
        rp_crypter->crypter, &num_overhead_bytes, &error_details);
    if (status == GRPC_STATUS_OK) {
      return num_overhead_bytes;
    }
  }
  return 0;
}

void alts_record_protocol_crypter_destruct(alts_crypter* c) {
  if (c != nullptr) {
    alts_record_protocol_crypter* rp_crypter =
        reinterpret_cast<alts_record_protocol_crypter*>(c);
    alts_counter_destroy(rp_crypter->ctr);
    gsec_aead_crypter_destroy(rp_crypter->crypter);
  }
}

alts_record_protocol_crypter* alts_crypter_create_common(
    gsec_aead_crypter* crypter, bool is_client, size_t overflow_size,
    char** error_details) {
  if (crypter != nullptr) {
    auto* rp_crypter = static_cast<alts_record_protocol_crypter*>(
        gpr_malloc(sizeof(alts_record_protocol_crypter)));
    size_t counter_size = 0;
    grpc_status_code status =
        gsec_aead_crypter_nonce_length(crypter, &counter_size, error_details);
    if (status != GRPC_STATUS_OK) {
      return nullptr;
    }
    // Create a counter.
    status = alts_counter_create(is_client, counter_size, overflow_size,
                                 &rp_crypter->ctr, error_details);
    if (status != GRPC_STATUS_OK) {
      return nullptr;
    }
    rp_crypter->crypter = crypter;
    return rp_crypter;
  }
  const char error_msg[] = "crypter is nullptr.";
  maybe_copy_error_msg(error_msg, error_details);
  return nullptr;
}
