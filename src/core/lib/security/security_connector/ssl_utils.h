/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SSL_UTILS_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SSL_UTILS_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include <grpc/grpc_security.h>
#include <grpc/slice_buffer.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/tsi/transport_security_interface.h"

/* --- URL schemes. --- */
#define GRPC_SSL_URL_SCHEME "https"

/* Check peer information returned from SSL handshakes. */
grpc_error* grpc_ssl_check_peer(
    const char* peer_name, const tsi_peer* peer,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context);

/* Check peer information returned from SPIFFE SSL handshakes. */
grpc_error* grpc_spiffe_check_peer_property(const tsi_peer* peer);

/* Exposed for testing only. */
grpc_core::RefCountedPtr<grpc_auth_context> grpc_ssl_peer_to_auth_context(
    const tsi_peer* peer);
tsi_peer grpc_shallow_peer_from_ssl_auth_context(
    const grpc_auth_context* auth_context);
void grpc_shallow_peer_destruct(tsi_peer* peer);
int grpc_ssl_host_matches_name(const tsi_peer* peer, const char* peer_name);

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_SSL_UTILS_H \
        */
