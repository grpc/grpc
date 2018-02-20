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

#ifndef GRPC_CORE_LIB_SECURITY_TRANSPORT_ALTS_ALTS_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_TRANSPORT_ALTS_ALTS_SECURITY_CONNECTOR_H

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"

#define GRPC_ALTS_TRANSPORT_SECURITY_TYPE "alts"

/**
 * This method creates an ALTS channel security connector.
 *
 * - channel_creds: channel credential instance.
 * - request_metadata: credential object which metadata will be sent with each
 *   request. This parameter can be nullptr.
 * - target_name: the name of the endpoint that the channel is connecting to.
 * - sc: address of ALTS channel security connector instance to be returned from
 *   the method.
 *
 * It returns GRPC_SECURITY_OK on success, and an error stauts code on failure.
 */
grpc_security_status grpc_alts_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    grpc_channel_security_connector** sc);

/**
 * This method creates an ALTS server security connector.
 *
 * - server_creds: server credential instance.
 * - sc: address of ALTS server security connector instance to be returned from
 *   the method.
 *
 * It returns GRPC_SECURITY_OK on success, and an error status code on failure.
 */
grpc_security_status grpc_alts_server_security_connector_create(
    grpc_server_credentials* server_creds, grpc_server_security_connector** sc);

/* Exposed only for testing. */
grpc_security_status grpc_alts_auth_context_from_tsi_peer(
    const tsi_peer* peer, grpc_auth_context** ctx);
#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_ALTS_ALTS_SECURITY_CONNECTOR_H */
