// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_ARGS_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_ARGS_H

#include <grpc/support/port_platform.h>

#include <map>

#include "absl/types/optional.h"

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

class EvaluateArgs {
 public:
  EvaluateArgs(grpc_auth_context* auth_context, grpc_endpoint* endpoint,
               grpc_metadata_batch* metadata)
      : metadata_(metadata),
        channel_args_(
            absl::make_unique<PerChannelArgs>(auth_context, endpoint)) {}

  absl::string_view GetPath() const;
  absl::string_view GetHost() const;
  absl::string_view GetMethod() const;
  std::multimap<absl::string_view, absl::string_view> GetHeaders() const;
  // Returns metadata value(s) for the specified key.
  // If the key is not present in the batch, returns absl::nullopt.
  // If the key is present exactly once in the batch, returns a string_view of
  // that value.
  // If the key is present more than once in the batch, constructs a
  // comma-concatenated string of all values in concatenated_value and returns a
  // string_view of that string.
  absl::optional<absl::string_view> GetHeaderValue(
      absl::string_view key, std::string* concatenated_value) const;

  absl::string_view GetLocalAddress() const {
    return channel_args_->local_address;
  }
  int GetLocalPort() const { return channel_args_->local_port; }
  absl::string_view GetPeerAddress() const {
    return channel_args_->peer_address;
  }
  int GetPeerPort() const { return channel_args_->peer_port; }
  absl::string_view GetTransportSecurityType() const {
    return channel_args_->transport_security_type;
  }
  absl::string_view GetSpiffeId() const { return channel_args_->spiffe_id; }
  absl::string_view GetCommonName() const { return channel_args_->common_name; }

 private:
  struct PerChannelArgs {
    PerChannelArgs(grpc_auth_context* auth_context, grpc_endpoint* endpoint);

    grpc_core::RefCountedPtr<grpc_auth_context> auth_ctx;
    absl::string_view transport_security_type;
    absl::string_view spiffe_id;
    absl::string_view common_name;
    std::string local_address;
    int local_port = 0;
    std::string peer_address;
    int peer_port = 0;
  };

  grpc_metadata_batch* metadata_ = nullptr;
  std::unique_ptr<PerChannelArgs> channel_args_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_ARGS_H
