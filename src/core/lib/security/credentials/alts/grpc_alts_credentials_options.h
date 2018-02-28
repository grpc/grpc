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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

/**
 * Main interface for ALTS credentials options. The options will contain
 * information that will be passed from grpc to TSI layer such as RPC protocol
 * versions. ALTS client (channel) and server credentials will have their own
 * implementation of this interface. The APIs listed in this header are
 * thread-compatible.
 */
typedef struct grpc_alts_credentials_options grpc_alts_credentials_options;

/* V-table for grpc_alts_credentials_options */
typedef struct grpc_alts_credentials_options_vtable {
  grpc_alts_credentials_options* (*copy)(
      const grpc_alts_credentials_options* options);
  void (*destruct)(grpc_alts_credentials_options* options);
} grpc_alts_credentials_options_vtable;

struct grpc_alts_credentials_options {
  const struct grpc_alts_credentials_options_vtable* vtable;
  grpc_gcp_rpc_protocol_versions rpc_versions;
};

typedef struct target_service_account {
  struct target_service_account* next;
  char* data;
} target_service_account;

/**
 * Main struct for ALTS client credentials options. The options contain a
 * a list of target service accounts (if specified) used for secure naming
 * check.
 */
typedef struct grpc_alts_credentials_client_options {
  grpc_alts_credentials_options base;
  target_service_account* target_account_list_head;
} grpc_alts_credentials_client_options;

/**
 * Main struct for ALTS server credentials options. The options currently
 * do not contain any server-specific fields.
 */
typedef struct grpc_alts_credentials_server_options {
  grpc_alts_credentials_options base;
} grpc_alts_credentials_server_options;

/**
 * This method performs a deep copy on grpc_alts_credentials_options instance.
 *
 * - options: a grpc_alts_credentials_options instance that needs to be copied.
 *
 * It returns a new grpc_alts_credentials_options instance on success and NULL
 * on failure.
 */
grpc_alts_credentials_options* grpc_alts_credentials_options_copy(
    const grpc_alts_credentials_options* options);

/**
 * This method destroys a grpc_alts_credentials_options instance by
 * de-allocating all of its occupied memory.
 *
 * - options: a grpc_alts_credentials_options instance that needs to be
 *   destroyed.
 */
void grpc_alts_credentials_options_destroy(
    grpc_alts_credentials_options* options);

/* This method creates a grpc ALTS credentials client options instance. */
grpc_alts_credentials_options* grpc_alts_credentials_client_options_create();

/* This method creates a grpc ALTS credentials server options instance. */
grpc_alts_credentials_options* grpc_alts_credentials_server_options_create();

/**
 * This method adds a target service account to grpc ALTS credentials client
 * options instance.
 *
 * - options: grpc ALTS credentials client options instance.
 * - service_account: service account of target endpoint.
 *
 * It returns true on success and false on failure.
 */
bool grpc_alts_credentials_client_options_add_target_service_account(
    grpc_alts_credentials_client_options* options, const char* service_account);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H \
        */
