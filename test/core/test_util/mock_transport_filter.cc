// Copyright 2026 gRPC authors.
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

#include "test/core/test_util/mock_transport_filter.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/unique_type_name.h"
#include "absl/status/status.h"

namespace grpc_core {

void MockTransportFilter::StartBatch(grpc_call_element* elem,
                                     grpc_transport_stream_op_batch* op) {
  auto* state = *static_cast<State**>(elem->channel_data);
  if (op->recv_initial_metadata) {
    state->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    state->recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
  }
  if (op->recv_message) {
    state->recv_message = op->payload->recv_message.recv_message;
    state->recv_message_flags = op->payload->recv_message.flags;
    state->recv_message_ready = op->payload->recv_message.recv_message_ready;
  }
  if (op->recv_trailing_metadata) {
    state->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    state->recv_trailing_metadata_ready =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  }
  if (op->on_complete != nullptr) {
    // The mock has no async sends, so complete the batch immediately.
    GRPC_CALL_COMBINER_START(state->call_combiner, op->on_complete,
                             absl::OkStatus(), "mock_on_complete");
  }
  // As the terminal transport, relinquish the call combiner that was passed
  // down with this batch (mirrors connected_channel.cc).
  GRPC_CALL_COMBINER_STOP(state->call_combiner,
                          "mock passed batch to transport");
}

void MockTransportFilter::StartTransportOp(grpc_channel_element*,
                                           grpc_transport_op* op) {
  if (op->on_consumed != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  }
}

grpc_error_handle MockTransportFilter::InitCallElem(
    grpc_call_element*, const grpc_call_element_args*) {
  return absl::OkStatus();
}

void MockTransportFilter::DestroyCallElem(grpc_call_element*,
                                          const grpc_call_final_info*,
                                          grpc_closure*) {}

grpc_error_handle MockTransportFilter::InitChannelElem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  *static_cast<State**>(elem->channel_data) =
      args->channel_args.GetObject<State>();
  return absl::OkStatus();
}

void MockTransportFilter::DestroyChannelElem(grpc_channel_element*) {}

const grpc_channel_filter MockTransportFilter::kFilter = {
    MockTransportFilter::StartBatch,
    MockTransportFilter::StartTransportOp,
    0,  // sizeof_call_data
    MockTransportFilter::InitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    MockTransportFilter::DestroyCallElem,
    sizeof(MockTransportFilter::State*),  // sizeof_channel_data
    MockTransportFilter::InitChannelElem,
    grpc_channel_stack_no_post_init,
    MockTransportFilter::DestroyChannelElem,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("mock_transport"),
};

}  // namespace grpc_core
