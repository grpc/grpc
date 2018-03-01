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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_TRANSPORT_SECURITY_COMMON_API_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_TRANSPORT_SECURITY_COMMON_API_H

#include <grpc/support/port_platform.h>

#include "third_party/nanopb/pb_decode.h"
#include "third_party/nanopb/pb_encode.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/tsi/alts/handshaker/transport_security_common.pb.h"

typedef grpc_gcp_RpcProtocolVersions grpc_gcp_rpc_protocol_versions;

typedef grpc_gcp_RpcProtocolVersions_Version
    grpc_gcp_rpc_protocol_versions_version;

/**
 * This method sets the value for max_rpc_versions field of rpc protocol
 * versions.
 *
 * - versions: an rpc protocol version instance.
 * - max_major: a major version of maximum supported RPC version.
 * - max_minor: a minor version of maximum supported RPC version.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_set_max(
    grpc_gcp_rpc_protocol_versions* versions, uint32_t max_major,
    uint32_t max_minor);

/**
 * This method sets the value for min_rpc_versions field of rpc protocol
 * versions.
 *
 * - versions: an rpc protocol version instance.
 * - min_major: a major version of minimum supported RPC version.
 * - min_minor: a minor version of minimum supported RPC version.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_set_min(
    grpc_gcp_rpc_protocol_versions* versions, uint32_t min_major,
    uint32_t min_minor);

/**
 * This method computes serialized byte length of rpc protocol versions.
 *
 * - versions: an rpc protocol versions instance.
 *
 * The method returns serialized byte length. It returns 0 on failure.
 */
size_t grpc_gcp_rpc_protocol_versions_encode_length(
    const grpc_gcp_rpc_protocol_versions* versions);

/**
 * This method serializes rpc protocol versions and writes the result to
 * the memory buffer provided by the caller. Caller is responsible for
 * allocating sufficient memory to store the serialized data.
 *
 * - versions: an rpc protocol versions instance.
 * - bytes: bytes buffer where the result will be written to.
 * - bytes_length: length of the bytes buffer.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_encode_to_raw_bytes(
    const grpc_gcp_rpc_protocol_versions* versions, uint8_t* bytes,
    size_t bytes_length);

/**
 * This method serializes an rpc protocol version and returns serialized rpc
 * versions in grpc slice.
 *
 * - versions: an rpc protocol versions instance.
 * - slice: grpc slice where the serialized result will be written.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_encode(
    const grpc_gcp_rpc_protocol_versions* versions, grpc_slice* slice);

/**
 * This method de-serializes input in grpc slice form and stores the result
 * in rpc protocol versions.
 *
 * - slice: a data stream containing a serialized rpc protocol version.
 * - versions: an rpc protocol version instance used to hold de-serialized
 *   result.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_decode(
    grpc_slice slice, grpc_gcp_rpc_protocol_versions* versions);

/**
 * This method performs a deep copy operation on rpc protocol versions
 * instance.
 *
 * - src: rpc protocol versions instance that needs to be copied.
 * - dst: rpc protocol versions instance that stores the copied result.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_copy(
    const grpc_gcp_rpc_protocol_versions* src,
    grpc_gcp_rpc_protocol_versions* dst);

/**
 * This method performs a version check between local and peer rpc protocol
 * versions.
 *
 * - local_versions: local rpc protocol versions instance.
 * - peer_versions: peer rpc protocol versions instance.
 * - highest_common_version: an output parameter that will store the highest
 *   common rpc protocol version both parties agreed on.
 *
 * The method returns true if the check passes which means both parties agreed
 * on a common rpc protocol to use, and false otherwise.
 */
bool grpc_gcp_rpc_protocol_versions_check(
    const grpc_gcp_rpc_protocol_versions* local_versions,
    const grpc_gcp_rpc_protocol_versions* peer_versions,
    grpc_gcp_rpc_protocol_versions_version* highest_common_version);

namespace grpc_core {
namespace internal {

/**
 * Exposed for testing only.
 * The method returns 0 if v1 = v2,
 *            returns 1 if v1 > v2,
 *            returns -1 if v1 < v2.
 */
int grpc_gcp_rpc_protocol_version_compare(
    const grpc_gcp_rpc_protocol_versions_version* v1,
    const grpc_gcp_rpc_protocol_versions_version* v2);

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_TRANSPORT_SECURITY_COMMON_API_H */
