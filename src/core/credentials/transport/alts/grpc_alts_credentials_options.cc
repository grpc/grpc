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

#include <vector>

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

void grpc_alts_credentials_options_add_transport_protocol_preference(
    grpc_alts_credentials_options* options, const char* transport_protocol) {
  if (options == nullptr || transport_protocol == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to "
                  "grpc_alts_credentials_client_options_add_transport_protocol_"
                  "preference()";
    return;
  }
  options->transport_protocol_preferences.push_back(transport_protocol);
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

  std::copy(src->transport_protocol_preferences.begin(),
            src->transport_protocol_preferences.end(),
            std::back_inserter(dst->transport_protocol_preferences));
  return true;
}

void grpc_alts_credentials_options_destroy(
    grpc_alts_credentials_options* options) {
  if (options != nullptr) {
    if (options->vtable != nullptr && options->vtable->destruct != nullptr) {
      options->vtable->destruct(options);
    }
    gpr_free(options);
  }
}
