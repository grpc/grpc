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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/lame_client.h"

#include <string.h>

#include <atomic>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

#define GRPC_ARG_LAME_FILTER_ERROR "grpc.lame_filter_error"

namespace grpc_core {

const grpc_channel_filter LameClientFilter::kFilter =
    MakePromiseBasedFilter<LameClientFilter, FilterEndpoint::kClient>(
        "lame-client");

absl::StatusOr<LameClientFilter> LameClientFilter::Create(
    ChannelArgs args, ChannelFilter::Args filter_args) {
  return LameClientFilter(grpc_error_to_absl_status(
      *args.GetPointer<grpc_error_handle>(GRPC_ARG_LAME_FILTER_ERROR)));
}

LameClientFilter::LameClientFilter(absl::Status error)
    : error_(std::move(error)), state_(absl::make_unique<State>()) {}

LameClientFilter::State::State()
    : state_tracker("lame_client", GRPC_CHANNEL_SHUTDOWN) {}

ArenaPromise<ServerMetadataHandle> LameClientFilter::MakeCallPromise(
    CallArgs, NextPromiseFactory) {
  return Immediate(ServerMetadataHandle(error_));
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
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"));
  }
  if (op->send_ping.on_ack != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_ack,
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"));
  }
  GRPC_ERROR_UNREF(op->disconnect_with_error);
  if (op->on_consumed != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
  }
  return true;
}

namespace {

// Channel arg vtable for a grpc_error_handle.
void* ErrorCopy(void* p) {
  grpc_error_handle* new_error = nullptr;
  if (p != nullptr) {
    grpc_error_handle* error = static_cast<grpc_error_handle*>(p);
    new_error = new grpc_error_handle();
    *new_error = GRPC_ERROR_REF(*error);
  }
  return new_error;
}
void ErrorDestroy(void* p) {
  if (p != nullptr) {
    grpc_error_handle* error = static_cast<grpc_error_handle*>(p);
    GRPC_ERROR_UNREF(*error);
    delete error;
  }
}
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
  grpc_error_handle error = grpc_error_set_str(
      grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"),
          GRPC_ERROR_INT_GRPC_STATUS, error_code),
      GRPC_ERROR_STR_GRPC_MESSAGE, error_message);
  grpc_core::ChannelArgs args =
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr)
          .Set(GRPC_ARG_LAME_FILTER_ERROR,
               grpc_core::ChannelArgs::Pointer(
                   new grpc_error_handle(error),
                   &grpc_core::kLameFilterErrorArgVtable));
  auto channel = grpc_core::Channel::Create(target, std::move(args),
                                            GRPC_CLIENT_LAME_CHANNEL, nullptr);
  GPR_ASSERT(channel.ok());
  return channel->release()->c_ptr();
}
