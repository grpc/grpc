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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#define GRPC_TLS_SPIFFE_TRANSPORT_SECURITY_TYPE "spiffe"

/**
 * This method creates a TLS SPIFFE channel security connector.
 *
 * - channel_creds: channel credential instance.
 * - request_metadata_creds: credential object which will be sent with each
 *   request. This parameter can be nullptr.
 * - target_name: the name of the endpoint that the channel is connecting to.
 * - overridden_target_name: overridden target name used for testing.
 * - ssl_session_cache: TSI SSL session cache instance used for sessions
 *   resumption.
 * - sc: address of TLS SPIFFE channel security connector instance to be
 *   returned from the method.
 *
 * It returns GRPC_SECURITY_OK on success, and an error stauts code on failure.
 */
grpc_security_status grpc_tls_spiffe_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache,
    grpc_channel_security_connector** sc);

/**
 * This method creates an TLS SPIFFE server security connector.
 *
 * - server_creds: server credential instance.
 * - sc: address of TLS SPIFFE sserver security connector instance to be
 *   returned from the method.
 *
 * It returns GRPC_SECURITY_OK on success, and an error status code on failure.
 */
grpc_security_status grpc_tls_spiffe_server_security_connector_create(
    grpc_server_credentials* server_creds, grpc_server_security_connector** sc);

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H \
        */
