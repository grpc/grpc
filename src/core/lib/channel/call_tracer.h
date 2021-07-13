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

#include "absl/strings/string_view.h"

#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Interface for a tracer that records activities on a particular call attempt.
// (A single RPC can have multiple attempts due to retry/hedging policies or as
// transparent retry attempts.)

class CallAttemptTracer {
 public:
  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  virtual void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata, uint32_t flags) = 0;
  virtual void RecordOnDoneSendInitialMetadata(gpr_atm* peer_string) = 0;
  virtual void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) = 0;
  virtual void RecordSendMessage(
      grpc_core::OrphanablePtr<grpc_core::ByteStream> send_message) = 0;
  virtual void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata, uint32_t* flags,
      gpr_atm* peer_string) = 0;
  virtual void RecordReceivedMessage(
      grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message) = 0;
  virtual void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* recv_trailing_metadata) = 0;
  virtual void RecordCancel(grpc_error_handle cancel_error) = 0;
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
  virtual void RecordEnd(const grpc_call_final_info* final_info) = 0;
};

// Interface for a tracer that records activities on a call. Actual attempts for
// this call are traced with CallAttemptTracer after invoking RecordNewAttempt()
// on the CallTracer object.
class CallTracer {
 public:
  // Records a new attempt for the associated call. \a transparent denotes
  // whether the attempt is being made as a transparent retry or as a
  // non-transparent retry/heding attempt.
  virtual CallAttemptTracer* RecordNewAttempt(bool transparent) = 0;
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_CALL_TRACER_H
