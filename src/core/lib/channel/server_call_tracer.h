//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_SERVER_CALL_TRACER_H
#define GRPC_SRC_CORE_LIB_CHANNEL_SERVER_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// Interface for a tracer that records activities on a server call.
class ServerCallTracer {
 public:
  virtual ~ServerCallTracer() {}
  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  virtual void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) = 0;
  virtual void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) = 0;
  virtual void RecordSendMessage(const SliceBuffer& send_message) = 0;
  // The `RecordReceivedInitialMetadata()` and `RecordReceivedMessage()`
  // methods should only be invoked when the metadata/message was
  // successfully received, i.e., without any error.
  virtual void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata, uint32_t flags) = 0;
  virtual void RecordReceivedMessage(const SliceBuffer& recv_message) = 0;
  // If the call was cancelled before the recv_trailing_metadata op
  // was started, recv_trailing_metadata and transport_stream_stats
  // will be null.
  virtual void RecordReceivedTrailingMetadata(
      absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
      const grpc_transport_stream_stats* transport_stream_stats) = 0;
  virtual void RecordCancel(grpc_error_handle cancel_error) = 0;
  // Should be the last API call to the object. Once invoked, the tracer
  // library is free to destroy the object.
  virtual void RecordEnd(const gpr_timespec& latency) = 0;
  // Records an annotation on the call attempt.
  // TODO(yashykt): If needed, extend this to attach attributes with
  // annotations.
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
};

// Interface for a factory that can create a ServerCallTracer object per server
// call.
class ServerCallTracerFactory {
 public:
  struct RawPointerChannelArgTag {};

  virtual ~ServerCallTracerFactory() {}

  virtual ServerCallTracer* CreateNewServerCallTracer() = 0;

  // Use this method to get the server call tracer factory from channel args,
  // instead of directly fetching it with `GetObject`.
  static ServerCallTracerFactory* Get(const ChannelArgs& channel_args);

  // Registers a global ServerCallTracerFactory that wil be used by default if
  // no corresponding channel arg was found. It is only valid to call this
  // before grpc_init(). It is the responsibility of the caller to maintain this
  // for the lifetime of the process.
  static void RegisterGlobal(ServerCallTracerFactory* factory);

  static absl::string_view ChannelArgName();
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_SERVER_CALL_TRACER_H
