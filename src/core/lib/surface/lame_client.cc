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

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/atomic.h"

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/transport/static_metadata.h"

namespace grpc_core {

namespace {

struct CallData {
  grpc_call_combiner* call_combiner;
  grpc_linked_mdelem status;
  grpc_linked_mdelem details;
  grpc_core::atomic<bool> filled_metadata;
};

struct ChannelData {
  grpc_status_code error_code;
  const char* error_message;
};

static void fill_metadata(grpc_call_element* elem, grpc_metadata_batch* mdb) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  bool expected = false;
  if (!calld->filled_metadata.compare_exchange_strong(
          expected, true, grpc_core::memory_order_relaxed,
          grpc_core::memory_order_relaxed)) {
    return;
  }
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  char tmp[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(chand->error_code, tmp);
  calld->status.md = grpc_mdelem_from_slices(
      GRPC_MDSTR_GRPC_STATUS, grpc_slice_from_copied_string(tmp));
  calld->details.md = grpc_mdelem_from_slices(
      GRPC_MDSTR_GRPC_MESSAGE,
      grpc_slice_from_copied_string(chand->error_message));
  calld->status.prev = calld->details.next = nullptr;
  calld->status.next = &calld->details;
  calld->details.prev = &calld->status;
  mdb->list.head = &calld->status;
  mdb->list.tail = &calld->details;
  mdb->list.count = 2;
  mdb->deadline = GRPC_MILLIS_INF_FUTURE;
}

static void lame_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (op->recv_initial_metadata) {
    fill_metadata(elem,
                  op->payload->recv_initial_metadata.recv_initial_metadata);
  } else if (op->recv_trailing_metadata) {
    fill_metadata(elem,
                  op->payload->recv_trailing_metadata.recv_trailing_metadata);
  }
  grpc_transport_stream_op_batch_finish_with_failure(
      op, GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"),
      calld->call_combiner);
}

static void lame_get_channel_info(grpc_channel_element* elem,
                                  const grpc_channel_info* channel_info) {}

static void lame_start_transport_op(grpc_channel_element* elem,
                                    grpc_transport_op* op) {
  if (op->on_connectivity_state_change) {
    GPR_ASSERT(*op->connectivity_state != GRPC_CHANNEL_SHUTDOWN);
    *op->connectivity_state = GRPC_CHANNEL_SHUTDOWN;
    GRPC_CLOSURE_SCHED(op->on_connectivity_state_change, GRPC_ERROR_NONE);
  }
  if (op->send_ping.on_initiate != nullptr) {
    GRPC_CLOSURE_SCHED(
        op->send_ping.on_initiate,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"));
  }
  if (op->send_ping.on_ack != nullptr) {
    GRPC_CLOSURE_SCHED(
        op->send_ping.on_ack,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("lame client channel"));
  }
  GRPC_ERROR_UNREF(op->disconnect_with_error);
  if (op->on_consumed != nullptr) {
    GRPC_CLOSURE_SCHED(op->on_consumed, GRPC_ERROR_NONE);
  }
}

static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->call_combiner = args->call_combiner;
  return GRPC_ERROR_NONE;
}

static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* then_schedule_closure) {
  GRPC_CLOSURE_SCHED(then_schedule_closure, GRPC_ERROR_NONE);
}

static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(args->is_last);
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_channel_element* elem) {}

}  // namespace

}  // namespace grpc_core

const grpc_channel_filter grpc_lame_filter = {
    grpc_core::lame_start_transport_stream_op_batch,
    grpc_core::lame_start_transport_op,
    sizeof(grpc_core::CallData),
    grpc_core::init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    grpc_core::destroy_call_elem,
    sizeof(grpc_core::ChannelData),
    grpc_core::init_channel_elem,
    grpc_core::destroy_channel_elem,
    grpc_core::lame_get_channel_info,
    "lame-client",
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack*)((c) + 1))

grpc_channel* grpc_lame_client_channel_create(const char* target,
                                              grpc_status_code error_code,
                                              const char* error_message) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_element* elem;
  grpc_channel* channel =
      grpc_channel_create(target, nullptr, GRPC_CLIENT_LAME_CHANNEL, nullptr);
  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  GRPC_API_TRACE(
      "grpc_lame_client_channel_create(target=%s, error_code=%d, "
      "error_message=%s)",
      3, (target, (int)error_code, error_message));
  GPR_ASSERT(elem->filter == &grpc_lame_filter);
  auto chand = static_cast<grpc_core::ChannelData*>(elem->channel_data);
  chand->error_code = error_code;
  chand->error_message = error_message;

  return channel;
}
