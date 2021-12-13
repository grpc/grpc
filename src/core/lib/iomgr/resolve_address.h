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
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET
#include <ws2tcpip.h>
#endif

#if defined(GRPC_POSIX_SOCKET) || defined(GRPC_CFSTREAM)
#include <sys/socket.h>
#endif

#include "src/core/lib/iomgr/pollset_set.h"

#define GRPC_MAX_SOCKADDR_SIZE 128

struct grpc_resolved_address {
  char addr[GRPC_MAX_SOCKADDR_SIZE];
  socklen_t len;
};
struct grpc_resolved_addresses {
  size_t naddrs;
  grpc_resolved_address* addrs;
};

/* Destroy resolved addresses */
void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses);

namespace grpc_core {
extern const char* kDefaultSecurePort;
constexpr int kDefaultSecurePortInt = 443;

// A fire and forget class used by Request implementations to run DNS
// resolution callbacks on the ExecCtx, which is frequently necessary to avoid
// lock inversion related problems.
class DNSCallbackExecCtxScheduler {
 public:
  DNSCallbackExecCtxScheduler(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done,
      absl::StatusOr<std::vector<grpc_resolved_address>> param)
      : on_done_(std::move(on_done)), param_(std::move(param)) {
    GRPC_CLOSURE_INIT(&closure_, RunCallback, this, grpc_schedule_on_exec_ctx);
    ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
  }

 private:
  static void RunCallback(void* arg, grpc_error_handle /* error */) {
    DNSCallbackExecCtxScheduler* self =
        static_cast<DNSCallbackExecCtxScheduler*>(arg);
    self->on_done_(std::move(self->param_));
    delete self;
  }

  const std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
      on_done_;
  absl::StatusOr<std::vector<grpc_resolved_address>> param_;
  grpc_closure closure_;
};

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

  // Asynchronously resolve addr. Use default_port if a port isn't designated
  // in addr, otherwise use the port in addr.
  virtual OrphanablePtr<Request> ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) GRPC_MUST_USE_RESULT = 0;

  // Resolve addr in a blocking fashion.
  virtual absl::StatusOr<std::vector<grpc_resolved_address>>
  ResolveNameBlocking(absl::string_view name,
                      absl::string_view default_port) = 0;
};

// Override the active DNS resolver, which should be used for all DNS
// resolution in gRPC. Note: this should only be used during library
// initialization, or tests.
void SetDNSResolver(DNSResolver* resolver);

// Get the singleton instance
DNSResolver* GetDNSResolver();

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H */
