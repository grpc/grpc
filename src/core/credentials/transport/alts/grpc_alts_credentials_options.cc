//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/credentials/transport/alts/grpc_alts_credentials_options.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "absl/log/log.h"
#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

grpc_alts_credentials_options* grpc_alts_credentials_options_copy(
    const grpc_alts_credentials_options* options) {
  if (options != nullptr && options->vtable != nullptr &&
      options->vtable->copy != nullptr) {
    return options->vtable->copy(options);
  }
  // An error occurred.
  LOG(ERROR) << "Invalid arguments to grpc_alts_credentials_options_copy()";
  return nullptr;
}

static transport_protocol_preferences* transport_protocol_preferences_create(
    const char* transport_protocol) {
  if (transport_protocol == nullptr) {
    return nullptr;
  }
  auto* tp = static_cast<transport_protocol_preferences*>(
      gpr_zalloc(sizeof(transport_protocol_preferences)));
  tp->data = gpr_strdup(transport_protocol);
  return tp;
}

void grpc_alts_credentials_options_add_transport_protocol_preference(
    grpc_alts_credentials_options* options, const char* transport_protocol) {
  if (options == nullptr || transport_protocol == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to "
                  "grpc_alts_credentials_client_options_add_transport_protocol_"
                  "preference()";
    return;
  }
  transport_protocol_preferences* node =
      transport_protocol_preferences_create(transport_protocol);
  if (options->transport_protocol_preferences_head == nullptr) {
    options->transport_protocol_preferences_head = node;
    options->transport_protocol_preferences_last = node;
    return;
  }

  options->transport_protocol_preferences_last->next = node;
  options->transport_protocol_preferences_last = node;
}

bool grpc_gcp_transport_protocol_preference_copy(
    const grpc_alts_credentials_options* src,
    grpc_alts_credentials_options* dst) {
  if ((src == nullptr && dst != nullptr) ||
      (src != nullptr && dst == nullptr)) {
    LOG(ERROR) << "Invalid arguments to "
                  "grpc_gcp_transport_protocol_preference_copy().";
    return false;
  }
  if (src == nullptr) {
    return true;
  }

  // Copy transport protocols.
  transport_protocol_preferences* node =
      src->transport_protocol_preferences_head;
  if (node == nullptr) {
    return true;
  }

  transport_protocol_preferences* prev = nullptr;
  while (node != nullptr) {
    transport_protocol_preferences* new_node =
        transport_protocol_preferences_create(node->data);
    if (prev == nullptr) {
      dst->transport_protocol_preferences_head = new_node;
    } else {
      prev->next = new_node;
    }
    prev = new_node;
    node = node->next;
  }
  return true;
}

static void transport_protocol_preferences_destroy(
    transport_protocol_preferences* transport_protocol) {
  if (transport_protocol == nullptr) {
    return;
  }
  gpr_free(transport_protocol->data);
  gpr_free(transport_protocol);
}

void grpc_alts_credentials_options_destroy(
    grpc_alts_credentials_options* options) {
  if (options != nullptr) {
    if (options->vtable != nullptr && options->vtable->destruct != nullptr) {
      options->vtable->destruct(options);
    }
    gpr_free(options);
  }
  transport_protocol_preferences* node =
      options->transport_protocol_preferences_head;
  while (node != nullptr) {
    transport_protocol_preferences* next_node = node->next;
    transport_protocol_preferences_destroy(node);
    node = next_node;
  }
}
