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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

const size_t kMaxRpcVersionMajor = 3;
const size_t kMaxRpcVersionMinor = 2;
const size_t kMinRpcVersionMajor = 2;
const size_t kMinRpcVersionMinor = 1;

static bool grpc_gcp_rpc_protocol_versions_equal(
    grpc_gcp_rpc_protocol_versions* l_versions,
    grpc_gcp_rpc_protocol_versions* r_versions) {
  GPR_ASSERT(l_versions != nullptr && r_versions != nullptr);
  if ((l_versions->has_max_rpc_version ^ r_versions->has_max_rpc_version) |
      (l_versions->has_min_rpc_version ^ r_versions->has_min_rpc_version)) {
    return false;
  }
  if (l_versions->has_max_rpc_version) {
    if ((l_versions->max_rpc_version.major !=
         r_versions->max_rpc_version.major) ||
        (l_versions->max_rpc_version.minor !=
         r_versions->max_rpc_version.minor)) {
      return false;
    }
  }
  if (l_versions->has_min_rpc_version) {
    if ((l_versions->min_rpc_version.major !=
         r_versions->min_rpc_version.major) ||
        (l_versions->min_rpc_version.minor !=
         r_versions->min_rpc_version.minor)) {
      return false;
    }
  }
  return true;
}

static void test_success() {
  grpc_gcp_rpc_protocol_versions version;
  grpc_gcp_rpc_protocol_versions decoded_version;
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(
      &version, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(
      &version, kMinRpcVersionMajor, kMinRpcVersionMinor));
  /* Serializes to raw bytes. */
  size_t encoded_length =
      grpc_gcp_rpc_protocol_versions_encode_length(&version);
  uint8_t* encoded_bytes = static_cast<uint8_t*>(gpr_malloc(encoded_length));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
      &version, encoded_bytes, encoded_length));
  grpc_slice encoded_slice;
  /* Serializes to grpc slice. */
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_encode(&version, &encoded_slice));
  /* Checks serialized raw bytes and serialized grpc slice have same content. */
  GPR_ASSERT(encoded_length == GRPC_SLICE_LENGTH(encoded_slice));
  GPR_ASSERT(memcmp(encoded_bytes, GRPC_SLICE_START_PTR(encoded_slice),
                    encoded_length) == 0);
  /* Deserializes and compares with the original version. */
  GPR_ASSERT(
      grpc_gcp_rpc_protocol_versions_decode(encoded_slice, &decoded_version));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_equal(&version, &decoded_version));
  grpc_slice_unref(encoded_slice);
  gpr_free(encoded_bytes);
}

static void test_failure() {
  grpc_gcp_rpc_protocol_versions version, decoded_version;
  grpc_slice encoded_slice;
  /* Test for invalid arguments. */
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_set_max(
      nullptr, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_set_min(
      nullptr, kMinRpcVersionMajor, kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_encode_length(nullptr) == 0);
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(
      &version, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(
      &version, kMinRpcVersionMajor, kMinRpcVersionMinor));
  size_t encoded_length =
      grpc_gcp_rpc_protocol_versions_encode_length(&version);
  uint8_t* encoded_bytes = static_cast<uint8_t*>(gpr_malloc(encoded_length));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
      nullptr, encoded_bytes, encoded_length));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
      &version, nullptr, encoded_length));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
      &version, encoded_bytes, 0));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_encode(nullptr, &encoded_slice));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_encode(&version, nullptr));
  GPR_ASSERT(!grpc_gcp_rpc_protocol_versions_decode(encoded_slice, nullptr));
  /* Test for nanopb decode. */
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_encode(&version, &encoded_slice));
  grpc_slice bad_slice = grpc_slice_split_head(
      &encoded_slice, GRPC_SLICE_LENGTH(encoded_slice) - 1);
  grpc_slice_unref(encoded_slice);
  GPR_ASSERT(
      !grpc_gcp_rpc_protocol_versions_decode(bad_slice, &decoded_version));
  grpc_slice_unref(bad_slice);
  gpr_free(encoded_bytes);
}

static void test_copy() {
  grpc_gcp_rpc_protocol_versions src;
  grpc_gcp_rpc_protocol_versions des;
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&src, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&src, kMinRpcVersionMajor,
                                                    kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_copy(&src, &des));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_equal(&src, &des));
}

static void test_check_success() {
  grpc_gcp_rpc_protocol_versions v1;
  grpc_gcp_rpc_protocol_versions v2;
  grpc_gcp_rpc_protocol_versions_version highest_common_version;
  /* test equality. */
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_check(
                 (const grpc_gcp_rpc_protocol_versions*)&v1,
                 (const grpc_gcp_rpc_protocol_versions*)&v2,
                 &highest_common_version) == 1);
  GPR_ASSERT(grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
                 &highest_common_version, &v1.max_rpc_version) == 0);

  /* test inequality. */
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMinRpcVersionMinor,
                                                    kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                    kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMinRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_check(
                 (const grpc_gcp_rpc_protocol_versions*)&v1,
                 (const grpc_gcp_rpc_protocol_versions*)&v2,
                 &highest_common_version) == 1);
  GPR_ASSERT(grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
                 &highest_common_version, &v2.max_rpc_version) == 0);
}

static void test_check_failure() {
  grpc_gcp_rpc_protocol_versions v1;
  grpc_gcp_rpc_protocol_versions v2;
  grpc_gcp_rpc_protocol_versions_version highest_common_version;

  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v1, kMinRpcVersionMajor,
                                                    kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v1, kMinRpcVersionMajor,
                                                    kMinRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(&v2, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(&v2, kMaxRpcVersionMajor,
                                                    kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_check(
                 (const grpc_gcp_rpc_protocol_versions*)&v1,
                 (const grpc_gcp_rpc_protocol_versions*)&v2,
                 &highest_common_version) == 0);
}

int main(int argc, char** argv) {
  /* Run tests. */
  test_success();
  test_failure();
  test_copy();
  test_check_success();
  test_check_failure();
  return 0;
}
