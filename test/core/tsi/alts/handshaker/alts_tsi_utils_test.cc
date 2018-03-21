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

#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"
#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"

#define ALTS_TSI_UTILS_TEST_OUT_FRAME "Hello Google"

static void convert_to_tsi_result_test() {
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(GRPC_STATUS_OK) == TSI_OK);
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(GRPC_STATUS_UNKNOWN) ==
             TSI_UNKNOWN_ERROR);
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(
                 GRPC_STATUS_INVALID_ARGUMENT) == TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(GRPC_STATUS_OUT_OF_RANGE) ==
             TSI_UNKNOWN_ERROR);
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(GRPC_STATUS_INTERNAL) ==
             TSI_INTERNAL_ERROR);
  GPR_ASSERT(alts_tsi_utils_convert_to_tsi_result(GRPC_STATUS_NOT_FOUND) ==
             TSI_NOT_FOUND);
}

static void deserialize_response_test() {
  grpc_gcp_handshaker_resp* resp = grpc_gcp_handshaker_resp_create();
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_out_frames(
      resp, ALTS_TSI_UTILS_TEST_OUT_FRAME,
      strlen(ALTS_TSI_UTILS_TEST_OUT_FRAME)));
  grpc_slice slice;
  GPR_ASSERT(grpc_gcp_handshaker_resp_encode(resp, &slice));

  /* Valid serialization. */
  grpc_byte_buffer* buffer =
      grpc_raw_byte_buffer_create(&slice, 1 /* number of slices */);
  grpc_gcp_handshaker_resp* decoded_resp =
      alts_tsi_utils_deserialize_response(buffer);
  GPR_ASSERT(grpc_gcp_handshaker_resp_equals(resp, decoded_resp));
  grpc_byte_buffer_destroy(buffer);

  /* Invalid serializaiton. */
  grpc_slice bad_slice =
      grpc_slice_split_head(&slice, GRPC_SLICE_LENGTH(slice) - 1);
  buffer = grpc_raw_byte_buffer_create(&bad_slice, 1 /* number of slices */);
  GPR_ASSERT(alts_tsi_utils_deserialize_response(buffer) == nullptr);

  /* Clean up. */
  grpc_slice_unref(slice);
  grpc_slice_unref(bad_slice);
  grpc_byte_buffer_destroy(buffer);
  grpc_gcp_handshaker_resp_destroy(resp);
  grpc_gcp_handshaker_resp_destroy(decoded_resp);
}

int main(int argc, char** argv) {
  /* Tests. */
  deserialize_response_test();
  convert_to_tsi_result_test();
  return 0;
}
