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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_POLLING_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_POLLING_RESOLVER_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/types/optional.h"

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"

namespace grpc_core {

// A base class for polling-based resolvers.
// Handles cooldown and backoff timers.
// Implementations need only to implement StartRequest().
class PollingResolver : public Resolver {
 public:
  PollingResolver(ResolverArgs args, const ChannelArgs& channel_args,
                  Duration min_time_between_resolutions,
                  BackOff::Options backoff_options, TraceFlag* tracer);
  ~PollingResolver() override;

  void StartLocked() override;
  void RequestReresolutionLocked() override;
  void ResetBackoffLocked() override;
  void ShutdownLocked() override;

 protected:
  // Implemented by subclass.
  // Starts a request, returning an object representing the pending
  // request.  Orphaning that object should cancel the request.
  // When the request is complete, the implementation must call
  // OnRequestComplete() with the result.
  virtual OrphanablePtr<Orphanable> StartRequest() = 0;

  // To be invoked by the subclass when a request is complete.
  void OnRequestComplete(Result result);

  // Convenient accessor methods for subclasses.
  const std::string& authority() const { return authority_; }
  const std::string& name_to_resolve() const { return name_to_resolve_; }
  grpc_pollset_set* interested_parties() const { return interested_parties_; }
  const ChannelArgs& channel_args() const { return channel_args_; }

 private:
  void MaybeStartResolvingLocked();
  void StartResolvingLocked();

  void OnRequestCompleteLocked(Result result);

  static void OnNextResolution(void* arg, grpc_error_handle error);
  void OnNextResolutionLocked(grpc_error_handle error);

  /// authority
  std::string authority_;
  /// name to resolve
  std::string name_to_resolve_;
  /// channel args
  ChannelArgs channel_args_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  TraceFlag* tracer_;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_ = nullptr;
  /// are we shutting down?
  bool shutdown_ = false;
  /// are we currently resolving?
  OrphanablePtr<Orphanable> request_;
  /// next resolution timer
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  grpc_closure on_next_resolution_;
  /// min time between DNS requests
  Duration min_time_between_resolutions_;
  /// timestamp of last DNS request
  absl::optional<Timestamp> last_resolution_timestamp_;
  /// retry backoff state
  BackOff backoff_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_POLLING_RESOLVER_H
