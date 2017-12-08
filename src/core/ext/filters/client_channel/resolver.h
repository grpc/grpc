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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/support/abstract.h"
#include "src/core/lib/support/ref_counted.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount;

namespace grpc_core {

/// Interface for name resolution.
/// Note: All methods with a "Locked" suffix must be called from the
/// combiner passed to the constructor.
class Resolver : public RefCountedWithTracing {
 public:
  // Not copyable nor movable.
  Resolver(const Resolver&) = delete;
  Resolver& operator=(const Resolver&) = delete;

  /// Gets the next result from the resolver.  Sets \a *result to the
  /// new channel args and schedules \a on_complete for execution.
  ///
  /// If resolution is fatally broken, sets \a *result to NULL and
  /// schedules \a on_complete with an error.
  virtual void NextLocked(grpc_channel_args** result,
                          grpc_closure* on_complete) GRPC_ABSTRACT;

  /// Notifies the resolver that the channel has seen an error on some address.
  /// Can be used as a hint that re-resolution is desirable soon.
  virtual void ChannelSawErrorLocked() GRPC_ABSTRACT;

  /// Shuts down the resolver.  If there is a pending call to
  /// NextLocked(), the callback will be scheduled with an error.
  virtual void ShutdownLocked() GRPC_ABSTRACT;

  grpc_combiner* combiner() const { return combiner_; }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  /// Does NOT take ownership of the reference to \a combiner.
  explicit Resolver(grpc_combiner* combiner);

  virtual ~Resolver();

 private:
  grpc_combiner* combiner_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H */
