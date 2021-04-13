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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_CHANNEL_ARGS_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_CHANNEL_ARGS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/security/context/security_context.h"

namespace grpc_core {

// Channel specific data.
class EvaluateChannelArgs {
 public:
  EvaluateChannelArgs(grpc_auth_context* auth_context, grpc_endpoint* endpoint);
  ~EvaluateChannelArgs() = default;

  absl::string_view GetLocalAddress() const { return local_address_; }
  int GetLocalPort() const { return local_port_; }
  absl::string_view GetPeerAddress() const { return peer_address_; }
  int GetPeerPort() const { return peer_port_; }
  absl::string_view GetTransportSecurityType() const {
    return transport_security_type_;
  }
  absl::string_view GetSpiffeId() const { return spiffe_id_; }
  absl::string_view GetCommonName() const { return common_name_; }

 private:
  std::string local_address_;
  int local_port_ = 0;
  std::string peer_address_;
  int peer_port_ = 0;
  std::string transport_security_type_;
  std::string spiffe_id_;
  std::string common_name_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVALUATE_CHANNEL_ARGS_H
