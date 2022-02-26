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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"

#include "absl/strings/strip.h"

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

PollingResolver::PollingResolver(ResolverArgs args,
                                 const grpc_channel_args* channel_args,
                                 grpc_millis min_time_between_resolutions,
                                 BackOff::Options backoff_options)
    : authority_(args.uri.authority()),
      name_to_resolve_(absl::StripPrefix(args.uri.path(), "/")),
      channel_args_(grpc_channel_args_copy(channel_args)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      interested_parties_(grpc_pollset_set_create()),
      min_time_between_resolutions_(min_time_between_resolutions),
      backoff_(backoff_options) {
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
}

PollingResolver::~PollingResolver() {
  grpc_channel_args_destroy(channel_args_);
  grpc_pollset_set_destroy(interested_parties_);
}

void PollingResolver::StartLocked() { MaybeStartResolvingLocked(); }

void PollingResolver::RequestReresolutionLocked() {
  if (request_ == nullptr) {
    MaybeStartResolvingLocked();
  }
}

void PollingResolver::ResetBackoffLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  backoff_.Reset();
}

void PollingResolver::ShutdownLocked() {
  shutdown_ = true;
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  request_.reset();
}

void PollingResolver::OnNextResolution(void* arg, grpc_error_handle error) {
  auto* self = static_cast<PollingResolver*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  self->work_serializer_->Run(
      [self, error]() { self->OnNextResolutionLocked(error); }, DEBUG_LOCATION);
}

void PollingResolver::OnNextResolutionLocked(grpc_error_handle error) {
  have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && request_ == nullptr) {
    StartResolvingLocked();
  }
  Unref(DEBUG_LOCATION, "retry-timer");
  GRPC_ERROR_UNREF(error);
}

void PollingResolver::OnRequestComplete(Result result) {
  work_serializer_->Run(
      [this, result]() mutable { OnRequestCompleteLocked(std::move(result)); },
      DEBUG_LOCATION);
}

void PollingResolver::OnRequestCompleteLocked(Result result) {
  request_.reset();
  if (shutdown_) return;
  if (result.service_config.ok() && result.addresses.ok()) {
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    backoff_.Reset();
  } else {
    gpr_log(GPR_INFO,
            "resolution failed (will retry): addresses status: %s; "
            "service config status: %s",
            result.addresses.status().ToString().c_str(),
            result.service_config.status().ToString().c_str());
    // Set up for retry.
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
    grpc_millis next_try = backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    GPR_ASSERT(!have_next_resolution_timer_);
    have_next_resolution_timer_ = true;
    Ref(DEBUG_LOCATION, "next_resolution_timer").release();
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    GRPC_CLOSURE_INIT(&on_next_resolution_, OnNextResolution, this, nullptr);
    grpc_timer_init(&next_resolution_timer_, next_try, &on_next_resolution_);
  }
  result_handler_->ReportResult(std::move(result));
}

void PollingResolver::MaybeStartResolvingLocked() {
  // If there is an existing timer, the time it fires is the earliest time we
  // can start the next resolution.
  if (have_next_resolution_timer_) return;
  if (last_resolution_timestamp_ >= 0) {
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
    const grpc_millis earliest_next_resolution =
        last_resolution_timestamp_ + min_time_between_resolutions_;
    const grpc_millis ms_until_next_resolution =
        earliest_next_resolution - ExecCtx::Get()->Now();
    if (ms_until_next_resolution > 0) {
      const grpc_millis last_resolution_ago =
          ExecCtx::Get()->Now() - last_resolution_timestamp_;
      gpr_log(GPR_DEBUG,
              "In cooldown from last resolution (from %" PRId64
              " ms ago). Will resolve again in %" PRId64 " ms",
              last_resolution_ago, ms_until_next_resolution);
      have_next_resolution_timer_ = true;
      Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown").release();
      GRPC_CLOSURE_INIT(&on_next_resolution_, OnNextResolution, this, nullptr);
      grpc_timer_init(&next_resolution_timer_,
                      ExecCtx::Get()->Now() + ms_until_next_resolution,
                      &on_next_resolution_);
      return;
    }
  }
  StartResolvingLocked();
}

void PollingResolver::StartResolvingLocked() {
  gpr_log(GPR_DEBUG, "Start resolving.");
  request_ = StartRequest();
  last_resolution_timestamp_ = ExecCtx::Get()->Now();
}

}  // namespace grpc_core
