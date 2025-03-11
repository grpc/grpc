// Copyright 2025 The gRPC Authors.
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

#include "test/core/test_util/fail_first_call_filter.h"

namespace grpc_core {
namespace testing {

grpc_channel_filter FailFirstCallFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(FailFirstCallFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("FailFirstCallFilter"),
};

void FailFirstCallFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* chand = static_cast<FailFirstCallFilter*>(elem->channel_data);
  auto* calld = static_cast<CallData*>(elem->call_data);
  if (!chand->seen_call_) {
    calld->fail_ = true;
    chand->seen_call_ = true;
  }
  if (calld->fail_) {
    if (batch->recv_trailing_metadata) {
      batch->payload->recv_trailing_metadata.recv_trailing_metadata->Set(
          GrpcStreamNetworkState(), GrpcStreamNetworkState::kNotSeenByServer);
    }
    if (!batch->cancel_stream) {
      grpc_transport_stream_op_batch_finish_with_failure(
          batch,
          grpc_error_set_int(
              GRPC_ERROR_CREATE("FailFirstCallFilter failing batch"),
              StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE),
          calld->call_combiner_);
      return;
    }
  }
  grpc_call_next_op(elem, batch);
}

}  // namespace testing
}  // namespace grpc_core