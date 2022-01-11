//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_RESOLVER_RESOLVER_H
#define GRPC_CORE_LIB_RESOLVER_RESOLVER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/service_config/service_config.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount;

// Name associated with individual address, if available.
#define GRPC_ARG_ADDRESS_NAME "grpc.address_name"

namespace grpc_core {

/// Interface for name resolution.
///
/// This interface is designed to support both push-based and pull-based
/// mechanisms.  A push-based mechanism is one where the resolver will
/// subscribe to updates for a given name, and the name service will
/// proactively send new data to the resolver whenever the data associated
/// with the name changes.  A pull-based mechanism is one where the resolver
/// needs to query the name service again to get updated information (e.g.,
/// DNS).
///
/// Note: All methods with a "Locked" suffix must be called from the
/// work_serializer passed to the constructor.
class Resolver : public InternallyRefCounted<Resolver> {
 public:
  /// Results returned by the resolver.
  struct Result {
    /// A list of addresses, or an error.
    absl::StatusOr<ServerAddressList> addresses;
    /// A service config, or an error.
    absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config = nullptr;
    /// An optional human-readable note describing context about the resolution,
    /// to be passed along to the LB policy for inclusion in RPC failure status
    /// messages in cases where neither \a addresses nor \a service_config
    /// has a non-OK status.  For example, a resolver that returns an empty
    /// address list but a valid service config may set to this to something
    /// like "no DNS entries found for <name>".
    std::string resolution_note;
    // TODO(roth): Before making this a public API, figure out a way to
    // avoid exposing channel args this way.
    const grpc_channel_args* args = nullptr;

    // TODO(roth): Remove everything below once grpc_channel_args is
    // converted to a copyable and movable C++ object.
    Result() = default;
    ~Result();
    Result(const Result& other);
    Result(Result&& other) noexcept;
    Result& operator=(const Result& other);
    Result& operator=(Result&& other) noexcept;
  };

  /// A proxy object used by the resolver to return results to the
  /// client channel.
  class ResultHandler {
   public:
    virtual ~ResultHandler() {}

    /// Reports a result to the channel.
    // TODO(roth): Add a mechanism for the resolver to get back a signal
    // indicating if the result was accepted by the LB policy, so that it
    // knows whether to go into backoff to retry to resolution.
    virtual void ReportResult(Result result) = 0;  // NOLINT
  };

  // Not copyable nor movable.
  Resolver(const Resolver&) = delete;
  Resolver& operator=(const Resolver&) = delete;
  ~Resolver() override = default;

  /// Starts resolving.
  virtual void StartLocked() = 0;

  /// Asks the resolver to obtain an updated resolver result, if
  /// applicable.
  ///
  /// This is useful for pull-based implementations to decide when to
  /// re-resolve.  However, the implementation is not required to
  /// re-resolve immediately upon receiving this call; it may instead
  /// elect to delay based on some configured minimum time between
  /// queries, to avoid hammering the name service with queries.
  ///
  /// For push-based implementations, this may be a no-op.
  ///
  /// Note: Implementations must not invoke any method on the
  /// ResultHandler from within this call.
  virtual void RequestReresolutionLocked() {}

  /// Resets the re-resolution backoff, if any.
  /// This needs to be implemented only by pull-based implementations;
  /// for push-based implementations, it will be a no-op.
  virtual void ResetBackoffLocked() {}

  // Note: This must be invoked while holding the work_serializer.
  void Orphan() override {
    ShutdownLocked();
    Unref();
  }

 protected:
  Resolver();

  /// Shuts down the resolver.
  virtual void ShutdownLocked() = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOLVER_RESOLVER_H
