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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_CALL_TRACER_H
#define GRPC_SRC_CORE_LIB_CHANNEL_CALL_TRACER_H

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// The interface heirarchy is as follows -
//                 CallTracerInterface
//                      /          \
//               CallTracer       RpcTracerInterface
//                                /             \
//                      CallAttemptTracer    ServerCallTracer

// The base class for all tracer implementations.
class CallTracerInterface {
 public:
  virtual ~CallTracerInterface() {}
  // Records an annotation on the call attempt.
  // TODO(yashykt): If needed, extend this to attach attributes with
  // annotations.
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
};

// The base class for CallAttemptTracer and ServerCallTracer.
// TODO(yashykt): What's a better name for this?
class RpcTracerInterface : public CallTracerInterface {
 public:
  ~RpcTracerInterface() override {}
  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  virtual void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) = 0;
  virtual void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) = 0;
  virtual void RecordSendMessage(const SliceBuffer& send_message) = 0;
  // Only invoked if message was actually compressed.
  virtual void RecordSendCompressedMessage(
      const SliceBuffer& send_compressed_message) = 0;
  // The `RecordReceivedInitialMetadata()` and `RecordReceivedMessage()`
  // methods should only be invoked when the metadata/message was
  // successfully received, i.e., without any error.
  virtual void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) = 0;
  virtual void RecordReceivedMessage(const SliceBuffer& recv_message) = 0;
  // Only invoked if message was actually decompressed.
  virtual void RecordReceivedDecompressedMessage(
      const SliceBuffer& recv_decompressed_message) = 0;
  virtual void RecordCancel(grpc_error_handle cancel_error) = 0;
};

// Interface for a tracer that records activities on a call. Actual attempts for
// this call are traced with CallAttemptTracer after invoking RecordNewAttempt()
// on the CallTracer object.
class CallTracer : public CallTracerInterface {
 public:
  // Interface for a tracer that records activities on a particular call
  // attempt.
  // (A single RPC can have multiple attempts due to retry/hedging policies or
  // as transparent retry attempts.)
  class CallAttemptTracer : public RpcTracerInterface {
   public:
    ~CallAttemptTracer() override {}
    // If the call was cancelled before the recv_trailing_metadata op
    // was started, recv_trailing_metadata and transport_stream_stats
    // will be null.
    virtual void RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) = 0;
    // Should be the last API call to the object. Once invoked, the tracer
    // library is free to destroy the object.
    virtual void RecordEnd(const gpr_timespec& latency) = 0;
  };

  ~CallTracer() override {}

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

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_CALL_TRACER_H
