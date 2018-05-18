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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_ALTS_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_ALTS_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"
#include "src/core/lib/security/credentials/credentials.h"

/* Main struct for grpc ALTS channel credential. */
typedef struct grpc_alts_credentials {
  grpc_channel_credentials base;
  grpc_alts_credentials_options* options;
  char* handshaker_service_url;
} grpc_alts_credentials;

/* Main struct for grpc ALTS server credential. */
typedef struct grpc_alts_server_credentials {
  grpc_server_credentials base;
  grpc_alts_credentials_options* options;
  char* handshaker_service_url;
} grpc_alts_server_credentials;

/**
 * This method creates an ALTS channel credential object with customized
 * information provided by caller.
 *
 * - options: grpc ALTS credentials options instance for client.
 * - handshaker_service_url: address of ALTS handshaker service in the format of
 *   "host:port". If it's nullptr, the address of default metadata server will
 *   be used.
 * - enable_untrusted_alts: a boolean flag used to enable ALTS in untrusted
 *   mode. This mode can be enabled when we are sure ALTS is running on GCP or
 * for testing purpose.
 *
 * It returns nullptr if the flag is disabled AND ALTS is not running on GCP.
 * Otherwise, it returns the created credential object.
 */

grpc_channel_credentials* grpc_alts_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts);

/**
 * This method creates an ALTS server credential object with customized
 * information provided by caller.
 *
 * - options: grpc ALTS credentials options instance for server.
 * - handshaker_service_url: address of ALTS handshaker service in the format of
 *   "host:port". If it's nullptr, the address of default metadata server will
 *   be used.
 * - enable_untrusted_alts: a boolean flag used to enable ALTS in untrusted
 *   mode. This mode can be enabled when we are sure ALTS is running on GCP or
 * for testing purpose.
 *
 * It returns nullptr if the flag is disabled and ALTS is not running on GCP.
 * Otherwise, it returns the created credential object.
 */
grpc_server_credentials* grpc_alts_server_credentials_create_customized(
    const grpc_alts_credentials_options* options,
    const char* handshaker_service_url, bool enable_untrusted_alts);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_ALTS_CREDENTIALS_H */
