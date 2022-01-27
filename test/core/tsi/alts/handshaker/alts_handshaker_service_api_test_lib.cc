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

#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"

bool grpc_gcp_handshaker_resp_set_peer_rpc_versions(
    grpc_gcp_HandshakerResp* resp, upb_arena* arena, uint32_t max_major,
    uint32_t max_minor, uint32_t min_major, uint32_t min_minor) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to "
            "grpc_gcp_handshaker_resp_set_peer_rpc_versions().");
    return false;
  }
  grpc_gcp_rpc_protocol_versions versions;
  versions.max_rpc_version.major = max_major;
  versions.max_rpc_version.minor = max_minor;
  versions.min_rpc_version.major = min_major;
  versions.min_rpc_version.minor = min_minor;
  grpc_gcp_HandshakerResult* result =
      grpc_gcp_HandshakerResp_mutable_result(resp, arena);
  grpc_gcp_RpcProtocolVersions* upb_versions =
      grpc_gcp_HandshakerResult_mutable_peer_rpc_versions(result, arena);
  grpc_gcp_RpcProtocolVersions_assign_from_struct(upb_versions, arena,
                                                  &versions);
  return true;
}

grpc_gcp_HandshakerReq* grpc_gcp_handshaker_req_decode(grpc_slice slice,
                                                       upb_arena* arena) {
  size_t buf_size = GPR_SLICE_LENGTH(slice);
  void* buf = upb_arena_malloc(arena, buf_size);
  memcpy(buf, reinterpret_cast<const char*>(GPR_SLICE_START_PTR(slice)),
         buf_size);
  grpc_gcp_HandshakerReq* resp = grpc_gcp_HandshakerReq_parse(
      reinterpret_cast<char*>(buf), buf_size, arena);
  if (!resp) {
    gpr_log(GPR_ERROR, "grpc_gcp_HandshakerReq decode error");
    return nullptr;
  }
  return resp;
}

/* Check equality of a pair of grpc_gcp_identity fields. */
static bool handshaker_identity_equals(const grpc_gcp_Identity* l_id,
                                       const grpc_gcp_Identity* r_id) {
  if ((grpc_gcp_Identity_has_service_account(l_id) !=
       grpc_gcp_Identity_has_service_account(r_id)) ||
      (grpc_gcp_Identity_has_hostname(l_id) !=
       grpc_gcp_Identity_has_hostname(r_id))) {
    return false;
  }

  if (grpc_gcp_Identity_has_service_account(l_id)) {
    if (!upb_strview_eql(grpc_gcp_Identity_service_account(l_id),
                         grpc_gcp_Identity_service_account(r_id))) {
      return false;
    }
  } else if (grpc_gcp_Identity_has_hostname(l_id)) {
    if (!upb_strview_eql(grpc_gcp_Identity_hostname(l_id),
                         grpc_gcp_Identity_hostname(r_id))) {
      return false;
    }
  }
  return true;
}

static bool handshaker_rpc_versions_equals(
    const grpc_gcp_RpcProtocolVersions* l_version,
    const grpc_gcp_RpcProtocolVersions* r_version) {
  const grpc_gcp_RpcProtocolVersions_Version* l_maxver =
      grpc_gcp_RpcProtocolVersions_max_rpc_version(l_version);
  const grpc_gcp_RpcProtocolVersions_Version* r_maxver =
      grpc_gcp_RpcProtocolVersions_max_rpc_version(r_version);
  const grpc_gcp_RpcProtocolVersions_Version* l_minver =
      grpc_gcp_RpcProtocolVersions_min_rpc_version(l_version);
  const grpc_gcp_RpcProtocolVersions_Version* r_minver =
      grpc_gcp_RpcProtocolVersions_min_rpc_version(r_version);
  return (grpc_gcp_RpcProtocolVersions_Version_major(l_maxver) ==
          grpc_gcp_RpcProtocolVersions_Version_major(r_maxver)) &&
         (grpc_gcp_RpcProtocolVersions_Version_minor(l_maxver) ==
          grpc_gcp_RpcProtocolVersions_Version_minor(r_maxver)) &&
         (grpc_gcp_RpcProtocolVersions_Version_major(l_minver) ==
          grpc_gcp_RpcProtocolVersions_Version_major(r_minver)) &&
         (grpc_gcp_RpcProtocolVersions_Version_minor(l_minver) ==
          grpc_gcp_RpcProtocolVersions_Version_minor(r_minver));
}

/* Check equality of a pair of ALTS handshake responses. */
bool grpc_gcp_handshaker_resp_equals(const grpc_gcp_HandshakerResp* l_resp,
                                     const grpc_gcp_HandshakerResp* r_resp) {
  return upb_strview_eql(grpc_gcp_HandshakerResp_out_frames(l_resp),
                         grpc_gcp_HandshakerResp_out_frames(r_resp)) &&
         (grpc_gcp_HandshakerResp_bytes_consumed(l_resp) ==
          grpc_gcp_HandshakerResp_bytes_consumed(l_resp)) &&
         grpc_gcp_handshaker_resp_result_equals(
             grpc_gcp_HandshakerResp_result(l_resp),
             grpc_gcp_HandshakerResp_result(r_resp)) &&
         grpc_gcp_handshaker_resp_status_equals(
             grpc_gcp_HandshakerResp_status(l_resp),
             grpc_gcp_HandshakerResp_status(r_resp));
}

/* This method checks equality of two handshaker response results. */
bool grpc_gcp_handshaker_resp_result_equals(
    const grpc_gcp_HandshakerResult* l_result,
    const grpc_gcp_HandshakerResult* r_result) {
  if (l_result == nullptr && r_result == nullptr) {
    return true;
  } else if ((l_result != nullptr && r_result == nullptr) ||
             (l_result == nullptr && r_result != nullptr)) {
    return false;
  }
  return upb_strview_eql(
             grpc_gcp_HandshakerResult_application_protocol(l_result),
             grpc_gcp_HandshakerResult_application_protocol(r_result)) &&
         upb_strview_eql(grpc_gcp_HandshakerResult_record_protocol(l_result),
                         grpc_gcp_HandshakerResult_record_protocol(r_result)) &&
         upb_strview_eql(grpc_gcp_HandshakerResult_key_data(l_result),
                         grpc_gcp_HandshakerResult_key_data(r_result)) &&
         handshaker_identity_equals(
             grpc_gcp_HandshakerResult_peer_identity(l_result),
             grpc_gcp_HandshakerResult_peer_identity(r_result)) &&
         handshaker_identity_equals(
             grpc_gcp_HandshakerResult_local_identity(l_result),
             grpc_gcp_HandshakerResult_local_identity(r_result)) &&
         (grpc_gcp_HandshakerResult_keep_channel_open(l_result) ==
          grpc_gcp_HandshakerResult_keep_channel_open(r_result)) &&
         handshaker_rpc_versions_equals(
             grpc_gcp_HandshakerResult_peer_rpc_versions(l_result),
             grpc_gcp_HandshakerResult_peer_rpc_versions(r_result));
}

/* This method checks equality of two handshaker response statuses. */
bool grpc_gcp_handshaker_resp_status_equals(
    const grpc_gcp_HandshakerStatus* l_status,
    const grpc_gcp_HandshakerStatus* r_status) {
  if (l_status == nullptr && r_status == nullptr) {
    return true;
  } else if ((l_status != nullptr && r_status == nullptr) ||
             (l_status == nullptr && r_status != nullptr)) {
    return false;
  }
  return (grpc_gcp_HandshakerStatus_code(l_status) ==
          grpc_gcp_HandshakerStatus_code(r_status)) &&
         upb_strview_eql(grpc_gcp_HandshakerStatus_details(l_status),
                         grpc_gcp_HandshakerStatus_details(r_status));
}
