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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_H

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.h"

/**
 * An implementation of nanopb thin wrapper used to set/get and
 * serialize/de-serialize of ALTS handshake requests and responses.
 *
 * All APIs in the header are thread-compatible. A typical usage of this API at
 * the client side is as follows:
 *
 * -----------------------------------------------------------------------------
 * // Create, populate, and serialize an ALTS client_start handshake request to
 * // send to the server.
 * grpc_gcp_handshaker_req* req =
 *     grpc_gcp_handshaker_req_create(CLIENT_START_REQ);
 * grpc_gcp_handshaker_req_set_handshake_protocol(
       req, grpc_gcp_HandshakeProtocol_ALTS);
 * grpc_gcp_handshaker_req_add_application_protocol(req, "grpc");
 * grpc_gcp_handshaker_req_add_record_protocol(req, "ALTSRP_GCM_AES128");
 * grpc_slice client_slice;
 * if (!grpc_gcp_handshaker_req_encode(req, &client_slice)) {
 *   fprintf(stderr, "ALTS handshake request encoding failed.";
 * }
 *
 * // De-serialize a data stream received from the server, and store the result
 * // at ALTS handshake response.
 * grpc_gcp_handshaker_resp* resp = grpc_gcp_handshaker_resp_create();
 * if (!grpc_gcp_handshaker_resp_decode(server_slice, resp)) {
 *    fprintf(stderr, "ALTS handshake response decoding failed.");
 * }
 * // To access a variable-length datatype field (i.e., pb_callback_t),
 * // access its "arg" subfield (if it has been set).
 * if (resp->out_frames.arg != nullptr) {
 *   grpc_slice* slice = resp->out_frames.arg;
 * }
 * // To access a fixed-length datatype field (i.e., not pb_calback_t),
 * // access the field directly (if it has been set).
 * if (resp->has_status && resp->status->has_code) {
 *   uint32_t code = resp->status->code;
 * }
 *------------------------------------------------------------------------------
 */

/**
 * This method creates an ALTS handshake request.
 *
 * - type: an enum type value that can be either CLIENT_START_REQ,
 *   SERVER_START_REQ, or NEXT_REQ to indicate the created instance will be
 *   client_start, server_start, and next handshake request message
 * respectively.
 *
 * The method returns a pointer to the created instance.
 */
grpc_gcp_handshaker_req* grpc_gcp_handshaker_req_create(
    grpc_gcp_handshaker_req_type type);

/**
 * This method sets the value for handshake_security_protocol field of ALTS
 * client_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - handshake_protocol: a enum type value representing the handshake security
 *   protocol.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_handshake_protocol(
    grpc_gcp_handshaker_req* req,
    grpc_gcp_handshake_protocol handshake_protocol);

/**
 * This method sets the value for target_name field of ALTS client_start
 * handshake request.
 *
 * - req: an ALTS handshake request.
 * - target_name: a target name to be set.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_target_name(grpc_gcp_handshaker_req* req,
                                             const char* target_name);

/**
 * This method adds an application protocol supported by the server (or
 * client) to ALTS server_start (or client_start) handshake request.
 *
 * - req: an ALTS handshake request.
 * - application_protocol: an application protocol (e.g., grpc) to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_add_application_protocol(
    grpc_gcp_handshaker_req* req, const char* application_protocol);

/**
 * This method adds a record protocol supported by the client to ALTS
 * client_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - record_protocol: a record protocol (e.g., ALTSRP_GCM_AES128) to be
 *   added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_add_record_protocol(grpc_gcp_handshaker_req* req,
                                                 const char* record_protocol);

/**
 * This method adds a target server identity represented as hostname and
 * acceptable by a client to ALTS client_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - hostname: a string representation of hostname at the connection
 *   endpoint to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_add_target_identity_hostname(
    grpc_gcp_handshaker_req* req, const char* hostname);

/**
 * This method adds a target server identity represented as service account and
 * acceptable by a client to ALTS client_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - service_account: a string representation of service account at the
 *   connection endpoint to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_add_target_identity_service_account(
    grpc_gcp_handshaker_req* req, const char* service_account);

/**
 * This method sets the hostname for local_identity field of ALTS client_start
 * handshake request.
 *
 * - req: an ALTS handshake request.
 * - hostname: a string representation of hostname.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_local_identity_hostname(
    grpc_gcp_handshaker_req* req, const char* hostname);

/**
 * This method sets the service account for local_identity field of ALTS
 * client_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - service_account: a string representation of service account.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_local_identity_service_account(
    grpc_gcp_handshaker_req* req, const char* service_account);

/**
 * This method sets the value for local_endpoint field of either ALTS
 * client_start or server_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - ip_address: a string representation of ip address associated with the
 *   local endpoint, that could be either IPv4 or IPv6.
 * - port: a port number associated with the local endpoint.
 * - protocol: a network protocol (e.g., TCP or UDP) associated with the
 *   local endpoint.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_local_endpoint(
    grpc_gcp_handshaker_req* req, const char* ip_address, size_t port,
    grpc_gcp_network_protocol protocol);

/**
 * This method sets the value for remote_endpoint field of either ALTS
 * client_start or server_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - ip_address: a string representation of ip address associated with the
 *   remote endpoint, that could be either IPv4 or IPv6.
 * - port: a port number associated with the remote endpoint.
 * - protocol: a network protocol (e.g., TCP or UDP) associated with the
 *   remote endpoint.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_remote_endpoint(
    grpc_gcp_handshaker_req* req, const char* ip_address, size_t port,
    grpc_gcp_network_protocol protocol);

/**
 * This method sets the value for in_bytes field of either ALTS server_start or
 * next handshake request.
 *
 * - req: an ALTS handshake request.
 * - in_bytes: a buffer containing bytes taken from out_frames of the peer's
 *   ALTS handshake response. It is possible that the peer's out_frames are
 * split into multiple handshake request messages.
 * - size: size of in_bytes buffer.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_in_bytes(grpc_gcp_handshaker_req* req,
                                          const char* in_bytes, size_t size);

/**
 * This method adds a record protocol to handshake parameters mapped by the
 * handshake protocol for ALTS server_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - key: an enum type value representing a handshake security protocol.
 * - record_protocol: a record protocol to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_param_add_record_protocol(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* record_protocol);

/**
 * This method adds a local identity represented as hostname to handshake
 * parameters mapped by the handshake protocol for ALTS server_start handshake
 * request.
 *
 * - req: an ALTS handshake request.
 * - key: an enum type value representing a handshake security protocol.
 * - hostname: a string representation of hostname to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_param_add_local_identity_hostname(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* hostname);

/**
 * This method adds a local identity represented as service account to handshake
 * parameters mapped by the handshake protocol for ALTS server_start handshake
 * request.
 *
 * - req: an ALTS handshake request.
 * - key: an enum type value representing a handshake security protocol.
 * - service_account: a string representation of service account to be added.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_param_add_local_identity_service_account(
    grpc_gcp_handshaker_req* req, grpc_gcp_handshake_protocol key,
    const char* service_account);

/**
 * This method sets the value for rpc_versions field of either ALTS
 * client_start or server_start handshake request.
 *
 * - req: an ALTS handshake request.
 * - max_major: a major version of maximum supported RPC version.
 * - max_minor: a minor version of maximum supported RPC version.
 * - min_major: a major version of minimum supported RPC version.
 * - min_minor: a minor version of minimum supported RPC version.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_set_rpc_versions(grpc_gcp_handshaker_req* req,
                                              uint32_t max_major,
                                              uint32_t max_minor,
                                              uint32_t min_major,
                                              uint32_t min_minor);

/**
 * This method serializes an ALTS handshake request and returns a data stream.
 *
 * - req: an ALTS handshake request.
 * - slice: a data stream where the serialized result will be written.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_req_encode(grpc_gcp_handshaker_req* req,
                                    grpc_slice* slice);

/* This method destroys an ALTS handshake request. */
void grpc_gcp_handshaker_req_destroy(grpc_gcp_handshaker_req* req);

/* This method creates an ALTS handshake response. */
grpc_gcp_handshaker_resp* grpc_gcp_handshaker_resp_create(void);

/**
 * This method de-serializes a data stream and stores the result
 * in an ALTS handshake response.
 *
 * - slice: a data stream containing a serialized ALTS handshake response.
 * - resp: an ALTS handshake response used to hold de-serialized result.
 *
 * The method returns true on success and false otherwise.
 */
bool grpc_gcp_handshaker_resp_decode(grpc_slice slice,
                                     grpc_gcp_handshaker_resp* resp);

/* This method destroys an ALTS handshake response. */
void grpc_gcp_handshaker_resp_destroy(grpc_gcp_handshaker_resp* resp);

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_SERVICE_API_H */
