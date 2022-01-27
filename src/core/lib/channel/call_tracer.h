//
//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_CALL_TRACER_H
#define GRPC_CORE_LIB_CHANNEL_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Interface for a tracer that records activities on a call. Actual attempts for
// this call are traced with CallAttemptTracer after invoking RecordNewAttempt()
// on the CallTracer object.
class CallTracer {
 public:
  // Interface for a tracer that records activities on a particular call
  // attempt.
  // (A single RPC can have multiple attempts due to retry/hedging policies or
  // as transparent retry attempts.)
  class CallAttemptTracer {
   public:
    virtual ~CallAttemptTracer() {}
    // Please refer to `grpc_transport_stream_op_batch_payload` for details on
    // arguments.
    virtual void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata, uint32_t flags) = 0;
    // TODO(yashkt): We are using gpr_atm here instead of absl::string_view
    // since that's what the transport API uses, and performing an atomic load
    // is unnecessary if the census tracer does not need it at present. Fix this
    // when the transport API changes.
    virtual void RecordOnDoneSendInitialMetadata(gpr_atm* peer_string) = 0;
    virtual void RecordSendTrailingMetadata(
        grpc_metadata_batch* send_trailing_metadata) = 0;
    virtual void RecordSendMessage(const ByteStream& send_message) = 0;
    // The `RecordReceivedInitialMetadata()` and `RecordReceivedMessage()`
    // methods should only be invoked when the metadata/message was
    // successfully received, i.e., without any error.
    virtual void RecordReceivedInitialMetadata(
        grpc_metadata_batch* recv_initial_metadata, uint32_t flags) = 0;
    virtual void RecordReceivedMessage(const ByteStream& recv_message) = 0;
    virtual void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats& transport_stream_stats) = 0;
    virtual void RecordCancel(grpc_error_handle cancel_error) = 0;
    // Should be the last API call to the object. Once invoked, the tracer
    // library is free to destroy the object.
    virtual void RecordEnd(const gpr_timespec& latency) = 0;
  };

  virtual ~CallTracer() {}

  // Records a new attempt for the associated call. \a transparent denotes
  // whether the attempt is being made as a transparent retry or as a
  // non-transparent retry/heding attempt. (There will be at least one attempt
  // even if the call is not being retried.) The `CallTracer` object retains
  // ownership to the newly created `CallAttemptTracer` object. RecordEnd()
  // serves as an indication that the call stack is done with all API calls, and
  // the tracer library is free to destroy it after that.
  virtual CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_CALL_TRACER_H
