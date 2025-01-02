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

#include "src/core/lib/channel/connected_channel.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/alloc.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"

typedef struct connected_channel_channel_data {
  grpc_core::Transport* transport;
} channel_data;

struct callback_state {
  grpc_closure closure;
  grpc_closure* original_closure;
  grpc_core::CallCombiner* call_combiner;
  const char* reason;
};
typedef struct connected_channel_call_data {
  grpc_core::CallCombiner* call_combiner;
  // Closures used for returning results on the call combiner.
  callback_state on_complete[6];  // Max number of pending batches.
  callback_state recv_initial_metadata_ready;
  callback_state recv_message_ready;
  callback_state recv_trailing_metadata_ready;
} call_data;

static void run_in_call_combiner(void* arg, grpc_error_handle error) {
  callback_state* state = static_cast<callback_state*>(arg);
  GRPC_CALL_COMBINER_START(state->call_combiner, state->original_closure, error,
                           state->reason);
}

static void run_cancel_in_call_combiner(void* arg, grpc_error_handle error) {
  run_in_call_combiner(arg, error);
  gpr_free(arg);
}

static void intercept_callback(call_data* calld, callback_state* state,
                               bool free_when_done, const char* reason,
                               grpc_closure** original_closure) {
  state->original_closure = *original_closure;
  state->call_combiner = calld->call_combiner;
  state->reason = reason;
  *original_closure = GRPC_CLOSURE_INIT(
      &state->closure,
      free_when_done ? run_cancel_in_call_combiner : run_in_call_combiner,
      state, grpc_schedule_on_exec_ctx);
}

static callback_state* get_state_for_batch(
    call_data* calld, grpc_transport_stream_op_batch* batch) {
  if (batch->send_initial_metadata) return &calld->on_complete[0];
  if (batch->send_message) return &calld->on_complete[1];
  if (batch->send_trailing_metadata) return &calld->on_complete[2];
  if (batch->recv_initial_metadata) return &calld->on_complete[3];
  if (batch->recv_message) return &calld->on_complete[4];
  if (batch->recv_trailing_metadata) return &calld->on_complete[5];
  GPR_UNREACHABLE_CODE(return nullptr);
}

// We perform a small hack to locate transport data alongside the connected
// channel data in call allocations, to allow everything to be pulled in minimal
// cache line requests
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) \
  ((grpc_stream*)(((char*)(calld)) +           \
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  ((call_data*)(((char*)(transport_stream)) -             \
                GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))

// Intercept a call operation and either push it directly up or translate it
// into transport stream operations
static void connected_channel_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (batch->recv_initial_metadata) {
    callback_state* state = &calld->recv_initial_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_initial_metadata_ready",
        &batch->payload->recv_initial_metadata.recv_initial_metadata_ready);
  }
  if (batch->recv_message) {
    callback_state* state = &calld->recv_message_ready;
    intercept_callback(calld, state, false, "recv_message_ready",
                       &batch->payload->recv_message.recv_message_ready);
  }
  if (batch->recv_trailing_metadata) {
    callback_state* state = &calld->recv_trailing_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_trailing_metadata_ready",
        &batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready);
  }
  if (batch->cancel_stream) {
    // There can be more than one cancellation batch in flight at any
    // given time, so we can't just pick out a fixed index into
    // calld->on_complete like we can for the other ops.  However,
    // cancellation isn't in the fast path, so we just allocate a new
    // closure for each one.
    callback_state* state =
        static_cast<callback_state*>(gpr_malloc(sizeof(*state)));
    intercept_callback(calld, state, true, "on_complete (cancel_stream)",
                       &batch->on_complete);
  } else if (batch->on_complete != nullptr) {
    callback_state* state = get_state_for_batch(calld, batch);
    intercept_callback(calld, state, false, "on_complete", &batch->on_complete);
  }
  chand->transport->filter_stack_transport()->PerformStreamOp(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), batch);
  GRPC_CALL_COMBINER_STOP(calld->call_combiner, "passed batch to transport");
}

static void connected_channel_start_transport_op(grpc_channel_element* elem,
                                                 grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->PerformOp(op);
}

// Constructor for call_data
static grpc_error_handle connected_channel_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  calld->call_combiner = args->call_combiner;
  chand->transport->filter_stack_transport()->InitStream(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), &args->call_stack->refcount,
      args->server_transport_data, args->arena);
  return absl::OkStatus();
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->SetPollingEntity(TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                     pollent);
}

// Destructor for call_data
static void connected_channel_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->filter_stack_transport()->DestroyStream(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), then_schedule_closure);
}

// Constructor for channel_data
static grpc_error_handle connected_channel_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  CHECK(args->is_last);
  cd->transport = args->channel_args.GetObject<grpc_core::Transport>();
  return absl::OkStatus();
}

// Destructor for channel_data
static void connected_channel_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  if (cd->transport) {
    cd->transport->Orphan();
  }
}

// No-op.
static void connected_channel_get_channel_info(
    grpc_channel_element* /*elem*/, const grpc_channel_info* /*channel_info*/) {
}

namespace grpc_core {
namespace {
const grpc_channel_filter kConnectedFilter{
    connected_channel_start_transport_stream_op_batch,
    connected_channel_start_transport_op,
    sizeof(call_data),
    connected_channel_init_call_elem,
    set_pollset_or_pollset_set,
    connected_channel_destroy_call_elem,
    sizeof(channel_data),
    connected_channel_init_channel_elem,
    +[](grpc_channel_stack* channel_stack, grpc_channel_element* elem) {
      // HACK(ctiller): increase call stack size for the channel to make
      // space for channel data. We need a cleaner (but performant) way to
      // do this, and I'm not sure what that is yet. This is only "safe"
      // because call stacks place no additional data after the last call
      // element, and the last call element MUST be the connected channel.
      auto* transport =
          static_cast<channel_data*>(elem->channel_data)->transport;
      if (transport->filter_stack_transport() != nullptr) {
        channel_stack->call_stack_size +=
            transport->filter_stack_transport()->SizeOfStream();
      }
    },
    connected_channel_destroy_channel_elem,
    connected_channel_get_channel_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("connected"),
};

// noop filter for the v3 stack: placeholder for now because other code requires
// we have a terminator.
// TODO(ctiller): delete when v3 transition is complete.
const grpc_channel_filter kPromiseBasedTransportFilter = {
    nullptr,
    connected_channel_start_transport_op,
    0,
    nullptr,
    set_pollset_or_pollset_set,
    nullptr,
    sizeof(channel_data),
    +[](grpc_channel_element*, grpc_channel_element_args*) {
      return absl::InternalError(
          "Cannot use filter based stack with promise based transports");
    },
    +[](grpc_channel_stack*, grpc_channel_element*) {},
    connected_channel_destroy_channel_elem,
    connected_channel_get_channel_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("connected"),
};

bool TransportSupportsClientPromiseBasedCalls(const ChannelArgs& args) {
  auto* transport = args.GetObject<Transport>();
  return transport->client_transport() != nullptr;
}

bool TransportSupportsServerPromiseBasedCalls(const ChannelArgs& args) {
  auto* transport = args.GetObject<Transport>();
  return transport->server_transport() != nullptr;
}
}  // namespace

void RegisterConnectedChannel(CoreConfiguration::Builder* builder) {
  // We can't know promise based call or not here (that decision needs the
  // collaboration of all of the filters on the channel, and we don't want
  // ordering constraints on when we add filters).
  // We can know if this results in a promise based call how we'll create
  // our promise (if indeed we can), and so that is the choice made here.

  // Option 1, and our ideal: the transport supports promise based calls,
  // and so we simply use the transport directly.
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL, &kPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_DIRECT_CHANNEL,
                       &kPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_SERVER_CHANNEL, &kPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsServerPromiseBasedCalls);

  // Option 2: the transport does not support promise based calls.
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL, &kConnectedFilter)
      .Terminal()
      .IfNot(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_DIRECT_CHANNEL, &kConnectedFilter)
      .Terminal()
      .IfNot(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_SERVER_CHANNEL, &kConnectedFilter)
      .Terminal()
      .IfNot(TransportSupportsServerPromiseBasedCalls);
}

}  // namespace grpc_core
