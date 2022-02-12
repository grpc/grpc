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

#ifndef GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_CUSTOM_H
#define GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_CUSTOM_H

#include <grpc/support/port_platform.h>

#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

// User-configured custom DNS resolution APIs

// TODO(apolcyn): This type could be deleted as a part of converting
// this grpc_custom_resolver API to C++.
struct grpc_resolved_addresses {
  size_t naddrs;
  grpc_resolved_address* addrs;
};

// Destroy resolved addresses
void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses);

typedef struct grpc_custom_resolver grpc_custom_resolver;

typedef struct grpc_custom_resolver_vtable {
  grpc_error_handle (*resolve)(const char* host, const char* port,
                               grpc_resolved_addresses** res);
  void (*resolve_async)(grpc_custom_resolver* resolver, const char* host,
                        const char* port);
} grpc_custom_resolver_vtable;

// TODO(apolcyn): as a part of converting this API to C++,
// callers of \a grpc_custom_resolve_callback could instead just invoke
// CustomDNSResolver::Request::ResolveCallback directly.
void grpc_custom_resolve_callback(grpc_custom_resolver* resolver,
                                  grpc_resolved_addresses* result,
                                  grpc_error_handle error);

// Internal APIs

namespace grpc_core {

class CustomDNSResolver : public DNSResolver {
 public:
  class Request : public DNSResolver::Request,
                  public CppImplOf<Request, grpc_custom_resolver> {
   public:
    Request(
        absl::string_view name, absl::string_view default_port,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_done,
        const grpc_custom_resolver_vtable* resolve_address_vtable)
        : name_(name),
          default_port_(default_port),
          on_done_(std::move(on_done)),
          resolve_address_vtable_(resolve_address_vtable) {}

    // Starts the resolution
    void Start() override;

    // This is a no-op for the native resolver. Note
    // that no I/O polling is required for the resolution to finish.
    void Orphan() override { Unref(); }

    // Continues async resolution with the results passed first in to
    // grpc_custom_resolve_callback.
    void ResolveCallback(
        absl::StatusOr<std::vector<grpc_resolved_address>> result);

   private:
    const std::string name_;
    const std::string default_port_;
    std::string host_;
    std::string port_;
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done_;
    // user-defined DNS methods
    const grpc_custom_resolver_vtable* resolve_address_vtable_;
  };

  // Creates the global custom resolver with the specified vtable.
  static void Create(grpc_custom_resolver_vtable* vtable);

  // Gets the singleton instance.
  static CustomDNSResolver* Get();

  explicit CustomDNSResolver(const grpc_custom_resolver_vtable* vtable)
      : resolve_address_vtable_(vtable) {}

  OrphanablePtr<DNSResolver::Request> ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* /* interested_parties */,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) override {
    return MakeOrphanable<Request>(name, default_port, std::move(on_done),
                                   resolve_address_vtable_);
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> ResolveNameBlocking(
      absl::string_view name, absl::string_view default_port) override;

 private:
  // user-defined DNS methods
  const grpc_custom_resolver_vtable* resolve_address_vtable_;
};

}  // namespace grpc_core
#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_CUSTOM_H */
