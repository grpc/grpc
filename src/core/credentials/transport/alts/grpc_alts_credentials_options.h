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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"

namespace grpc::alts {

// Its implementation must be thread-safe.
class TokenFetcher {
 public:
  virtual ~TokenFetcher() = default;

  // Thread-safe and non-blocking. The returned token must be strongly bound.
  // Failure to comply with this requirement will result in a serious security
  // issue. The token must also be valid for at least 9 hours to outlive an
  // arbitrary ALTS connection.
  virtual absl::StatusOr<std::string> GetToken() = 0;
};

}  // namespace grpc::alts

// V-table for grpc_alts_credentials_options
typedef struct grpc_alts_credentials_options_vtable {
  grpc_alts_credentials_options* (*copy)(
      const grpc_alts_credentials_options* options);
  void (*destruct)(grpc_alts_credentials_options* options);
} grpc_alts_credentials_options_vtable;

struct grpc_alts_credentials_options {
  const struct grpc_alts_credentials_options_vtable* vtable;
  grpc_gcp_rpc_protocol_versions rpc_versions;
  std::vector<std::string> record_protocols;
};

typedef struct target_service_account {
  struct target_service_account* next;
  char* data;
} target_service_account;

///
/// Main struct for ALTS client credentials options. The options contain a
/// a list of target service accounts (if specified) used for secure naming
/// check.
///
typedef struct grpc_alts_credentials_client_options {
  grpc_alts_credentials_options base;
  target_service_account* target_account_list_head;
  std::shared_ptr<grpc::alts::TokenFetcher> token_fetcher;
} grpc_alts_credentials_client_options;

///
/// Main struct for ALTS server credentials options. The options currently
/// do not contain any server-specific fields.
///
typedef struct grpc_alts_credentials_server_options {
  grpc_alts_credentials_options base;
} grpc_alts_credentials_server_options;

///
/// This method performs a deep copy on grpc_alts_credentials_options instance.
///
///- options: a grpc_alts_credentials_options instance that needs to be copied.
///
/// It returns a new grpc_alts_credentials_options instance on success and NULL
/// on failure.
///
grpc_alts_credentials_options* grpc_alts_credentials_options_copy(
    const grpc_alts_credentials_options* options);

void grpc_alts_credentials_client_options_set_token_fetcher(
    grpc_alts_credentials_options* options,
    std::shared_ptr<grpc::alts::TokenFetcher> token_fetcher);

void grpc_alts_credentials_client_options_set_record_protocols(
    grpc_alts_credentials_options* options,
    const absl::Span<std::string> record_protocols);

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_ALTS_GRPC_ALTS_CREDENTIALS_OPTIONS_H
