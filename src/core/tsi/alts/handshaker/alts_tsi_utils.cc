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

#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"

#include <grpc/byte_buffer_reader.h>

#include "src/core/lib/slice/slice_internal.h"

tsi_result alts_tsi_utils_convert_to_tsi_result(grpc_status_code code) {
  switch (code) {
    case GRPC_STATUS_OK:
      return TSI_OK;
    case GRPC_STATUS_UNKNOWN:
      return TSI_UNKNOWN_ERROR;
    case GRPC_STATUS_INVALID_ARGUMENT:
      return TSI_INVALID_ARGUMENT;
    case GRPC_STATUS_NOT_FOUND:
      return TSI_NOT_FOUND;
    case GRPC_STATUS_INTERNAL:
      return TSI_INTERNAL_ERROR;
    default:
      return TSI_UNKNOWN_ERROR;
  }
}

grpc_gcp_HandshakerResp* alts_tsi_utils_deserialize_response(
    grpc_byte_buffer* resp_buffer, upb_Arena* arena) {
  GPR_ASSERT(resp_buffer != nullptr);
  GPR_ASSERT(arena != nullptr);
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, resp_buffer);
  grpc_slice slice = grpc_byte_buffer_reader_readall(&bbr);
  size_t buf_size = GPR_SLICE_LENGTH(slice);
  void* buf = upb_Arena_Malloc(arena, buf_size);
  memcpy(buf, reinterpret_cast<const char*>(GPR_SLICE_START_PTR(slice)),
         buf_size);
  grpc_gcp_HandshakerResp* resp = grpc_gcp_HandshakerResp_parse(
      reinterpret_cast<char*>(buf), buf_size, arena);
  grpc_slice_unref_internal(slice);
  grpc_byte_buffer_reader_destroy(&bbr);
  if (resp == nullptr) {
    gpr_log(GPR_ERROR, "grpc_gcp_handshaker_resp_decode() failed");
    return nullptr;
  }
  return resp;
}
