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

#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"

#include "absl/log/log.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

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

void grpc_alts_credentials_options_destroy(
    grpc_alts_credentials_options* options) {
  if (options != nullptr) {
    if (options->vtable != nullptr && options->vtable->destruct != nullptr) {
      options->vtable->destruct(options);
    }
    gpr_free(options);
  }
}
