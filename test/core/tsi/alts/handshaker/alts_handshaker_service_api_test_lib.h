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

#ifndef GRPC_TEST_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_TEST_LIB_H
#define GRPC_TEST_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_TEST_LIB_H

#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.h"
#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

/**
 * The first part of this file contains function signatures for de-serializing
 * ALTS handshake requests and setting/serializing ALTS handshake responses,
 * which simulate the behaviour of grpc server that runs ALTS handshaker
 * service.
 */

/**
 * This method creates a ALTS handshaker request that is used to hold
 * de-serialized result.
 */
grpc_gcp_handshaker_req* grpc_gcp_handshaker_decoded_req_create(
    grpc_gcp_handshaker_req_type type);

/* This method de-serializes a ALTS handshaker request. */
bool grpc_gcp_handshaker_req_decode(grpc_slice slice,
                                    grpc_gcp_handshaker_req* req);

/* This method serializes a ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_encode(grpc_gcp_handshaker_resp* resp,
                                     grpc_slice* slice);

/* This method sets application protocol of ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_application_protocol(
    grpc_gcp_handshaker_resp* resp, const char* application_protocol);

/* This method sets record protocol of ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_record_protocol(
    grpc_gcp_handshaker_resp* resp, const char* record_protocol);

/* This method sets key_data of ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_key_data(grpc_gcp_handshaker_resp* resp,
                                           const char* key_data, size_t size);

/* This method sets local identity's hostname for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_local_identity_hostname(
    grpc_gcp_handshaker_resp* resp, const char* hostname);

/**
 * This method sets local identity's service account for ALTS handshaker
 * response.
 */
bool grpc_gcp_handshaker_resp_set_local_identity_service_account(
    grpc_gcp_handshaker_resp* resp, const char* service_account);

/* This method sets peer identity's hostname for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_peer_identity_hostname(
    grpc_gcp_handshaker_resp* resp, const char* hostname);

/**
 * This method sets peer identity's service account for ALTS handshaker
 * response.
 */
bool grpc_gcp_handshaker_resp_set_peer_identity_service_account(
    grpc_gcp_handshaker_resp* resp, const char* service_account);

/* This method sets keep_channel_open for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_channel_open(grpc_gcp_handshaker_resp* resp,
                                               bool keep_channel_open);

/* This method sets code for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_code(grpc_gcp_handshaker_resp* resp,
                                       uint32_t code);

/* This method sets details for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_details(grpc_gcp_handshaker_resp* resp,
                                          const char* details);

/* This method sets out_frames for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_out_frames(grpc_gcp_handshaker_resp* resp,
                                             const char* out_frames,
                                             size_t size);

/* This method sets peer_rpc_versions for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_peer_rpc_versions(
    grpc_gcp_handshaker_resp* resp, uint32_t max_major, uint32_t max_minor,
    uint32_t min_major, uint32_t min_minor);

/* This method sets bytes_consumed for ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_set_bytes_consumed(grpc_gcp_handshaker_resp* resp,
                                                 int32_t bytes_consumed);

/* This method serializes ALTS handshaker response. */
bool grpc_gcp_handshaker_resp_encode(grpc_gcp_handshaker_resp* resp,
                                     grpc_slice* slice);

/* This method de-serializes ALTS handshaker request. */
bool grpc_gcp_handshaker_req_decode(grpc_slice slice,
                                    grpc_gcp_handshaker_req* req);

/**
 * The second part contains function signatures for checking equality of a pair
 * of ALTS handshake requests/responses.
 */

/* This method checks equality of two client_start handshaker requests. */
bool grpc_gcp_handshaker_client_start_req_equals(
    grpc_gcp_start_client_handshake_req* l_req,
    grpc_gcp_start_client_handshake_req* r_req);

/* This method checks equality of two server_start handshaker requests. */
bool grpc_gcp_handshaker_server_start_req_equals(
    grpc_gcp_start_server_handshake_req* l_req,
    grpc_gcp_start_server_handshake_req* r_req);

/* This method checks equality of two ALTS handshaker requests. */
bool grpc_gcp_handshaker_req_equals(grpc_gcp_handshaker_req* l_req,
                                    grpc_gcp_handshaker_req* r_req);

/* This method checks equality of two handshaker response results. */
bool grpc_gcp_handshaker_resp_result_equals(
    grpc_gcp_handshaker_result* l_result, grpc_gcp_handshaker_result* r_result);

/* This method checks equality of two ALTS handshaker responses. */
bool grpc_gcp_handshaker_resp_equals(grpc_gcp_handshaker_resp* l_resp,
                                     grpc_gcp_handshaker_resp* r_resp);

#endif  // GRPC_TEST_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_TEST_LIB_H
