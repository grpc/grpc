//
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
//

#include "src/core/lib/surface/lame_client.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/useful.h"

// Avoid some IWYU confusion:
// IWYU pragma: no_include "src/core/util/orphanable.h"

namespace grpc_core {

const grpc_channel_filter LameClientFilter::kFilter =
    MakePromiseBasedFilter<LameClientFilter, FilterEndpoint::kClient,
                           kFilterIsLast>();

absl::StatusOr<std::unique_ptr<LameClientFilter>> LameClientFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return std::make_unique<LameClientFilter>(
      *args.GetPointer<absl::Status>(GRPC_ARG_LAME_FILTER_ERROR));
}

LameClientFilter::LameClientFilter(absl::Status error)
    : error_(std::move(error)),
      state_tracker_("lame_client", GRPC_CHANNEL_SHUTDOWN) {}

ArenaPromise<ServerMetadataHandle> LameClientFilter::MakeCallPromise(
    CallArgs args, NextPromiseFactory) {
  // TODO(ctiller): remove if check once promise_based_filter is removed (Close
  // is still needed)
  if (args.server_to_client_messages != nullptr) {
    args.server_to_client_messages->CloseWithError();
  }
  if (args.client_to_server_messages != nullptr) {
    args.client_to_server_messages->CloseWithError();
  }
  if (args.server_initial_metadata != nullptr) {
    args.server_initial_metadata->CloseWithError();
  }
  args.client_initial_metadata_outstanding.Complete(true);
  return Immediate(ServerMetadataFromStatus(error_));
}

bool LameClientFilter::GetChannelInfo(const grpc_channel_info*) { return true; }

bool LameClientFilter::StartTransportOp(grpc_transport_op* op) {
  {
    MutexLock lock(&mu_);
    if (op->start_connectivity_watch != nullptr) {
      state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                                std::move(op->start_connectivity_watch));
    }
    if (op->stop_connectivity_watch != nullptr) {
      state_tracker_.RemoveWatcher(op->stop_connectivity_watch);
    }
  }
  if (op->send_ping.on_initiate != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_initiate,
                 GRPC_ERROR_CREATE("lame client channel"));
  }
  if (op->send_ping.on_ack != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_ack,
                 GRPC_ERROR_CREATE("lame client channel"));
  }
  if (op->on_consumed != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  }
  return true;
}

namespace {

// Channel arg vtable for a grpc_error_handle.
void* ErrorCopy(void* p) {
  return new absl::Status(*static_cast<absl::Status*>(p));
}
void ErrorDestroy(void* p) { delete static_cast<absl::Status*>(p); }
int ErrorCompare(void* p, void* q) { return QsortCompare(p, q); }

}  // namespace

const grpc_arg_pointer_vtable kLameFilterErrorArgVtable = {
    ErrorCopy, ErrorDestroy, ErrorCompare};

grpc_arg MakeLameClientErrorArg(grpc_error_handle* error) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_LAME_FILTER_ERROR), error,
      &kLameFilterErrorArgVtable);
}

}  // namespace grpc_core
