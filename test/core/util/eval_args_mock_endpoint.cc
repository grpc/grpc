// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "test/core/util/eval_args_mock_endpoint.h"

#include <inttypes.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

namespace grpc_core {

class EvalArgsMockEndpoint {
 public:
  EvalArgsMockEndpoint(absl::string_view local_uri, absl::string_view peer_uri)
      : local_address_(local_uri), peer_(peer_uri) {
    base_.vtable = &vtable_;
  }
  grpc_endpoint* base() const { return const_cast<grpc_endpoint*>(&base_); }
  static void Read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool unused) {}
  static void Write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, void* unused) {}
  static void AddToPollset(grpc_endpoint* ep, grpc_pollset* unused) {}
  static void AddToPollsetSet(grpc_endpoint* ep, grpc_pollset_set* unused) {}
  static void DeleteFromPollsetSet(grpc_endpoint* ep,
                                   grpc_pollset_set* unused) {}
  static void Shutdown(grpc_endpoint* ep, grpc_error* why) {}
  static void Destroy(grpc_endpoint* ep) {
    EvalArgsMockEndpoint* m = reinterpret_cast<EvalArgsMockEndpoint*>(ep);
    delete m;
  }

  static absl::string_view GetPeer(grpc_endpoint* ep) {
    EvalArgsMockEndpoint* m = reinterpret_cast<EvalArgsMockEndpoint*>(ep);
    return m->peer_;
  }

  static absl::string_view GetLocalAddress(grpc_endpoint* ep) {
    EvalArgsMockEndpoint* m = reinterpret_cast<EvalArgsMockEndpoint*>(ep);
    return m->local_address_;
  }

  static grpc_resource_user* GetResourceUser(grpc_endpoint* ep) {
    return nullptr;
  }

  static int GetFd(grpc_endpoint* unused) { return -1; }
  static bool CanTrackErr(grpc_endpoint* unused) { return false; }

 private:
  static constexpr grpc_endpoint_vtable vtable_ = {
      EvalArgsMockEndpoint::Read,
      EvalArgsMockEndpoint::Write,
      EvalArgsMockEndpoint::AddToPollset,
      EvalArgsMockEndpoint::AddToPollsetSet,
      EvalArgsMockEndpoint::DeleteFromPollsetSet,
      EvalArgsMockEndpoint::Shutdown,
      EvalArgsMockEndpoint::Destroy,
      EvalArgsMockEndpoint::GetResourceUser,
      EvalArgsMockEndpoint::GetPeer,
      EvalArgsMockEndpoint::GetLocalAddress,
      EvalArgsMockEndpoint::GetFd,
      EvalArgsMockEndpoint::CanTrackErr};
  grpc_endpoint base_;
  std::string local_address_;
  std::string peer_;
};

constexpr grpc_endpoint_vtable EvalArgsMockEndpoint::vtable_;

namespace {

std::string NameAndPortToURI(const char* addr, const int port) {
  grpc_sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, addr, &address.sin_addr);
  grpc_resolved_address resolved;
  memset(&resolved, 0, sizeof(resolved));
  memcpy(resolved.addr, &address, sizeof(address));
  resolved.len = sizeof(address);
  return grpc_sockaddr_to_uri(&resolved);
}

}  // namespace

grpc_endpoint* CreateEvalArgsMockEndpoint(const char* local_address,
                                          const int local_port,
                                          const char* peer_address,
                                          const int peer_port) {
  EvalArgsMockEndpoint* m =
      new EvalArgsMockEndpoint(NameAndPortToURI(local_address, local_port),
                               NameAndPortToURI(peer_address, peer_port));
  return m->base();
}

}  // namespace grpc_core
