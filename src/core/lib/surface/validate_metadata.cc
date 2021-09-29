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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/validate_metadata.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#if __cplusplus > 201103l
#define GRPC_VALIDATE_METADATA_CONSTEXPR_FN constexpr
#define GRPC_VALIDATE_METADATA_CONSTEXPR_VALUE constexpr
#else
#define GRPC_VALIDATE_METADATA_CONSTEXPR_FN
#define GRPC_VALIDATE_METADATA_CONSTEXPR_VALUE const
#endif

static grpc_error_handle conforms_to(const grpc_slice& slice,
                                     const grpc_core::BitSet<256>& legal_bits,
                                     const char* err_desc) {
  const uint8_t* p = GRPC_SLICE_START_PTR(slice);
  const uint8_t* e = GRPC_SLICE_END_PTR(slice);
  for (; p != e; p++) {
    if (!legal_bits.is_set(*p)) {
      grpc_error_handle error = grpc_error_set_str(
          grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_desc),
                             GRPC_ERROR_INT_OFFSET,
                             p - GRPC_SLICE_START_PTR(slice)),
          GRPC_ERROR_STR_RAW_BYTES,
          grpc_dump_slice_to_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII));
      return error;
    }
  }
  return GRPC_ERROR_NONE;
}

static int error2int(grpc_error_handle error) {
  int r = (error == GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  return r;
}

namespace {
class LegalHeaderKeyBits : public grpc_core::BitSet<256> {
 public:
  GRPC_VALIDATE_METADATA_CONSTEXPR_FN LegalHeaderKeyBits() {
    for (int i = 'a'; i <= 'z'; i++) set(i);
    for (int i = '0'; i <= '9'; i++) set(i);
    set('-');
    set('_');
    set('.');
  }
};
static GRPC_VALIDATE_METADATA_CONSTEXPR_VALUE LegalHeaderKeyBits
    g_legal_header_key_bits;
}  // namespace

grpc_error_handle grpc_validate_header_key_is_legal(const grpc_slice& slice) {
  if (GRPC_SLICE_LENGTH(slice) == 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Metadata keys cannot be zero length");
  }
  if (GRPC_SLICE_LENGTH(slice) > UINT32_MAX) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Metadata keys cannot be larger than UINT32_MAX");
  }
  if (GRPC_SLICE_START_PTR(slice)[0] == ':') {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Metadata keys cannot start with :");
  }
  return conforms_to(slice, g_legal_header_key_bits, "Illegal header key");
}

int grpc_header_key_is_legal(grpc_slice slice) {
  return error2int(grpc_validate_header_key_is_legal(slice));
}

namespace {
class LegalHeaderNonBinValueBits : public grpc_core::BitSet<256> {
 public:
  GRPC_VALIDATE_METADATA_CONSTEXPR_FN LegalHeaderNonBinValueBits() {
    for (int i = 32; i <= 126; i++) {
      set(i);
    }
  }
};
static GRPC_VALIDATE_METADATA_CONSTEXPR_VALUE LegalHeaderNonBinValueBits
    g_legal_header_non_bin_value_bits;
}  // namespace

grpc_error_handle grpc_validate_header_nonbin_value_is_legal(
    const grpc_slice& slice) {
  return conforms_to(slice, g_legal_header_non_bin_value_bits,
                     "Illegal header value");
}

int grpc_header_nonbin_value_is_legal(grpc_slice slice) {
  return error2int(grpc_validate_header_nonbin_value_is_legal(slice));
}

int grpc_is_binary_header_internal(const grpc_slice& slice) {
  return grpc_key_is_binary_header(GRPC_SLICE_START_PTR(slice),
                                   GRPC_SLICE_LENGTH(slice));
}

int grpc_is_binary_header(grpc_slice slice) {
  return grpc_is_binary_header_internal(slice);
}
