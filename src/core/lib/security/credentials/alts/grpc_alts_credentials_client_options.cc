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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"
#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

static grpc_alts_credentials_options* alts_client_options_copy(
    const grpc_alts_credentials_options* options);

static void alts_client_options_destroy(grpc_alts_credentials_options* options);

static target_service_account* target_service_account_create(
    const char* service_account) {
  if (service_account == nullptr) {
    return nullptr;
  }
  auto* sa = static_cast<target_service_account*>(
      gpr_zalloc(sizeof(target_service_account)));
  sa->data = gpr_strdup(service_account);
  return sa;
}

void grpc_alts_credentials_client_options_add_target_service_account(
    grpc_alts_credentials_options* options, const char* service_account) {
  if (options == nullptr || service_account == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to "
        "grpc_alts_credentials_client_options_add_target_service_account()");
    return;
  }
  auto client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(options);
  target_service_account* node = target_service_account_create(service_account);
  node->next = client_options->target_account_list_head;
  client_options->target_account_list_head = node;
}

static void target_service_account_destroy(
    target_service_account* service_account) {
  if (service_account == nullptr) {
    return;
  }
  gpr_free(service_account->data);
  gpr_free(service_account);
}

static const grpc_alts_credentials_options_vtable vtable = {
    alts_client_options_copy, alts_client_options_destroy};

grpc_alts_credentials_options* grpc_alts_credentials_client_options_create(
    void) {
  auto client_options = static_cast<grpc_alts_credentials_client_options*>(
      gpr_zalloc(sizeof(grpc_alts_credentials_client_options)));
  client_options->base.vtable = &vtable;
  return &client_options->base;
}

static grpc_alts_credentials_options* alts_client_options_copy(
    const grpc_alts_credentials_options* options) {
  if (options == nullptr) {
    return nullptr;
  }
  grpc_alts_credentials_options* new_options =
      grpc_alts_credentials_client_options_create();
  auto new_client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(new_options);
  /* Copy target service accounts. */
  target_service_account* prev = nullptr;
  auto node =
      (reinterpret_cast<const grpc_alts_credentials_client_options*>(options))
          ->target_account_list_head;
  while (node != nullptr) {
    target_service_account* new_node =
        target_service_account_create(node->data);
    if (prev == nullptr) {
      new_client_options->target_account_list_head = new_node;
    } else {
      prev->next = new_node;
    }
    prev = new_node;
    node = node->next;
  }
  /* Copy rpc protocol versions. */
  grpc_gcp_rpc_protocol_versions_copy(&options->rpc_versions,
                                      &new_options->rpc_versions);
  return new_options;
}

static void alts_client_options_destroy(
    grpc_alts_credentials_options* options) {
  if (options == nullptr) {
    return;
  }
  auto* client_options =
      reinterpret_cast<grpc_alts_credentials_client_options*>(options);
  target_service_account* node = client_options->target_account_list_head;
  while (node != nullptr) {
    target_service_account* next_node = node->next;
    target_service_account_destroy(node);
    node = next_node;
  }
}
