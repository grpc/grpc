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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/lame_client.h"

#include <memory>
#include <utility>

#include "absl/status/statusor.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"

// Avoid some IWYU confusion:
// IWYU pragma: no_include "src/core/lib/gprpp/orphanable.h"

#define GRPC_ARG_LAME_FILTER_ERROR "grpc.lame_filter_error"

namespace grpc_core {

const grpc_channel_filter LameClientFilter::kFilter =
    MakePromiseBasedFilter<LameClientFilter, FilterEndpoint::kClient,
                           kFilterIsLast>("lame-client");

absl::StatusOr<LameClientFilter> LameClientFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return LameClientFilter(
      *args.GetPointer<absl::Status>(GRPC_ARG_LAME_FILTER_ERROR));
}

LameClientFilter::LameClientFilter(absl::Status error)
    : error_(std::move(error)), state_(std::make_unique<State>()) {}

LameClientFilter::State::State()
    : state_tracker("lame_client", GRPC_CHANNEL_SHUTDOWN) {}

ArenaPromise<ServerMetadataHandle> LameClientFilter::MakeCallPromise(
    CallArgs args, NextPromiseFactory) {
  // TODO(ctiller): remove if check once promise_based_filter is removed (Close
  // is still needed)
  if (args.server_to_client_messages != nullptr) {
    args.server_to_client_messages->Close();
  }
  args.client_initial_metadata_outstanding.Complete(true);
  return Immediate(ServerMetadataFromStatus(error_));
}

bool LameClientFilter::GetChannelInfo(const grpc_channel_info*) { return true; }

bool LameClientFilter::StartTransportOp(grpc_transport_op* op) {
  {
    MutexLock lock(&state_->mu);
    if (op->start_connectivity_watch != nullptr) {
      state_->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                       std::move(op->start_connectivity_watch));
    }
    if (op->stop_connectivity_watch != nullptr) {
      state_->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
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

const grpc_arg_pointer_vtable kLameFilterErrorArgVtable = {
    ErrorCopy, ErrorDestroy, ErrorCompare};

}  // namespace

grpc_arg MakeLameClientErrorArg(grpc_error_handle* error) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_LAME_FILTER_ERROR), error,
      &kLameFilterErrorArgVtable);
}

}  // namespace grpc_core

grpc_channel* grpc_lame_client_channel_create(const char* target,
                                              grpc_status_code error_code,
                                              const char* error_message) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_lame_client_channel_create(target=%s, error_code=%d, "
      "error_message=%s)",
      3, (target, (int)error_code, error_message));
  if (error_code == GRPC_STATUS_OK) error_code = GRPC_STATUS_UNKNOWN;
  grpc_core::ChannelArgs args =
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr)
          .Set(GRPC_ARG_LAME_FILTER_ERROR,
               grpc_core::ChannelArgs::Pointer(
                   new absl::Status(static_cast<absl::StatusCode>(error_code),
                                    error_message),
                   &grpc_core::kLameFilterErrorArgVtable));
  auto channel = grpc_core::Channel::Create(target, std::move(args),
                                            GRPC_CLIENT_LAME_CHANNEL, nullptr);
  GPR_ASSERT(channel.ok());
  return channel->release()->c_ptr();
}
