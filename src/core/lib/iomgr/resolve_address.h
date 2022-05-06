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

#include <grpc/event_engine/event_engine.h>

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
  using TaskHandle = ::grpc_event_engine::experimental::EventEngine::
      DNSResolver::LookupTaskHandle;
  static constexpr TaskHandle NULL_HANDLE{0, 0};

  // Tracks a single asynchronous DNS resolution attempt. DNS resolution should
  // begin upon construction.
  class Request {
   public:
    virtual ~Request() = default;
    // Cancels an async DNS resolution.
    //
    // The return value's meaning is the same as EventEngine's CancelLookup:
    // * if true, the request will be cancelled, and the callback will not be
    //   run.
    // * if false, cancellation is not possible; the callback is either running
    //   or will be run.
    //
    // It is an error to call Cancel more than once.
    virtual bool Cancel() = 0;
  };

  virtual ~DNSResolver() {}

  static std::string HandleToString(TaskHandle handle);

  // Asynchronously resolve name. Use \a default_port if a port isn't designated
  // in \a name, otherwise use the port in \a name. On completion, \a on_done is
  // invoked with the result.
  //
  // Note for implementations: calls may acquire locks in \a on_done which
  // were previously held while starting the request. Therefore,
  // implementations must not invoke \a on_done inline from the call site that
  // starts the request. The DNSCallbackExecCtxScheduler utility may help
  // address this.
  virtual TaskHandle ResolveName(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_done) = 0;

  // Resolve name in a blocking fashion. Use \a default_port if a port isn't
  // designated in \a name, otherwise use the port in \a name.
  virtual absl::StatusOr<std::vector<grpc_resolved_address>>
  ResolveNameBlocking(absl::string_view name,
                      absl::string_view default_port) = 0;

  // This shares the same semantics with \a EventEngine::Cancel: successfully
  // cancelled lookups will not have their callbacks executed, and this
  // method returns true. If a TaskHandle is unknown, this method should return
  // false.
  virtual bool Cancel(TaskHandle handle) = 0;
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
