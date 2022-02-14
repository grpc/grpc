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
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

#define GRPC_ARG_LAME_FILTER_ERROR "grpc.lame_filter_error"

namespace grpc_core {

namespace {

struct ChannelData {
  explicit ChannelData(grpc_channel_element_args* args)
      : state_tracker("lame_channel", GRPC_CHANNEL_SHUTDOWN) {
    grpc_error_handle* err = grpc_channel_args_find_pointer<grpc_error_handle>(
        args->channel_args, GRPC_ARG_LAME_FILTER_ERROR);
    if (err != nullptr) error = GRPC_ERROR_REF(*err);
  }

  ~ChannelData() { GRPC_ERROR_UNREF(error); }

  grpc_error_handle error = GRPC_ERROR_NONE;
  Mutex mu;
  ConnectivityStateTracker state_tracker;
};

struct CallData {
  CallCombiner* call_combiner;
};

void lame_start_transport_stream_op_batch(grpc_call_element* elem,
                                          grpc_transport_stream_op_batch* op) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  grpc_transport_stream_op_batch_finish_with_failure(
      op, GRPC_ERROR_REF(chand->error), calld->call_combiner);
}

void lame_get_channel_info(grpc_channel_element* /*elem*/,
                           const grpc_channel_info* /*channel_info*/) {}

void lame_start_transport_op(grpc_channel_element* elem,
                             grpc_transport_op* op) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  {
    MutexLock lock(&chand->mu);
    if (op->start_connectivity_watch != nullptr) {
      chand->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                      std::move(op->start_connectivity_watch));
    }
    if (op->stop_connectivity_watch != nullptr) {
      chand->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
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
}

grpc_error_handle lame_init_call_elem(grpc_call_element* elem,
                                      const grpc_call_element_args* args) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->call_combiner = args->call_combiner;
  return GRPC_ERROR_NONE;
}

void lame_destroy_call_elem(grpc_call_element* /*elem*/,
                            const grpc_call_final_info* /*final_info*/,
                            grpc_closure* then_schedule_closure) {
  ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
}

grpc_error_handle lame_init_channel_elem(grpc_channel_element* elem,
                                         grpc_channel_element_args* args) {
  new (elem->channel_data) ChannelData(args);
  return GRPC_ERROR_NONE;
}

void lame_destroy_channel_elem(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

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

const grpc_channel_filter grpc_lame_filter = {
    grpc_core::lame_start_transport_stream_op_batch,
    nullptr,
    grpc_core::lame_start_transport_op,
    sizeof(grpc_core::CallData),
    grpc_core::lame_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    grpc_core::lame_destroy_call_elem,
    sizeof(grpc_core::ChannelData),
    grpc_core::lame_init_channel_elem,
    grpc_core::lame_destroy_channel_elem,
    grpc_core::lame_get_channel_info,
    "lame-client",
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack*)((c) + 1))

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
  grpc_arg error_arg = grpc_core::MakeLameClientErrorArg(&error);
  grpc_channel_args* args0 =
      grpc_channel_args_copy_and_add(nullptr, &error_arg, 1);
  const grpc_channel_args* args = grpc_core::CoreConfiguration::Get()
                                      .channel_args_preconditioning()
                                      .PreconditionChannelArgs(args0);
  grpc_channel_args_destroy(args0);
  grpc_channel* channel = grpc_channel_create_internal(
      target, args, GRPC_CLIENT_LAME_CHANNEL, nullptr, nullptr);
  grpc_channel_args_destroy(args);
  GRPC_ERROR_UNREF(error);
  return channel;
}
