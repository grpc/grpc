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

#ifndef GRPC_TEST_CORE_UTIL_MOCK_AUTHORIZATION_ENDPOINT_H
#define GRPC_TEST_CORE_UTIL_MOCK_AUTHORIZATION_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"

#include "src/core/lib/iomgr/endpoint.h"

namespace grpc_core {

class MockAuthorizationEndpoint : public grpc_endpoint {
 public:
  MockAuthorizationEndpoint(absl::string_view local_uri,
                            absl::string_view peer_uri)
      : local_address_(local_uri), peer_address_(peer_uri) {
    static constexpr grpc_endpoint_vtable vtable = {
        nullptr, nullptr, nullptr,         nullptr, nullptr, nullptr,
        nullptr, GetPeer, GetLocalAddress, nullptr, nullptr};
    grpc_endpoint::vtable = &vtable;
  }

  static absl::string_view GetPeer(grpc_endpoint* ep) {
    MockAuthorizationEndpoint* m =
        reinterpret_cast<MockAuthorizationEndpoint*>(ep);
    return m->peer_address_;
  }

  static absl::string_view GetLocalAddress(grpc_endpoint* ep) {
    MockAuthorizationEndpoint* m =
        reinterpret_cast<MockAuthorizationEndpoint*>(ep);
    return m->local_address_;
  }

  void SetPeer(absl::string_view peer_address) {
    peer_address_ = std::string(peer_address);
  }

  void SetLocalAddress(absl::string_view local_address) {
    local_address_ = std::string(local_address);
  }

 private:
  std::string local_address_;
  std::string peer_address_;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_MOCK_AUTHORIZATION_ENDPOINT_H
