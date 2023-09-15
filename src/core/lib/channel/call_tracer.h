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

#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// The interface hierarchy is as follows -
//                 CallTracerAnnotationInterface
//                      /          \
//        ClientCallTracer       CallTracerInterface
//                                /             \
//                      CallAttemptTracer    ServerCallTracer

// The base class for all tracer implementations.
class CallTracerAnnotationInterface {
 public:
  // Enum associated with types of Annotations.
  enum class AnnotationType {
    kMetadataSizes,
    kDoNotUse_MustBeLast,
  };

  // Base class to define a new type of annotation.
  class Annotation {
   public:
    explicit Annotation(AnnotationType type) : type_(type) {}
    AnnotationType type() const { return type_; }
    virtual std::string ToString() const = 0;
    virtual ~Annotation() = default;

   private:
    const AnnotationType type_;
  };

  virtual ~CallTracerAnnotationInterface() {}
  // Records an annotation on the call attempt.
  // TODO(yashykt): If needed, extend this to attach attributes with
  // annotations.
  virtual void RecordAnnotation(absl::string_view annotation) = 0;
  virtual void RecordAnnotation(const Annotation& annotation) = 0;
  virtual std::string TraceId() = 0;
  virtual std::string SpanId() = 0;
  virtual bool IsSampled() = 0;
  // Indicates whether this tracer is a delegating tracer or not.
  // `DelegatingClientCallTracer`, `DelegatingClientCallAttemptTracer` and
  // `DelegatingServerCallTracer` are the only delegating call tracers.
  virtual bool IsDelegatingTracer() { return false; }
};

// The base class for CallAttemptTracer and ServerCallTracer.
// TODO(yashykt): What's a better name for this?
class CallTracerInterface : public CallTracerAnnotationInterface {
 public:
  ~CallTracerInterface() override {}
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
// on the ClientCallTracer object.
class ClientCallTracer : public CallTracerAnnotationInterface {
 public:
  // Interface for a tracer that records activities on a particular call
  // attempt.
  // (A single RPC can have multiple attempts due to retry/hedging policies or
  // as transparent retry attempts.)
  class CallAttemptTracer : public CallTracerInterface {
   public:
    ~CallAttemptTracer() override {}
    // TODO(yashykt): The following two methods `RecordReceivedTrailingMetadata`
    // and `RecordEnd` should be moved into CallTracerInterface.
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

  ~ClientCallTracer() override {}

  // Records a new attempt for the associated call. \a transparent denotes
  // whether the attempt is being made as a transparent retry or as a
  // non-transparent retry/hedging attempt. (There will be at least one attempt
  // even if the call is not being retried.) The `ClientCallTracer` object
  // retains ownership to the newly created `CallAttemptTracer` object.
  // RecordEnd() serves as an indication that the call stack is done with all
  // API calls, and the tracer library is free to destroy it after that.
  virtual CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) = 0;
};

// Interface for a tracer that records activities on a server call.
class ServerCallTracer : public CallTracerInterface {
 public:
  ~ServerCallTracer() override {}
  // TODO(yashykt): The following two methods `RecordReceivedTrailingMetadata`
  // and `RecordEnd` should be moved into CallTracerInterface.
  virtual void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* recv_trailing_metadata) = 0;
  // Should be the last API call to the object. Once invoked, the tracer
  // library is free to destroy the object.
  virtual void RecordEnd(const grpc_call_final_info* final_info) = 0;
};

// Interface for a factory that can create a ServerCallTracer object per
// server call.
class ServerCallTracerFactory {
 public:
  struct RawPointerChannelArgTag {};

  virtual ~ServerCallTracerFactory() {}

  virtual ServerCallTracer* CreateNewServerCallTracer(Arena* arena) = 0;

  // Returns true if a server is to be traced, false otherwise.
  virtual bool IsServerTraced(const ChannelArgs& /*args*/) { return true; }

  // Use this method to get the server call tracer factory from channel args,
  // instead of directly fetching it with `GetObject`.
  static ServerCallTracerFactory* Get(const ChannelArgs& channel_args);

  // Registers a global ServerCallTracerFactory that wil be used by default if
  // no corresponding channel arg was found. It is only valid to call this
  // before grpc_init(). It is the responsibility of the caller to maintain
  // this for the lifetime of the process.
  static void RegisterGlobal(ServerCallTracerFactory* factory);

  static absl::string_view ChannelArgName();
};

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder);

// Convenience functions to add call tracers to a call context. Allows setting
// multiple call tracers to a single call. It is only valid to add client call
// tracers before the client_channel filter sees the send_initial_metadata op.
void AddClientCallTracerToContext(grpc_call_context_element* call_context,
                                  ClientCallTracer* tracer);

// TODO(yashykt): We want server call tracers to be registered through the
// ServerCallTracerFactory, which has yet to be made into a list.
void AddServerCallTracerToContext(grpc_call_context_element* call_context,
                                  ServerCallTracer* tracer);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_CALL_TRACER_H
