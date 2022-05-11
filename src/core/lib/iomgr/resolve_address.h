/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H
#define GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "absl/status/statusor.h"

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolved_address.h"

#define GRPC_MAX_SOCKADDR_SIZE 128

namespace grpc_core {
extern const char* kDefaultSecurePort;
constexpr int kDefaultSecurePortInt = 443;

// A singleton class used for async and blocking DNS resolution
class DNSResolver {
 public:
  // Tracks a single asynchronous DNS resolution attempt. The DNS
  // resolution should be arranged to be cancelled as soon as possible
  // when Orphan is called.
  class Request : public InternallyRefCounted<Request> {
   public:
    // Begins async DNS resolution
    virtual void Start() = 0;
  };

  virtual ~DNSResolver() {}

  // Asynchronously resolve name. Use \a default_port if a port isn't designated
  // in \a name, otherwise use the port in \a name. On completion, \a on_done is
  // invoked with the result.
  //
  // Note for implementations: calls may acquire locks in \a on_done which
  // were previously held while calling Request::Start(). Therefore,
  // implementations must not invoke \a on_done inline from the call to
  // Request::Start(). The DNSCallbackExecCtxScheduler utility may help address
  // this.
  virtual OrphanablePtr<Request> ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) GRPC_MUST_USE_RESULT = 0;

  // Resolve name in a blocking fashion. Use \a default_port if a port isn't
  // designated in \a name, otherwise use the port in \a name.
  virtual absl::StatusOr<std::vector<grpc_resolved_address>>
  ResolveNameBlocking(absl::string_view name,
                      absl::string_view default_port) = 0;
};

// Override the active DNS resolver which should be used for all DNS
// resolution in gRPC. Note this should only be used during library
// initialization or within tests.
void SetDNSResolver(DNSResolver* resolver);

// Get the singleton DNS resolver instance which should be used for all
// DNS resolution in gRPC.
DNSResolver* GetDNSResolver();

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H */
