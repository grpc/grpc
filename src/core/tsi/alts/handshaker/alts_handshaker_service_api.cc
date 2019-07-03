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

#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api.h"

#include <stdio.h>
#include <stdlib.h>

#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

/* HandshakerReq */
grpc_gcp_handshaker_req* grpc_gcp_handshaker_req_create(
    grpc_gcp_handshaker_req_type type) {
  grpc_gcp_handshaker_req* req =
      static_cast<grpc_gcp_handshaker_req*>(gpr_zalloc(sizeof(*req)));
  switch (type) {
    case CLIENT_START_REQ:
      req->has_client_start = true;
      break;
    case SERVER_START_REQ:
      req->has_server_start = true;
      break;
    case NEXT_REQ:
      req->has_next = true;
      break;
  }
  return req;
}

void grpc_gcp_handshaker_req_destroy(grpc_gcp_handshaker_req* req) {
  if (req == nullptr) {
    return;
  }
  if (req->has_client_start) {
    /* Destroy client_start request. */
    destroy_repeated_field_list_identity(
        static_cast<repeated_field*>(req->client_start.target_identities.arg));
    destroy_repeated_field_list_string(static_cast<repeated_field*>(
        req->client_start.application_protocols.arg));
    destroy_repeated_field_list_string(
        static_cast<repeated_field*>(req->client_start.record_protocols.arg));
    if (req->client_start.has_local_identity) {
      destroy_slice(static_cast<grpc_slice*>(
          req->client_start.local_identity.hostname.arg));
      destroy_slice(static_cast<grpc_slice*>(
          req->client_start.local_identity.service_account.arg));
    }
    if (req->client_start.has_local_endpoint) {
      destroy_slice(static_cast<grpc_slice*>(
          req->client_start.local_endpoint.ip_address.arg));
    }
    if (req->client_start.has_remote_endpoint) {
      destroy_slice(static_cast<grpc_slice*>(
          req->client_start.remote_endpoint.ip_address.arg));
    }
    destroy_slice(static_cast<grpc_slice*>(req->client_start.target_name.arg));
  } else if (req->has_server_start) {
    /* Destroy server_start request. */
    size_t i = 0;
    for (i = 0; i < req->server_start.handshake_parameters_count; i++) {
      destroy_repeated_field_list_identity(
          static_cast<repeated_field*>(req->server_start.handshake_parameters[i]
                                           .value.local_identities.arg));
      destroy_repeated_field_list_string(
          static_cast<repeated_field*>(req->server_start.handshake_parameters[i]
                                           .value.record_protocols.arg));
    }
    destroy_repeated_field_list_string(static_cast<repeated_field*>(
        req->server_start.application_protocols.arg));
    if (req->server_start.has_local_endpoint) {
      destroy_slice(static_cast<grpc_slice*>(
          req->server_start.local_endpoint.ip_address.arg));
    }
    if (req->server_start.has_remote_endpoint) {
      destroy_slice(static_cast<grpc_slice*>(
          req->server_start.remote_endpoint.ip_address.arg));
    }
    destroy_slice(static_cast<grpc_slice*>(req->server_start.in_bytes.arg));
  } else {
    /* Destroy next request. */
    destroy_slice(static_cast<grpc_slice*>(req->next.in_bytes.arg));
  }
  gpr_free(req);
}

bool grpc_gcp_handshaker_req_set_handshake_protocol(
    grpc_gcp_handshaker_req* req,
    grpc_gcp_handshake_protocol handshake_protocol) {
  if (req == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_handshake_protocol().");
    return false;
  }
  req->client_start.has_handshake_security_protocol = true;
  req->client_start.handshake_security_protocol = handshake_protocol;
  return true;
}

bool grpc_gcp_handshaker_req_set_target_name(grpc_gcp_handshaker_req* req,
                                             const char* target_name) {
  if (req == nullptr || target_name == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_target_name().");
    return false;
  }
  grpc_slice* slice = create_slice(target_name, strlen(target_name));
  req->client_start.target_name.arg = slice;
  req->client_start.target_name.funcs.encode = encode_string_or_bytes_cb;
  return true;
}

bool grpc_gcp_handshaker_req_add_application_protocol(
    grpc_gcp_handshaker_req* req, const char* application_protocol) {
  if (req == nullptr || application_protocol == nullptr || req->has_next) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_add_application_protocol().");
    return false;
  }
  grpc_slice* slice =
      create_slice(application_protocol, strlen(application_protocol));
  if (req->has_client_start) {
    add_repeated_field(reinterpret_cast<repeated_field**>(
                           &req->client_start.application_protocols.arg),
                       slice);
    req->client_start.application_protocols.funcs.encode =
        encode_repeated_string_cb;
  } else {
    add_repeated_field(reinterpret_cast<repeated_field**>(
                           &req->server_start.application_protocols.arg),
                       slice);
    req->server_start.application_protocols.funcs.encode =
        encode_repeated_string_cb;
  }
  return true;
}

bool grpc_gcp_handshaker_req_add_record_protocol(grpc_gcp_handshaker_req* req,
                                                 const char* record_protocol) {
  if (req == nullptr || record_protocol == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_add_record_protocol().");
    return false;
  }
  grpc_slice* slice = create_slice(record_protocol, strlen(record_protocol));
  add_repeated_field(reinterpret_cast<repeated_field**>(
                         &req->client_start.record_protocols.arg),
                     slice);
  req->client_start.record_protocols.funcs.encode = encode_repeated_string_cb;
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

bool grpc_gcp_handshaker_req_add_target_identity_hostname(
    grpc_gcp_handshaker_req* req, const char* hostname) {
  if (req == nullptr || hostname == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_req_add_target_identity_hostname().");
    return false;
  }
  grpc_gcp_identity* target_identity =
      static_cast<grpc_gcp_identity*>(gpr_zalloc(sizeof(*target_identity)));
  set_identity_hostname(target_identity, hostname);
  req->client_start.target_identities.funcs.encode =
      encode_repeated_identity_cb;
  add_repeated_field(reinterpret_cast<repeated_field**>(
                         &req->client_start.target_identities.arg),
                     target_identity);
  return true;
}

bool grpc_gcp_handshaker_req_add_target_identity_service_account(
    grpc_gcp_handshaker_req* req, const char* service_account) {
  if (req == nullptr || service_account == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_req_add_target_identity_service_account().");
    return false;
  }
  grpc_gcp_identity* target_identity =
      static_cast<grpc_gcp_identity*>(gpr_zalloc(sizeof(*target_identity)));
  set_identity_service_account(target_identity, service_account);
  req->client_start.target_identities.funcs.encode =
      encode_repeated_identity_cb;
  add_repeated_field(reinterpret_cast<repeated_field**>(
                         &req->client_start.target_identities.arg),
                     target_identity);
  return true;
}

bool grpc_gcp_handshaker_req_set_local_identity_hostname(
    grpc_gcp_handshaker_req* req, const char* hostname) {
  if (req == nullptr || hostname == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_req_set_local_identity_hostname().");
    return false;
  }
  req->client_start.has_local_identity = true;
  set_identity_hostname(&req->client_start.local_identity, hostname);
  return true;
}

bool grpc_gcp_handshaker_req_set_local_identity_service_account(
    grpc_gcp_handshaker_req* req, const char* service_account) {
  if (req == nullptr || service_account == nullptr || !req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_gcp_handshaker_req_set_local_identity_service_account().");
    return false;
  }
  req->client_start.has_local_identity = true;
  set_identity_service_account(&req->client_start.local_identity,
                               service_account);
  return true;
}

static void set_endpoint(grpc_gcp_endpoint* endpoint, const char* ip_address,
                         size_t port, grpc_gcp_network_protocol protocol) {
  grpc_slice* slice = create_slice(ip_address, strlen(ip_address));
  endpoint->ip_address.arg = slice;
  endpoint->ip_address.funcs.encode = encode_string_or_bytes_cb;
  endpoint->has_port = true;
  endpoint->port = static_cast<int32_t>(port);
  endpoint->has_protocol = true;
  endpoint->protocol = protocol;
}

bool grpc_gcp_handshaker_req_set_rpc_versions(grpc_gcp_handshaker_req* req,
                                              uint32_t max_major,
                                              uint32_t max_minor,
                                              uint32_t min_major,
                                              uint32_t min_minor) {
  if (req == nullptr || req->has_next) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_rpc_versions().");
    return false;
  }
  if (req->has_client_start) {
    req->client_start.has_rpc_versions = true;
    grpc_gcp_rpc_protocol_versions_set_max(&req->client_start.rpc_versions,
                                           max_major, max_minor);
    grpc_gcp_rpc_protocol_versions_set_min(&req->client_start.rpc_versions,
                                           min_major, min_minor);
  } else {
    req->server_start.has_rpc_versions = true;
    grpc_gcp_rpc_protocol_versions_set_max(&req->server_start.rpc_versions,
                                           max_major, max_minor);
    grpc_gcp_rpc_protocol_versions_set_min(&req->server_start.rpc_versions,
                                           min_major, min_minor);
  }
  return true;
}

bool grpc_gcp_handshaker_req_set_local_endpoint(
    grpc_gcp_handshaker_req* req, const char* ip_address, size_t port,
    grpc_gcp_network_protocol protocol) {
  if (req == nullptr || ip_address == nullptr || port > 65535 ||
      req->has_next) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_local_endpoint().");
    return false;
  }
  if (req->has_client_start) {
    req->client_start.has_local_endpoint = true;
    set_endpoint(&req->client_start.local_endpoint, ip_address, port, protocol);
  } else {
    req->server_start.has_local_endpoint = true;
    set_endpoint(&req->server_start.local_endpoint, ip_address, port, protocol);
  }
  return true;
}

bool grpc_gcp_handshaker_req_set_remote_endpoint(
    grpc_gcp_handshaker_req* req, const char* ip_address, size_t port,
    grpc_gcp_network_protocol protocol) {
  if (req == nullptr || ip_address == nullptr || port > 65535 ||
      req->has_next) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_remote_endpoint().");
    return false;
  }
  if (req->has_client_start) {
    req->client_start.has_remote_endpoint = true;
    set_endpoint(&req->client_start.remote_endpoint, ip_address, port,
                 protocol);
  } else {
    req->server_start.has_remote_endpoint = true;
    set_endpoint(&req->server_start.remote_endpoint, ip_address, port,
                 protocol);
  }
  return true;
}

bool grpc_gcp_handshaker_req_set_in_bytes(grpc_gcp_handshaker_req* req,
                                          const char* in_bytes, size_t size) {
  if (req == nullptr || in_bytes == nullptr || req->has_client_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_set_in_bytes().");
    return false;
  }
  grpc_slice* slice = create_slice(in_bytes, size);
  if (req->has_next) {
    req->next.in_bytes.arg = slice;
    req->next.in_bytes.funcs.encode = &encode_string_or_bytes_cb;
  } else {
    req->server_start.in_bytes.arg = slice;
    req->server_start.in_bytes.funcs.encode = &encode_string_or_bytes_cb;
  }
  return true;
}

static grpc_gcp_server_handshake_parameters* server_start_find_param(
    grpc_gcp_handshaker_req* req, int32_t key) {
  size_t i = 0;
  for (i = 0; i < req->server_start.handshake_parameters_count; i++) {
    if (req->server_start.handshake_parameters[i].key == key) {
      return &req->server_start.handshake_parameters[i].value;
    }
  }
  req->server_start
      .handshake_parameters[req->server_start.handshake_parameters_count]
      .has_key = true;
  req->server_start
      .handshake_parameters[req->server_start.handshake_parameters_count]
      .has_value = true;
  req->server_start
      .handshake_parameters[req->server_start.handshake_parameters_count++]
      .key = key;
  return &req->server_start
              .handshake_parameters
                  [req->server_start.handshake_parameters_count - 1]
              .value;
}

bool grpc_gcp_handshaker_req_param_add_record_protocol(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* record_protocol) {
  if (req == nullptr || record_protocol == nullptr || !req->has_server_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_param_add_record_protocol().");
    return false;
  }
  grpc_gcp_server_handshake_parameters* param =
      server_start_find_param(req, key);
  grpc_slice* slice = create_slice(record_protocol, strlen(record_protocol));
  add_repeated_field(
      reinterpret_cast<repeated_field**>(&param->record_protocols.arg), slice);
  param->record_protocols.funcs.encode = &encode_repeated_string_cb;
  return true;
}

bool grpc_gcp_handshaker_req_param_add_local_identity_hostname(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* hostname) {
  if (req == nullptr || hostname == nullptr || !req->has_server_start) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_gcp_handshaker_req_param_add_local_identity_hostname().");
    return false;
  }
  grpc_gcp_server_handshake_parameters* param =
      server_start_find_param(req, key);
  grpc_gcp_identity* local_identity =
      static_cast<grpc_gcp_identity*>(gpr_zalloc(sizeof(*local_identity)));
  set_identity_hostname(local_identity, hostname);
  add_repeated_field(
      reinterpret_cast<repeated_field**>(&param->local_identities.arg),
      local_identity);
  param->local_identities.funcs.encode = &encode_repeated_identity_cb;
  return true;
}

bool grpc_gcp_handshaker_req_param_add_local_identity_service_account(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* service_account) {
  if (req == nullptr || service_account == nullptr || !req->has_server_start) {
    gpr_log(
        GPR_ERROR,
        "Invalid arguments to "
        "grpc_gcp_handshaker_req_param_add_local_identity_service_account().");
    return false;
  }
  grpc_gcp_server_handshake_parameters* param =
      server_start_find_param(req, key);
  grpc_gcp_identity* local_identity =
      static_cast<grpc_gcp_identity*>(gpr_zalloc(sizeof(*local_identity)));
  set_identity_service_account(local_identity, service_account);
  add_repeated_field(
      reinterpret_cast<repeated_field**>(&param->local_identities.arg),
      local_identity);
  param->local_identities.funcs.encode = &encode_repeated_identity_cb;
  return true;
}

bool grpc_gcp_handshaker_req_encode(grpc_gcp_handshaker_req* req,
                                    grpc_slice* slice) {
  if (req == nullptr || slice == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to grpc_gcp_handshaker_req_encode().");
    return false;
  }
  pb_ostream_t size_stream;
  memset(&size_stream, 0, sizeof(pb_ostream_t));
  if (!pb_encode(&size_stream, grpc_gcp_HandshakerReq_fields, req)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&size_stream));
    return false;
  }
  size_t encoded_length = size_stream.bytes_written;
  *slice = grpc_slice_malloc(encoded_length);
  pb_ostream_t output_stream =
      pb_ostream_from_buffer(GRPC_SLICE_START_PTR(*slice), encoded_length);
  if (!pb_encode(&output_stream, grpc_gcp_HandshakerReq_fields, req) != 0) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&output_stream));
    return false;
  }
  return true;
}

/* HandshakerResp. */
grpc_gcp_handshaker_resp* grpc_gcp_handshaker_resp_create(void) {
  grpc_gcp_handshaker_resp* resp =
      static_cast<grpc_gcp_handshaker_resp*>(gpr_zalloc(sizeof(*resp)));
  return resp;
}

void grpc_gcp_handshaker_resp_destroy(grpc_gcp_handshaker_resp* resp) {
  if (resp != nullptr) {
    destroy_slice(static_cast<grpc_slice*>(resp->out_frames.arg));
    if (resp->has_status) {
      destroy_slice(static_cast<grpc_slice*>(resp->status.details.arg));
    }
    if (resp->has_result) {
      destroy_slice(
          static_cast<grpc_slice*>(resp->result.application_protocol.arg));
      destroy_slice(static_cast<grpc_slice*>(resp->result.record_protocol.arg));
      destroy_slice(static_cast<grpc_slice*>(resp->result.key_data.arg));
      if (resp->result.has_local_identity) {
        destroy_slice(
            static_cast<grpc_slice*>(resp->result.local_identity.hostname.arg));
        destroy_slice(static_cast<grpc_slice*>(
            resp->result.local_identity.service_account.arg));
      }
      if (resp->result.has_peer_identity) {
        destroy_slice(
            static_cast<grpc_slice*>(resp->result.peer_identity.hostname.arg));
        destroy_slice(static_cast<grpc_slice*>(
            resp->result.peer_identity.service_account.arg));
      }
    }
    gpr_free(resp);
  }
}

bool grpc_gcp_handshaker_resp_decode(grpc_slice encoded_handshaker_resp,
                                     grpc_gcp_handshaker_resp* resp) {
  if (resp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr argument to grpc_gcp_handshaker_resp_decode().");
    return false;
  }
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_handshaker_resp),
                             GRPC_SLICE_LENGTH(encoded_handshaker_resp));
  resp->out_frames.funcs.decode = decode_string_or_bytes_cb;
  resp->status.details.funcs.decode = decode_string_or_bytes_cb;
  resp->result.application_protocol.funcs.decode = decode_string_or_bytes_cb;
  resp->result.record_protocol.funcs.decode = decode_string_or_bytes_cb;
  resp->result.key_data.funcs.decode = decode_string_or_bytes_cb;
  resp->result.peer_identity.hostname.funcs.decode = decode_string_or_bytes_cb;
  resp->result.peer_identity.service_account.funcs.decode =
      decode_string_or_bytes_cb;
  resp->result.local_identity.hostname.funcs.decode = decode_string_or_bytes_cb;
  resp->result.local_identity.service_account.funcs.decode =
      decode_string_or_bytes_cb;
  if (!pb_decode(&stream, grpc_gcp_HandshakerResp_fields, resp)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}
