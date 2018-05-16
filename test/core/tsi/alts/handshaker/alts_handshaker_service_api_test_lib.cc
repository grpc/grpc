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

const size_t kHandshakeProtocolNum = 3;

grpc_gcp_handshaker_req* grpc_gcp_handshaker_decoded_req_create(
    grpc_gcp_handshaker_req_type type) {
  grpc_gcp_handshaker_req* req =
      static_cast<grpc_gcp_handshaker_req*>(gpr_zalloc(sizeof(*req)));
  switch (type) {
    case CLIENT_START_REQ:
      req->has_client_start = true;
      req->client_start.target_identities.funcs.decode =
          decode_repeated_identity_cb;
      req->client_start.application_protocols.funcs.decode =
          decode_repeated_string_cb;
      req->client_start.record_protocols.funcs.decode =
          decode_repeated_string_cb;
      req->client_start.local_identity.hostname.funcs.decode =
          decode_string_or_bytes_cb;
      req->client_start.local_identity.service_account.funcs.decode =
          decode_string_or_bytes_cb;
      req->client_start.local_endpoint.ip_address.funcs.decode =
          decode_string_or_bytes_cb;
      req->client_start.remote_endpoint.ip_address.funcs.decode =
          decode_string_or_bytes_cb;
      req->client_start.target_name.funcs.decode = decode_string_or_bytes_cb;
      break;
    case SERVER_START_REQ:
      req->has_server_start = true;
      req->server_start.application_protocols.funcs.decode =
          &decode_repeated_string_cb;
      for (size_t i = 0; i < kHandshakeProtocolNum; i++) {
        req->server_start.handshake_parameters[i]
            .value.local_identities.funcs.decode = &decode_repeated_identity_cb;
        req->server_start.handshake_parameters[i]
            .value.record_protocols.funcs.decode = &decode_repeated_string_cb;
      }
      req->server_start.in_bytes.funcs.decode = decode_string_or_bytes_cb;
      req->server_start.local_endpoint.ip_address.funcs.decode =
          decode_string_or_bytes_cb;
      req->server_start.remote_endpoint.ip_address.funcs.decode =
          decode_string_or_bytes_cb;
      break;
    case NEXT_REQ:
      req->has_next = true;
      break;
  }
  return req;
}

bool grpc_gcp_handshaker_resp_set_application_protocol(
    grpc_gcp_handshaker_resp* resp, const char* application_protocol) {
  if (resp == nullptr || application_protocol == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "handshaker_resp_set_application_protocol().");
    return false;
  }
  resp->has_result = true;
  grpc_slice* slice =
      create_slice(application_protocol, strlen(application_protocol));
  resp->result.application_protocol.arg = slice;
  resp->result.application_protocol.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

bool grpc_gcp_handshaker_resp_set_record_protocol(
    grpc_gcp_handshaker_resp* resp, const char* record_protocol) {
  if (resp == nullptr || record_protocol == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "handshaker_resp_set_record_protocol().");
    return false;
  }
  resp->has_result = true;
  grpc_slice* slice = create_slice(record_protocol, strlen(record_protocol));
  resp->result.record_protocol.arg = slice;
  resp->result.record_protocol.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

bool grpc_gcp_handshaker_resp_set_key_data(grpc_gcp_handshaker_resp* resp,
                                           const char* key_data, size_t size) {
  if (resp == nullptr || key_data == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to handshaker_resp_set_key_data().");
    return false;
  }
  resp->has_result = true;
  grpc_slice* slice = create_slice(key_data, size);
  resp->result.key_data.arg = slice;
  resp->result.key_data.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

static void set_identity_hostname(grpc_gcp_identity* identity,
                                  const char* hostname) {
  grpc_slice* slice = create_slice(hostname, strlen(hostname));
  identity->hostname.arg = slice;
  identity->hostname.funcs.encode = encode_string_or_bytes_cb;
}

static void set_identity_service_account(grpc_gcp_identity* identity,
                                         const char* service_account) {
  grpc_slice* slice = create_slice(service_account, strlen(service_account));
  identity->service_account.arg = slice;
  identity->service_account.funcs.encode = encode_string_or_bytes_cb;
}

bool grpc_gcp_handshaker_resp_set_local_identity_hostname(
    grpc_gcp_handshaker_resp* resp, const char* hostname) {
  if (resp == nullptr || hostname == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_resp_set_local_identity_hostname().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_local_identity = true;
  set_identity_hostname(&resp->result.local_identity, hostname);
  return true;
}

bool grpc_gcp_handshaker_resp_set_local_identity_service_account(
    grpc_gcp_handshaker_resp* resp, const char* service_account) {
  if (resp == nullptr || service_account == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_resp_set_local_identity_service_account().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_local_identity = true;
  set_identity_service_account(&resp->result.local_identity, service_account);
  return true;
}

bool grpc_gcp_handshaker_resp_set_peer_identity_hostname(
    grpc_gcp_handshaker_resp* resp, const char* hostname) {
  if (resp == nullptr || hostname == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_resp_set_peer_identity_hostname().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_peer_identity = true;
  set_identity_hostname(&resp->result.peer_identity, hostname);
  return true;
}

bool grpc_gcp_handshaker_resp_set_peer_identity_service_account(
    grpc_gcp_handshaker_resp* resp, const char* service_account) {
  if (resp == nullptr || service_account == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_resp_set_peer_identity_service_account().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_peer_identity = true;
  set_identity_service_account(&resp->result.peer_identity, service_account);
  return true;
}

bool grpc_gcp_handshaker_resp_set_channel_open(grpc_gcp_handshaker_resp* resp,
                                               bool keep_channel_open) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to "
            "grpc_gcp_handshaker_resp_set_channel_open().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_keep_channel_open = true;
  resp->result.keep_channel_open = keep_channel_open;
  return true;
}

bool grpc_gcp_handshaker_resp_set_code(grpc_gcp_handshaker_resp* resp,
                                       uint32_t code) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to grpc_gcp_handshaker_resp_set_code().");
    return false;
  }
  resp->has_status = true;
  resp->status.has_code = true;
  resp->status.code = code;
  return true;
}

bool grpc_gcp_handshaker_resp_set_details(grpc_gcp_handshaker_resp* resp,
                                          const char* details) {
  if (resp == nullptr || details == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to grpc_gcp_handshaker_resp_set_details().");
    return false;
  }
  resp->has_status = true;
  grpc_slice* slice = create_slice(details, strlen(details));
  resp->status.details.arg = slice;
  resp->status.details.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

bool grpc_gcp_handshaker_resp_set_out_frames(grpc_gcp_handshaker_resp* resp,
                                             const char* out_frames,
                                             size_t size) {
  if (resp == nullptr || out_frames == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_resp_set_out_frames().");
    return false;
  }
  grpc_slice* slice = create_slice(out_frames, size);
  resp->out_frames.arg = slice;
  resp->out_frames.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

bool grpc_gcp_handshaker_resp_set_bytes_consumed(grpc_gcp_handshaker_resp* resp,
                                                 int32_t bytes_consumed) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to "
            "grpc_gcp_handshaker_resp_set_bytes_consumed().");
    return false;
  }
  resp->has_bytes_consumed = true;
  resp->bytes_consumed = bytes_consumed;
  return true;
}

bool grpc_gcp_handshaker_resp_set_peer_rpc_versions(
    grpc_gcp_handshaker_resp* resp, uint32_t max_major, uint32_t max_minor,
    uint32_t min_major, uint32_t min_minor) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to "
            "grpc_gcp_handshaker_resp_set_peer_rpc_versions().");
    return false;
  }
  resp->has_result = true;
  resp->result.has_peer_rpc_versions = true;
  grpc_gcp_rpc_protocol_versions* versions = &resp->result.peer_rpc_versions;
  versions->has_max_rpc_version = true;
  versions->has_min_rpc_version = true;
  versions->max_rpc_version.has_major = true;
  versions->max_rpc_version.has_minor = true;
  versions->min_rpc_version.has_major = true;
  versions->min_rpc_version.has_minor = true;
  versions->max_rpc_version.major = max_major;
  versions->max_rpc_version.minor = max_minor;
  versions->min_rpc_version.major = min_major;
  versions->min_rpc_version.minor = min_minor;
  return true;
}

bool grpc_gcp_handshaker_resp_encode(grpc_gcp_handshaker_resp* resp,
                                     grpc_slice* slice) {
  if (resp == nullptr || slice == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to grpc_gcp_handshaker_resp_encode().");
    return false;
  }
  pb_ostream_t size_stream;
  memset(&size_stream, 0, sizeof(pb_ostream_t));
  if (!pb_encode(&size_stream, grpc_gcp_HandshakerResp_fields, resp)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&size_stream));
    return false;
  }
  size_t encoded_length = size_stream.bytes_written;
  *slice = grpc_slice_malloc(encoded_length);
  pb_ostream_t output_stream =
      pb_ostream_from_buffer(GRPC_SLICE_START_PTR(*slice), encoded_length);
  if (!pb_encode(&output_stream, grpc_gcp_HandshakerResp_fields, resp)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&size_stream));
    return false;
  }
  return true;
}

bool grpc_gcp_handshaker_req_decode(grpc_slice slice,
                                    grpc_gcp_handshaker_req* req) {
  if (req == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to grpc_gcp_handshaker_req_decode().");
    return false;
  }
  pb_istream_t stream = pb_istream_from_buffer(GRPC_SLICE_START_PTR(slice),
                                               GRPC_SLICE_LENGTH(slice));
  req->next.in_bytes.funcs.decode = decode_string_or_bytes_cb;
  if (!pb_decode(&stream, grpc_gcp_HandshakerReq_fields, req)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}

/* Check equality of a pair of grpc_slice fields. */
static bool slice_equals(grpc_slice* l_slice, grpc_slice* r_slice) {
  if (l_slice == nullptr && r_slice == nullptr) {
    return true;
  }
  if (l_slice != nullptr && r_slice != nullptr) {
    return grpc_slice_eq(*l_slice, *r_slice);
  }
  return false;
}

/* Check equality of a pair of grpc_gcp_identity fields. */
static bool handshaker_identity_equals(const grpc_gcp_identity* l_id,
                                       const grpc_gcp_identity* r_id) {
  if (!((l_id->hostname.arg != nullptr) != (r_id->hostname.arg != nullptr))) {
    if (l_id->hostname.arg != nullptr) {
      return slice_equals(static_cast<grpc_slice*>(l_id->hostname.arg),
                          static_cast<grpc_slice*>(r_id->hostname.arg));
    }
  } else {
    return false;
  }
  if (!((l_id->service_account.arg != nullptr) !=
        (r_id->service_account.arg != nullptr))) {
    if (l_id->service_account.arg != nullptr) {
      return slice_equals(static_cast<grpc_slice*>(l_id->service_account.arg),
                          static_cast<grpc_slice*>(r_id->service_account.arg));
    }
  } else {
    return false;
  }
  return true;
}

static bool handshaker_rpc_versions_equals(
    const grpc_gcp_rpc_protocol_versions* l_version,
    const grpc_gcp_rpc_protocol_versions* r_version) {
  bool result = true;
  result &=
      (l_version->max_rpc_version.major == r_version->max_rpc_version.major);
  result &=
      (l_version->max_rpc_version.minor == r_version->max_rpc_version.minor);
  result &=
      (l_version->min_rpc_version.major == r_version->min_rpc_version.major);
  result &=
      (l_version->min_rpc_version.minor == r_version->min_rpc_version.minor);
  return result;
}

/* Check equality of a pair of grpc_gcp_endpoint fields. */
static bool handshaker_endpoint_equals(const grpc_gcp_endpoint* l_end,
                                       const grpc_gcp_endpoint* r_end) {
  bool result = true;
  result &= (l_end->port == r_end->port);
  result &= (l_end->protocol == r_end->protocol);
  if (!((l_end->ip_address.arg != nullptr) !=
        (r_end->ip_address.arg != nullptr))) {
    if (l_end->ip_address.arg != nullptr) {
      result &= slice_equals(static_cast<grpc_slice*>(l_end->ip_address.arg),
                             static_cast<grpc_slice*>(r_end->ip_address.arg));
    }
  } else {
    return false;
  }
  return result;
}
/**
 * Check if a specific repeated field (i.e., target) is contained in a repeated
 * field list (i.e., head).
 */
static bool repeated_field_list_contains_identity(
    const repeated_field* head, const repeated_field* target) {
  repeated_field* field = const_cast<repeated_field*>(head);
  while (field != nullptr) {
    if (handshaker_identity_equals(
            static_cast<const grpc_gcp_identity*>(field->data),
            static_cast<const grpc_gcp_identity*>(target->data))) {
      return true;
    }
    field = field->next;
  }
  return false;
}

static bool repeated_field_list_contains_string(const repeated_field* head,
                                                const repeated_field* target) {
  repeated_field* field = const_cast<repeated_field*>(head);
  while (field != nullptr) {
    if (slice_equals((grpc_slice*)field->data, (grpc_slice*)target->data)) {
      return true;
    }
    field = field->next;
  }
  return false;
}

/* Return a length of repeated field list. */
static size_t repeated_field_list_get_length(const repeated_field* head) {
  repeated_field* field = const_cast<repeated_field*>(head);
  size_t len = 0;
  while (field != nullptr) {
    len++;
    field = field->next;
  }
  return len;
}

/**
 * Check if a pair of repeated field lists contain the same set of repeated
 * fields.
 */
static bool repeated_field_list_equals_identity(const repeated_field* l_head,
                                                const repeated_field* r_head) {
  if (repeated_field_list_get_length(l_head) !=
      repeated_field_list_get_length(r_head)) {
    return false;
  }
  repeated_field* field = const_cast<repeated_field*>(l_head);
  repeated_field* head = const_cast<repeated_field*>(r_head);
  while (field != nullptr) {
    if (!repeated_field_list_contains_identity(head, field)) {
      return false;
    }
    field = field->next;
  }
  return true;
}

static bool repeated_field_list_equals_string(const repeated_field* l_head,
                                              const repeated_field* r_head) {
  if (repeated_field_list_get_length(l_head) !=
      repeated_field_list_get_length(r_head)) {
    return false;
  }
  repeated_field* field = const_cast<repeated_field*>(l_head);
  repeated_field* head = const_cast<repeated_field*>(r_head);
  while (field != nullptr) {
    if (!repeated_field_list_contains_string(head, field)) {
      return false;
    }
    field = field->next;
  }
  return true;
}

/* Check equality of a pair of ALTS client_start handshake requests. */
bool grpc_gcp_handshaker_client_start_req_equals(
    grpc_gcp_start_client_handshake_req* l_req,
    grpc_gcp_start_client_handshake_req* r_req) {
  bool result = true;
  /* Compare handshake_security_protocol. */
  result &=
      l_req->handshake_security_protocol == r_req->handshake_security_protocol;
  /* Compare application_protocols, record_protocols, and target_identities. */
  result &= repeated_field_list_equals_string(
      static_cast<const repeated_field*>(l_req->application_protocols.arg),
      static_cast<const repeated_field*>(r_req->application_protocols.arg));
  result &= repeated_field_list_equals_string(
      static_cast<const repeated_field*>(l_req->record_protocols.arg),
      static_cast<const repeated_field*>(r_req->record_protocols.arg));
  result &= repeated_field_list_equals_identity(
      static_cast<const repeated_field*>(l_req->target_identities.arg),
      static_cast<const repeated_field*>(r_req->target_identities.arg));
  if ((l_req->has_local_identity ^ r_req->has_local_identity) |
      (l_req->has_local_endpoint ^ r_req->has_local_endpoint) |
      ((l_req->has_remote_endpoint ^ r_req->has_remote_endpoint)) |
      (l_req->has_rpc_versions ^ r_req->has_rpc_versions)) {
    return false;
  }
  /* Compare local_identity, local_endpoint, and remote_endpoint. */
  if (l_req->has_local_identity) {
    result &= handshaker_identity_equals(&l_req->local_identity,
                                         &r_req->local_identity);
  }
  if (l_req->has_local_endpoint) {
    result &= handshaker_endpoint_equals(&l_req->local_endpoint,
                                         &r_req->local_endpoint);
  }
  if (l_req->has_remote_endpoint) {
    result &= handshaker_endpoint_equals(&l_req->remote_endpoint,
                                         &r_req->remote_endpoint);
  }
  if (l_req->has_rpc_versions) {
    result &= handshaker_rpc_versions_equals(&l_req->rpc_versions,
                                             &r_req->rpc_versions);
  }
  return result;
}

/* Check equality of a pair of ALTS server_start handshake requests. */
bool grpc_gcp_handshaker_server_start_req_equals(
    grpc_gcp_start_server_handshake_req* l_req,
    grpc_gcp_start_server_handshake_req* r_req) {
  bool result = true;
  /* Compare application_protocols. */
  result &= repeated_field_list_equals_string(
      static_cast<const repeated_field*>(l_req->application_protocols.arg),
      static_cast<const repeated_field*>(r_req->application_protocols.arg));
  /* Compare handshake_parameters. */
  size_t i = 0, j = 0;
  result &=
      (l_req->handshake_parameters_count == r_req->handshake_parameters_count);
  for (i = 0; i < l_req->handshake_parameters_count; i++) {
    bool found = false;
    for (j = 0; j < r_req->handshake_parameters_count; j++) {
      if (l_req->handshake_parameters[i].key ==
          r_req->handshake_parameters[j].key) {
        found = true;
        result &= repeated_field_list_equals_string(
            static_cast<const repeated_field*>(
                l_req->handshake_parameters[i].value.record_protocols.arg),
            static_cast<const repeated_field*>(
                r_req->handshake_parameters[j].value.record_protocols.arg));
        result &= repeated_field_list_equals_identity(
            static_cast<const repeated_field*>(
                l_req->handshake_parameters[i].value.local_identities.arg),
            static_cast<const repeated_field*>(
                r_req->handshake_parameters[j].value.local_identities.arg));
      }
    }
    if (!found) {
      return false;
    }
  }
  /* Compare in_bytes, local_endpoint, remote_endpoint. */
  result &= slice_equals(static_cast<grpc_slice*>(l_req->in_bytes.arg),
                         static_cast<grpc_slice*>(r_req->in_bytes.arg));
  if ((l_req->has_local_endpoint ^ r_req->has_local_endpoint) |
      (l_req->has_remote_endpoint ^ r_req->has_remote_endpoint) |
      (l_req->has_rpc_versions ^ r_req->has_rpc_versions))
    return false;
  if (l_req->has_local_endpoint) {
    result &= handshaker_endpoint_equals(&l_req->local_endpoint,
                                         &r_req->local_endpoint);
  }
  if (l_req->has_remote_endpoint) {
    result &= handshaker_endpoint_equals(&l_req->remote_endpoint,
                                         &r_req->remote_endpoint);
  }
  if (l_req->has_rpc_versions) {
    result &= handshaker_rpc_versions_equals(&l_req->rpc_versions,
                                             &r_req->rpc_versions);
  }
  return result;
}

/* Check equality of a pair of ALTS handshake requests. */
bool grpc_gcp_handshaker_req_equals(grpc_gcp_handshaker_req* l_req,
                                    grpc_gcp_handshaker_req* r_req) {
  if (l_req->has_next && r_req->has_next) {
    return slice_equals(static_cast<grpc_slice*>(l_req->next.in_bytes.arg),
                        static_cast<grpc_slice*>(r_req->next.in_bytes.arg));
  } else if (l_req->has_client_start && r_req->has_client_start) {
    return grpc_gcp_handshaker_client_start_req_equals(&l_req->client_start,
                                                       &r_req->client_start);
  } else if (l_req->has_server_start && r_req->has_server_start) {
    return grpc_gcp_handshaker_server_start_req_equals(&l_req->server_start,
                                                       &r_req->server_start);
  }
  return false;
}

/* Check equality of a pair of ALTS handshake results. */
bool grpc_gcp_handshaker_resp_result_equals(
    grpc_gcp_handshaker_result* l_result,
    grpc_gcp_handshaker_result* r_result) {
  bool result = true;
  /* Compare application_protocol, record_protocol, and key_data. */
  result &= slice_equals(
      static_cast<grpc_slice*>(l_result->application_protocol.arg),
      static_cast<grpc_slice*>(r_result->application_protocol.arg));
  result &=
      slice_equals(static_cast<grpc_slice*>(l_result->record_protocol.arg),
                   static_cast<grpc_slice*>(r_result->record_protocol.arg));
  result &= slice_equals(static_cast<grpc_slice*>(l_result->key_data.arg),
                         static_cast<grpc_slice*>(r_result->key_data.arg));
  /* Compare local_identity, peer_identity, and keep_channel_open. */
  if ((l_result->has_local_identity ^ r_result->has_local_identity) |
      (l_result->has_peer_identity ^ r_result->has_peer_identity) |
      (l_result->has_peer_rpc_versions ^ r_result->has_peer_rpc_versions)) {
    return false;
  }
  if (l_result->has_local_identity) {
    result &= handshaker_identity_equals(&l_result->local_identity,
                                         &r_result->local_identity);
  }
  if (l_result->has_peer_identity) {
    result &= handshaker_identity_equals(&l_result->peer_identity,
                                         &r_result->peer_identity);
  }
  if (l_result->has_peer_rpc_versions) {
    result &= handshaker_rpc_versions_equals(&l_result->peer_rpc_versions,
                                             &r_result->peer_rpc_versions);
  }
  result &= (l_result->keep_channel_open == r_result->keep_channel_open);
  return result;
}

/* Check equality of a pair of ALTS handshake responses. */
bool grpc_gcp_handshaker_resp_equals(grpc_gcp_handshaker_resp* l_resp,
                                     grpc_gcp_handshaker_resp* r_resp) {
  bool result = true;
  /* Compare out_frames and bytes_consumed. */
  result &= slice_equals(static_cast<grpc_slice*>(l_resp->out_frames.arg),
                         static_cast<grpc_slice*>(r_resp->out_frames.arg));
  result &= (l_resp->bytes_consumed == r_resp->bytes_consumed);
  /* Compare result and status. */
  if ((l_resp->has_result ^ r_resp->has_result) |
      (l_resp->has_status ^ r_resp->has_status)) {
    return false;
  }
  if (l_resp->has_result) {
    result &= grpc_gcp_handshaker_resp_result_equals(&l_resp->result,
                                                     &r_resp->result);
  }
  if (l_resp->has_status) {
    result &= (l_resp->status.code == r_resp->status.code);
    result &=
        slice_equals(static_cast<grpc_slice*>(l_resp->status.details.arg),
                     static_cast<grpc_slice*>(r_resp->status.details.arg));
  }
  return result;
}
