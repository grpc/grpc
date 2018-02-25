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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"
#include "src/core/tsi/alts_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"

#define TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY "service_accont"
#define TSI_ALTS_CERTIFICATE_TYPE "ALTS"
#define TSI_ALTS_RPC_VERSIONS "rpc_versions"

const size_t kTsiAltsNumOfPeerProperties = 3;

/**
 * Main struct for ALTS TSI handshaker. All APIs in the header are
 * thread-comptabile.
 */
typedef struct alts_tsi_handshaker alts_tsi_handshaker;

/**
 * This method creates a ALTS TSI handshaker instance.
 *
 * - options: ALTS credentials options containing information passed from TSI
 *   caller (e.g., rpc protocol versions).
 * - target_name: the name of the endpoint that the channel is connecting to,
 *   and will be used for secure naming check.
 * - handshaker_service_url: address of ALTS handshaker service in the format of
 *   "host:port".
 * - is_client: boolean value indicating if the handshaker is used at the client
 *   (is_client = true) or server (is_client = false) side.
 * - self: address of ALTS TSI handshaker instance to be returned from the
 *   method.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_tsi_handshaker_create(
    const grpc_alts_credentials_options* options, const char* target_name,
    const char* handshaker_service_url, bool is_client, tsi_handshaker** self);

/**
 * This method handles handshaker response returned from ALTS handshaker
 * service.
 *
 * - handshaker: ALTS TSI handshaker instance.
 * - recv_buffer: buffer holding data received from the handshaker service.
 * - status: status of the grpc call made to the handshaker service.
 * - details: error details of the grpc call made to the handshaker service.
 * - cb: callback function of ALTS TSI event.
 * - user_data: argument of callback function.
 * - is_ok: a boolean value indicating if the handshaker response is ok to read.
 *
 */
void alts_tsi_handshaker_handle_response(alts_tsi_handshaker* handshaker,
                                         grpc_byte_buffer* recv_buffer,
                                         grpc_status_code status,
                                         grpc_slice* details,
                                         tsi_handshaker_on_next_done_cb cb,
                                         void* user_data, bool is_ok);

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_H */
