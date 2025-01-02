// Copyright 2024 gRPC authors.
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

#include "src/core/lib/transport/call_state.h"

namespace grpc_core {

std::string CallState::DebugString() const {
  return absl::StrCat(
      "client_to_server_pull_state:", client_to_server_pull_state_,
      " client_to_server_push_state:", client_to_server_push_state_,
      " server_to_client_pull_state:", server_to_client_pull_state_,
      " server_to_client_message_push_state:", server_to_client_push_state_,
      " server_trailing_metadata_state:", server_trailing_metadata_state_,
      client_to_server_push_waiter_.DebugString(),
      " server_to_client_push_waiter:",
      server_to_client_push_waiter_.DebugString(),
      " client_to_server_pull_waiter:",
      client_to_server_pull_waiter_.DebugString(),
      " server_to_client_pull_waiter:",
      server_to_client_pull_waiter_.DebugString(),
      " server_trailing_metadata_waiter:",
      server_trailing_metadata_waiter_.DebugString());
}

static_assert(sizeof(CallState) <= 16, "CallState too large");

}  // namespace grpc_core
