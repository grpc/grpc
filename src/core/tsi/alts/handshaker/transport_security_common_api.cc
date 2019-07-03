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

#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

bool grpc_gcp_rpc_protocol_versions_set_max(
    grpc_gcp_rpc_protocol_versions* versions, uint32_t max_major,
    uint32_t max_minor) {
  if (versions == nullptr) {
    gpr_log(GPR_ERROR,
            "versions is nullptr in "
            "grpc_gcp_rpc_protocol_versions_set_max().");
    return false;
  }
  versions->has_max_rpc_version = true;
  versions->max_rpc_version.has_major = true;
  versions->max_rpc_version.has_minor = true;
  versions->max_rpc_version.major = max_major;
  versions->max_rpc_version.minor = max_minor;
  return true;
}

bool grpc_gcp_rpc_protocol_versions_set_min(
    grpc_gcp_rpc_protocol_versions* versions, uint32_t min_major,
    uint32_t min_minor) {
  if (versions == nullptr) {
    gpr_log(GPR_ERROR,
            "versions is nullptr in "
            "grpc_gcp_rpc_protocol_versions_set_min().");
    return false;
  }
  versions->has_min_rpc_version = true;
  versions->min_rpc_version.has_major = true;
  versions->min_rpc_version.has_minor = true;
  versions->min_rpc_version.major = min_major;
  versions->min_rpc_version.minor = min_minor;
  return true;
}

size_t grpc_gcp_rpc_protocol_versions_encode_length(
    const grpc_gcp_rpc_protocol_versions* versions) {
  if (versions == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_rpc_protocol_versions_encode_length().");
    return 0;
  }
  pb_ostream_t size_stream;
  memset(&size_stream, 0, sizeof(pb_ostream_t));
  if (!pb_encode(&size_stream, grpc_gcp_RpcProtocolVersions_fields, versions)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&size_stream));
    return 0;
  }
  return size_stream.bytes_written;
}

bool grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
    const grpc_gcp_rpc_protocol_versions* versions, uint8_t* bytes,
    size_t bytes_length) {
  if (versions == nullptr || bytes == nullptr || bytes_length == 0) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes().");
    return false;
  }
  pb_ostream_t output_stream = pb_ostream_from_buffer(bytes, bytes_length);
  if (!pb_encode(&output_stream, grpc_gcp_RpcProtocolVersions_fields,
                 versions)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&output_stream));
    return false;
  }
  return true;
}

bool grpc_gcp_rpc_protocol_versions_encode(
    const grpc_gcp_rpc_protocol_versions* versions, grpc_slice* slice) {
  if (versions == nullptr || slice == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_rpc_protocol_versions_encode().");
    return false;
  }
  size_t encoded_length =
      grpc_gcp_rpc_protocol_versions_encode_length(versions);
  if (encoded_length == 0) return false;
  *slice = grpc_slice_malloc(encoded_length);
  return grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
      versions, GRPC_SLICE_START_PTR(*slice), encoded_length);
}

bool grpc_gcp_rpc_protocol_versions_decode(
    const grpc_slice& slice, grpc_gcp_rpc_protocol_versions* versions) {
  if (versions == nullptr) {
    gpr_log(GPR_ERROR,
            "version is nullptr in "
            "grpc_gcp_rpc_protocol_versions_decode().");
    return false;
  }
  pb_istream_t stream =
      pb_istream_from_buffer(const_cast<uint8_t*>(GRPC_SLICE_START_PTR(slice)),
                             GRPC_SLICE_LENGTH(slice));
  if (!pb_decode(&stream, grpc_gcp_RpcProtocolVersions_fields, versions)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}

bool grpc_gcp_rpc_protocol_versions_copy(
    const grpc_gcp_rpc_protocol_versions* src,
    grpc_gcp_rpc_protocol_versions* dst) {
  if ((src == nullptr && dst != nullptr) ||
      (src != nullptr && dst == nullptr)) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_rpc_protocol_versions_copy().");
    return false;
  }
  if (src == nullptr) {
    return true;
  }
  grpc_gcp_rpc_protocol_versions_set_max(dst, src->max_rpc_version.major,
                                         src->max_rpc_version.minor);
  grpc_gcp_rpc_protocol_versions_set_min(dst, src->min_rpc_version.major,
                                         src->min_rpc_version.minor);
  return true;
}

namespace grpc_core {
namespace internal {

int grpc_gcp_rpc_protocol_version_compare(
    const grpc_gcp_rpc_protocol_versions_version* v1,
    const grpc_gcp_rpc_protocol_versions_version* v2) {
  if ((v1->major > v2->major) ||
      (v1->major == v2->major && v1->minor > v2->minor)) {
    return 1;
  }
  if ((v1->major < v2->major) ||
      (v1->major == v2->major && v1->minor < v2->minor)) {
    return -1;
  }
  return 0;
}

}  // namespace internal
}  // namespace grpc_core

bool grpc_gcp_rpc_protocol_versions_check(
    const grpc_gcp_rpc_protocol_versions* local_versions,
    const grpc_gcp_rpc_protocol_versions* peer_versions,
    grpc_gcp_rpc_protocol_versions_version* highest_common_version) {
  if (local_versions == nullptr || peer_versions == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_rpc_protocol_versions_check().");
    return false;
  }
  /* max_common_version is MIN(local.max, peer.max) */
  const grpc_gcp_rpc_protocol_versions_version* max_common_version =
      grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
          &local_versions->max_rpc_version, &peer_versions->max_rpc_version) > 0
          ? &peer_versions->max_rpc_version
          : &local_versions->max_rpc_version;
  /* min_common_version is MAX(local.min, peer.min) */
  const grpc_gcp_rpc_protocol_versions_version* min_common_version =
      grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
          &local_versions->min_rpc_version, &peer_versions->min_rpc_version) > 0
          ? &local_versions->min_rpc_version
          : &peer_versions->min_rpc_version;
  bool result = grpc_core::internal::grpc_gcp_rpc_protocol_version_compare(
                    max_common_version, min_common_version) >= 0
                    ? true
                    : false;
  if (result && highest_common_version != nullptr) {
    memcpy(highest_common_version, max_common_version,
           sizeof(grpc_gcp_rpc_protocol_versions_version));
  }
  return result;
}
